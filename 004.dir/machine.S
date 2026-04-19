/*
    This entire test was inspired by Dr. Gheith's test and Claude also contributed a huge majority
    of this test case throughout the entire folder. A huge chunk of the code in this file specifically was generated
	by Claude because I was too lazy to write some of the Assembly that wasn't already in Dr. Gheith's file.
	Was readapted by whatever our system needed. 
*/

	.text

	/* ----------------------------------------------------------------
	 * Entry point — call main, then exit with its return value
	 * ---------------------------------------------------------------- */
	.global start
start:
	call main
	mov %rax, %rdi
	call exit
	ud2


	/* ----------------------------------------------------------------
	 * strlen — count bytes until NUL
	 * ---------------------------------------------------------------- */
	.global strlen
strlen:
	mov  %rdi, %rax
	mov  $0,   %rcx
1:
	movb (%rax), %r11b
	cmpb $0,   %r11b
	je   1f
	inc  %rax
	inc  %rcx
	jmp  1b
1:
	mov  %rcx, %rax
	ret


	/* ----------------------------------------------------------------
	 * syscall stubs
	 *
	 * Linux x86-64 syscall ABI:
	 *   number in %rax
	 *   args    in %rdi, %rsi, %rdx, %r10, %r8, %r9
	 *                                ^^^^
	 *   NOTE: the 4th argument slot for SYSCALL is %r10,
	 *         but the C calling convention uses %rcx for arg4.
	 *         Any syscall with ≥4 args needs "mov %rcx, %r10".
	 * ---------------------------------------------------------------- */

	/* sys_read  (0)  read(fd, buf, count) */
	.global read
read:
	mov  $0, %rax
	syscall
	ret

	/* sys_write  (1)  write(fd, buf, count) */
	.global write
write:
	mov  $1, %rax
	syscall
	ret

	/* sys_open  (2)  open(path, flags, mode) */
	.global open
open:
	mov  $2, %rax
	syscall
	ret

	/* sys_close  (3)  close(fd) */
	.global close
close:
	mov  $3, %rax
	syscall
	ret

	/* sys_lseek  (8)  lseek(fd, offset, whence) */
	.global lseek
lseek:
	mov  $8, %rax
	syscall
	ret

	/* sys_brk  (12)  brk(addr) */
	.global brk
brk:
	mov  $12, %rax
	syscall
	ret

	/* sys_sched_yield  (24) */
	.global sched_yield
sched_yield:
	mov  $24, %rax
	syscall
	ret

	/* sys_fork  (57) */
	.global fork
fork:
	mov  $57, %rax
	syscall
	ret

	/* sys_exit  (60)  exit(status) */
	.global exit
exit:
	mov  $60, %rax
	syscall
	ud2

	/* sys_wait4  (61) — used as waitpid(pid, &status, options)
	 * wait4 takes a 4th arg (rusage); pass NULL via %r10.          */
	.global waitpid
waitpid:
	mov  $61, %rax
	xor  %r10, %r10        /* rusage = NULL */
	syscall
	ret

	/* sys_semget  (64)  semget(key, nsems, semflg) */
	.global semget
semget:
	mov  $64, %rax
	syscall
	ret

	/* sys_semop  (65)  semop(semid, sops, nsops) */
	.global semop
semop:
	mov  $65, %rax
	syscall
	ret

	/* sys_semctl  (66)  semctl(semid, semnum, cmd, arg)
	 * 4th C arg is in %rcx; move to %r10 for syscall ABI.          */
	.global semctl
semctl:
	mov  $66, %rax
	mov  %rcx, %r10        /* arg (union semun) */
	syscall
	ret
