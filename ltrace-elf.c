#include "config.h"

#include <endian.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <gelf.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "common.h"
//modify code for android
#define ANDROID_LIB_PATH_CNT 4
const static char* ANDROID_LIB_PATH[ANDROID_LIB_PATH_CNT+1] ={"/system/lib/", "/system/lib/hw/", "/system/lib/egl/", "/system/bin/", 0};
static android_lib_path[128];

void do_init_elf(struct ltelf *lte, const char *filename);
void do_close_elf(struct ltelf *lte);
void add_library_symbol(GElf_Addr addr, const char *name,
		struct library_symbol **library_symbolspp,
		enum toplt type_of_plt, int is_weak);
int in_load_libraries(const char *name, struct ltelf *lte, size_t count, GElf_Sym *sym);
static GElf_Addr opd2addr(struct ltelf *ltc, GElf_Addr addr);

struct library_symbol *library_symbols = NULL;
struct ltelf main_lte;

#ifdef PLT_REINITALISATION_BP
extern char *PLTs_initialized_by_here;
#endif

#ifndef DT_PPC_GOT
# define DT_PPC_GOT		(DT_LOPROC + 0)
#endif

#define PPC_PLT_STUB_SIZE 16

static Elf_Data *loaddata(Elf_Scn *scn, GElf_Shdr *shdr)
{
	Elf_Data *data = elf_getdata(scn, NULL);
	if (data == NULL || elf_getdata(scn, data) != NULL
	    || data->d_off || data->d_size != shdr->sh_size)
		return NULL;
	return data;
}

static int inside(GElf_Addr addr, GElf_Shdr *shdr)
{
	return addr >= shdr->sh_addr
		&& addr < shdr->sh_addr + shdr->sh_size;
}

static int maybe_pick_section(GElf_Addr addr,
			      Elf_Scn *in_sec, GElf_Shdr *in_shdr,
			      Elf_Scn **tgt_sec, GElf_Shdr *tgt_shdr)
{
	if (inside (addr, in_shdr)) {
		*tgt_sec = in_sec;
		*tgt_shdr = *in_shdr;
		return 1;
	}
	return 0;
}

static int get_section_covering(struct ltelf *lte, GElf_Addr addr,
				Elf_Scn **tgt_sec, GElf_Shdr *tgt_shdr)
{
	int i;
	for (i = 1; i < lte->ehdr.e_shnum; ++i) {
		Elf_Scn *scn;
		GElf_Shdr shdr;

		scn = elf_getscn(lte->elf, i);
		if (scn == NULL || gelf_getshdr(scn, &shdr) == NULL) {
			debug(1, "Couldn't read section or header.");
			return 0;
		}

		if (maybe_pick_section(addr, scn, &shdr, tgt_sec, tgt_shdr))
			return 1;
	}

	return 0;
}

static GElf_Addr read32be(Elf_Data *data, size_t offset)
{
	if (data->d_size < offset + 4) {
		debug(1, "Not enough data to read 32bit value at offset %zd.",
		      offset);
		return 0;
	}

	unsigned char const *buf = data->d_buf + offset;
	return ((Elf32_Word)buf[0] << 24)
		| ((Elf32_Word)buf[1] << 16)
		| ((Elf32_Word)buf[2] << 8)
		| ((Elf32_Word)buf[3]);
}

