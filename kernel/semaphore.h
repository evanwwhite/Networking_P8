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
#include "condition.h"

class Semaphore {
  SpinLock lock{};
  uint64_t count;
  Condition not_zero{};

public:
  Semaphore(uint32_t n) : count(n) {}
  Semaphore(const Semaphore &) = delete;
  Semaphore &operator=(const Semaphore &) = delete;

  [[gnu::noinline]]
  void down() {
    lock.lock();
    while (count == 0) {
      not_zero.wait(lock);
    }
    count--;
    lock.unlock();
  }

  [[gnu::noinline]]
  void up() {
    lock.lock();
    count += 1;
    not_zero.notify_one(lock);
  }
};
