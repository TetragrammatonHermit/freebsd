/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 1998 Peter Wemm <peter@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/exec.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/stdint.h>
#include <string.h>
#include <machine/elf.h>
#include <stand.h>
#define FREEBSD_ELF
#include <link.h>

#include "bootstrap.h"

#define COPYOUT(s,d,l)	archsw.arch_copyout((vm_offset_t)(s), d, l)

#if defined(__i386__) && __ELF_WORD_SIZE == 64
#undef ELF_TARG_CLASS
#undef ELF_TARG_MACH
#define ELF_TARG_CLASS  ELFCLASS64
#define ELF_TARG_MACH   EM_X86_64
#endif

typedef struct elf_file {
    Elf_Phdr 	*ph;
    Elf_Ehdr	*ehdr;
    Elf_Sym	*symtab;
    Elf_Hashelt	*hashtab;
    Elf_Hashelt	nbuckets;
    Elf_Hashelt	nchains;
    Elf_Hashelt	*buckets;
    Elf_Hashelt	*chains;
    Elf_Rel	*rel;
    size_t	relsz;
    Elf_Rela	*rela;
    size_t	relasz;
    char	*strtab;
    size_t	strsz;
    int		fd;
    caddr_t	firstpage;
    size_t	firstlen;
    int		kernel;
    u_int64_t	off;
} *elf_file_t;

static int __elfN(loadimage)(struct preloaded_file *mp, elf_file_t ef, u_int64_t loadaddr);
static int __elfN(lookup_symbol)(struct preloaded_file *mp, elf_file_t ef, const char* name, Elf_Sym* sym);
static int __elfN(reloc_ptr)(struct preloaded_file *mp, elf_file_t ef,
    Elf_Addr p, void *val, size_t len);
static int __elfN(parse_modmetadata)(struct preloaded_file *mp, elf_file_t ef);
static symaddr_fn __elfN(symaddr);
static char	*fake_modname(const char *name);

const char	*__elfN(kerneltype) = "elf kernel";
const char	*__elfN(moduletype) = "elf module";

u_int64_t	__elfN(relocation_offset) = 0;

/*
 * Attempt to load the file (file) as an ELF module.  It will be stored at
 * (dest), and a pointer to a module structure describing the loaded object
 * will be saved in (result).
 */
