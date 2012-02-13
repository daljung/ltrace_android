#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "ptrace.h"
#include <asm/unistd.h>

#include "common.h"
#include <dirent.h>

/* If the system headers did not provide the constants, hard-code the normal
   values.  */
#ifndef PTRACE_EVENT_FORK

#define PTRACE_OLDSETOPTIONS    21
#define PTRACE_SETOPTIONS       0x4200
#define PTRACE_GETEVENTMSG      0x4201

/* options set using PTRACE_SETOPTIONS */
#define PTRACE_O_TRACESYSGOOD   0x00000001
#define PTRACE_O_TRACEFORK      0x00000002
#define PTRACE_O_TRACEVFORK     0x00000004
#define PTRACE_O_TRACECLONE     0x00000008
#define PTRACE_O_TRACEEXEC      0x00000010
#define PTRACE_O_TRACEVFORKDONE 0x00000020
#define PTRACE_O_TRACEEXIT      0x00000040

/* Wait extended result codes for the above trace options.  */
#define PTRACE_EVENT_FORK       1
#define PTRACE_EVENT_VFORK      2
#define PTRACE_EVENT_CLONE      3
#define PTRACE_EVENT_EXEC       4
#define PTRACE_EVENT_VFORK_DONE 5
#define PTRACE_EVENT_EXIT       6

#endif /* PTRACE_EVENT_FORK */

#ifdef ARCH_HAVE_UMOVELONG
extern int arch_umovelong (Process *, void *, long *, arg_type_info *);
int
umovelong (Process *proc, void *addr, long *result, arg_type_info *info) {
	return arch_umovelong (proc, addr, result, info);
}
#else
/* Read a single long from the process's memory address 'addr' */
int
umovelong (Process *proc, void *addr, long *result, arg_type_info *info) {
	long pointed_to;

	errno = 0;
	pointed_to = ptrace (PTRACE_PEEKTEXT, proc->pid, addr, 0);
	if (pointed_to == -1 && errno)
		return -errno;

	*result = pointed_to;
	if (info) {
		switch(info->type) {
			case ARGTYPE_INT:
				*result &= 0x00000000ffffffffUL;
			default:
				break;
		};
	}
	return 0;
}
#endif

void
trace_me(void) {
	debug(DEBUG_PROCESS, "trace_me: pid=%d\n", getpid());
	if (ptrace(PTRACE_TRACEME, 0, 1, 0) < 0) {
		perror("PTRACE_TRACEME");
		exit(1);
	}
}

static void *
address_thread(void * addr) {
	debug(DEBUG_FUNCTION, "address_thread(%p)", addr);
	return addr;
}

static void *
breakpoint_thread(void * bp) {
	Breakpoint * b;
	debug(DEBUG_FUNCTION, "breakpoint_thread(%p)", bp);
	b = malloc(sizeof(Breakpoint));
	if (!b) {
		perror("malloc()");
		exit(1);
	}
	memcpy(b, bp, sizeof(Breakpoint));
	return b;
}


//modify for android
void attach_child_thread(Process *proc){
    char task_path[1024];
    DIR *d;
    struct dirent *de;
    int need_cleanup = 0;
    unsigned int pid;

    if(proc == NULL)
        return;

    pid = proc->pid;
    
    sprintf(task_path, "/proc/%d/task", pid);

    d = opendir(task_path);
    if (d == NULL) {
        return ;
    }
    while ((de = readdir(d)) != NULL) {
        unsigned new_tid;
        Process *p;
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        new_tid = atoi(de->d_name);
        if (new_tid == pid)
            continue;

        if (ptrace(PTRACE_ATTACH, new_tid, 0, 0) < 0)
            continue;

        debug(DEBUG_FUNCTION, "attach thread(tid=%d)", new_tid);
    
        p = malloc(sizeof(Process));

        memcpy(p, proc, sizeof(Process));
        p->breakpoints = dict_clone(proc->breakpoints, address_thread, breakpoint_thread);
        p->pid = new_tid;
        p->parent = proc;

        p->state = STATE_ATTACHED;

        if (waitpid (new_tid, NULL, 0) != new_tid) {
            fprintf(stderr, "trace_pid: waitpid tid = %d\n", new_tid);
    	}

        //ptrace(PTRACE_CONT, new_tid, 0, 0);
        continue_process(new_tid);

        p->next = list_of_processes;

        list_of_processes = p;
    }
    closedir(d);

    return ;
}

int
trace_pid(pid_t pid) {
	debug(DEBUG_PROCESS, "trace_pid: pid=%d\n", pid);


	if (ptrace(PTRACE_ATTACH, pid, 1, 0) < 0) {
		return -1;
	}

	/* man ptrace: PTRACE_ATTACH attaches to the process specified
	   in pid.  The child is sent a SIGSTOP, but will not
	   necessarily have stopped by the completion of this call;
	   use wait() to wait for the child to stop. */
	if (waitpid (pid, NULL, 0) != pid) {
		perror ("trace_pid: waitpid");
		exit (1);
	}
    
	return 0;
}

