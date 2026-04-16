# `userpreempt`

This testcase is meant to answer a very specific question: while a user-mode
program is running and not making syscalls, does the kernel still receive timer
interrupts and drive the user preemption path?

## What it does

- Boots a tiny `/init` program in user mode.
- The program writes `*** preempt begin`.
- It then spins in a long CPU-bound loop entirely in user mode.
- After the loop, it writes `*** preempt end` and exits.

## What success means

The normal harness result only checks the `***` lines, so a passing
`make -s userpreempt.test` tells you that:

- user `write` works
- the user program survived a long uninterrupted stretch of user execution
- user `exit` works

The real preemption signal is in the raw serial log:

```sh
grep '^@@@ n_preempt' userpreempt.raw
```

Interpret it like this:

- `@@@ n_preempt = 0` means user-mode timer preemption did not happen.
- `@@@ n_preempt = <nonzero>` means the APIT interrupted user mode and the
  kernel executed the preemption path at least once.

## Suggested run

```sh
make -s userpreempt.test
grep '^@@@ n_preempt' userpreempt.raw
```

If you want to remove multicore effects while debugging scheduling behavior,
run it on one virtual CPU:

```sh
make -s userpreempt.test QEMU_SMP=1
```