int
__elfN(loadfile)(char *filename, u_int64_t dest, struct preloaded_file **result)
{
    struct preloaded_file	*fp, *kfp;
    struct elf_file		ef;
    Elf_Ehdr 			*ehdr;
    int				err;
    ssize_t			bytes_read;

    fp = NULL;
    bzero(&ef, sizeof(struct elf_file));

    /*
     * Open the image, read and validate the ELF header 
     */
    if (filename == NULL)	/* can't handle nameless */
	return(EFTYPE);
    if ((ef.fd = open(filename, O_RDONLY)) == -1)
	return(errno);
    ef.firstpage = malloc(PAGE_SIZE);
    if (ef.firstpage == NULL) {
	close(ef.fd);
	return(ENOMEM);
    }
    bytes_read = read(ef.fd, ef.firstpage, PAGE_SIZE);
    ef.firstlen = (size_t)bytes_read;
    if (bytes_read < 0 || ef.firstlen <= sizeof(Elf_Ehdr)) {
	err = EFTYPE;		/* could be EIO, but may be small file */
	goto oerr;
    }
    ehdr = ef.ehdr = (Elf_Ehdr *)ef.firstpage;

    /* Is it ELF? */
    if (!IS_ELF(*ehdr)) {
	err = EFTYPE;
	goto oerr;
    }
    if (ehdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||	/* Layout ? */
	ehdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
	ehdr->e_ident[EI_VERSION] != EV_CURRENT ||	/* Version ? */
	ehdr->e_version != EV_CURRENT ||
	ehdr->e_machine != ELF_TARG_MACH) {		/* Machine ? */
	err = EFTYPE;
	goto oerr;
    }


    /*
     * Check to see what sort of module we are.
     */
    kfp = file_findfile(NULL, NULL);
    if (ehdr->e_type == ET_DYN) {
	/* Looks like a kld module */
	if (kfp == NULL) {
	    printf("elf" __XSTRING(__ELF_WORD_SIZE) "_loadfile: can't load module before kernel\n");
	    err = EPERM;
	    goto oerr;
	}
	if (strcmp(__elfN(kerneltype), kfp->f_type)) {
	    printf("elf" __XSTRING(__ELF_WORD_SIZE) "_loadfile: can't load module with kernel type '%s'\n", kfp->f_type);
	    err = EPERM;
	    goto oerr;
	}
	/* Looks OK, got ahead */
	ef.kernel = 0;

    } else if (ehdr->e_type == ET_EXEC) {
	/* Looks like a kernel */
	if (kfp != NULL) {
	    printf("elf" __XSTRING(__ELF_WORD_SIZE) "_loadfile: kernel already loaded\n");
	    err = EPERM;
	    goto oerr;
	}
	/* 
	 * Calculate destination address based on kernel entrypoint 	
	 */
	dest = (ehdr->e_entry & ~PAGE_MASK);
	if (dest == 0) {
	    printf("elf" __XSTRING(__ELF_WORD_SIZE) "_loadfile: not a kernel (maybe static binary?)\n");
	    err = EPERM;
	    goto oerr;
	}
	ef.kernel = 1;

    } else {
	err = EFTYPE;
	goto oerr;
    }

    if (archsw.arch_loadaddr != NULL)
	dest = archsw.arch_loadaddr(LOAD_ELF, ehdr, dest);
    else
	dest = roundup(dest, PAGE_SIZE);

    /* 
     * Ok, we think we should handle this.
     */
    fp = file_alloc();
    if (fp == NULL) {
	    printf("elf" __XSTRING(__ELF_WORD_SIZE) "_loadfile: cannot allocate module info\n");
	    err = EPERM;
	    goto out;
    }
    if (ef.kernel)
	setenv("kernelname", filename, 1);
    fp->f_name = strdup(filename);
    fp->f_type = strdup(ef.kernel ? __elfN(kerneltype) : __elfN(moduletype));

#ifdef ELF_VERBOSE
    if (ef.kernel)
	printf("%s entry at 0x%jx\n", filename, (uintmax_t)ehdr->e_entry);
#else
    printf("%s ", filename);
#endif

    fp->f_size = __elfN(loadimage)(fp, &ef, dest);
    if (fp->f_size == 0 || fp->f_addr == 0)
	goto ioerr;

    /* save exec header as metadata */
    file_addmetadata(fp, MODINFOMD_ELFHDR, sizeof(*ehdr), ehdr);

    /* Load OK, return module pointer */
    *result = (struct preloaded_file *)fp;
    err = 0;
    goto out;
    
 ioerr:
    err = EIO;
 oerr:
    file_discard(fp);
 out:
    if (ef.firstpage)
	free(ef.firstpage);
    close(ef.fd);
    return(err);
}

/*
 * With the file (fd) open on the image, and (ehdr) containing
 * the Elf header, load the image at (off)
 */