void
trace_set_options(Process *proc, pid_t pid) {
	if (proc->tracesysgood & 0x80)
		return;

	debug(DEBUG_PROCESS, "trace_set_options: pid=%d\n", pid);

	long options = PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEFORK |
		PTRACE_O_TRACEVFORK | PTRACE_O_TRACECLONE |
		PTRACE_O_TRACEEXEC;
	if (ptrace(PTRACE_SETOPTIONS, pid, 0, options) < 0 &&
	    ptrace(PTRACE_OLDSETOPTIONS, pid, 0, options) < 0) {
		perror("PTRACE_SETOPTIONS");
		return;
	}
	proc->tracesysgood |= 0x80;
}

void
untrace_pid(pid_t pid) {
	debug(DEBUG_PROCESS, "untrace_pid: pid=%d\n", pid);
	ptrace(PTRACE_DETACH, pid, 1, 0);
}

void
continue_after_signal(pid_t pid, int signum) {
	Process *proc;

	debug(DEBUG_PROCESS, "continue_after_signal: pid=%d, signum=%d", pid, signum);

	proc = pid2proc(pid);
	if (proc && proc->breakpoint_being_enabled) {
//modify code for android
#if defined __sparc__  || defined __ia64___ || defined __mips__ || defined __arm__
		ptrace(PTRACE_SYSCALL, pid, 0, signum);
#else
		ptrace(PTRACE_SINGLESTEP, pid, 0, signum);
#endif
	} else {
		ptrace(PTRACE_SYSCALL, pid, 0, signum);
	}
}

void
continue_process(pid_t pid) {
	/* We always trace syscalls to control fork(), clone(), execve()... */

	debug(DEBUG_PROCESS, "continue_process: pid=%d", pid);

	ptrace(PTRACE_SYSCALL, pid, 0, 0);
}

void
continue_enabling_breakpoint(pid_t pid, Breakpoint *sbp) {
	enable_breakpoint(pid, sbp);
	continue_process(pid);
}

void
continue_after_breakpoint(Process *proc, Breakpoint *sbp) {
	if (sbp->enabled)
		disable_breakpoint(proc->pid, sbp);
	set_instruction_pointer(proc, sbp->addr);
	if (sbp->enabled == 0) {
		continue_process(proc->pid);
	} else {
		debug(DEBUG_PROCESS, "continue_after_breakpoint: pid=%d, addr=%p", proc->pid, sbp->addr);
		proc->breakpoint_being_enabled = sbp;
//modify code for android
#if defined __sparc__  || defined __ia64___ || defined __mips__ || defined __arm__
		/* we don't want to singlestep here */
		continue_process(proc->pid);
#else
		ptrace(PTRACE_SINGLESTEP, proc->pid, 0, 0);
#endif
	}
}

size_t
umovebytes(Process *proc, void *addr, void *laddr, size_t len) {

	union {
		long a;
		char c[sizeof(long)];
	} a;
	int started = 0;
	size_t offset = 0, bytes_read = 0;

	while (offset < len) {
		a.a = ptrace(PTRACE_PEEKTEXT, proc->pid, addr + offset, 0);
		if (a.a == -1 && errno) {
			if (started && errno == EIO)
				return bytes_read;
			else
				return -1;
		}
		started = 1;

		if (len - offset >= sizeof(long)) {
			memcpy(laddr + offset, &a.c[0], sizeof(long));
			bytes_read += sizeof(long);
		}
		else {
			memcpy(laddr + offset, &a.c[0], len - offset);
			bytes_read += (len - offset);
		}
		offset += sizeof(long);
	}

	return bytes_read;
}

/* Read a series of bytes starting at the process's memory address
   'addr' and continuing until a NUL ('\0') is seen or 'len' bytes
   have been read.
*/
int
umovestr(Process *proc, void *addr, int len, void *laddr) {
	union {
		long a;
		char c[sizeof(long)];
	} a;
	unsigned i;
	int offset = 0;

	while (offset < len) {
		a.a = ptrace(PTRACE_PEEKTEXT, proc->pid, addr + offset, 0);
		for (i = 0; i < sizeof(long); i++) {
			if (a.c[i] && offset + (signed)i < len) {
				*(char *)(laddr + offset + i) = a.c[i];
			} else {
				*(char *)(laddr + offset + i) = '\0';
				return 0;
			}
		}
		offset += sizeof(long);
	}
	*(char *)(laddr + offset) = '\0';
	return 0;
}