static GElf_Addr get_glink_vma(struct ltelf *lte, GElf_Addr ppcgot,
			       Elf_Data *plt_data)
{
	Elf_Scn *ppcgot_sec = NULL;
	GElf_Shdr ppcgot_shdr;
	if (ppcgot != 0
	    && !get_section_covering(lte, ppcgot, &ppcgot_sec, &ppcgot_shdr))
		// xxx should be the log out
		fprintf(stderr,
			"DT_PPC_GOT=%#" PRIx64 ", but no such section found.\n",
			ppcgot);

	if (ppcgot_sec != NULL) {
		Elf_Data *data = loaddata(ppcgot_sec, &ppcgot_shdr);
		if (data == NULL
		    || data->d_size < 8 )
			debug(1, "Couldn't read GOT data.");
		else {
			// where PPCGOT begins in .got
			size_t offset = ppcgot - ppcgot_shdr.sh_addr;
			GElf_Addr glink_vma = read32be(data, offset + 4);
			if (glink_vma != 0) {
				debug(1, "PPC GOT glink_vma address: %#" PRIx64,
				      glink_vma);
				return glink_vma;
			}
		}
	}

	if (plt_data != NULL) {
		GElf_Addr glink_vma = read32be(plt_data, 0);
		debug(1, ".plt glink_vma address: %#" PRIx64, glink_vma);
		return glink_vma;
	}

	return 0;
}

