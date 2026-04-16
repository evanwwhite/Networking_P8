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
#include "blocking_lock.h"
#include <cstdint>

namespace heap {
void init(std::uintptr_t base, std::uint64_t bytes);
}

namespace impl {
extern std::uint64_t n_free;
extern std::uint64_t n_leak;
extern std::uint64_t n_malloc;
extern BlockingLock theLock;
extern void check_leaks();
} // namespace impl

extern void *malloc(std::size_t bytes) noexcept;
extern void free(void *p) noexcept;

template <typename T> T *leak(T *p, bool flag) noexcept {
  using namespace impl;
  if (flag) {
    LockGuard g{theLock};
    n_leak += 1;
  }
  return p;
}