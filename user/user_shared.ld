/*
 * Linker script for shared libraries (e.g. libc.so).
 *
 * Shared libraries are ET_DYN objects loaded at an arbitrary address
 * chosen by the runtime linker. All addresses are relative to load bias.
 */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

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
    .gnu.hash : { *(.gnu.hash) }  :text
    .dynsym  : { *(.dynsym) }    :text
    .dynstr  : { *(.dynstr) }    :text
    .rela.dyn : { *(.rela.dyn) } :text
    .rela.plt : { *(.rela.plt) } :text

    .text : {
        *(.text .text.* .plt)
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