void
do_init_elf(struct ltelf *lte, const char *filename) {
	int i;
	GElf_Addr relplt_addr = 0;
	size_t relplt_size = 0;

	debug(DEBUG_FUNCTION, "do_init_elf(filename=%s)", filename);
	debug(1, "Reading ELF from %s...", filename);

	lte->fd = open(filename, O_RDONLY);
	if (lte->fd == -1)
	{
	    int i=0, str_index = 0;
	    //modify code for android
        if(filename[0] == '/')
            str_index = 1;
       
        for( ; i < ANDROID_LIB_PATH_CNT; i++)
        {   
            strcpy(android_lib_path, ANDROID_LIB_PATH[i]);
            strcat(android_lib_path, filename);
            fprintf(stderr, "try to open \"%s\"\n", android_lib_path);
            lte->fd = open(android_lib_path, O_RDONLY);
            if(lte->fd != -1)
                break;
        }
        
        if(lte->fd == -1)
        {
            fprintf(stderr, "Can't open \"%s\"\n", filename);
            exit(1);
        }
    }

//modify for android
#if 0//def HAVE_ELF_C_READ_MMAP
	lte->elf = elf_begin(lte->fd, ELF_C_READ_MMAP, NULL);
#else
	lte->elf = elf_begin(lte->fd, ELF_C_READ, NULL);
#endif

	if (lte->elf == NULL || elf_kind(lte->elf) != ELF_K_ELF)
		fprintf(stderr, "Can't open ELF file \"%s\"\n", filename);

	if (gelf_getehdr(lte->elf, &lte->ehdr) == NULL)
		fprintf(stderr, 0, "Can't read ELF header of \"%s\"\n",
		      filename);

	if (lte->ehdr.e_type != ET_EXEC && lte->ehdr.e_type != ET_DYN)
		fprintf(stderr, 0,
		      "\"%s\" is not an ELF executable nor shared library\n",
		      filename);

	if ((lte->ehdr.e_ident[EI_CLASS] != LT_ELFCLASS
	     || lte->ehdr.e_machine != LT_ELF_MACHINE)
#ifdef LT_ELF_MACHINE2
	    && (lte->ehdr.e_ident[EI_CLASS] != LT_ELFCLASS2
		|| lte->ehdr.e_machine != LT_ELF_MACHINE2)
#endif
#ifdef LT_ELF_MACHINE3
	    && (lte->ehdr.e_ident[EI_CLASS] != LT_ELFCLASS3
		|| lte->ehdr.e_machine != LT_ELF_MACHINE3)
#endif
	    )
		fprintf(stderr,
		      "\"%s\" is ELF from incompatible architecture\n", filename);

	Elf_Data *plt_data = NULL;
	GElf_Addr ppcgot = 0;

	for (i = 1; i < lte->ehdr.e_shnum; ++i) {
		Elf_Scn *scn;
		GElf_Shdr shdr;
		const char *name;

		scn = elf_getscn(lte->elf, i);
		if (scn == NULL || gelf_getshdr(scn, &shdr) == NULL)
			fprintf(stderr,
			      "Couldn't get section header from \"%s\"\n",
			      filename);

		name = elf_strptr(lte->elf, lte->ehdr.e_shstrndx, shdr.sh_name);
		if (name == NULL)
			fprintf(stderr,
			      "Couldn't get section header from \"%s\"\n",
			      filename);

		if (shdr.sh_type == SHT_SYMTAB) {
			Elf_Data *data;

			lte->symtab = elf_getdata(scn, NULL);
			lte->symtab_count = shdr.sh_size / shdr.sh_entsize;
			if ((lte->symtab == NULL
			     || elf_getdata(scn, lte->symtab) != NULL)
			    && opt_x != NULL)
				fprintf(stderr,
				      "Couldn't get .symtab data from \"%s\"\n",
				      filename);

			scn = elf_getscn(lte->elf, shdr.sh_link);
			if (scn == NULL || gelf_getshdr(scn, &shdr) == NULL)
				fprintf(stderr,
				      "Couldn't get section header from \"%s\"\n",
				      filename);

			data = elf_getdata(scn, NULL);
			if (data == NULL || elf_getdata(scn, data) != NULL
			    || shdr.sh_size != data->d_size || data->d_off)
				fprintf(stderr,
				      "Couldn't get .strtab data from \"%s\"\n",
				      filename);

			lte->strtab = data->d_buf;
		} else if (shdr.sh_type == SHT_DYNSYM) {
			Elf_Data *data;

			lte->dynsym = elf_getdata(scn, NULL);
			lte->dynsym_count = shdr.sh_size / shdr.sh_entsize;
			if (lte->dynsym == NULL
			    || elf_getdata(scn, lte->dynsym) != NULL)
				fprintf(stderr,
				      "Couldn't get .dynsym data from \"%s\"\n",
				      filename);

			scn = elf_getscn(lte->elf, shdr.sh_link);
			if (scn == NULL || gelf_getshdr(scn, &shdr) == NULL)
				fprintf(stderr,
				      "Couldn't get section header from \"%s\"\n",
				      filename);

			data = elf_getdata(scn, NULL);
			if (data == NULL || elf_getdata(scn, data) != NULL
			    || shdr.sh_size != data->d_size || data->d_off)
				fprintf(stderr,
				      "Couldn't get .dynstr data from \"%s\"\n",
				      filename);

			lte->dynstr = data->d_buf;
		} else if (shdr.sh_type == SHT_DYNAMIC) {
			Elf_Data *data;
			size_t j;

			lte->dyn_addr = shdr.sh_addr;
			lte->dyn_sz = shdr.sh_size;

			data = elf_getdata(scn, NULL);
			if (data == NULL || elf_getdata(scn, data) != NULL)
				fprintf(stderr,
				      "Couldn't get .dynamic data from \"%s\"\n",
				      filename);

			for (j = 0; j < shdr.sh_size / shdr.sh_entsize; ++j) {
				GElf_Dyn dyn;

				if (gelf_getdyn(data, j, &dyn) == NULL)
					fprintf(stderr,
					      "Couldn't get .dynamic data from \"%s\"\n",
					      filename);
#ifdef __mips__
/**
  MIPS ABI Supplement:

  DT_PLTGOT This member holds the address of the .got section.

  DT_MIPS_SYMTABNO This member holds the number of entries in the
  .dynsym section.

  DT_MIPS_LOCAL_GOTNO This member holds the number of local global
  offset table entries.

  DT_MIPS_GOTSYM This member holds the index of the first dyamic
  symbol table entry that corresponds to an entry in the gobal offset
  table.

 */
				if(dyn.d_tag==DT_PLTGOT){
					lte->pltgot_addr=dyn.d_un.d_ptr;
				}
				if(dyn.d_tag==DT_MIPS_LOCAL_GOTNO){
					lte->mips_local_gotno=dyn.d_un.d_val;
				}
				if(dyn.d_tag==DT_MIPS_GOTSYM){
					lte->mips_gotsym=dyn.d_un.d_val;
				}
#endif // __mips__
				if (dyn.d_tag == DT_JMPREL)
					relplt_addr = dyn.d_un.d_ptr;
				else if (dyn.d_tag == DT_PLTRELSZ)
					relplt_size = dyn.d_un.d_val;
				else if (dyn.d_tag == DT_PPC_GOT) {
					ppcgot = dyn.d_un.d_val;
					debug(1, "ppcgot %#" PRIx64, ppcgot);
				}
			}
		} else if (shdr.sh_type == SHT_HASH) {
			Elf_Data *data;
			size_t j;

			lte->hash_type = SHT_HASH;

			data = elf_getdata(scn, NULL);
			if (data == NULL || elf_getdata(scn, data) != NULL
			    || data->d_off || data->d_size != shdr.sh_size)
				fprintf(stderr,
				      "Couldn't get .hash data from \"%s\"",
				      filename);

			if (shdr.sh_entsize == 4) {
				/* Standard conforming ELF.  */
				if (data->d_type != ELF_T_WORD)
					fprintf(stderr,
					      "Couldn't get .hash data from \"%s\"",
					      filename);
				lte->hash = (Elf32_Word *) data->d_buf;
			} else if (shdr.sh_entsize == 8) {
				/* Alpha or s390x.  */
				Elf32_Word *dst, *src;
				size_t hash_count = data->d_size / 8;

				lte->hash = (Elf32_Word *)
				    malloc(hash_count * sizeof(Elf32_Word));
				if (lte->hash == NULL)
					fprintf(stderr,
					      "Couldn't convert .hash section from \"%s\"",
					      filename);
				lte->lte_flags |= LTE_HASH_MALLOCED;
				dst = lte->hash;
				src = (Elf32_Word *) data->d_buf;
				if ((data->d_type == ELF_T_WORD
				     && __BYTE_ORDER == __BIG_ENDIAN)
				    || (data->d_type == ELF_T_XWORD
					&& lte->ehdr.e_ident[EI_DATA] ==
					ELFDATA2MSB))
					++src;
				for (j = 0; j < hash_count; ++j, src += 2)
					*dst++ = *src;
			} else
				fprintf(stderr,
				      "Unknown .hash sh_entsize in \"%s\"",
				      filename);
		} else if (shdr.sh_type == SHT_GNU_HASH
			   && lte->hash == NULL) {
			Elf_Data *data;

			lte->hash_type = SHT_GNU_HASH;

			if (shdr.sh_entsize != 0
			    && shdr.sh_entsize != 4) {
				fprintf(stderr,
				      ".gnu.hash sh_entsize in \"%s\" "
					"should be 4, but is %#" PRIx64,
					filename, shdr.sh_entsize);
			}

			data = loaddata(scn, &shdr);
			if (data == NULL)
				fprintf(stderr,
				      "Couldn't get .gnu.hash data from \"%s\"",
				      filename);

			lte->hash = (Elf32_Word *) data->d_buf;
		} else if (shdr.sh_type == SHT_PROGBITS
			   || shdr.sh_type == SHT_NOBITS) {
			if (strcmp(name, ".plt") == 0) {
				lte->plt_addr = shdr.sh_addr;
				lte->plt_size = shdr.sh_size;
				if (shdr.sh_flags & SHF_EXECINSTR) {
					lte->lte_flags |= LTE_PLT_EXECUTABLE;
				}
				if (lte->ehdr.e_machine == EM_PPC) {
					plt_data = loaddata(scn, &shdr);
					if (plt_data == NULL)
						fprintf(stderr,
							"Can't load .plt data\n");
				}
			}
#ifdef ARCH_SUPPORTS_OPD
			else if (strcmp(name, ".opd") == 0) {
				lte->opd_addr = (GElf_Addr *) (long) shdr.sh_addr;
				lte->opd_size = shdr.sh_size;
				lte->opd = elf_rawdata(scn, NULL);
			}
#endif
		}
	}

	if (lte->dynsym == NULL || lte->dynstr == NULL)
		fprintf(stderr,
		      "Couldn't find .dynsym or .dynstr in \"%s\"", filename);

	if (!relplt_addr || !lte->plt_addr) {
		debug(1, "%s has no PLT relocations", filename);
		lte->relplt = NULL;
		lte->relplt_count = 0;
	} else if (relplt_size == 0) {
		debug(1, "%s has unknown PLT size", filename);
		lte->relplt = NULL;
		lte->relplt_count = 0;
	} else {
		if (lte->ehdr.e_machine == EM_PPC) {
			GElf_Addr glink_vma
				= get_glink_vma(lte, ppcgot, plt_data);

			assert (relplt_size % 12 == 0);
			size_t count = relplt_size / 12; // size of RELA entry
			lte->plt_stub_vma = glink_vma
				- (GElf_Addr)count * PPC_PLT_STUB_SIZE;
			debug(1, "stub_vma is %#" PRIx64, lte->plt_stub_vma);
		}

		for (i = 1; i < lte->ehdr.e_shnum; ++i) {
			Elf_Scn *scn;
			GElf_Shdr shdr;

			scn = elf_getscn(lte->elf, i);
			if (scn == NULL || gelf_getshdr(scn, &shdr) == NULL)
				fprintf(stderr,
				      "Couldn't get section header from \"%s\"",
				      filename);
			if (shdr.sh_addr == relplt_addr
			    && shdr.sh_size == relplt_size) {
				lte->relplt = elf_getdata(scn, NULL);
				lte->relplt_count =
				    shdr.sh_size / shdr.sh_entsize;
				if (lte->relplt == NULL
				    || elf_getdata(scn, lte->relplt) != NULL)
					fprintf(stderr,
					      "Couldn't get .rel*.plt data from \"%s\"",
					      filename);
				break;
			}
		}

		if (i == lte->ehdr.e_shnum)
			fprintf(stderr,
			      "Couldn't find .rel*.plt section in \"%s\"",
			      filename);

		debug(1, "%s %zd PLT relocations", filename, lte->relplt_count);
	}
}

