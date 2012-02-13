#ifndef _LINK_H_
#define _LINK_H_

//#include <unistd.h>
//#include <sys/types.h>
//#include <linux/elf.h>
// These structures are needed by libgc
// They are defined in mydroid/bionic/linker/linker.h, but are expected to be in <link.h>
struct link_map
{
    uintptr_t l_addr;
    char * l_name;
    uintptr_t l_ld;
    struct link_map * l_next;
    struct link_map * l_prev;
};


struct r_debug
{
    int32_t r_version;
    struct link_map * r_map;
    void (*r_brk)(void);
    int32_t r_state;
    uintptr_t r_ldbase;
};

enum {
    RT_CONSISTENT,
    RT_ADD,
    RT_DELETE
};

// If there is no ElfW macro, let's define it by ourself.
//#ifndef ElfW
//# if SIZEOF_VOID_P == 4
#define ElfW(type) Elf32_##type
//# elif SIZEOF_VOID_P == 8
//#  define ElfW(type) Elf64_##type
//# else
//#  error "Unknown sizeof(void *)"
//# endif
//#endif

#endif
