.text
.global start
start:
    call main
    mov %rax, %rdi
    call exit
    ud2

.global read
read:
    mov $0, %rax
    syscall
    ret

.global write
write:
    mov $1, %rax
    syscall
    ret

.global open
open:
    mov $2, %rax
    syscall
    ret

.global close
close:
    mov $3, %rax
    syscall
    ret

.global lseek
lseek:
    mov $8, %rax
    syscall
    ret

.global exit
exit:
    mov $60, %rax
    syscall
    ud2

.global strlen
strlen:
    mov %rdi, %rax          # rax = s
    xor %rcx, %rcx          # rcx = len
1:
    cmpb $0, (%rax)
    je 2f
    inc %rax
    inc %rcx
    jmp 1b
2:
    mov %rcx, %rax          # return len
    ret