/*
 * Linker script for dynamically linked user programs.
 *
 * Same base address (0x400000) as static executables, but adds
 * PT_PHDR, PT_INTERP, and PT_DYNAMIC program headers plus the
 * dynamic linking sections (.interp, .dynsym, .dynamic, .got, etc.).
 */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    interp  PT_INTERP FLAGS(4);
    text    PT_LOAD FLAGS(5);  /* R-X */
    rodata  PT_LOAD FLAGS(4);  /* R-- */
    data    PT_LOAD FLAGS(6);  /* RW- */
    dynamic PT_DYNAMIC FLAGS(6);
    tls     PT_TLS FLAGS(4);
}

SECTIONS
{
    . = 0x400000;

    .interp : { *(.interp) } :interp :text

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

    . = ALIGN(64);

    .tdata : {
        __tdata_start = .;
        *(.tdata .tdata.*)
        __tdata_end = .;
    } :data :tls

    .tbss : {
        __tbss_start = .;
        *(.tbss .tbss.*)
        __tbss_end = .;
    } :data :tls

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
