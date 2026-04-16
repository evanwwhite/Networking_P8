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

#include <cstdint>

#include "atomic.h"
#include "condition.h"
#include "debug.h"
#include "thread.h"

class CountDownLatch {
  SpinLock spin{};
  volatile uint64_t count{0};
  Condition is_zero{};

public:
  CountDownLatch() {}

  [[gnu::noinline]]
  void up() {
    spin.lock();
    count += 1;
    spin.unlock();
  }

  [[gnu::noinline]]
  void down() {
    spin.lock();
    ASSERT(count != 0);
    count -= 1;
    if (count == 0) {
      is_zero.notify_all(spin);
    } else {
      spin.unlock();
    }
  }

  [[gnu::noinline]]
  void sync() {
    spin.lock();
    while (count != 0) {
      is_zero.wait(spin);
    }
    spin.unlock();
  }
};