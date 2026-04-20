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

#include "vmm.h"

#include "ext2.h"
#include "idt.h"
#include "machine.h"
#include "physmem.h"
#include "print.h"
#include "shared.h"
#include "system_main.h"
#include <cstdint>

// Claude used for logic and understanding documentation
// anonymous namespace keeps all of this internal to this translation unit,
namespace {

// memory layout
constexpr uint64_t PAGE_SIZE = FRAME_SIZE;
constexpr uint64_t PAGE_MASK = PAGE_SIZE - 1;      // for alignment math
constexpr uint64_t HUGE_2M = 2ULL * 1024 * 1024;  // size of 2MB huge page
constexpr uint64_t PRIVATE_BASE = PAGE_SIZE;        // skip page 0 so null derefs fault cleanly
constexpr uint64_t CONFIGURED_MAX_PA = 128ULL * 1024 * 1024;  // 128MB physical memory limit
constexpr uint64_t SHARED_LIMIT = UINT64_C(0xFFFFFFFF80000000); // top of the shared VA region

// page table entry flag bits
constexpr uint64_t PTE_PRESENT   = 1ULL << 0;  // mapped/ valid
constexpr uint64_t PTE_WRITE     = 1ULL << 1;  // writable
constexpr uint64_t PTE_USER      = 1ULL << 2;  // user mode
constexpr uint64_t PTE_HUGE      = 1ULL << 7;  // maps large page
constexpr uint64_t PTE_ADDR_MASK = UINT64_C(0x000FFFFFFFFFF000); // strips leaving PPN

// round a VA down to containing page
inline uint64_t page_align_down(uint64_t value) { return value & ~PAGE_MASK; }

// round a VA up to the next page boundary (no-op if aligned)
inline uint64_t page_align_up(uint64_t value) {
    return (value + PAGE_MASK) & ~PAGE_MASK;
}

// build a page table entry: shift PPN into position and OR in the requested flags.
// PTE_PRESENT is always set — there's no reason to build an entry a non-present page
inline uint64_t make_entry(PPN ppn, bool user, bool write) {
    return (uint64_t(ppn.ppn()) << 12) | PTE_PRESENT | (write ? PTE_WRITE : 0) |
            (user ? PTE_USER : 0);
}

// each level consumes 9 bits of the VPN starting from the most significant
inline uint64_t table_index(VPN vpn, uint32_t level) {
    return (vpn.vpn() >> (level * 9)) & 0x1FF;
}

inline bool ppn_in_configured_phys(PPN ppn) {
    return PA(ppn).pa() < CONFIGURED_MAX_PA;
}

} // namespace

// 0 until init_system() captures the cr3
// using 0 as a stopper any use before will crash
uint64_t impl::common_cr3 = 0;

//  63:48 must be sign-extended copies of bit 47.
// left-shift by 16 then arithmetic right-shift if the address was canonical, nothing changes
uint64_t VA::check_canonical(uint64_t va) {
    int64_t sa = int64_t(va);
    ASSERT(((sa << 16) >> 16) == sa);
    return va;
}

