
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


	/* syscall 0: read(fd, buf, count) */
	.global read
read:
	mov $0, %rax
	syscall
	ret

	/* syscall 1: write(fd, buf, count) */
	.global write
write:
	mov $1, %rax
	syscall
	ret

	/* syscall 2: open(path, flags) */
	.global open
open:
	mov $2, %rax
	syscall
	ret

	/* syscall 3: close(fd) */
	.global close
close:
	mov $3, %rax
	syscall
	ret

	/* syscall 8: lseek(fd, offset, whence) */
	.global lseek
lseek:
	mov $8, %rax
	syscall
	ret

	/* syscall 12: brk(addr) */
	.global brk
brk:
	mov $12, %rax
	syscall
	ret

	/* syscall 24: sched_yield() */
	.global sched_yield
sched_yield:
	mov $24, %rax
	syscall
	ret

	/* syscall 57: fork() */
	.global fork
fork:
	mov $57, %rax
	syscall
	ret

	/* syscall 60: exit(status) */
	.global exit
exit:
	mov $60, %rax
	syscall
	ud2

	/* syscall 61: waitpid(pid, status, options) */
	.global waitpid
waitpid:
	/* Linux wait4 is 61; args: pid=%rdi, status=%rsi, options=%rdx */
	mov $61, %rax
	syscall
	ret
