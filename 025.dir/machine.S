
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

