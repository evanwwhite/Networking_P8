
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
	mov $60, %rax
	syscall
	ud2

	.global write
write:
	mov $1, %rax
	syscall
	ret
    .global read
read:
    mov $0, %rax
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

    .global brk
brk:
    mov $12, %rax
    syscall
    ret

    .global sched_yield
sched_yield:
    mov $24, %rax
    syscall
    ret

    .global fork
fork:
    mov $57, %rax
    syscall
    ret

    .global waitpid
waitpid:
    mov $61, %rax
    syscall
    ret

    .global semget
semget:
    mov $64, %rax
    syscall
    ret

    .global semop
semop:
    mov $65, %rax
    syscall
    ret

    .global semctl
semctl:
    mov $66, %rax
    syscall
    ret
