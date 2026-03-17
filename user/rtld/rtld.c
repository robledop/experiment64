#include "rtld.h"

/*
 * Self-relocation is performed in rtld_start.S before calling rtld_main().
 * The assembly accesses _DYNAMIC via a PC-relative lea to avoid any GOT
 * dependency, then applies R_X86_64_RELATIVE relocations to fix the GOT.
 */

/* ── Minimal string/memory helpers (no libc) ───────────────────────────── */

static size_t rtld_strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int rtld_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void *rtld_memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

static void *rtld_memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

/* ── Syscall convenience wrappers ──────────────────────────────────────── */

/**
 * @brief Write a buffer to a file descriptor.
 */
static long rtld_write(int fd, const void *buf, size_t count)
{
    return rtld_syscall3(RTLD_SYS_WRITE, fd, (long)buf, (long)count);
}

/**
 * @brief Open a file by path.
 * @return File descriptor on success, negative on error.
 */
static long rtld_open(const char *path, int flags)
{
    return rtld_syscall2(RTLD_SYS_OPEN, (long)path, flags);
}

/**
 * @brief Read from a file descriptor.
 */
static long rtld_read(int fd, void *buf, size_t count)
{
    return rtld_syscall3(RTLD_SYS_READ, fd, (long)buf, (long)count);
}

/**
 * @brief Close a file descriptor.
 */
static long rtld_close(int fd)
{
    return rtld_syscall1(RTLD_SYS_CLOSE, fd);
}

/**
 * @brief Map anonymous memory.
 */
static void *rtld_mmap(void *addr, size_t len, int prot, int flags, int fd, long offset)
{
    return (void *)rtld_syscall6(RTLD_SYS_MMAP,
                                 (long)addr, (long)len, prot, flags, fd, offset);
}

/**
 * @brief Terminate the process.
 */
__attribute__((noreturn))
static void rtld_exit(int code)
{
    rtld_syscall1(RTLD_SYS_EXIT, code);
    __builtin_unreachable();
}

/* ── Output helpers ────────────────────────────────────────────────────── */

void rtld_puts(const char *s)
{
    rtld_write(1, s, rtld_strlen(s));
}

__attribute__((noreturn))
void rtld_die(const char *msg)
{
    rtld_puts("ld.so: ");
    rtld_puts(msg);
    rtld_puts("\r\n");
    rtld_exit(127);
}

/**
 * @brief Print a 64-bit value in hexadecimal (for debug output).
 */
static void rtld_print_hex(uint64_t val)
{
    char buf[19]; /* "0x" + 16 hex digits + null */
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        int nibble = val & 0xF;
        buf[2 + i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        val >>= 4;
    }
    buf[18] = '\0';
    rtld_puts(buf);
}

/* ── Static DSO pool (no malloc) ───────────────────────────────────────── */

static dso_t dso_pool[RTLD_MAX_DSO];
static int dso_count = 0;

/** @brief Head of the DSO linked list (load order). */
static dso_t *dso_list = (void *)0;

/**
 * @brief Allocate a new DSO descriptor from the static pool.
 * @return Pointer to the new DSO, or dies if pool exhausted.
 */
static dso_t *dso_alloc(void)
{
    if (dso_count >= RTLD_MAX_DSO)
        rtld_die("too many shared objects");
    dso_t *d = &dso_pool[dso_count++];
    rtld_memset(d, 0, sizeof(*d));
    return d;
}

/**
 * @brief Append a DSO to the end of the global load-order list.
 */
static void dso_append(dso_t *d)
{
    d->next = (void *)0;
    if (!dso_list) {
        dso_list = d;
        return;
    }
    dso_t *tail = dso_list;
    while (tail->next) tail = tail->next;
    tail->next = d;
}

/* ── Auxiliary vector parsing ──────────────────────────────────────────── */

/**
 * @brief Parse the auxiliary vector from the initial stack pointer.
 *
 * Walks past argc, argv[], envp[] to reach the auxv array, then extracts
 * the values needed by the runtime linker.
 *
 * @param sp   Initial stack pointer (points to argc).
 * @param out  Output structure filled with extracted auxv values.
 */