namespace impl {

// layout of the stack frame pushed by the CPU on a page fault
struct PageFaultTrapFrame {
    std::uint64_t rax;
    std::uint64_t rbx;
    std::uint64_t rcx;
    std::uint64_t rdx;
    std::uint64_t rdi;
    std::uint64_t rsi;
    std::uint64_t rbp;
    std::uint64_t r8;
    std::uint64_t r9;
    std::uint64_t r10;
    std::uint64_t r11;
    std::uint64_t r12;
    std::uint64_t r13;
    std::uint64_t r14;
    std::uint64_t r15;
    std::uint64_t error_code;
    std::uint64_t rip;
    std::uint64_t cs;
    std::uint64_t rflags;
    std::uint64_t rsp;
    std::uint64_t ss;
};

// a virtual memory entry: describes a one contiguous mapped region.
// for file-backed mappings, node + offset say where the data comes from
// for anonymous mappings, node is null and pages are zeroed
struct VME {
    uint64_t start;           // first byte of the mapping
    uint64_t end;             // one past the final byte
    StrongRef<Node> node;     // backing file, or null for anonymous
    uint64_t offset;          // byte offset into the file for the very first page
    VME *next = nullptr;      // intrusive linked list, sorted by thestart address
    bool shared = false;      // true if came from MAP_SHARED
};

thread_local VME *vmes = nullptr;   // per-thread private VME list (it is not shared)
VME *shared_vmes = nullptr;          // global shared VME list always access under shared_lock
SpinLock shared_lock{};
Atomic<uint64_t> active_vmm_threads{0}; // how many threads are currently have an active VMM

// used to make ensure_shared_roots() across threads
Atomic<uint64_t> shared_roots_ready{0};
SpinLock shared_roots_init_lock{};

// shared region starts right after HHDM. everything from here to SHARED_LIMIT
// lives in common_cr3 and is visible to all
uint64_t shared_base() { 
    return Sys::hhdm_offset + CONFIGURED_MAX_PA; 
}

// list is kept sorted, but we scan linearly since it's usually short
VME *find_vme(VME *head, uint64_t va) {
    while (head != nullptr) {
        if (va >= head->start && va < head->end) {
            return head;
        }
        head = head->next;
    }
    return nullptr;
}

VME *clone_vme_list(VME *head) {
    VME *copy_head = nullptr;
    VME **tail = &copy_head;

    while (head != nullptr) {
        auto *copy = new VME();
        copy->start = head->start;
        copy->end = head->end;
        copy->node = head->node;
        copy->offset = head->offset;
        copy->shared = head->shared;
        copy->next = nullptr;
        *tail = copy;
        tail = &copy->next;
        head = head->next;
    }

    return copy_head;
}

// check that [start, start+length) fits within [0, limit) without overflow
bool fits_in_range(uint64_t start, uint64_t length, uint64_t limit) {
    return start < limit && length <= (limit - start);
}

// populate a physical frame with the data it should contain for va_page.
// always zeroes the whole frame first, handles anonymous mappings and 
// also ensures we never leak stale kernel data to user space
void load_page(const VME *vme, uint64_t va_page, PPN frame) {
    char *frame_ptr = VA(frame);

    // zero the whole page first
    uint64_t *words = (uint64_t *)frame_ptr;
    for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
        words[i] = 0;
    }

    StrongRef<Node> null_node{};
    if (vme->node == null_node) {
        return; // anonymous mapping zeroed page is all we need
    }

    // figure out which part of the file this page covers
    uint64_t page_index   = (va_page - vme->start) / PAGE_SIZE;
    uint64_t file_offset  = vme->offset + page_index * PAGE_SIZE;
    uint64_t file_size    = vme->node->size_in_bytes();

    if (file_offset >= file_size) {
        return; // mapping extends past EOF leave the page zeroed
    }

    // clamp to however many bytes remain in the file
    uint64_t bytes_to_read = PAGE_SIZE;
    if (file_offset + bytes_to_read > file_size) {
        bytes_to_read = file_size - file_offset;
    }

    auto cnt = vme->node->read_all(uint32_t(file_offset), uint32_t(bytes_to_read), frame_ptr);
    ASSERT(cnt == int64_t(bytes_to_read));
}

// first-fit allocator for virtual address space: walk the sorted VME list looking
// for gap that's big enough, then insert a new VME there
void *insert_vme(VME **head, uint64_t base, uint64_t limit, uint64_t length,
                 StrongRef<Node> file, uint64_t offset, bool shared) {
    uint64_t candidate = base;
    VME *next = *head;
    VME **pprev = head;

    // walk forward until we find a gap that fits, or fall off the end
    while (next != nullptr) {
        if (candidate <= next->start && length <= (next->start - candidate)) {
            break; // found a gap
        }
        candidate = next->end;
        pprev = &next->next;
        next = next->next;
    }

    if (!fits_in_range(candidate, length, limit)) {
        return (void *)-1; // no space found
    }

    auto vme = new VME();
    vme->start = candidate;
    vme->end = candidate + length;
    vme->node = file;
    vme->offset = offset;
    vme->next = next;
    vme->shared = shared;
    *pprev = vme;
    return (void *)candidate;
}

