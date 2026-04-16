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

#include "syscall.h"

#include "per_core.h"
#include "print.h"
#include "thread.h"

#include <asm/prctl.h>
#include <cstdint>
#include <sys/syscall.h>

extern "C" [[gnu::force_align_arg_pointer]] uint64_t
syscallHandler(SyscallFrame *frame) {
  // SAY("rax = ?\n", Dec(frame->rax));

  switch (frame->rax) {
  case SYS_brk: {
    const auto me = impl::TCB::current();
    const auto new_brk = frame->rdi;
    if (new_brk > me->min_brk) {
      me->brk = new_brk;
    }
    return me->brk;
  }

  case SYS_arch_prctl:
    switch (frame->rdi) {
    case ARCH_SET_FS:
      PerCore::get()->saved_fsbase = frame->rsi;
      return 0;
    default:
      KPANIC("unknown arch_prctl ?\n", Dec(frame->rdi));
    }

  default:
    SAY("syscall ?\n", Dec(frame->rax));
    KPANIC("unknown syscall ?\n", Dec(frame->rax));
  }
}
