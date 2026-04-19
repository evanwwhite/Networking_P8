.text

    .global start
start:
    call main
    mov %rax, %rdi
    call exit
    ud2

    .global strlen
strlen:
    mov %rdi, %rax
    mov $0, %rcx
1:
    movb (%rax), %r11b
    cmpb $0, %r11b
    je 1f
    inc %rax
    inc %rcx
    jmp 1b
1:
    mov %rcx, %rax
    ret

    .global exit
exit:
    mov $60, %rax       /* exit syscall number */
    syscall
    ud2

    .global write
write:
    mov $1, %rax        /* write syscall number */
    syscall
    ret

    .global fork
fork:
    mov $57, %rax       /* fork syscall number */
    syscall
    ret

    .global waitpid
waitpid:
    mov $61, %rax       /* waitpid syscall number */
    syscall
    ret