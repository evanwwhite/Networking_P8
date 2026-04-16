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

#include "ramdisk.h"
#include "limine++.h"
#include "machine.h"
#include "pit.h"
#include "print.h"
#include "thread.h"

[[gnu::section(
    ".limine_requests")]] constexpr volatile limine::Module modules_request{};

static limine_file *find_module(const char *module_name) {
  auto response = modules_request.response;
  ASSERT(response != nullptr);

  for (uint64_t i = 0; i < response->module_count; i++) {
    auto m = response->modules[i];
    if (streq(m->path, module_name)) {
      return m;
    }
  }

  KPANIC("module ? not found\n", module_name);
}

RamDisk::RamDisk(const limine_file *m, uint64_t delay)
    : BlockIO(512), mod(m), delay(delay) {}

RamDisk::RamDisk(const char *module_name, uint64_t delay)
    : RamDisk(find_module(module_name), delay) {}

void RamDisk::read_block(uint32_t block, char *buffer) {
  ASSERT(block < size_in_blocks());
  auto when = Pit::jiffies + delay;
  while (Pit::jiffies < when)
    Thread::yield();
  auto offset = block_size * block;
  memcpy(buffer, (char *)(mod->address) + offset, block_size);
}

uint32_t RamDisk::size_in_bytes() { return mod->size; }
