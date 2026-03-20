#include <symresolve.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: ldd <elf>\n");
        return 1;
    }

    elf_file_t *ef = elf_file_open(argv[1]);
    if (!ef) {
        printf("ldd: cannot open %s\n", argv[1]);
        return 1;
    }

    int ph_count = 0;
    const Elf64_Phdr *phdrs = elf_file_phdrs(ef, &ph_count);

    if (!phdrs || ph_count == 0) {
        printf("\tstatically linked\n");
        elf_file_close(ef);
        return 0;
    }

    /* Find PT_INTERP and PT_DYNAMIC */
    char interp[128] = {0};
    const Elf64_Phdr *dyn_phdr = nullptr;

    for (int i = 0; i < ph_count; i++) {
        if (phdrs[i].p_type == PT_INTERP) {
            size_t len = phdrs[i].p_filesz;
            if (len >= sizeof(interp))
                len = sizeof(interp) - 1;
            elf_file_read_at(ef, phdrs[i].p_offset, interp, len);
            interp[len] = '\0';
        }
        if (phdrs[i].p_type == PT_DYNAMIC)
            dyn_phdr = &phdrs[i];
    }

    if (!interp[0] && !dyn_phdr) {
        printf("\tstatically linked\n");
        elf_file_close(ef);
        return 0;
    }

    if (interp[0])
        printf("\t%s\n", interp);

    if (!dyn_phdr) {
        elf_file_close(ef);
        return 0;
    }

    /* Read PT_DYNAMIC */
    size_t dyn_size = dyn_phdr->p_filesz;
    Elf64_Dyn *dyns = malloc(dyn_size);
    if (!dyns || !elf_file_read_at(ef, dyn_phdr->p_offset, dyns, dyn_size)) {
        free(dyns);
        elf_file_close(ef);
        return 1;
    }

    /* Find DT_STRTAB and DT_STRSZ */
    uint64_t strtab_vaddr = 0;
    uint64_t strtab_size = 0;
    size_t dyn_count = dyn_size / sizeof(Elf64_Dyn);
    for (size_t i = 0; i < dyn_count; i++) {
        if (dyns[i].d_tag == DT_NULL) break;
        if (dyns[i].d_tag == DT_STRTAB) strtab_vaddr = dyns[i].d_un.d_ptr;
        if (dyns[i].d_tag == DT_STRSZ)  strtab_size = dyns[i].d_un.d_val;
    }

    char *strtab = nullptr;
    if (strtab_vaddr && strtab_size) {
        uint64_t offset = elf_file_vaddr_to_offset(ef, strtab_vaddr);
        if (offset) {
            strtab = malloc(strtab_size);
            if (strtab && !elf_file_read_at(ef, offset, strtab, strtab_size)) {
                free(strtab);
                strtab = nullptr;
            }
        }
    }

    /* Print DT_NEEDED libraries */
    for (size_t i = 0; i < dyn_count; i++) {
        if (dyns[i].d_tag == DT_NULL) break;
        if (dyns[i].d_tag != DT_NEEDED) continue;

        uint64_t name_off = dyns[i].d_un.d_val;
        if (strtab && name_off < strtab_size)
            printf("\t%s => /lib/%s\n", strtab + name_off, strtab + name_off);
        else
            printf("\t<strtab offset %lu>\n", name_off);
    }

    free(strtab);
    free(dyns);
    elf_file_close(ef);
    return 0;
}
