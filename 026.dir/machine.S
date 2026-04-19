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

	.global open
open:
	mov $2, %rax
	syscall
	ret

	.global read
read:
	mov $0, %rax
	syscall
	ret

	.global sched_yield
sched_yield:
	mov $24, %rax
	syscall
	ret
