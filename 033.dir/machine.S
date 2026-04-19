
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

	.global write_on_tiny_stack
write_on_tiny_stack:
	// save the real user stack pointer first so c code can keep running later.
	push %rbx
	mov %rsp, %rbx

	// run the syscall with rsp moved right down near the edge we want to test.
	mov %rcx, %rsp
	mov $1, %rax
	syscall

	// restore the original user stack before returning to normal c code.
	mov %rbx, %rsp
	pop %rbx
	ret