// pre-allocate the PML4 slots covering the shared VA region so every new
// thread can copy them from common_cr3 and immediately see shared mappings.
// without this, threads start after a shared page is faulted in would
// have a NULL PML4 entry and fault on access.
// it uses double checkeed locking, first check outside the lock is cheap and
// handles the common case. second check inside handles the race where two
// threads both see it as not-ready simultaneously
void ensure_shared_roots() {
    if (shared_roots_ready.get()) {
        return; // fast path
    }

    LockGuard guard{shared_roots_init_lock};

    if (shared_roots_ready.get()) {
        return; // another thread beat us here
    }

    uint64_t *root = VA(PPN(common_cr3 >> 12));
    uint64_t first_slot = (shared_base() >> 39) & 0x1FF;
    uint64_t last_slot = (SHARED_LIMIT >> 39) & 0x1FF;

    // first slot should already be present
    ASSERT(root[first_slot] & PTE_PRESENT);

    // allocate empty page tables for the remaining PML4 slots
    for (uint64_t slot = first_slot + 1; slot < last_slot; slot++) {
        if (root[slot] & PTE_PRESENT) {
            continue;
        }
        PPN child = physMem.alloc();
        root[slot] = make_entry(child, true, true);
    }

    shared_roots_ready.set(1);
}

// when we need to map a single 4KB page inside a region covered by a huge page,
// we have to split huge entry into 512 smaller ones first.
// level 2 splits a 1GB entry into 512 2MB entries; level 1 splits a 2MB entry into 512 4KB entries
void split_large_entry(uint64_t *table, uint64_t index, uint32_t level) {
    ASSERT(level == 2 || level == 1);

    uint64_t entry = table[index];
    ASSERT(entry & PTE_PRESENT);
    ASSERT(entry & PTE_HUGE);

    PPN child = physMem.alloc();
    auto *child_table = (uint64_t *)VA(child);
    uint64_t base       = entry & PTE_ADDR_MASK;
    uint64_t leaf_flags = entry & (PTE_PRESENT | PTE_WRITE | PTE_USER);

    if (level == 2) {
        // 1GB 512 x 2MB entries
        for (uint64_t i = 0; i < 512; i++) {
            child_table[i] = (base + i * HUGE_2M) | leaf_flags | PTE_HUGE;
        }
    } else {
        // 2MB 512 x 4KB entries
        for (uint64_t i = 0; i < 512; i++) {
            child_table[i] = (base + i * PAGE_SIZE) | leaf_flags;
        }
    }

    // replace the huge entry with a pointer to the new child table
    table[index] = make_entry(child, (leaf_flags & PTE_USER) != 0,
                              (leaf_flags & PTE_WRITE) != 0);
}

// recursive page table walk we descend from the given table at 'level' until we
// reach level 0, then write the leaf entry. allocates intermediate tables as needed.
// also handles splitting huge entries if a fine-grained mapping is needed inside one
void map(const PPN table_ppn, uint32_t level, VPN vpn, PPN ppn, bool user,
         bool write) {
    uint64_t *table = VA(table_ppn);
    uint64_t index  = table_index(vpn, level);

    if (level == 0) {
        // leaf level write the entry and we're done
        table[index] = make_entry(ppn, user, write);
        return;
    }

    if (table[index] & PTE_PRESENT) {
        if ((table[index] & PTE_HUGE) && (level == 2 || level == 1)) {
            // can't descend huge entry split it into smaller pages first
            split_large_entry(table, index, level);
        }
        // intermediate table already exists, just descend
    } else {
        PPN child = physMem.alloc();
        table[index] = make_entry(child, true, true);
    }

    PPN child_ppn((table[index] >> 12) & 0xFFFFFFFFF);
    map(child_ppn, level - 1, vpn, ppn, user, write);
}

