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

#include <cstdint>

#include "count_down_latch.h"
#include "elf.h"
#include "ext2.h"
#include "future.h"
#include "heap.h"
#include "machine.h"
#include "per_core.h"
#include "print.h"
#include "ramdisk.h"
#include "thread.h"
#include "vmm.h"

/*
 * This file is the kernel-side syscall and has the instructions associated
 *
 * The assembly syscall entry code saves user register state into a SyscallFrame
 * and then hands control to syscallHandler. From there, this file
 * acts as a bridge between untrusted user code and the other part of the kernel.
 *
 * The main jobs of this file are:
 *   - decode the syscall number from the saved register frame
 *   - validate user pointers before touching user memory
 *   - keep just enough per-process state for the project tests
 *   - wire syscall requests into the existing kernel code for files, memory,
 *     scheduling, and blocking
 *
 * In practice that means this file keeps a small fd table, tracks parent/child
 * relationships for fork/waitpid, remembers each process's current brk, lazily
 * mounts the ramdisk-backed filesystem, and provides a simple kernel-side
 * semaphore implementation. The goal is not to build a full Unix process
 * subsystem, just to safely support the Linux-like syscall subset required by
 * the assignment. Claude used to understand and control logic of syscall 
 * instructions.
 */


struct SyscallFrame {
  uint64_t rax;
  uint64_t rbx;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rsi;
  uint64_t rdi;
  uint64_t rbp;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t user_rsp;
};