static void rtld_parse_auxv(uint64_t *sp, rtld_auxv_t *out)
{
    rtld_memset(out, 0, sizeof(*out));

    /* Skip argc */
    uint64_t argc = *sp++;

    /* Skip argv[] (argc pointers + NULL terminator) */
    sp += argc + 1;

    /* Skip envp[] (walk to NULL terminator) */
    while (*sp) sp++;
    sp++; /* skip the NULL */

    /* Now sp points to the auxv array */
    while (1) {
        uint64_t type = sp[0];
        uint64_t val  = sp[1];
        sp += 2;

        switch (type) {
        case RTLD_AT_NULL:   return;
        case RTLD_AT_PHDR:   out->at_phdr   = val; break;
        case RTLD_AT_PHENT:  out->at_phent  = val; break;
        case RTLD_AT_PHNUM:  out->at_phnum  = val; break;
        case RTLD_AT_PAGESZ: out->at_pagesz = val; break;
        case RTLD_AT_BASE:   out->at_base   = val; break;
        case RTLD_AT_ENTRY:  out->at_entry  = val; break;
        }
    }
}

/* ── Dynamic section parsing ───────────────────────────────────────────── */

/**
 * @brief Parse the PT_DYNAMIC entries of a DSO and populate its metadata fields.
 *
 * Walks the dynamic section array to find the symbol table, string table,
 * hash table, and relocation tables. All pointers are adjusted by the DSO's
 * load bias.
 *
 * @param d DSO whose ->dynamic field must already be set.
 */
static void dso_parse_dynamic(dso_t *d)
{
    uint64_t rela_addr = 0, rela_sz = 0, rela_ent = 0;
    uint64_t jmprel_addr = 0, jmprel_sz = 0;

    for (rtld_Elf64_Dyn *dyn = d->dynamic; dyn->d_tag != RTLD_DT_NULL; dyn++) {
        switch (dyn->d_tag) {
        case RTLD_DT_SYMTAB:
            d->symtab = (rtld_elf64_sym *)(d->base + dyn->d_un.d_ptr);
            break;
        case RTLD_DT_STRTAB:
            d->strtab = (const char *)(d->base + dyn->d_un.d_ptr);
            break;
        case RTLD_DT_HASH:
            d->hash = (uint32_t *)(d->base + dyn->d_un.d_ptr);
            /* SysV hash: hash[0] = nbucket, hash[1] = nchain (= nsyms) */
            d->nsyms = d->hash[1];
            break;
        case RTLD_DT_RELA:     rela_addr = dyn->d_un.d_ptr; break;
        case RTLD_DT_RELASZ:   rela_sz   = dyn->d_un.d_val; break;
        case RTLD_DT_RELAENT:  rela_ent  = dyn->d_un.d_val; break;
        case RTLD_DT_JMPREL:   jmprel_addr = dyn->d_un.d_ptr; break;
        case RTLD_DT_PLTRELSZ: jmprel_sz   = dyn->d_un.d_val; break;
        }
    }

    if (rela_addr && rela_ent) {
        d->rela = (rtld_Elf64_Rela *)(d->base + rela_addr);
        d->rela_count = rela_sz / rela_ent;
    }
    if (jmprel_addr) {
        d->jmprel = (rtld_Elf64_Rela *)(d->base + jmprel_addr);
        d->jmprel_count = jmprel_sz / sizeof(rtld_Elf64_Rela);
    }
}

/* ── Symbol resolution ─────────────────────────────────────────────────── */

/**
 * @brief Look up a symbol by name across all loaded DSOs.
 *
 * Searches DSOs in load order (main executable first, then libraries).
 * Only considers globally-bound, defined symbols.
 *
 * @param name  Symbol name to look up.
 * @param skip  DSO to skip during search (used for R_X86_64_COPY to avoid
 *              finding the copy relocation target in the executable itself).
 *              May be NULL.
 * @param out_sym  If non-NULL, filled with the found symbol entry.
 * @param out_dso  If non-NULL, filled with the DSO containing the symbol.
 * @return Resolved virtual address of the symbol, or 0 if not found.
 */
static uint64_t rtld_resolve_symbol(const char *name, const dso_t *skip,
                                    rtld_elf64_sym **out_sym, dso_t **out_dso)
{
    for (dso_t *d = dso_list; d; d = d->next) {
        if (d == skip) continue;
        if (!d->symtab || !d->strtab || !d->nsyms) continue;

        for (uint32_t i = 0; i < d->nsyms; i++) {
            rtld_elf64_sym *sym = &d->symtab[i];

            /* Skip undefined, local, and section symbols */
            if (sym->st_shndx == RTLD_SHN_UNDEF) continue;
            uint8_t bind = RTLD_ELF64_ST_BIND(sym->st_info);
            if (bind == RTLD_STB_LOCAL) continue;

            if (rtld_strcmp(name, d->strtab + sym->st_name) == 0) {
                if (out_sym) *out_sym = sym;
                if (out_dso) *out_dso = d;
                return d->base + sym->st_value;
            }
        }
    }
    return 0;
}