// map into an explicit CR3 
void map_in_root(uint64_t cr3, VPN vpn, PPN ppn, bool user, bool write) {
    map(PPN(cr3 >> 12), 3, vpn, ppn, user, write);
}

// map into the current thread's CR3
void map(VPN vpn, PPN ppn, bool user, bool write) {
    map_in_root(get_cr3(), vpn, ppn, user, write);
}

} // namespace impl

// walk the 4 level page table rooted at cr3 and return a pointer to the
// leaf PTE for va, or nullptr if any level is not present or hits huge page
static uint64_t *find_pte_in_root(uint64_t cr3, uint64_t va) {
    uint64_t *table   = VA(PPN(cr3 >> 12));
    uint64_t vpn_full = va >> 12;

    for (int level = 3; level >= 0; level--) {
        uint64_t index = (vpn_full >> (level * 9)) & 0x1FF;
        uint64_t entry = table[index];

        if (!(entry & PTE_PRESENT)) {
            return nullptr;
        }

        if (level == 0) {
            return &table[index]; // found the leaf entry
        }

        if ((entry & PTE_HUGE) && (level == 2 || level == 1)) {
            return nullptr; // inside a huge page
        }

        table = VA(PPN((entry >> 12) & 0xFFFFFFFFF));
    }

    return nullptr;
}

// find_pte for the current thread's address space
static uint64_t *find_pte(uint64_t va) {
    return find_pte_in_root(get_cr3(), va);
}

bool VMM::user_buffer_ok(const void *addr, size_t length) {
    uint64_t start = uint64_t(addr);

    if (length == 0) {
        return start < Sys::hhdm_offset;
    }

    if (start < PRIVATE_BASE || start >= Sys::hhdm_offset) {
        return false;
    }

    uint64_t end = start + length - 1;
    if (end < start || end >= Sys::hhdm_offset) {
        return false;
    }

    uint64_t first_page = page_align_down(start);
    uint64_t last_page  = page_align_down(end);
    for (uint64_t va = first_page;; va += PAGE_SIZE) {
        uint64_t *pte = find_pte(va);
        if (pte == nullptr || !(*pte & PTE_PRESENT) || !(*pte & PTE_USER)) {
            return false;
        }
        if (va == last_page) {
            return true;
        }
    }
}

// flush a single page from the TLB. must be called after clearing a PTE
static inline void invlpg(uint64_t addr) {
    asm volatile("invlpg (%0)" ::"r"(addr) : "memory");
}

// unmap and free every page that was faulted in for this VME.
// called during thread teardown
static void free_mapped_frames(impl::VME *vme) {
    for (uint64_t va = vme->start; va < vme->end; va += PAGE_SIZE) {
        uint64_t *pte = find_pte(va);
        if (pte && (*pte & PTE_PRESENT)) {
            uint64_t raw_ppn = (*pte >> 12) & 0xFFFFFFFFF;
            *pte = 0;
            invlpg(va);
            if (raw_ppn != 0) {
                physMem.free(PPN(raw_ppn));
            }
        }
    }
}

// free the shared VME structs. called only by the last thread to exit,
// since the shared region has no per-thread owner.
// does NOT free the physical frames or PTE
static void free_shared_vmes_only() {
    using namespace impl;

    VME *p = nullptr;
    {
        LockGuard guard{shared_lock};
        p = shared_vmes;
        shared_vmes = nullptr;
    }

    while (p != nullptr) {
        auto next = p->next;
        delete p;
        p = next;
    }
}