void
do_close_elf(struct ltelf *lte) {
	debug(DEBUG_FUNCTION, "do_close_elf()");
	if (lte->lte_flags & LTE_HASH_MALLOCED)
		free((char *)lte->hash);
	elf_end(lte->elf);
	close(lte->fd);
}

void
add_library_symbol(GElf_Addr addr, const char *name,
		   struct library_symbol **library_symbolspp,
		   enum toplt type_of_plt, int is_weak) {
	struct library_symbol *s;

	debug(DEBUG_FUNCTION, "add_library_symbol()");

	s = malloc(sizeof(struct library_symbol) + strlen(name) + 1);
	if (s == NULL)
		error(EXIT_FAILURE, errno, "add_library_symbol failed");

	s->needs_init = 1;
	s->is_weak = is_weak;
	s->plt_type = type_of_plt;
	s->next = *library_symbolspp;
	s->enter_addr = (void *)(uintptr_t) addr;
	s->name = (char *)(s + 1);
	strcpy(s->name, name);
	*library_symbolspp = s;

	debug(2, "addr: %p, symbol: \"%s\"", (void *)(uintptr_t) addr, name);
}

/* stolen from elfutils-0.123 */
static unsigned long
private_elf_gnu_hash(const char *name) {
	unsigned long h = 5381;
	const unsigned char *string = (const unsigned char *)name;
	unsigned char c;
	for (c = *string; c; c = *++string)
		h = h * 33 + c;
	return h & 0xffffffff;
}