static int
__elfN(loadimage)(struct preloaded_file *fp, elf_file_t ef, u_int64_t off)
{
    int 	i;
    u_int	j;
    Elf_Ehdr	*ehdr;
    Elf_Phdr	*phdr, *php;
    Elf_Shdr	*shdr;
    char	*shstr;
    int		ret;
    vm_offset_t firstaddr;
    vm_offset_t lastaddr;
    size_t	chunk;
    ssize_t	result;
    Elf_Addr	ssym, esym;
    Elf_Dyn	*dp;
    Elf_Addr	adp;
    Elf_Addr	ctors;
    int		ndp;
    int		symstrindex;
    int		symtabindex;
    Elf_Size	size;
    u_int	fpcopy;

    dp = NULL;
    shdr = NULL;
    ret = 0;
    firstaddr = lastaddr = 0;
    ehdr = ef->ehdr;
    if (ef->kernel) {
#if defined(__i386__) || defined(__amd64__)
#if __ELF_WORD_SIZE == 64
	off = - (off & 0xffffffffff000000ull);/* x86_64 relocates after locore */
#else
	off = - (off & 0xff000000u);	/* i386 relocates after locore */
#endif
#elif defined(__powerpc__)
	/*
	 * On the purely virtual memory machines like e500, the kernel is
	 * linked against its final VA range, which is most often not
	 * available at the loader stage, but only after kernel initializes
	 * and completes its VM settings. In such cases we cannot use p_vaddr
	 * field directly to load ELF segments, but put them at some
	 * 'load-time' locations.
	 */
	if (off & 0xf0000000u) {
	    off = -(off & 0xf0000000u);
	    /*
	     * XXX the physical load address should not be hardcoded. Note
	     * that the Book-E kernel assumes that it's loaded at a 16MB
	     * boundary for now...
	     */
	    off += 0x01000000;
	    ehdr->e_entry += off;
#ifdef ELF_VERBOSE
	    printf("Converted entry 0x%08x\n", ehdr->e_entry);
#endif
	} else
	    off = 0;
#elif defined(__arm__)
	/*
	 * The elf headers in some kernels specify virtual addresses in all
	 * header fields.  More recently, the e_entry and p_paddr fields are the
	 * proper physical addresses.  Even when the p_paddr fields are correct,
	 * the MI code below uses the p_vaddr fields with an offset added for
	 * loading (doing so is arguably wrong).  To make loading work, we need
	 * an offset that represents the difference between physical and virtual
	 * addressing.  ARM kernels are always linked at 0xCnnnnnnn.  Depending
	 * on the headers, the offset value passed in may be physical or virtual
	 * (because it typically comes from e_entry), but we always replace
	 * whatever is passed in with the va<->pa offset.  On the other hand, we
	 * always remove the high-order part of the entry address whether it's
	 * physical or virtual, because it will be adjusted later for the actual
	 * physical entry point based on where the image gets loaded.
	 */
	off = -0xc0000000;
	ehdr->e_entry &= ~0xf0000000;
#ifdef ELF_VERBOSE
	printf("ehdr->e_entry 0x%08x, va<->pa off %llx\n", ehdr->e_entry, off);
#endif
#else
	off = 0;		/* other archs use direct mapped kernels */
#endif
	__elfN(relocation_offset) = off;
    }
    ef->off = off;

    if ((ehdr->e_phoff + ehdr->e_phnum * sizeof(*phdr)) > ef->firstlen) {
	printf("elf" __XSTRING(__ELF_WORD_SIZE) "_loadimage: program header not within first page\n");
	goto out;
    }
    phdr = (Elf_Phdr *)(ef->firstpage + ehdr->e_phoff);

    for (i = 0; i < ehdr->e_phnum; i++) {
	/* We want to load PT_LOAD segments only.. */
	if (phdr[i].p_type != PT_LOAD)
	    continue;

#ifdef ELF_VERBOSE
	printf("Segment: 0x%lx@0x%lx -> 0x%lx-0x%lx",
	    (long)phdr[i].p_filesz, (long)phdr[i].p_offset,
	    (long)(phdr[i].p_vaddr + off),
	    (long)(phdr[i].p_vaddr + off + phdr[i].p_memsz - 1));
#else
	if ((phdr[i].p_flags & PF_W) == 0) {
	    printf("text=0x%lx ", (long)phdr[i].p_filesz);
	} else {
	    printf("data=0x%lx", (long)phdr[i].p_filesz);
	    if (phdr[i].p_filesz < phdr[i].p_memsz)
		printf("+0x%lx", (long)(phdr[i].p_memsz -phdr[i].p_filesz));
	    printf(" ");
	}
#endif
	fpcopy = 0;
	if (ef->firstlen > phdr[i].p_offset) {
	    fpcopy = ef->firstlen - phdr[i].p_offset;
	    archsw.arch_copyin(ef->firstpage + phdr[i].p_offset,
			       phdr[i].p_vaddr + off, fpcopy);
	}
	if (phdr[i].p_filesz > fpcopy) {
	    if (kern_pread(ef->fd, phdr[i].p_vaddr + off + fpcopy,
		phdr[i].p_filesz - fpcopy, phdr[i].p_offset + fpcopy) != 0) {
		printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
		    "_loadimage: read failed\n");
		goto out;
	    }
	}
	/* clear space from oversized segments; eg: bss */
	if (phdr[i].p_filesz < phdr[i].p_memsz) {
#ifdef ELF_VERBOSE
	    printf(" (bss: 0x%lx-0x%lx)",
		(long)(phdr[i].p_vaddr + off + phdr[i].p_filesz),
		(long)(phdr[i].p_vaddr + off + phdr[i].p_memsz - 1));
#endif

	    kern_bzero(phdr[i].p_vaddr + off + phdr[i].p_filesz,
		phdr[i].p_memsz - phdr[i].p_filesz);
	}
#ifdef ELF_VERBOSE
	printf("\n");
#endif

	if (archsw.arch_loadseg != NULL)
	    archsw.arch_loadseg(ehdr, phdr + i, off);

	if (firstaddr == 0 || firstaddr > (phdr[i].p_vaddr + off))
	    firstaddr = phdr[i].p_vaddr + off;
	if (lastaddr == 0 || lastaddr < (phdr[i].p_vaddr + off + phdr[i].p_memsz))
	    lastaddr = phdr[i].p_vaddr + off + phdr[i].p_memsz;
    }
    lastaddr = roundup(lastaddr, sizeof(long));

    /*
     * Get the section headers.  We need this for finding the .ctors
     * section as well as for loading any symbols.  Both may be hard
     * to do if reading from a .gz file as it involves seeking.  I
     * think the rule is going to have to be that you must strip a
     * file to remove symbols before gzipping it.
     */
    chunk = ehdr->e_shnum * ehdr->e_shentsize;
    if (chunk == 0 || ehdr->e_shoff == 0)
	goto nosyms;
    shdr = alloc_pread(ef->fd, ehdr->e_shoff, chunk);
    if (shdr == NULL) {
	printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
	    "_loadimage: failed to read section headers");
	goto nosyms;
    }
    file_addmetadata(fp, MODINFOMD_SHDR, chunk, shdr);

    /*
     * Read the section string table and look for the .ctors section.
     * We need to tell the kernel where it is so that it can call the
     * ctors.
     */
    chunk = shdr[ehdr->e_shstrndx].sh_size;
    if (chunk) {
	shstr = alloc_pread(ef->fd, shdr[ehdr->e_shstrndx].sh_offset, chunk);
	if (shstr) {
	    for (i = 0; i < ehdr->e_shnum; i++) {
		if (strcmp(shstr + shdr[i].sh_name, ".ctors") != 0)
		    continue;
		ctors = shdr[i].sh_addr;
		file_addmetadata(fp, MODINFOMD_CTORS_ADDR, sizeof(ctors),
		    &ctors);
		size = shdr[i].sh_size;
		file_addmetadata(fp, MODINFOMD_CTORS_SIZE, sizeof(size),
		    &size);
		break;
	    }
	    free(shstr);
	}
    }

    /*
     * Now load any symbols.
     */
    symtabindex = -1;
    symstrindex = -1;
    for (i = 0; i < ehdr->e_shnum; i++) {
	if (shdr[i].sh_type != SHT_SYMTAB)
	    continue;
	for (j = 0; j < ehdr->e_phnum; j++) {
	    if (phdr[j].p_type != PT_LOAD)
		continue;
	    if (shdr[i].sh_offset >= phdr[j].p_offset &&
		(shdr[i].sh_offset + shdr[i].sh_size <=
		 phdr[j].p_offset + phdr[j].p_filesz)) {
		shdr[i].sh_offset = 0;
		shdr[i].sh_size = 0;
		break;
	    }
	}
	if (shdr[i].sh_offset == 0 || shdr[i].sh_size == 0)
	    continue;		/* alread loaded in a PT_LOAD above */
	/* Save it for loading below */
	symtabindex = i;
	symstrindex = shdr[i].sh_link;
    }
    if (symtabindex < 0 || symstrindex < 0)
	goto nosyms;

    /* Ok, committed to a load. */
