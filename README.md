Due date:
    4/10/2026 @ 11:59pm (test and t0)
    4/13/2026 @ 11:59pm (everything working)

Important:
~~~~~~~~~~

Remember to run "make clean" when you make any changes to the test
directory.

You have more freedom in changing the kernel implementation. See below for
details.


Assignment:
~~~~~~~~~~~

- Go to user mode
- Implement the required system calls
- Donate a test case (<csid>.dir, <csid>.md, <csid>.ok)

Simplifications:
~~~~~~~~~~~~~~~~

Schedule limitations forced the following simplifcations (wouldn't be there
if we had the full 10 days):

* preemption is disabled
* demand paging is not required

Rationale: saves the time needed to learn and debug the details of some x86-64
           features (The TSS)

System calls to implement:
~~~~~~~~~~~~~~~~~~~~~~~~~~

We will implement a simplified subset of the Linux system calls; using the same
linkage convention (syscall/sysret, %rax contains syscall number, ...).

Looks at https://filippo.io/linux-syscall-table/ for a convenient listing of
the Linux sys calls.

Keep in mind the slight differences between a system call and its libc
wrapper; the kernel implements the system call and libc.h/... implement
the wrapper.

Ignore all uses of permissions and times

We will implement:

- open (ignoring all args except for path)
- read
- write (all writes go to the console)
- close
- lseek (no support for SEEK_HOME and SEEK_DATA)
- brk (read the comment about syscall vs. library function, you need to implement
       the syscall)
- sched_yield
- exit
- fork (our kernel will implement fork directly; not as a special case of clone)
- semget (nsems must be 1)
- semop (ignore NO_WAIT flag)
- semctl (the only supported operation is IPC_RMID)
- waitpid



Attacks:
~~~~~~~~

Try to come up with a test case that exploits vulnabilities in the given code.

Hint: running syscalls on the user stack.


What can you change:
~~~~~~~~~~~~~~~~~~~~

- The kernel is yours, you can do anything with it as long as:

     * you leave the Makefile's alone
     * you leave the kernel link script alone
     * it continues to implement preemptive multi-threading with
       the informal liveness semantics:
            o All cores get to participate
            o All threads get a fair share of resources
     * it adheres to the user contract:
            o The virtual address space structure
            o Protects the kernel from user processes
            o Protects user processes from each other
            o Implements the system call semantics
            o Bootstraps the process tree from /init

The given code:
~~~~~~~~~~~~~~~

There is enough code in kernel/ to bootstrap the first user process but:

     o The file system and virtual memory implementations are incomplete
        and in some places incorrect (e.g. look for prints tagged with
        FIXME). You are responsible for making them work.

     o You can use any parts of the code you want for this (and future)
       assignments but you can't copy it in late submissions for previous
       assignments.

     o You need to understand everything you submit.

Details:
~~~~~~~~

(1) kernel_main mounts the ext2 file system in drive #1

(2) it looks for a file named /init

(3) it loads it in user memory

    It should reject any non-ELF files or an ELF file that tries to load
    a program outside the user range

(4) it switches to user mode and starts running the user process at the
    program entry point

(5) the kernel should protect all its resources from the user program

(6) Look in t0.dir/machins.S for examples of user-side syscall stubs

(7) System calls to implement:

Most of our system calls are modelled after their Linux counterparts (some
with minor simplifications). To learn about a Linux system call (e.g. fork),
you can login into a CS machine and type:

    man 2 fork  # or use google, chatgpt, ...


Testcase:
~~~~~~~~~

Your testcase consists of a <csid>.dir, <csid>.ok, and a <csid>.md (optional)
Your .dir should contain a /sbin/init, where init is the compiled ELF file that needs to be run.
Your .md is a markdown file which describes what your testcase is doing.
- Specify exactly what your testcase tests (e.g. a list of syscalls, what it stresses, etc.)
- Summarize how your testcase helps you test the things above. (Be relatively concise here)
- (Optional) Describe in detail what your testcase does (You can have a more extended description here)
If you choose to write your testcase in a language not commonly used or difficult to read,
you should write an extended description in your markdown file.

"Common" languages (everyone in this class has used these):
- C
- C++
- Java

files:
~~~~~~

- kernel/          contains the kernel files

- <test>.md        the README containing a description of the testcase

- <test>.dir/      the contents of the root disk

- <test>.dir/
    init           ... the elf init file packaged in t0.img

for makefile help:
~~~~~~~~~~~~~~~~~~

    make help

to run test:
~~~~~~~~~~~~

    make -s clean test

to run one test:
~~~~~~~~~~~~~~~~

    make -s t0.test

To make the output more noisy:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    make clean test

To run by hand
~~~~~~~~~~~~~~

    ./run_qemu t0

To attach with gdb
~~~~~~~~~~~~~~~~~~

    ./debug_qemu t0


