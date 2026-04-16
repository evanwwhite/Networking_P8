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

#pragma once

#include "atomic.h"
#include <cstdint>

class SpinLock {
  static AtomicMax<uint64_t> max_critical_section_cycles;
  static Atomic<uint64_t> total_critical_section_cycles;
  static Atomic<uint64_t> critical_section_count;

  Atomic<bool> taken{false};
  uint64_t start = 0;
  volatile bool count_it = false;
  volatile bool was_disabled = false;

  friend void shutdown(bool);

public:
  SpinLock() {}
  SpinLock(const SpinLock &) = delete;
  SpinLock &operator=(const SpinLock &) = delete;

  bool tryLock();
  void lock();
  void unlock();

  static void print_stats();

  friend void impl::reap();
};