/* ── Relocation processing ─────────────────────────────────────────────── */

/**
 * @brief Process a single RELA relocation entry for a DSO.
 *
 * @param d    The DSO being relocated.
 * @param rela The relocation entry to process.
 */
static void rtld_apply_rela(dso_t *d, const rtld_Elf64_Rela *rela)
{
    uint64_t *target = (uint64_t *)(d->base + rela->r_offset);
    uint32_t type = RTLD_ELF64_R_TYPE(rela->r_info);
    uint32_t sym_idx = RTLD_ELF64_R_SYM(rela->r_info);

    switch (type) {
    case RTLD_R_X86_64_NONE:
        break;

    case RTLD_R_X86_64_RELATIVE:
        /* B + A: no symbol lookup needed */
        *target = d->base + rela->r_addend;
        break;

    case RTLD_R_X86_64_GLOB_DAT:
    case RTLD_R_X86_64_JUMP_SLOT:
    {
        /* S: resolve symbol, store its address */
        const char *name = d->strtab + d->symtab[sym_idx].st_name;
        uint64_t addr = rtld_resolve_symbol(name, (void *)0, (void *)0, (void *)0);
        if (!addr) {
            rtld_puts("ld.so: undefined symbol: ");
            rtld_puts(name);
            rtld_puts("\r\n");
            rtld_exit(127);
        }
        *target = addr;
        break;
    }

    case RTLD_R_X86_64_64:
    {
        /* S + A */
        const char *name = d->strtab + d->symtab[sym_idx].st_name;
        uint64_t addr = rtld_resolve_symbol(name, (void *)0, (void *)0, (void *)0);
        if (!addr) {
            rtld_puts("ld.so: undefined symbol: ");
            rtld_puts(name);
            rtld_puts("\r\n");
            rtld_exit(127);
        }
        *target = addr + rela->r_addend;
        break;
    }

    case RTLD_R_X86_64_COPY:
    {
        /*
         * Copy relocation: the executable reserves space for a global variable
         * that is defined in a shared library. We copy the library's initial
         * value into the executable's copy.
         */
        const char *name = d->strtab + d->symtab[sym_idx].st_name;
        rtld_elf64_sym *lib_sym = (void *)0;
        /* Skip the executable itself to find the symbol in a library */
        uint64_t addr = rtld_resolve_symbol(name, d, &lib_sym, (void *)0);
        if (!addr || !lib_sym) {
            rtld_puts("ld.so: copy reloc failed: ");
            rtld_puts(name);
            rtld_puts("\r\n");
            rtld_exit(127);
        }
        rtld_memcpy(target, (void *)addr, lib_sym->st_size);
        break;
    }

    case RTLD_R_X86_64_TPOFF64:
    {
        /*
         * Thread-local storage offset: the value stored is the offset of
         * the TLS variable from the thread pointer (FS base). For initial-exec
         * TLS in shared libraries loaded at startup, we store the symbol's
         * value + addend. The libc's __tls_init handles actual TLS allocation.
         *
         * Since we don't support dynamic TLS, just resolve the symbol and
         * write its offset. The actual TLS template data is in the library's
         * .tdata section.
         */
        if (sym_idx == 0) {
            /* No symbol: just use addend as offset */
            *target = (uint64_t)rela->r_addend;
        } else {
            const char *name = d->strtab + d->symtab[sym_idx].st_name;
            uint64_t addr = rtld_resolve_symbol(name, (void *)0, (void *)0, (void *)0);
            if (!addr) {
                /* TLS symbol might be defined in the library itself as a TLS offset */
                *target = d->symtab[sym_idx].st_value + (uint64_t)rela->r_addend;
            } else {
                *target = addr + (uint64_t)rela->r_addend;
            }
        }
        break;
    }

    default:
        rtld_puts("ld.so: unsupported relocation type\r\n");
        rtld_exit(127);
    }
}

/**
 * @brief Process all relocations for a DSO (eager binding).
 *
 * Applies both DT_RELA (data relocations) and DT_JMPREL (PLT relocations)
 * immediately. No lazy binding.
 *
 * @param d The DSO to relocate.
 */
