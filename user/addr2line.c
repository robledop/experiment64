#include <symresolve.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("usage: addr2line <elf> <addr> [addr...]\n");
        exit();
    }

    sym_table_t *table = sym_load(argv[1]);
    if (!table) {
        printf("addr2line: failed to load symbols from %s\n", argv[1]);
        exit();
    }

    for (int i = 2; i < argc; i++) {
        uint64_t addr = strtoul(argv[i], nullptr, 16);
        const char *name = sym_resolve(table, addr);
        if (name)
            printf("0x%lx: %s\n", addr, name);
        else
            printf("0x%lx: [unknown]\n", addr);
    }

    sym_free(table);
    exit();
}