#ifndef ELF_VERBOSE
    printf("syms=[");
#endif
    ssym = lastaddr;
    for (i = symtabindex; i >= 0; i = symstrindex) {
#ifdef ELF_VERBOSE
	char	*secname;

	switch(shdr[i].sh_type) {
	    case SHT_SYMTAB:		/* Symbol table */
		secname = "symtab";
		break;
	    case SHT_STRTAB:		/* String table */
		secname = "strtab";
		break;
	    default:
		secname = "WHOA!!";
		break;
	}
#endif

	size = shdr[i].sh_size;
	archsw.arch_copyin(&size, lastaddr, sizeof(size));
	lastaddr += sizeof(size);

#ifdef ELF_VERBOSE
	printf("\n%s: 0x%jx@0x%jx -> 0x%jx-0x%jx", secname,
	    (uintmax_t)shdr[i].sh_size, (uintmax_t)shdr[i].sh_offset,
	    (uintmax_t)lastaddr, (uintmax_t)(lastaddr + shdr[i].sh_size));
#else
	if (i == symstrindex)
	    printf("+");
	printf("0x%lx+0x%lx", (long)sizeof(size), (long)size);
#endif

	if (lseek(ef->fd, (off_t)shdr[i].sh_offset, SEEK_SET) == -1) {
	    printf("\nelf" __XSTRING(__ELF_WORD_SIZE) "_loadimage: could not seek for symbols - skipped!");
	    lastaddr = ssym;
	    ssym = 0;
	    goto nosyms;
	}
	result = archsw.arch_readin(ef->fd, lastaddr, shdr[i].sh_size);
	if (result < 0 || (size_t)result != shdr[i].sh_size) {
	    printf("\nelf" __XSTRING(__ELF_WORD_SIZE) "_loadimage: could not read symbols - skipped! (%ju != %ju)", (uintmax_t)result,
		(uintmax_t)shdr[i].sh_size);
	    lastaddr = ssym;
	    ssym = 0;
	    goto nosyms;
	}
	/* Reset offsets relative to ssym */
	lastaddr += shdr[i].sh_size;
	lastaddr = roundup(lastaddr, sizeof(size));
	if (i == symtabindex)
	    symtabindex = -1;
	else if (i == symstrindex)
	    symstrindex = -1;
    }
    esym = lastaddr;