static void rtld_relocate(dso_t *d)
{
    for (size_t i = 0; i < d->rela_count; i++)
        rtld_apply_rela(d, &d->rela[i]);

    for (size_t i = 0; i < d->jmprel_count; i++)
        rtld_apply_rela(d, &d->jmprel[i]);
}

/* ── Library loading ───────────────────────────────────────────────────── */

/**
 * @brief Load a shared library from /lib/<name> into memory.
 *
 * Opens the file, reads the ELF header and program headers, allocates a
 * contiguous anonymous mapping, reads PT_LOAD segments into it, and
 * parses the PT_DYNAMIC section.
 *
 * @param name Shared library filename (e.g. "libc.so").
 * @return Pointer to the new DSO descriptor.
 */
static dso_t *rtld_load_library(const char *name)
{
    /* Construct path: /lib/<name> */
    char path[128];
    const char *prefix = "/lib/";
    size_t plen = rtld_strlen(prefix);
    size_t nlen = rtld_strlen(name);
    if (plen + nlen >= sizeof(path))
        rtld_die("library name too long");
    rtld_memcpy(path, prefix, plen);
    rtld_memcpy(path + plen, name, nlen + 1);

    long fd = rtld_open(path, 0 /* O_RDONLY */);
    if (fd < 0) {
        rtld_puts("ld.so: cannot open ");
        rtld_puts(path);
        rtld_puts("\r\n");
        rtld_exit(127);
    }

    /* Read ELF header */
    rtld_elf64_ehdr ehdr;
    if (rtld_read((int)fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr))
        rtld_die("cannot read ELF header");
    if (*(uint32_t *)ehdr.e_ident != RTLD_ELF_MAGIC)
        rtld_die("bad ELF magic in library");

    /* Read program headers */
    size_t phdr_size = ehdr.e_phnum * ehdr.e_phentsize;
    /* Use stack buffer (program headers are small) */
    rtld_Elf64_Phdr phdrs[32];
    if (ehdr.e_phnum > 32)
        rtld_die("too many program headers");

    /* Seek to phdr offset by reading and discarding bytes, or re-read from start.
     * Since we can't lseek, close and reopen then read the right offset.
     * Actually, the ELF header is at offset 0 and phdrs follow right after
     * in typical files, but e_phoff may not be sizeof(ehdr). Let's re-read. */
    rtld_close((int)fd);
    fd = rtld_open(path, 0);
    if (fd < 0)
        rtld_die("cannot reopen library");

    /* Read up to e_phoff + phdr_size bytes, then extract phdrs */
    size_t total_header = ehdr.e_phoff + phdr_size;
    /* We need a buffer large enough. Use a stack buffer. */
    uint8_t hdr_buf[4096];
    if (total_header > sizeof(hdr_buf))
        rtld_die("ELF headers too large");

    if ((size_t)rtld_read((int)fd, hdr_buf, total_header) != total_header)
        rtld_die("cannot read program headers");

    rtld_memcpy(phdrs, hdr_buf + ehdr.e_phoff, phdr_size);

    /* Compute the total memory span needed */
    uint64_t lo = ~0ULL, hi = 0;
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != RTLD_PT_LOAD) continue;
        uint64_t seg_start = phdrs[i].p_vaddr;
        uint64_t seg_end   = seg_start + phdrs[i].p_memsz;
        if (seg_start < lo) lo = seg_start;
        if (seg_end > hi) hi = seg_end;
    }
    if (hi <= lo)
        rtld_die("no PT_LOAD segments in library");

    lo = lo & ~(RTLD_PAGE_SIZE - 1);
    hi = (hi + RTLD_PAGE_SIZE - 1) & ~(RTLD_PAGE_SIZE - 1);
    size_t map_size = hi - lo;

    /* Allocate contiguous anonymous memory */
    void *base = rtld_mmap((void *)0, map_size,
                           RTLD_PROT_READ | RTLD_PROT_WRITE | RTLD_PROT_EXEC,
                           RTLD_MAP_PRIVATE | RTLD_MAP_ANONYMOUS,
                           -1, 0);
    if ((long)base < 0)
        rtld_die("mmap failed for library");

    uint64_t load_bias = (uint64_t)base - lo;

    /* Zero the entire mapping */
    rtld_memset(base, 0, map_size);

    /* Load each PT_LOAD segment by reading file data */
    /* We need to seek within the file. Since there's no lseek, we close/reopen
     * for each segment and skip bytes. This is inefficient but works. */
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != RTLD_PT_LOAD) continue;
        if (phdrs[i].p_filesz == 0) continue;

        rtld_close((int)fd);
        fd = rtld_open(path, 0);
        if (fd < 0)
            rtld_die("cannot reopen library for segment");

        /* Skip to p_offset by reading and discarding */
        uint64_t to_skip = phdrs[i].p_offset;
        uint8_t skip_buf[512];
        while (to_skip > 0) {
            size_t chunk = to_skip > sizeof(skip_buf) ? sizeof(skip_buf) : to_skip;
            long r = rtld_read((int)fd, skip_buf, chunk);
            if (r <= 0) rtld_die("read error skipping to segment");
            to_skip -= (size_t)r;
        }

        /* Read segment data into the mapped region */
        uint8_t *dest = (uint8_t *)(load_bias + phdrs[i].p_vaddr);
        size_t remaining = phdrs[i].p_filesz;
        while (remaining > 0) {
            long r = rtld_read((int)fd, dest, remaining);
            if (r <= 0) rtld_die("read error loading segment");
            dest += r;
            remaining -= (size_t)r;
        }
    }

    rtld_close((int)fd);

    /* Create DSO descriptor */
    dso_t *d = dso_alloc();
    d->name = name;
    d->base = load_bias;

    /* Find PT_DYNAMIC */
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == RTLD_PT_DYNAMIC) {
            d->dynamic = (rtld_Elf64_Dyn *)(load_bias + phdrs[i].p_vaddr);
            break;
        }
    }

    if (!d->dynamic)
        rtld_die("library has no PT_DYNAMIC");

    dso_parse_dynamic(d);
    dso_append(d);
    return d;
}