static int
symbol_matches(struct ltelf *lte, size_t lte_i, GElf_Sym *sym,
	       size_t symidx, const char *name)
{
	GElf_Sym tmp_sym;
	GElf_Sym *tmp;

	tmp = (sym) ? (sym) : (&tmp_sym);

	if (gelf_getsym(lte[lte_i].dynsym, symidx, tmp) == NULL)
		fprintf(stderr, "Couldn't get symbol from .dynsym");
	else {
		tmp->st_value += lte[lte_i].base_addr;
		debug(2, "symbol found: %s, %zd, %#" PRIx64,
		      name, lte_i, tmp->st_value);
	}
	return tmp->st_value != 0
		&& tmp->st_shndx != SHN_UNDEF
		&& strcmp(name, lte[lte_i].dynstr + tmp->st_name) == 0;
}

int
in_load_libraries(const char *name, struct ltelf *lte, size_t count, GElf_Sym *sym) {
	size_t i;
	unsigned long hash;
	unsigned long gnu_hash;

	if (!count)
		return 1;

#ifdef ELF_HASH_TAKES_SIGNED_CHAR
	hash = elf_hash(name);
#else
	hash = elf_hash((const unsigned char *)name);
#endif
	gnu_hash = private_elf_gnu_hash(name);

	for (i = 0; i < count; ++i) {
		if (lte[i].hash == NULL)
			continue;

		if (lte[i].hash_type == SHT_GNU_HASH) {
			Elf32_Word * hashbase = lte[i].hash;
			Elf32_Word nbuckets = *hashbase++;
			Elf32_Word symbias = *hashbase++;
			Elf32_Word bitmask_nwords = *hashbase++;
			Elf32_Word * buckets;
			Elf32_Word * chain_zero;
			Elf32_Word bucket;

			// +1 for skipped `shift'
			hashbase += lte[i].ehdr.e_ident[EI_CLASS] * bitmask_nwords + 1;
			buckets = hashbase;
			hashbase += nbuckets;
			chain_zero = hashbase - symbias;
			bucket = buckets[gnu_hash % nbuckets];

			if (bucket != 0) {
				const Elf32_Word *hasharr = &chain_zero[bucket];
				do
					if ((*hasharr & ~1u) == (gnu_hash & ~1u)) {
						int symidx = hasharr - chain_zero;
						if (symbol_matches(lte, i,
								   sym, symidx,
								   name))
							return 1;
					}
				while ((*hasharr++ & 1u) == 0);
			}
		} else {
			Elf32_Word nbuckets, symndx;
			Elf32_Word *buckets, *chain;
			nbuckets = lte[i].hash[0];
			buckets = &lte[i].hash[2];
			chain = &lte[i].hash[2 + nbuckets];

			for (symndx = buckets[hash % nbuckets];
			     symndx != STN_UNDEF; symndx = chain[symndx])
				if (symbol_matches(lte, i, sym, symndx, name))
					return 1;
		}
	}
	return 0;
}

