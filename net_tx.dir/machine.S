	.text

	.global start
start:
	call main
	mov %rax, %rdi
	mov $60, %rax
	syscall
	ud2

	.section .note.GNU-stack,"",@progbits
