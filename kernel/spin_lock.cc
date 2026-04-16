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

#include "spin_lock.h"

#include "atomic.h"
#include "print.h"
#include "system_main.h"

AtomicMax<uint64_t> SpinLock::max_critical_section_cycles{0};
Atomic<uint64_t> SpinLock::total_critical_section_cycles{0};
Atomic<uint64_t> SpinLock::critical_section_count{0};

void SpinLock::print_stats() {
  KPRINT("@@@ max_spin_cycles        = ?\n",
         Dec(SpinLock::max_critical_section_cycles.get()));
  KPRINT("@@@ critical_section_count = ?\n",
         Dec(SpinLock::critical_section_count.get()));
  if (SpinLock::critical_section_count.get() != 0) {
    KPRINT("@@@ avg_spin_cycles        = ?\n",
           Dec(SpinLock::total_critical_section_cycles.get() /
               SpinLock::critical_section_count.get()));
  }
  KPRINT("@@@ total_critial_section_cycles = ?\n",
         Dec(SpinLock::total_critical_section_cycles.get()));
}

bool SpinLock::tryLock() {
  auto was = disable();
  auto has_it = !taken.exchange(true);
  if (has_it) {
    was_disabled = was;
    start = rdtsc();
  } else {
    restore(was);
  }
  return has_it;
}

void SpinLock::lock() {
  while (!tryLock()) {
    stuckInALoop();
  }
}

void SpinLock::unlock() {
  auto c = count_it;
  count_it = true;
  auto s = start;
  auto end = rdtsc();

  auto was = was_disabled;
  taken.set(false);
  restore(was);
  if (c) {
    auto elapsed = end - s;
    total_critical_section_cycles.add_fetch(elapsed);
    critical_section_count.fetch_add(1);
    max_critical_section_cycles.update(elapsed);
  }
}
