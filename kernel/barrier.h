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
#include "machine.h"
#include <cstdint>

class Barrier {

  SpinLock spin{};
  Condition is_zero{};
  volatile std::uint64_t count;

public:
  Barrier(std::uint64_t count) : count(count) {}
  [[gnu::noinline]]
  void sync() {
    spin.lock();
    count -= 1;
    if (count == 0) {
      is_zero.notify_all(spin);
    } else {
      while (count != 0) {
        is_zero.wait(spin);
      }
      spin.unlock();
    }
  }
};