static GElf_Addr
opd2addr(struct ltelf *lte, GElf_Addr addr) {
#ifdef ARCH_SUPPORTS_OPD
	unsigned long base, offset;

	if (!lte->opd)
		return addr;

	base = (unsigned long)lte->opd->d_buf;
	offset = (unsigned long)addr - (unsigned long)lte->opd_addr;
	if (offset > lte->opd_size)
		fprintf(stderr, "static plt not in .opd");

	return *(GElf_Addr*)(base + offset);
#else //!ARCH_SUPPORTS_OPD
	return addr;
#endif
}

struct library_symbol *
read_elf(Process *proc) {
	struct ltelf lte[MAX_LIBRARIES + 1];
	size_t i;
	struct opt_x_t *xptr;
	struct library_symbol **lib_tail = NULL;
	int exit_out = 0;
	int count = 0, lib_index = 0, max_index = 1;

	debug(DEBUG_FUNCTION, "read_elf(file=%s)", proc->filename);

	memset(lte, 0, sizeof(lte));
	library_symbols = NULL;
	library_num = 0;
	proc->libdl_hooked = 0;

	elf_version(EV_CURRENT);

	do_init_elf(lte, proc->filename);

	memcpy(&main_lte, lte, sizeof(struct ltelf));

	if (opt_p && opt_p->pid > 0) {
		linkmap_init(proc, lte);
		proc->libdl_hooked = 1;
	}

	proc->e_machine = lte->ehdr.e_machine;

	for (i = 0; i < library_num; ++i) {
		do_init_elf(&lte[i + 1], library[i]);
	}

	if (!options.no_plt) {
#ifdef __mips__
		// MIPS doesn't use the PLT and the GOT entries get changed
		// on startup.
		proc->need_to_reinitialize_breakpoints = 1;
		for(i=lte->mips_gotsym; i<lte->dynsym_count;i++){
			GElf_Sym sym;
			const char *name;
			GElf_Addr addr = arch_plt_sym_val(lte, i, 0);
			if (gelf_getsym(lte->dynsym, i, &sym) == NULL){
				fprintf(stderr,
						"Couldn't get relocation from \"%s\"",
						proc->filename);
			}
			name=lte->dynstr+sym.st_name;
			if(ELF64_ST_TYPE(sym.st_info) != STT_FUNC){
				debug(2,"sym %s not a function",name);
				continue;
			}
			add_library_symbol(addr, name, &library_symbols, 0,
					ELF64_ST_BIND(sym.st_info) != 0);
			if (!lib_tail)
				lib_tail = &(library_symbols->next);
		}
#else
        //modify for android
        if(opt_p && opt_p->pid)
            max_index = library_num+1;
        else
            max_index = 1;

        for( lib_index = 0; lib_index < max_index && lte[lib_index].elf != NULL; ++lib_index)
            for (i = 0; i < lte[lib_index].relplt_count; ++i) {
    			GElf_Rel rel;
    			GElf_Rela rela;
    			GElf_Sym sym;
    			GElf_Addr addr;
    			void *ret;
    			const char *name;
                struct ltelf *plte = &lte[lib_index];

    			if (plte->relplt->d_type == ELF_T_REL) {
    				ret = gelf_getrel(plte->relplt, i, &rel);
    				rela.r_offset = rel.r_offset;
    				rela.r_info = rel.r_info;
    				rela.r_addend = 0;
    			} else
    				ret = gelf_getrela(plte->relplt, i, &rela);

    			if (ret == NULL
    					|| ELF64_R_SYM(rela.r_info) >= plte->dynsym_count
    					|| gelf_getsym(plte->dynsym, ELF64_R_SYM(rela.r_info),
    						&sym) == NULL)
    				fprintf(stderr,
    						"Couldn't get relocation from \"%s\"",
    						proc->filename);

#ifdef PLT_REINITALISATION_BP
    			if (!sym.st_value && PLTs_initialized_by_here)
    				proc->need_to_reinitialize_breakpoints = 1;
#endif

    			name = plte->dynstr + sym.st_name;
    			count = library_num ? library_num+1 : 0;

    			if (in_load_libraries(name, lte, count, NULL)) {
    				enum toplt pltt;
    				if (sym.st_value == 0 && plte->plt_stub_vma != 0) {
    					pltt = LS_TOPLT_EXEC;
    					addr = plte->plt_stub_vma + PPC_PLT_STUB_SIZE * i;
    				}
    				else {
    					pltt = PLTS_ARE_EXECUTABLE(plte)
    						?  LS_TOPLT_EXEC : LS_TOPLT_POINT;
    					addr = arch_plt_sym_val(plte, i, &rela);
    				}

    				add_library_symbol(addr+(plte->base_addr), name, &library_symbols, pltt,
    						ELF64_ST_BIND(sym.st_info) == STB_WEAK);
    				if (!lib_tail)
    					lib_tail = &(library_symbols->next);

    			}
    		}
#endif // !__mips__
#ifdef PLT_REINITALISATION_BP
		struct opt_x_t *main_cheat;

		if (proc->need_to_reinitialize_breakpoints) {
			/* Add "PLTs_initialized_by_here" to opt_x list, if not
				 already there. */
			main_cheat = (struct opt_x_t *)malloc(sizeof(struct opt_x_t));
			if (main_cheat == NULL)
				fprintf(stderr, "Couldn't allocate memory");
			main_cheat->next = opt_x;
			main_cheat->found = 0;
			main_cheat->name = PLTs_initialized_by_here;

			for (xptr = opt_x; xptr; xptr = xptr->next)
				if (strcmp(xptr->name, PLTs_initialized_by_here) == 0
						&& main_cheat) {
					free(main_cheat);
					main_cheat = NULL;
					break;
				}
			if (main_cheat)
				opt_x = main_cheat;
		}
#endif
	} else {
		lib_tail = &library_symbols;
	}

	for (i = 0; i < lte->symtab_count; ++i) {
		GElf_Sym sym;
		GElf_Addr addr;
		const char *name;

		if (gelf_getsym(lte->symtab, i, &sym) == NULL)
			fprintf(stderr,
			      "Couldn't get symbol from \"%s\"",
			      proc->filename);

		name = lte->strtab + sym.st_name;
		addr = sym.st_value;
		if (!addr)
			continue;

		for (xptr = opt_x; xptr; xptr = xptr->next)
			if (xptr->name && strcmp(xptr->name, name) == 0) {
				/* FIXME: Should be able to use &library_symbols as above.  But
				   when you do, none of the real library symbols cause breaks. */
				add_library_symbol(opd2addr(lte, addr),
						   name, lib_tail, LS_TOPLT_NONE, 0);
				xptr->found = 1;
				break;
			}
	}

	unsigned found_count = 0;

	for (xptr = opt_x; xptr; xptr = xptr->next) {
		if (xptr->found)
			continue;

		GElf_Sym sym;
		GElf_Addr addr;
		if (in_load_libraries(xptr->name, lte, library_num+1, &sym)) {
			debug(2, "found symbol %s @ %#" PRIx64 ", adding it.",
					xptr->name, sym.st_value);
			addr = sym.st_value;
			if (ELF32_ST_TYPE (sym.st_info) == STT_FUNC) {
				add_library_symbol(addr, xptr->name, lib_tail, LS_TOPLT_NONE, 0);
				xptr->found = 1;
				found_count++;
			}
		}
		if (found_count == opt_x_cnt){
			debug(2, "done, found everything: %d\n", found_count);
			break;
		}
	}

	for (xptr = opt_x; xptr; xptr = xptr->next)
		if ( ! xptr->found) {
			char *badthing = "WARNING";
#ifdef PLT_REINITALISATION_BP
			if (strcmp(xptr->name, PLTs_initialized_by_here) == 0) {
				if (lte->ehdr.e_entry) {
					add_library_symbol (
						opd2addr (lte, lte->ehdr.e_entry),
						PLTs_initialized_by_here,
						lib_tail, 1, 0);
					fprintf (stderr, "WARNING: Using e_ent"
						 "ry from elf header (%p) for "
						 "address of \"%s\"\n", (void*)
						 (long) lte->ehdr.e_entry,
						 PLTs_initialized_by_here);
					continue;
				}
				badthing = "ERROR";
				exit_out = 1;
			}
#endif
			fprintf (stderr,
				 "%s: Couldn't find symbol \"%s\" in file \"%s\" assuming it will be loaded by libdl!"
				 "\n", badthing, xptr->name, proc->filename);
		}
	if (exit_out) {
		exit (1);
	}

	for (i = 0; i < library_num + 1; ++i)
		do_close_elf(&lte[i]);

	return library_symbols;
}