#ifndef ELF_VERBOSE
    printf("]");
#endif

    file_addmetadata(fp, MODINFOMD_SSYM, sizeof(ssym), &ssym);
    file_addmetadata(fp, MODINFOMD_ESYM, sizeof(esym), &esym);

nosyms:
    printf("\n");

    ret = lastaddr - firstaddr;
    fp->f_addr = firstaddr;

    php = NULL;
    for (i = 0; i < ehdr->e_phnum; i++) {
	if (phdr[i].p_type == PT_DYNAMIC) {
	    php = phdr + i;
	    adp = php->p_vaddr;
	    file_addmetadata(fp, MODINFOMD_DYNAMIC, sizeof(adp), &adp);
	    break;
	}
    }

    if (php == NULL)	/* this is bad, we cannot get to symbols or _DYNAMIC */
	goto out;

    ndp = php->p_filesz / sizeof(Elf_Dyn);
    if (ndp == 0)
	goto out;
    dp = malloc(php->p_filesz);
    if (dp == NULL)
	goto out;
    archsw.arch_copyout(php->p_vaddr + off, dp, php->p_filesz);

    ef->strsz = 0;
    for (i = 0; i < ndp; i++) {
	if (dp[i].d_tag == 0)
	    break;
	switch (dp[i].d_tag) {
	case DT_HASH:
	    ef->hashtab = (Elf_Hashelt*)(uintptr_t)(dp[i].d_un.d_ptr + off);
	    break;
	case DT_STRTAB:
	    ef->strtab = (char *)(uintptr_t)(dp[i].d_un.d_ptr + off);
	    break;
	case DT_STRSZ:
	    ef->strsz = dp[i].d_un.d_val;
	    break;
	case DT_SYMTAB:
	    ef->symtab = (Elf_Sym*)(uintptr_t)(dp[i].d_un.d_ptr + off);
	    break;
	case DT_REL:
	    ef->rel = (Elf_Rel *)(uintptr_t)(dp[i].d_un.d_ptr + off);
	    break;
	case DT_RELSZ:
	    ef->relsz = dp[i].d_un.d_val;
	    break;
	case DT_RELA:
	    ef->rela = (Elf_Rela *)(uintptr_t)(dp[i].d_un.d_ptr + off);
	    break;
	case DT_RELASZ:
	    ef->relasz = dp[i].d_un.d_val;
	    break;
	default:
	    break;
	}
    }
    if (ef->hashtab == NULL || ef->symtab == NULL ||
	ef->strtab == NULL || ef->strsz == 0)
	goto out;
    COPYOUT(ef->hashtab, &ef->nbuckets, sizeof(ef->nbuckets));
    COPYOUT(ef->hashtab + 1, &ef->nchains, sizeof(ef->nchains));
    ef->buckets = ef->hashtab + 2;
    ef->chains = ef->buckets + ef->nbuckets;
    if (__elfN(parse_modmetadata)(fp, ef) == 0)
	goto out;

    if (ef->kernel)			/* kernel must not depend on anything */
	goto out;