// recursively free a page table subtree, stopping levels_below levels down.
// does not free the root table itself
static void free_table_tree(PPN table_ppn, uint32_t levels_below) {
    if (levels_below == 0) {
        return;
    }

    uint64_t *table = VA(table_ppn);
    for (uint64_t i = 0; i < 512; i++) {
        uint64_t entry = table[i];
        if (!(entry & PTE_PRESENT)) {
            continue;
        }
        if ((entry & PTE_HUGE) && (levels_below == 2 || levels_below == 1)) {
            // huge page no child table to recurse into
            table[i] = 0;
            continue;
        }

        PPN child((entry >> 12) & 0xFFFFFFFFF);
        if (child.ppn() == 0 || !ppn_in_configured_phys(child)) {
            table[i] = 0;
            continue;
        }
        free_table_tree(child, levels_below - 1);
        table[i] = 0;
        physMem.free(child);
    }
}

// free all page table structures for the private (lower-half) address space.
// only iterates the lower 256 PML4 slots the upper half belongs to common_cr3
static void free_private_page_tables(uint64_t cr3) {
    uint64_t *root = VA(PPN(cr3 >> 12));

    // only free lower half entries (0-255). upper half belongs to common_cr3
    for (uint64_t i = 0; i < 256; i++) {
        uint64_t entry = root[i];
        if (!(entry & PTE_PRESENT)) {
            continue;
        }

        PPN child((entry >> 12) & 0xFFFFFFFFF);
        if (child.ppn() == 0 || !ppn_in_configured_phys(child)) {
            root[i] = 0;
            continue;
        }
        free_table_tree(child, 2); // free PDPT/ everything below it
        root[i] = 0;
        physMem.free(child);
    }
}

static void clone_user_pages(PPN table_ppn, uint32_t level, uint64_t base_vpn) {
    uint64_t *table = VA(table_ppn);

    for (uint64_t i = 0; i < 512; i++) {
        uint64_t entry = table[i];
        if (!(entry & PTE_PRESENT)) {
            continue;
        }

        uint64_t vpn_bits = base_vpn | (i << (level * 9));

        if (level == 0) {
            if (!(entry & PTE_USER)) {
                continue;
            }

            PPN src_ppn((entry >> 12) & 0xFFFFFFFFF);
            PPN dst_ppn = physMem.alloc();
            memcpy(VA(dst_ppn), VA(src_ppn), PAGE_SIZE);
            impl::map_in_root(get_cr3(), VPN(VA(vpn_bits << LOG_FRAME_SIZE)),
                              dst_ppn, true, (entry & PTE_WRITE) != 0);
            continue;
        }

        if (entry & PTE_HUGE) {
            KPANIC("fork_from does not support huge pages ?\n", Dec(level));
        }

        clone_user_pages(PPN((entry >> 12) & 0xFFFFFFFFF), level - 1, vpn_bits);
    }
}

void VMM::fork_from(uint64_t source_cr3) {
    // New kernel threads inherit the parent's live TLS image, which means the
    // child starts out pointing at the parent's private VME list. Duplicate it
    // here so the child can tear down its address space without freeing the
    // parent's VM metadata.
    impl::vmes = impl::clone_vme_list(impl::vmes);

    uint64_t *root = VA(PPN(source_cr3 >> 12));
    for (uint64_t i = 0; i < 256; i++) {
        uint64_t entry = root[i];
        if (!(entry & PTE_PRESENT)) {
            continue;
        }
        clone_user_pages(PPN((entry >> 12) & 0xFFFFFFFFF), 2, i << 27);
    }
}