namespace {

// Initializing / setting limits here
constexpr uint64_t PAGE_SIZE = FRAME_SIZE;
constexpr uint64_t USER_TOP = UINT64_C(0x0000800000000000);
constexpr uint64_t USER_STACK_TOP = UINT64_C(0x7ffffff0000);
constexpr uint64_t USER_STACK_SIZE = 64 * 1024;
constexpr uint64_t BRK_LIMIT = USER_STACK_TOP - USER_STACK_SIZE;
constexpr size_t MAX_PATH = 256;
constexpr int MAX_FDS = 32;

// One open-file slot inside a process's fd table.
struct OpenFile {
  StrongRef<Node> node{};
  uint64_t offset = 0;
  bool used = false;
};

// Initialization of process syscalls work.
struct Process {
  int pid = 0;
  Process *parent = nullptr;
  Process *next_all = nullptr;
  Process *first_child = nullptr;
  Process *next_sibling = nullptr;
  bool waited = false;
  Future<int> exit_status{};
  Future<bool> stopped{};
  uint64_t brk_min = 0;
  uint64_t brk_current = 0;
  OpenFile fds[MAX_FDS]{};
};

// Semaphore object used by semget/semop/semctl.
struct KernelSemaphore {
  int id = 0;
  int key = 0;
  int value = 0;
  bool removed = false;
  KernelSemaphore *next = nullptr;
  SpinLock lock{};
  Condition changed{};
};

SpinLock process_lock{};
Process *processes = nullptr;
Atomic<int> next_pid{2};
thread_local Process *current_process = nullptr;

SpinLock fs_lock{};
Ext2 *root_fs = nullptr;
uint64_t initial_brk = 0;

SpinLock sem_lock{};
KernelSemaphore *semaphores = nullptr;
Atomic<int> next_semid{1};

inline uint64_t align_up(uint64_t value) {
  // Round up to the next page boundary.
  return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

void prepare_user_return() {
  // Refresh the kernel stack pointer the CPU should use the next time this
  // thread traps from user mode back into the kernel.
  auto me = impl::TCB::current();
  ASSERT(me != nullptr);
  PerCore::get()->tss.rsp0 = uint64_t(me->stack_bottom);
}

uint64_t compute_initial_brk(Ext2 *fs) {
  // The initial program break starts just above the highest loadable segment
  // in /init, which gives user code a reasonable heap starting point.
  auto init = fs->find(fs->root, "init");
  ASSERT(init != nullptr);

  ElfHeader hdr{};
  init->read(0, hdr);

  uint64_t max_end = 0;
  uint64_t phoff = hdr.phoff;
  for (uint16_t i = 0; i < hdr.phnum; i++) {
    ProgramHeader phdr{};
    init->read(phoff, phdr);
    phoff += hdr.phentsize;
    if (phdr.type != 1) {
      continue;
    }
    uint64_t end = phdr.vaddr + phdr.memsz;
    if (end > max_end) {
      max_end = end;
    }
  }

  return align_up(max_end);
}

Ext2 *filesystem() {
  // Mount the filesystem on first use, then keep reusing the same object.
  LockGuard guard{fs_lock};
  if (root_fs == nullptr) {
    StrongRef<BlockIO> disk{new RamDisk("/boot/ramdisk", 0)};
    root_fs = leak(new Ext2(disk), true);
    initial_brk = compute_initial_brk(root_fs);
  }
  return root_fs;
}

bool copy_user_string(char *out, size_t cap, const char *user) {
  // Copy one byte at a time so we can stop cleanly if the user pointer is bad.
  if (cap == 0 || user == nullptr) {
    return false;
  }

  for (size_t i = 0; i + 1 < cap; i++) {
    auto *ptr = (const char *)(uint64_t(user) + i);
    if (!VMM::user_buffer_ok(ptr, 1)) {
      return false;
    }
    out[i] = *ptr;
    if (out[i] == 0) {
      return true;
    }
  }

  out[cap - 1] = 0;
  return false;
}

StrongRef<Node> resolve_path(const char *path) {
  // Resolve a simple path by walking from the root directory through each path
  // component. This pretty minimal, but its enough for the tests.
  char local[MAX_PATH]{};
  if (!copy_user_string(local, sizeof(local), path)) {
    return {};
  }

  char *p = local;
  while (*p == '/') {
    p++;
  }
  if (*p == 0) {
    return {};
  }

  auto fs = filesystem();
  auto node = fs->root;

  while (*p != 0) {
    if (!node->is_dir()) {
      return {};
    }

    char *slash = p;
    while (*slash != 0 && *slash != '/') {
      slash++;
    }

    char saved = *slash;
    *slash = 0;
    node = fs->find(node, p);
    if (node == nullptr) {
      return {};
    }

    *slash = saved;
    while (*slash == '/') {
      slash++;
    }
    p = slash;
  }

  if (!node->is_file()) {
    return {};
  }
  return node;
}

Process *ensure_process() {
  // The first user thread that reaches the syscall layer becomes pid 1.
  if (current_process != nullptr) {
    return current_process;
  }

  filesystem();

  auto *proc = leak(new Process(), true);
  proc->pid = 1;
  proc->brk_min = initial_brk;
  proc->brk_current = initial_brk;

  LockGuard guard{process_lock};
  if (processes == nullptr) {
    processes = proc;
  }

  current_process = proc;
  return proc;
}

Process *find_child_locked(Process *parent, int pid) {
  // Caller already holds process_lock, so just scan this parent's child list.
  for (auto *child = parent->first_child; child != nullptr;
       child = child->next_sibling) {
    if (child->pid == pid) {
      return child;
    }
  }
  return nullptr;
}

int alloc_fd(Process *proc, StrongRef<Node> node) {
  // Return the lowest available fd, skipping the standard descriptors.
  for (int fd = 3; fd < MAX_FDS; fd++) {
    if (!proc->fds[fd].used) {
      proc->fds[fd].used = true;
      proc->fds[fd].node = node;
      proc->fds[fd].offset = 0;
      return fd;
    }
  }
  return -1;
}

KernelSemaphore *find_sem_locked(int semid) {
  // Semaphore lists stay small here, so a linear search is fine.
  for (auto *sem = semaphores; sem != nullptr; sem = sem->next) {
    if (sem->id == semid) {
      return sem;
    }
  }
  return nullptr;
}

int semaphore_op(KernelSemaphore *sem, short op) {
  // Support the three cases the tests care about:
  //   op > 0  -> increment
  //   op == 0 -> wait until the value becomes zero
  //   op < 0  -> wait until the value is large enough to decrement
  sem->lock.lock();

  if (sem->removed) {
    sem->lock.unlock();
    return -1;
  }

  if (op > 0) {
    sem->value += op;
    sem->changed.notify_all(sem->lock);
    return 0;
  }

  if (op == 0) {
    while (!sem->removed && sem->value != 0) {
      sem->changed.wait(sem->lock);
    }
    if (sem->removed) {
      sem->lock.unlock();
      return -1;
    }
    sem->lock.unlock();
    return 0;
  }

  int need = -op;
  while (!sem->removed && sem->value < need) {
    sem->changed.wait(sem->lock);
  }
  if (sem->removed) {
    sem->lock.unlock();
    return -1;
  }

  sem->value -= need;
  sem->lock.unlock();
  return 0;
}

} // namespace

extern "C" void syscall_process_stopped() {
  if (current_process != nullptr) {
    current_process->stopped.set(true);
  }
}

extern "C" [[gnu::force_align_arg_pointer]] uint64_t
syscallHandler(SyscallFrame *frame) {
  // Make sure the CPU knows which kernel stack to use before we eventually
  // return to user mode.
  prepare_user_return();
  auto *proc = ensure_process();
  // Syscalls enter with IF masked by IA32_FMASK. Many syscall paths can block
  // or fault and expect interrupts to be enabled while in kernel mode.
  bool entered_disabled = is_disabled();
  if (entered_disabled) {
    sti();
  }

  // Tiny helper so every normal syscall exit refreshes that return state.
  auto finish = [entered_disabled](uint64_t value) {
    prepare_user_return();
    if (entered_disabled) {
      disable();
    }
    return value;
  };

  switch (frame->rax) {
  case 0: { // read
    // read: pull bytes from an open file into validated user
    // memory and advance the file offset.
    int fd = int(frame->rdi);
    uint64_t len = frame->rdx;
    if (len == 0) {
      return finish(0);
    }
    if (fd < 0 || fd >= MAX_FDS || !proc->fds[fd].used ||
        !VMM::user_buffer_ok((void *)frame->rsi, len) ||
        len > UINT64_C(0xffffffff)) {
      return finish(uint64_t(-1));
    }

    auto &file = proc->fds[fd];
    auto cnt = file.node->read_all(uint32_t(file.offset), uint32_t(len),
                                   (char *)frame->rsi);
    if (cnt < 0) {
      return finish(uint64_t(-1));
    }
    file.offset += uint64_t(cnt);
    return finish(uint64_t(cnt));
  }

  case 1: { // write
    // write: all output goes to the console, but we still have
    // to reject bad user buffers instead of blindly dereferencing them.
    uint64_t len = frame->rdx;
    if (len == 0) {
      return finish(0);
    }
    if (!VMM::user_buffer_ok((void *)frame->rsi, len)) {
      return finish(uint64_t(-1));
    }

    auto *buffer = (const char *)frame->rsi;
    for (uint64_t i = 0; i < len; i++) {
      putch(buffer[i]);
    }
    return finish(len);
  }

  case 2: { // open
    // open: resolve the file and hand back a fresh fd.
    auto node = resolve_path((const char *)frame->rdi);
    if (node == nullptr) {
      return finish(uint64_t(-1));
    }
    return finish(uint64_t(alloc_fd(proc, node)));
  }

  case 3: { // close
    // close: clear the slot so the fd can be reused later.
    int fd = int(frame->rdi);
    if (fd >= 0 && fd <= 2) {
      return finish(0);
    }
    if (fd < 3 || fd >= MAX_FDS || !proc->fds[fd].used) {
      return finish(uint64_t(-1));
    }

    proc->fds[fd] = {};
    return finish(0);
  }

  case 8: { // lseek
    // lseek: update the saved per-fd offset.
    int fd = int(frame->rdi);
    int64_t off = int64_t(frame->rsi);
    int whence = int(frame->rdx);
    if (fd < 3 || fd >= MAX_FDS || !proc->fds[fd].used) {
      return finish(uint64_t(-1));
    }

    int64_t base = 0;
    auto &file = proc->fds[fd];
    switch (whence) {
    case 0:
      base = 0;
      break;
    case 1:
      base = int64_t(file.offset);
      break;
    case 2:
      base = int64_t(file.node->size_in_bytes());
      break;
    default:
      return finish(uint64_t(-1));
    }

    int64_t next = base + off;
    if (next < 0) {
      return finish(uint64_t(-1));
    }
    file.offset = uint64_t(next);
    return finish(uint64_t(next));
  }

  case 12: { // brk
    // brk(0) reports the current break. Otherwise, if the new address stays in
    // range, map any newly covered pages and record the exact break value.
    uint64_t addr = frame->rdi;
    if (addr == 0) {
      return finish(proc->brk_current);
    }
    if (addr < proc->brk_min || addr >= BRK_LIMIT || addr >= USER_TOP) {
      return finish(proc->brk_current);
    }

    uint64_t old_map_end = align_up(proc->brk_current);
    uint64_t new_map_end = align_up(addr);
    if (new_map_end > old_map_end) {
      impl::map_range(VA(old_map_end), new_map_end - old_map_end, true, true);
    }

    proc->brk_current = addr;
    return finish(proc->brk_current);
  }

  case 24: // sched_yield
    // Cooperative yield for the simplified scheduler
    Thread::yield();
    return finish(0);

  case 57: { // fork
    // fork(): duplicate the process bookkeeping first, then let a new kernel
    // thread clone the current address space before it resumes in user mode.
    auto *child = leak(new Process(), true);
    child->pid = next_pid.fetch_add(1);
    child->parent = proc;
    child->brk_min = proc->brk_min;
    child->brk_current = proc->brk_current;
    for (int fd = 0; fd < MAX_FDS; fd++) {
      child->fds[fd] = proc->fds[fd];
    }

    {
      LockGuard guard{process_lock};
      child->next_all = processes;
      processes = child;
      child->next_sibling = proc->first_child;
      proc->first_child = child;
    }

    auto *child_frame = leak(new SyscallFrame(*frame), true);
    child_frame->rax = 0;
    uint64_t parent_cr3 = get_cr3();

    CountDownLatch ready{};
    ready.up();
    Thread::create([child, child_frame, parent_cr3, &ready]() {
      // Finish the child setup inside the new thread, then jump straight back
      // to the saved syscall frame with fork() returning 0 in the child.
      current_process = child;
      VMM::fork_from(parent_cr3);
      prepare_user_return();
      ready.down();
      resume_syscall_(child_frame);
    });
    ready.sync();

    return finish(uint64_t(child->pid));
  }

  case 60: { // exit
    // Save the wait status in the usual Linux-style encoding and stop.
    proc->exit_status.set(int(frame->rdi & 0xff) << 8);
    Thread::stop();
  }

  case 61: { // waitpid
    // Only support waiting on a direct child once, which is enough for the
    // project tests and keeps the bookkeeping simple.
    int pid = int(frame->rdi);
    int *status = (int *)frame->rsi;
    if (pid <= 0 || frame->rdx != 0) {
      return finish(uint64_t(-1));
    }
    if (status != nullptr && !VMM::user_buffer_ok(status, sizeof(*status))) {
      return finish(uint64_t(-1));
    }

    Process *child = nullptr;
    {
      LockGuard guard{process_lock};
      child = find_child_locked(proc, pid);
      if (child == nullptr || child->waited) {
        return finish(uint64_t(-1));
      }
      child->waited = true;
    }

    // waitpid can block; make sure interrupts are enabled before and after
    // the blocking get so any subsequent lazy user page faults are handled
    // under the expected interrupt state.
    if (is_disabled()) {
      sti();
    }
    int value = child->exit_status.get();
    child->stopped.get();
    if (status != nullptr) {
      *status = value;
    }
    return finish(uint64_t(pid));
  }

  case 64: { // semget
    // semget: either return an existing single semaphore for this
    // key or create a new one.
    int key = int(frame->rdi);
    int nsems = int(frame->rsi);
    if (nsems != 1) {
      return finish(uint64_t(-1));
    }

    LockGuard guard{sem_lock};
    if (key != 0) {
      for (auto *sem = semaphores; sem != nullptr; sem = sem->next) {
        if (!sem->removed && sem->key == key) {
          return finish(uint64_t(sem->id));
        }
      }
    }

    auto *sem = leak(new KernelSemaphore(), true);
    sem->id = next_semid.fetch_add(1);
    sem->key = key;
    sem->next = semaphores;
    semaphores = sem;
    return finish(uint64_t(sem->id));
  }

  case 65: { // semop
    // semop(): this version only supports one operation at a time, which keeps
    // the kernel logic much easier to reason about.
    struct sembuf {
      unsigned short sem_num;
      short sem_op;
      short sem_flg;
    };

    int semid = int(frame->rdi);
    auto *ops = (sembuf *)frame->rsi;
    uint64_t nsops = frame->rdx;
    if (nsops != 1 || !VMM::user_buffer_ok(ops, sizeof(*ops))) {
      return finish(uint64_t(-1));
    }

    KernelSemaphore *sem = nullptr;
    {
      LockGuard guard{sem_lock};
      sem = find_sem_locked(semid);
      if (sem == nullptr || sem->removed) {
        return finish(uint64_t(-1));
      }
    }

    if (ops->sem_num != 0) {
      return finish(uint64_t(-1));
    }
    return finish(uint64_t(semaphore_op(sem, ops->sem_op)));
  }

  case 66: { // semctl
    // semctl: mark the semaphore as removed and wake blocked
    // waiters so they can fail instead of sleeping forever.
    int semid = int(frame->rdi);
    int semnum = int(frame->rsi);
    int cmd = int(frame->rdx);
    if (semnum != 0 || cmd != 0) {
      return finish(uint64_t(-1));
    }

    KernelSemaphore *sem = nullptr;
    {
      LockGuard guard{sem_lock};
      sem = find_sem_locked(semid);
      if (sem == nullptr || sem->removed) {
        return finish(uint64_t(-1));
      }
    }

    sem->lock.lock();
    sem->removed = true;
    sem->changed.notify_all(sem->lock);
    return finish(0);
  }

  default:
    // Anything else is outside the syscall subset this kernel implements.
    SAY("syscall ?\n", Dec(frame->rax));
    KPANIC("Unknown syscall ?\n", Dec(frame->rax));
  }
}