/* ── Main entry point ──────────────────────────────────────────────────── */

/**
 * @brief Runtime linker main function.
 *
 * Called from rtld_start.S AFTER self-relocation has been performed
 * in assembly. The GOT is already fixed, so normal C code works.
 *
 * Performs:
 * 1. Auxiliary vector parsing
 * 2. Main executable DSO setup
 * 3. Loading of DT_NEEDED shared libraries
 * 4. Eager relocation of all loaded objects
 * 5. Returns the application's entry point
 *
 * @param sp Original stack pointer (points to argc on the SysV ABI stack).
 * @return The application's entry point address.
 */
uint64_t rtld_main(uint64_t *sp)
{
    /* Parse auxv (self-relocation already done in rtld_start.S) */
    rtld_auxv_t auxv;
    rtld_parse_auxv(sp, &auxv);

    // rtld_puts("ld.so: dynamic linker started\r\n");

    /* Step 4: Build DSO for the main executable */
    dso_t *exe = dso_alloc();
    exe->name = "<main>";
    exe->base = 0; /* main executable has no load bias */

    /* Find PT_DYNAMIC in the main executable's program headers */
    rtld_Elf64_Phdr *phdr = (rtld_Elf64_Phdr *)auxv.at_phdr;
    uint64_t phnum = auxv.at_phnum;
    for (uint64_t i = 0; i < phnum; i++) {
        if (phdr[i].p_type == RTLD_PT_DYNAMIC) {
            exe->dynamic = (rtld_Elf64_Dyn *)(exe->base + phdr[i].p_vaddr);
            break;
        }
    }

    if (!exe->dynamic)
        rtld_die("executable has no PT_DYNAMIC");

    dso_parse_dynamic(exe);
    dso_append(exe);

    /* Step 5: Load DT_NEEDED libraries */
    for (rtld_Elf64_Dyn *dyn = exe->dynamic; dyn->d_tag != RTLD_DT_NULL; dyn++) {
        if (dyn->d_tag == RTLD_DT_NEEDED) {
            const char *name = exe->strtab + dyn->d_un.d_val;
            // rtld_puts("ld.so: loading ");
            // rtld_puts(name);
            // rtld_puts("\r\n");
            rtld_load_library(name);
        }
    }

    /* Step 6: Relocate all DSOs (libraries first, then executable) */
    /* Skip the executable in the first pass, relocate libraries */
    for (dso_t *d = exe->next; d; d = d->next)
        rtld_relocate(d);
    /* Now relocate the executable */
    rtld_relocate(exe);

    // rtld_puts("ld.so: relocation complete, transferring control\r\n");

    /* Step 7: Return the application's entry point */
    return auxv.at_entry;
}
