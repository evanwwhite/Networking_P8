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

#include "condition.h"
#include "debug.h"
#include "print.h"

class BlockingLock {
  SpinLock spin{};
  Condition not_locked{};
  volatile bool locked = false;

public:
  BlockingLock() {}
  BlockingLock(BlockingLock const &) = delete;
  BlockingLock &operator=(const BlockingLock &) = delete;
  [[gnu::noinline]]
  inline void lock() {
    spin.lock();
    while (locked) {
      not_locked.wait(spin);
    }
    locked = true;
    spin.unlock();
  }
  [[gnu::noinline]]
  inline void unlock() {
    spin.lock();
    ASSERT(locked);
    locked = false;
    not_locked.notify_one(spin);
  }
};
