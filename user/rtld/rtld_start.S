#
# @file rtld_start.S
# @brief Entry point and bootstrap for the runtime dynamic linker (ld.so).
#
# The kernel transfers control to _start after loading the dynamic executable
# and the interpreter. RSP points to the SysV ABI initial stack:
#   argc, argv[], NULL, envp[], NULL, auxv[]
#
# Before any C code can run, we must self-relocate ld.so:
# 1. Parse the auxv to find AT_BASE (our load bias)
# 2. Find _DYNAMIC via a PC-relative lea (no GOT needed)
# 3. Walk _DYNAMIC for DT_RELA/DT_RELASZ/DT_RELAENT
# 4. Apply all R_X86_64_RELATIVE relocations to fix our GOT
# 5. Then call rtld_main() which can now safely use globals/GOT
#
.intel_syntax noprefix

.section .text
.global _start
_start:
    mov rbp, rsp            /* save original RSP */

    /* Step 1: Parse auxv to find AT_BASE */
    mov rdi, rsp
    mov rcx, [rdi]          /* rcx = argc */
    lea rdi, [rdi + 8]      /* skip argc */
    lea rdi, [rdi + rcx*8 + 8]  /* skip argv[] + NULL */

    /* skip envp[] */
.Lskip_envp:
    mov rax, [rdi]
    add rdi, 8
    test rax, rax
    jnz .Lskip_envp

    /* rdi now points to auxv[]; find AT_BASE (type=7) */
    xor r12, r12            /* r12 = AT_BASE (load bias), default 0 */
.Lfind_base:
    mov rax, [rdi]          /* auxv type */
    test rax, rax
    jz .Ldone_auxv          /* AT_NULL */
    cmp rax, 7              /* AT_BASE */
    jne .Lnext_auxv
    mov r12, [rdi + 8]      /* r12 = AT_BASE value */
    jmp .Ldone_auxv
.Lnext_auxv:
    add rdi, 16
    jmp .Lfind_base
.Ldone_auxv:

    # Step 2: Find _DYNAMIC via PC-relative lea
    lea r13, [rip + _DYNAMIC]

    # Step 3: Walk _DYNAMIC for DT_RELA info
    xor r14, r14            /* r14 = rela address */
    xor r15, r15            /* r15 = rela total size */
    xor rbx, rbx            /* rbx = rela entry size */
    mov rcx, r13
.Lscan_dyn:
    mov rax, [rcx]          /* d_tag */
    test rax, rax
    jz .Ldone_dyn           /* DT_NULL */
    cmp rax, 7              /* DT_RELA */
    je .Lgot_rela
    cmp rax, 8              /* DT_RELASZ */
    je .Lgot_relasz
    cmp rax, 9              /* DT_RELAENT */
    je .Lgot_relaent
    add rcx, 16
    jmp .Lscan_dyn
.Lgot_rela:
    mov r14, [rcx + 8]
    add r14, r12            /* rela_addr = base + d_ptr */
    add rcx, 16
    jmp .Lscan_dyn
.Lgot_relasz:
    mov r15, [rcx + 8]
    add rcx, 16
    jmp .Lscan_dyn
.Lgot_relaent:
    mov rbx, [rcx + 8]
    add rcx, 16
    jmp .Lscan_dyn
.Ldone_dyn:

    /* Step 4: Apply R_X86_64_RELATIVE relocations */
    test r14, r14
    jz .Ldone_reloc
    test rbx, rbx
    jz .Ldone_reloc

    /* rela_count = relasz / relaent */
    mov rax, r15
    xor rdx, rdx
    div rbx
    mov rcx, rax            /* rcx = rela_count */

    mov rsi, r14            /* rsi = current rela entry */
.Lreloc_loop:
    test rcx, rcx
    jz .Ldone_reloc

    /* Check if R_X86_64_RELATIVE (type 8) */
    mov eax, [rsi + 8]      /* r_info low 32 bits = type */
    cmp eax, 8
    jne .Lreloc_next

    /* *target = base + addend */
    mov rdi, [rsi]           /* r_offset */
    add rdi, r12             /* target = base + r_offset */
    mov rax, [rsi + 16]      /* r_addend */
    add rax, r12             /* value = base + r_addend */
    mov [rdi], rax

.Lreloc_next:
    add rsi, rbx             /* next rela entry */
    dec rcx
    jmp .Lreloc_loop
.Ldone_reloc:

    /* Step 5: Call rtld_main(sp) */
    mov rdi, rbp             /* arg1 = original stack pointer */
    and rsp, -16             /* align stack for C ABI */
    call rtld_main           /* returns app entry point in rax */
    mov rsp, rbp             /* restore original stack for the app */
    xor rbp, rbp             /* clear frame pointer */
    jmp rax                  /* transfer control to app _start */
