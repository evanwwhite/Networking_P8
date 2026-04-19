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

.global sched_yield
sched_yield:
    mov $24, %rax
    syscall
    ret