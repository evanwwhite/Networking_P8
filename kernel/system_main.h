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
#include "x86_64.h"
#include <cstdint>

namespace impl {}

namespace Sys {
extern bool normal;
extern uint64_t core_count;
extern volatile bool bootstraping;
extern uint64_t start_tsc;
extern uint64_t hhdm_offset;
extern GDTR gdtr;
extern uint64_t base_tss_index;

// GDT segment selectors
// The order has to be consistent with the GDT layout in system_main.cc
// and the requirements of I32_STAR.
constexpr static uint16_t USER_BASE_SELECTOR = (1 << 3) | 3;
constexpr static uint16_t KERNEL_BASE_SELECTOR = (5 << 3);
constexpr static uint64_t STAR = (uint64_t(KERNEL_BASE_SELECTOR) << 32) |
                                 (uint64_t(USER_BASE_SELECTOR) << 48);
constexpr static uint16_t USER_CS = (3 << 3) | 3;
constexpr static uint16_t USER_DS = (4 << 3) | 3;
constexpr static uint16_t KERNEL_CS = (5 << 3);
constexpr static uint16_t KERNEL_DS = (6 << 3);
} // namespace Sys
