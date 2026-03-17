/*
 * Linker script for the runtime dynamic linker (ld.so).
 *
 * ld.so is built as an ET_DYN (shared object) with its own entry point.
 * It has no external dependencies — all symbols are resolved internally.
 * The kernel loads it at a high bias address (0x7000000000).
 */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    text    PT_LOAD FLAGS(5); /* R-X */
    rodata  PT_LOAD FLAGS(4); /* R-- */
    data    PT_LOAD FLAGS(6); /* RW- */
    dynamic PT_DYNAMIC FLAGS(6);
}

SECTIONS
{
    . = SIZEOF_HEADERS;

    .hash    : { *(.hash) }       :text
    .dynsym  : { *(.dynsym) }    :text
    .dynstr  : { *(.dynstr) }    :text
    .rela.dyn : { *(.rela.dyn) } :text

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(4096);

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(4096);

    .data : {
        *(.data .data.*)
    } :data

    .dynamic : {
        *(.dynamic)
    } :data :dynamic

    .got : {
        *(.got)
    } :data

    .got.plt : {
        *(.got.plt)
    } :data

    . = ALIGN(4096);

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
        *(.comment)
    }
}