out:
    if (dp)
	free(dp);
    if (shdr)
	free(shdr);
    return ret;
}

static char invalid_name[] = "bad";

char *
fake_modname(const char *name)
{
    const char *sp, *ep;
    char *fp;
    size_t len;

    sp = strrchr(name, '/');
    if (sp)
	sp++;
    else
	sp = name;
    ep = strrchr(name, '.');
    if (ep) {
	    if (ep == name) {
		sp = invalid_name;
		ep = invalid_name + sizeof(invalid_name) - 1;
	    } 
    } else
	ep = name + strlen(name);
    len = ep - sp;
    fp = malloc(len + 1);
    if (fp == NULL)
	return NULL;
    memcpy(fp, sp, len);
    fp[len] = '\0';
    return fp;
}

#if (defined(__i386__) || defined(__powerpc__)) && __ELF_WORD_SIZE == 64
struct mod_metadata64 {
	int		md_version;	/* structure version MDTV_* */  
	int		md_type;	/* type of entry MDT_* */
	u_int64_t	md_data;	/* specific data */
	u_int64_t	md_cval;	/* common string label */
};
#endif

int
__elfN(parse_modmetadata)(struct preloaded_file *fp, elf_file_t ef)
{
    struct mod_metadata md;
#if (defined(__i386__) || defined(__powerpc__)) && __ELF_WORD_SIZE == 64
    struct mod_metadata64 md64;
#endif
    struct mod_depend *mdepend;
    struct mod_version mver;
    Elf_Sym sym;
    char *s;
    int error, modcnt, minfolen;
    Elf_Addr v, p, p_stop;

    if (__elfN(lookup_symbol)(fp, ef, "__start_set_modmetadata_set", &sym) != 0)
	return 0;
    p = sym.st_value + ef->off;
    if (__elfN(lookup_symbol)(fp, ef, "__stop_set_modmetadata_set", &sym) != 0)
	return ENOENT;
    p_stop = sym.st_value + ef->off;

    modcnt = 0;
    while (p < p_stop) {
	COPYOUT(p, &v, sizeof(v));
	error = __elfN(reloc_ptr)(fp, ef, p, &v, sizeof(v));
	if (error == EOPNOTSUPP)
	    v += ef->off;
	else if (error != 0)
	    return (error);
#if (defined(__i386__) || defined(__powerpc__)) && __ELF_WORD_SIZE == 64
	COPYOUT(v, &md64, sizeof(md64));
	error = __elfN(reloc_ptr)(fp, ef, v, &md64, sizeof(md64));
	if (error == EOPNOTSUPP) {
	    md64.md_cval += ef->off;
	    md64.md_data += ef->off;
	} else if (error != 0)
	    return (error);
	md.md_version = md64.md_version;
	md.md_type = md64.md_type;
	md.md_cval = (const char *)(uintptr_t)md64.md_cval;
	md.md_data = (void *)(uintptr_t)md64.md_data;
#else
	COPYOUT(v, &md, sizeof(md));
	error = __elfN(reloc_ptr)(fp, ef, v, &md, sizeof(md));
	if (error == EOPNOTSUPP) {
	    md.md_cval += ef->off;
	    md.md_data += ef->off;
	} else if (error != 0)
	    return (error);
#endif
	p += sizeof(Elf_Addr);
	switch(md.md_type) {
	  case MDT_DEPEND:
	    if (ef->kernel)		/* kernel must not depend on anything */
	      break;
	    s = strdupout((vm_offset_t)md.md_cval);
	    minfolen = sizeof(*mdepend) + strlen(s) + 1;
	    mdepend = malloc(minfolen);
	    if (mdepend == NULL)
		return ENOMEM;
	    COPYOUT((vm_offset_t)md.md_data, mdepend, sizeof(*mdepend));
	    strcpy((char*)(mdepend + 1), s);
	    free(s);
	    file_addmetadata(fp, MODINFOMD_DEPLIST, minfolen, mdepend);
	    free(mdepend);
	    break;
	  case MDT_VERSION:
	    s = strdupout((vm_offset_t)md.md_cval);
	    COPYOUT((vm_offset_t)md.md_data, &mver, sizeof(mver));
	    file_addmodule(fp, s, mver.mv_version, NULL);
	    free(s);
	    modcnt++;
	    break;
	}
    }
    if (modcnt == 0) {
	s = fake_modname(fp->f_name);
	file_addmodule(fp, s, 1, NULL);
	free(s);
    }
    return 0;
}