// mmap: reserve a region of virtual address space backed by file or anonymous memory.
// addr is ignored, prot is ignored all mappings are rw.
// returns the chosen base address
void *VMM::mmap(void *addr, size_t length, int prot, int flags,
                StrongRef<Node> file, uint64_t offset) {
    (void)addr;
    (void)prot;

    if (length == 0) {
        return (void *)-1;
    }
    if (offset % PAGE_SIZE != 0) {
        return (void *)-1; // offset must be page-aligned per POSIX
    }

    length = page_align_up(length);

    using namespace impl;

    StrongRef<Node> null_node{};
    bool anonymous = (flags & MAP_ANONYMOUS) != 0;
    bool shared    = (flags & MAP_SHARED) != 0;

    // MAP_ANONYMOUS and a non-null file are contradictory
    if (anonymous != (file == null_node)) {
        return (void *)-1;
    }

    if (shared) {
        // shared mappings go into the global VME list under the lock.
        // they live in the shared VA region visible to all threads via common_cr3
        LockGuard guard{shared_lock};
        return insert_vme(&shared_vmes, shared_base(), SHARED_LIMIT, length, file,
                          offset, true);
    }

    // private mapping goes into this thread's VME list
    return insert_vme(&vmes, PRIVATE_BASE, Sys::hhdm_offset, length, file, offset,
                      false);
}

// munmap: unmap a region of private virtual address space.
// shared mappings are intentionally rejected
// handles partial unmaps by splitting or trimming existing VMEs
int VMM::munmap(void *addr, size_t length) {
    uint64_t start = uint64_t(addr);

    if (start % PAGE_SIZE != 0) {
        return -1;
    }
    if (length == 0) {
        return -1;
    }

    length = page_align_up(length);
    uint64_t end = start + length;
    if (end < start) {
        return -1; // overflow
    }

    using namespace impl;

    if (start < PRIVATE_BASE || end > Sys::hhdm_offset) {
        return -1;
    }
    // reject shared region addresses unmapping shared mappings is a no-op because
    // they live in common_cr3 and are shared across all threads. safely removing them
    // would require reference counting and a TLB shootdown across all cores, neither
    // of which we implement. the shared VMEs are cleaned up when the last thread exits
    if (start >= shared_base() || end > shared_base()) {
        return -1;
    }

    VME **pprev = &vmes;
    VME *curr   = vmes;

    while (curr != nullptr) {
        // skip VMEs that don't overlap the unmap range
        if (curr->end <= start || curr->start >= end) {
            pprev = &curr->next;
            curr  = curr->next;
            continue;
        }

        // clamp to actual overlap
        uint64_t unmap_start = (curr->start > start) ? curr->start : start;
        uint64_t unmap_end   = (curr->end   < end)   ? curr->end   : end;

        // free any frames that were faulted in for this range
        for (uint64_t va = unmap_start; va < unmap_end; va += PAGE_SIZE) {
            uint64_t *pte = find_pte(va);
            if (pte && (*pte & PTE_PRESENT)) {
                uint64_t raw_ppn = (*pte >> 12) & 0xFFFFFFFFF;
                *pte = 0;
                invlpg(va);
                if (raw_ppn != 0) {
                    physMem.free(PPN(raw_ppn));
                }
            }
        }

        // unmap punches a hole in the middle
        if (unmap_start > curr->start && unmap_end < curr->end) {
            VME *tail    = new VME();
            tail->start  = unmap_end;
            tail->end    = curr->end;
            tail->node   = curr->node;
            tail->offset = curr->offset + (unmap_end - curr->start);
            tail->next   = curr->next;
            tail->shared = false;

            curr->end  = unmap_start;
            curr->next = tail;
            pprev = &curr->next;
            curr  = tail->next;
            continue;
        }

        // unmap trims the front of this VME
        if (unmap_start == curr->start && unmap_end < curr->end) {
            curr->offset += (unmap_end - curr->start);
            curr->start   = unmap_end;
            pprev = &curr->next;
            curr  = curr->next;
            continue;
        }

        // unmap trims the tail of this VME
        if (unmap_start > curr->start && unmap_end == curr->end) {
            curr->end = unmap_start;
            pprev = &curr->next;
            curr  = curr->next;
            continue;
        }

        // VME is fully covered
        *pprev = curr->next;
        delete curr;
        curr = *pprev;
    }

    return 0;
}

