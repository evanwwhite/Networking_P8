	.text

	.global start
start:
	call main
	mov %rax, %rdi
	call exit
	ud2

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

	.section .note.GNU-stack,"",@progbits