static unsigned long
elf_hash(const char *name)
{
    const unsigned char *p = (const unsigned char *) name;
    unsigned long h = 0;
    unsigned long g;

    while (*p != '\0') {
	h = (h << 4) + *p++;
	if ((g = h & 0xf0000000) != 0)
	    h ^= g >> 24;
	h &= ~g;
    }
    return h;
}

static const char __elfN(bad_symtable)[] = "elf" __XSTRING(__ELF_WORD_SIZE) "_lookup_symbol: corrupt symbol table\n";
int
__elfN(lookup_symbol)(struct preloaded_file *fp, elf_file_t ef, const char* name,
		  Elf_Sym *symp)
{
    Elf_Hashelt symnum;
    Elf_Sym sym;
    char *strp;
    unsigned long hash;

    hash = elf_hash(name);
    COPYOUT(&ef->buckets[hash % ef->nbuckets], &symnum, sizeof(symnum));

    while (symnum != STN_UNDEF) {
	if (symnum >= ef->nchains) {
	    printf(__elfN(bad_symtable));
	    return ENOENT;
	}

	COPYOUT(ef->symtab + symnum, &sym, sizeof(sym));
	if (sym.st_name == 0) {
	    printf(__elfN(bad_symtable));
	    return ENOENT;
	}

	strp = strdupout((vm_offset_t)(ef->strtab + sym.st_name));
	if (strcmp(name, strp) == 0) {
	    free(strp);
	    if (sym.st_shndx != SHN_UNDEF ||
		(sym.st_value != 0 &&
		 ELF_ST_TYPE(sym.st_info) == STT_FUNC)) {
		*symp = sym;
		return 0;
	    }
	    return ENOENT;
	}
	free(strp);
	COPYOUT(&ef->chains[symnum], &symnum, sizeof(symnum));
    }
    return ENOENT;
}

/*
 * Apply any intra-module relocations to the value. p is the load address
 * of the value and val/len is the value to be modified. This does NOT modify
 * the image in-place, because this is done by kern_linker later on.
 *
 * Returns EOPNOTSUPP if no relocation method is supplied.
 */
static int
__elfN(reloc_ptr)(struct preloaded_file *mp, elf_file_t ef,
    Elf_Addr p, void *val, size_t len)
{
	size_t n;
	Elf_Rela a;
	Elf_Rel r;
	int error;

	/*
	 * The kernel is already relocated, but we still want to apply
	 * offset adjustments.
	 */
	if (ef->kernel)
		return (EOPNOTSUPP);

	for (n = 0; n < ef->relsz / sizeof(r); n++) {
		COPYOUT(ef->rel + n, &r, sizeof(r));

		error = __elfN(reloc)(ef, __elfN(symaddr), &r, ELF_RELOC_REL,
		    ef->off, p, val, len);
		if (error != 0)
			return (error);
	}
	for (n = 0; n < ef->relasz / sizeof(a); n++) {
		COPYOUT(ef->rela + n, &a, sizeof(a));

		error = __elfN(reloc)(ef, __elfN(symaddr), &a, ELF_RELOC_RELA,
		    ef->off, p, val, len);
		if (error != 0)
			return (error);
	}

	return (0);
}

static Elf_Addr
__elfN(symaddr)(struct elf_file *ef, Elf_Size symidx)
{

	/* Symbol lookup by index not required here. */
	return (0);
}