// page fault handler called by the CPU on any page fault
// cr2 holds the faulting VA
extern "C" [[gnu::force_align_arg_pointer]] void
pageFaultHandler(uintptr_t cr2, impl::PageFaultTrapFrame *trap_frame) {
    using namespace impl;

    // Assert
    ASSERT(!is_disabled());

    // if the present bit is set in the error code, the page is mapped but
    // the access was still rejected
    if (trap_frame->error_code & 1) {
        KPANIC("Page fault at ?, error code: ?, rip: ?\n", cr2,
               trap_frame->error_code, trap_frame->rip);
    }

    uint64_t va_page = page_align_down(cr2);

    if (va_page >= shared_base() && va_page < SHARED_LIMIT) {
        VME *shared_vme = nullptr;
        {
            LockGuard guard{shared_lock};
            shared_vme = find_vme(shared_vmes, va_page);
            if (shared_vme != nullptr) {
                // check if another thread already faulted this page in while we waited
                uint64_t *pte = find_pte_in_root(common_cr3, va_page);
                if (pte && (*pte & PTE_PRESENT)) {
                    return; // already mapped
                }
            }
        }

        if (shared_vme != nullptr) {
            // load the page outside the lock
            PPN frame = physMem.alloc();
            load_page(shared_vme, va_page, frame);

            // re-acquire and check again before mapping
            LockGuard guard{shared_lock};
            uint64_t *pte = find_pte_in_root(common_cr3, va_page);
            if (pte && (*pte & PTE_PRESENT)) {
                physMem.free(frame); // lost race
            } else {
                impl::map_in_root(common_cr3, VPN(VA(va_page)), frame, true, true);
            }
            return;
        }
    }

    // private fault
    auto p = find_vme(vmes, va_page);
    if (p != nullptr) {
        PPN frame = physMem.alloc();
        load_page(p, va_page, frame);
        impl::map(VPN(VA(va_page)), frame, true, true);
        return;
    }

    // faulting address isn't in any known mapping
    KPANIC("Page fault at ?, error code: ?, rip: ?\n", cr2, 
        trap_frame->error_code, trap_frame->rip);
}

// called once at boot to capture the kernel's CR3 as the shared template,
void VMM::init_system() {
    impl::common_cr3 = get_cr3();
    IDT::trap(14, uintptr_t(pageFaultHandler_), 0);
}

void VMM::init_core() {}

void VMM::init_thread() {
    impl::ensure_shared_roots();

    PPN new_cr3_ppn = physMem.alloc();
    PA  new_cr3_pa  = new_cr3_ppn;

    uint64_t *new_table = VA(new_cr3_ppn);
    uint64_t *old_table = VA(PPN(impl::common_cr3 >> 12));

    // copy only the upper half of the PML4
    // lower half stays zeroed
    for (uint64_t i = 512 / 2; i < 512; i++) {
        new_table[i] = old_table[i];
    }

    set_cr3(new_cr3_pa.pa());
    impl::active_vmm_threads.add_fetch(1);
}

// called when a thread exits free private mappings and page tables, then
// switch back to common_cr3 before freeing the thread's CR3 page.
void VMM::fini_thread() {
    using namespace impl;

    // free frames and VME structs for private mappings
    auto p = vmes;
    vmes   = nullptr;
    while (p != nullptr) {
        free_mapped_frames(p);
        auto next = p->next;
        delete p;
        p = next;
    }

    uint64_t cr3     = get_cr3();
    uint64_t cr3_ppn = cr3 >> 12;

    // switch back to the common CR3 before freeing this thread's page tables
    set_cr3(impl::common_cr3);

    // last thread to exit cleans up the shared VME list
    if (active_vmm_threads.sub_fetch(1) == 0) {
        free_shared_vmes_only();
    }

    // now safe to free the private page tables
    free_private_page_tables(cr3);
    physMem.free(PPN(cr3_ppn));
}
