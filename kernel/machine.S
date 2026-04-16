/* Copyright (C) 2025 Ahmed Gheith and contributors.
 *
 * Use restricted to classroom projects.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


	.global outb
outb:
	mov %di, %dx
	mov %sil, %al
	outb %al, %dx
	ret

	.global outl
outl:
	mov %di, %dx
	mov %esi, %eax
	outl %eax, %dx
	ret


	.global inb
inb:
	mov %di, %dx
	xor %rax, %rax
	inb %dx, %al
	ret

	.global inl
inl:
	mov %di, %dx
	xor %rax, %rax
	inl %dx, %eax
	ret

	.global get_cr0
get_cr0:
	mov %cr0, %rax
	ret

	.global get_cr4
get_cr4:
	mov %cr4, %rax
	ret

	.global set_cr4
set_cr4:
	mov %rdi, %rax
	mov %rax, %cr4
	ret

	.global enable_sse
enable_sse:
	mov %cr4, %rax
	and $0xfffb, %ax
	or $0x602, %ax
	mov %rax, %cr4
	ret

	.global rdfsbase
rdfsbase:
	rdfsbase %rax
	ret

	.global wrfsbase
wrfsbase:
	wrfsbase %rdi
	ret

	.global rdgsbase
rdgsbase:
	rdgsbase %rax
	ret

	.global wrgsbase
wrgsbase:
	wrgsbase %rdi
	ret

	.global memcpy
memcpy:
	cld
	mov %rdi, %rax
	mov %rdx, %rcx
	rep movsb
	ret

	.global memset
memset:
	mov %rdi, %rax
	mov %rdx, %rcx
	rep stosb
	ret


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


	.global streq
streq:
	movb (%rdi), %al
	cmpb (%rsi), %al
	
	je 1f
	// not equal, return 0
	xor %rax, %rax
	ret

	// equal
1:
	cmp $0, %al
	je 1f
	inc %rdi
	inc %rsi
	jmp streq     // keep going

1:
	movq $1, %rax     // equal, return 1
	ret


	.global context_switch_ // (uintptr_t fs)
context_switch_:

	rdfsbase %rax # current (will become previous) thread

	
	push %rbx
	push %rbp
	push %r12
	push %r13
	push %r14
	push %r15
	pushf             // IF=what A wants
	mov %cr2,%r15
	push %r15
	mov %cr3,%r14
	push %r14

	cli               // IF=0  
	                  // disable interrupts until we restore them correctly
	                  // below. Handles the case where B wants interrupts disabled
					  // but A has them enabled.

	mov %rsp, %fs:8   // IF=0

	# Thread A
	# ------>
	# Thread B

	wrfsbase %rdi     // IF=0

	mov %fs:8, %rsp   // IF=0

	pop %r14
	mov %r14, %cr3
	pop %r15
	mov %r15, %cr2
	popf              // IF=what B wants
	pop %r15
	pop %r14
	pop %r13
	pop %r12
	pop %rbp
	pop %rbx

	// prev is both the return value (for old threads) and
	// the first arguments (for new threads)
	mov %rax, %rdi

	ret


	.global pause
pause:
	pause
	ret

	.global get_flags
get_flags:
	pushf
	pop %rax
	ret

	.extern pageFaultHandler // (uintptr_t cr2, PageFaultTrapFrame* trap_frame)

	.global pageFaultHandler_
pageFaultHandler_:
    // we're about to call C++ code
	//     - Strictly speaking, we only need to to save the caller-saved registers
	//     - but it is better for debugging purposes to save all of them
	//     - this will also allow signal handlers to manipulate the machine state in
	//       the future
	push %r15
	push %r14
	push %r13
	push %r12
	push %r11
	push %r10
	push %r9
	push %r8
	push %rbp
	push %rsi
	push %rdi
	push %rdx
	push %rcx
	push %rbx
	push %rax

	mov %cr2, %rdi
	mov %rsp, %rsi
	call pageFaultHandler
	
	pop %rax
	pop %rbx
	pop %rcx
	pop %rdx
	pop %rdi
	pop %rsi
	pop %rbp
	pop %r8
	pop %r9
	pop %r10
	pop %r11
	pop %r12
	pop %r13
	pop %r14
	pop %r15

	add $8, %rsp // pop error code
	iretq

	.extern apiHandler
	.global apitHandler_
apitHandler_:
	
    // we're about to call C++ code
	//     - Strictly speaking, we only need to to save the caller-saved registers
	//     - but it is better for debugging purposes to save all of them
	//     - this will also allow signal handlers to manipulate the machine state in
	//       the future

	push %rax
	push %rbx
	push %rcx
	push %rdx
	push %rsi
	push %rdi
	push %rbp
	push %r8
	push %r9
	push %r10
	push %r11
	push %r12
	push %r13
	push %r14
	push %r15

	mov %rsp, %rdi
	call apitHandler

	pop %r15
	pop %r14
	pop %r13
	pop %r12
	pop %r11
	pop %r10
	pop %r9
	pop %r8
	pop %rbp
	pop %rdi
	pop %rsi
	pop %rdx
	pop %rcx
	pop %rbx
	pop %rax

	iretq

	.global spuriousHandler_
spuriousHandler_:
	iretq


	.global bounce_to_yield_
bounce_to_yield_:

	push %fs:16  // this will be the return address

	push %rbp
	mov %rsp, %rbp

    push %rax
	push %rcx
	push %rdx
	push %rsi
	push %rdi
	pushf

	call impl_bounce_to_yield

	popf
	pop %rdi
	pop %rsi
	pop %rdx
	pop %rcx
	pop %rax

	pop %rbp

	retq 


	.global switch_to_user
switch_to_user:
	mov %rdi, %rcx
	mov $0x200, %r11

	cli
	mov %rsi, %rsp
	sysretq

	// Things to consider:
	//     user code doesn't use:
	//          - SSE/AVX registers
	//          - FS/GS base registers
	//     we trust the user stack

	.extern syscallHandler
	.global syscallHandler_
syscallHandler_:
    // save caller state

	swapgs              // kernel GSBASE

	xchg %rsp, %gs:24   // kernel %rsp

	push %r15

    rdfsbase %r15
	xchg %r15, %gs:16   // kernel FSBASE
	wrfsbase %r15


	push %r14
	push %r13
	push %r12
	push %r11
	push %r10
	push %r9
	push %r8
	push %rbp
	push %rdi
	push %rsi
	push %rdx
	push %rcx
	push %rbx
	push %rax

	mov %rsp, %rdi
	call syscallHandler
	
	add $8, %rsp // pop %rax
	pop %rbx
	pop %rcx
	pop %rdx
	pop %rsi
	pop %rdi
	pop %rbp
	pop %r8
	pop %r9
	pop %r10
	pop %r11
	pop %r12
	pop %r13
	pop %r14

	rdfsbase %r15
	xchg %r15, %gs:16 // back to user FSBASE
	wrfsbase %r15
	
	pop %r15

	xchg %rsp, %gs:24 // back to user %rsp

	swapgs            // back to user GSBASE

    sysretq
	

	.section .note.GNU-stack,"",@progbits
	



