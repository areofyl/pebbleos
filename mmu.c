#include <stdint.h>

// mmu setup
// identity maps everything (VA == PA) using 1GB block mappings for now
// 4KB granule, 39-bit VA space (T0SZ=25), so we start at L1
//
// page table walk for 4KB granule, 39-bit VA:
//   L1 index = VA[38:30]  -> each entry covers 1GB
//   L2 index = VA[29:21]  -> each entry covers 2MB
//   L3 index = VA[20:12]  -> each entry covers 4KB

void print(const char *s);
void print_hex(unsigned long val);
uint64_t alloc_page(void);

// linker symbols
extern char _rodata_end[];

#define PAGE_SIZE 4096

// memory attribute indices (into MAIR_EL1)
#define MT_DEVICE 0  // device-nGnRnE
#define MT_NORMAL 1  // normal write-back cacheable

// page table entry bits
#define PTE_VALID    (1UL << 0)
#define PTE_TABLE    (3UL << 0)  // L1/L2 table descriptor (bits [1:0] = 11)
#define PTE_BLOCK    (1UL << 0)  // L1/L2 block descriptor (bits [1:0] = 01)
#define PTE_PAGE     (3UL << 0)  // L3 page descriptor (bits [1:0] = 11)
#define PTE_AF       (1UL << 10) // access flag, must set or instant fault
#define PTE_SH_INNER (3UL << 8)  // inner shareable
#define PTE_ATTR(n)  ((uint64_t)(n) << 2)

// access permission bits - AP[2:1] in the PTE
// these control EL0 (user) vs EL1 (kernel) access
#define PTE_AP_RW    (0UL << 6)  // EL1: rw, EL0: none
#define PTE_AP_BOTH  (1UL << 6)  // EL1: rw, EL0: rw
#define PTE_AP_RO    (2UL << 6)  // EL1: ro, EL0: none
#define PTE_AP_ROBOTH (3UL << 6) // EL1: ro, EL0: ro
#define PTE_UXN      (1UL << 54) // unprivileged execute never
#define PTE_PXN      (1UL << 53) // privileged execute never

// mask for extracting the physical address from a PTE
#define PTE_ADDR_MASK 0xFFFFFFF000UL

// L1 table, 512 entries, page-aligned
// with T0SZ=25, TTBR0 points straight to this
static uint64_t l1_table[512] __attribute__((aligned(4096)));

// allocate a zeroed page for use as a page table
static uint64_t *alloc_table(void) {
    uint64_t pa = alloc_page();
    if (!pa) {
        print("[mmu] alloc_table: out of memory!\n");
        for (;;) asm volatile("wfe");
    }
    // zero it out
    uint64_t *table = (uint64_t *)pa;
    for (int i = 0; i < 512; i++)
        table[i] = 0;
    return table;
}

// walk the page tables and return a pointer to the L3 entry for va
// returns NULL if any level isnt mapped
static uint64_t *walk_to_l3(uint64_t va) {
    uint64_t l1_idx = (va >> 30) & 0x1FF;
    uint64_t l2_idx = (va >> 21) & 0x1FF;
    uint64_t l3_idx = (va >> 12) & 0x1FF;

    if (!(l1_table[l1_idx] & PTE_VALID) || !(l1_table[l1_idx] & 0x2))
        return 0;

    uint64_t *l2 = (uint64_t *)(l1_table[l1_idx] & PTE_ADDR_MASK);
    if (!(l2[l2_idx] & PTE_VALID) || !(l2[l2_idx] & 0x2))
        return 0;

    uint64_t *l3 = (uint64_t *)(l2[l2_idx] & PTE_ADDR_MASK);
    return &l3[l3_idx];
}

static void tlb_flush_va(uint64_t va) {
    asm volatile("tlbi vale1, %0" : : "r"(va >> 12));
    asm volatile("dsb sy");
    asm volatile("isb");
}

// map a single 4KB page: va -> pa with given flags
// creates L2/L3 tables as needed
void map_page(uint64_t va, uint64_t pa, uint64_t flags) {
    uint64_t l1_idx = (va >> 30) & 0x1FF;
    uint64_t l2_idx = (va >> 21) & 0x1FF;
    uint64_t l3_idx = (va >> 12) & 0x1FF;

    // get or create L2 table
    uint64_t *l2;
    if (l1_table[l1_idx] & PTE_VALID) {
        if (!(l1_table[l1_idx] & 0x2)) {
            print("[mmu] map_page: L1 entry is a block, cant split yet\n");
            return;
        }
        l2 = (uint64_t *)(l1_table[l1_idx] & PTE_ADDR_MASK);
    } else {
        l2 = alloc_table();
        l1_table[l1_idx] = (uint64_t)l2 | PTE_TABLE;
    }

    // get or create L3 table
    uint64_t *l3;
    if (l2[l2_idx] & PTE_VALID) {
        if (!(l2[l2_idx] & 0x2)) {
            print("[mmu] map_page: L2 entry is a block, cant split yet\n");
            return;
        }
        l3 = (uint64_t *)(l2[l2_idx] & PTE_ADDR_MASK);
    } else {
        l3 = alloc_table();
        l2[l2_idx] = (uint64_t)l3 | PTE_TABLE;
    }

    l3[l3_idx] = (pa & PTE_ADDR_MASK) | PTE_PAGE | PTE_AF | flags;
    tlb_flush_va(va);
}

// remove a 4KB mapping
void unmap_page(uint64_t va) {
    uint64_t *pte = walk_to_l3(va);
    if (!pte || !(*pte & PTE_VALID)) {
        print("[mmu] unmap_page: not mapped: ");
        print_hex(va);
        print("\n");
        return;
    }
    *pte = 0;
    tlb_flush_va(va);
}

// change the flags on an existing 4KB mapping
// keeps the physical address, replaces everything else
void set_page_flags(uint64_t va, uint64_t flags) {
    uint64_t *pte = walk_to_l3(va);
    if (!pte || !(*pte & PTE_VALID)) {
        print("[mmu] set_page_flags: not mapped: ");
        print_hex(va);
        print("\n");
        return;
    }
    uint64_t pa = *pte & PTE_ADDR_MASK;
    *pte = pa | PTE_PAGE | PTE_AF | flags;
    tlb_flush_va(va);
}

// secondary cores reuse the same page tables but need their own system registers
// MAIR, TCR, TTBR0, SCTLR are all per-core — each core has its own copy
void mmu_init_secondary(void) {
    uint64_t mair = (0x00UL << (MT_DEVICE * 8))
                  | (0xFFUL << (MT_NORMAL * 8));
    asm volatile("msr mair_el1, %0" : : "r"(mair));

    uint64_t tcr = (25UL << 0)
                 | (0b00UL << 14)
                 | (0b11UL << 12)
                 | (0b01UL << 10)
                 | (0b01UL << 8)
                 | (0b010UL << 32);
    asm volatile("msr tcr_el1, %0" : : "r"(tcr));

    // point at the SAME L1 table that core 0 built
    asm volatile("msr ttbr0_el1, %0" : : "r"((uint64_t)l1_table));

    asm volatile("tlbi vmalle1");
    asm volatile("dsb sy");
    asm volatile("isb");
    asm volatile("ic iallu");
    asm volatile("dsb sy");
    asm volatile("isb");

    uint64_t sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1 << 0) | (1 << 2) | (1 << 12);
    asm volatile("msr sctlr_el1, %0" : : "r"(sctlr));
    asm volatile("isb");
}

void mmu_init(void) {
    print("[mmu] setting up page tables\n");

    for (int i = 0; i < 512; i++)
        l1_table[i] = 0;

    // RPi 5 (BCM2712) memory layout:
    //   RAM at 0x00000000, kernel loaded at 0x80000
    //   peripherals at 0x107C000000+ (L1 index 65-66)

    // 0x00000000 - 0x3FFFFFFF (1GB): RAM containing kernel
    // use L2 table with 2MB blocks so we can fine-grain the first 2MB
    uint64_t *l2_ram = alloc_table();
    l1_table[0] = (uint64_t)l2_ram | PTE_TABLE;

    // first 2MB (0x00000000-0x001fffff): use L3 table with 4KB pages
    // text/rodata pages: AP_ROBOTH (EL0+EL1 read-only, both can execute)
    // data/BSS/stack pages: AP_RW (EL1-only read-write)
    uint64_t *l3_first = alloc_table();
    l2_ram[0] = (uint64_t)l3_first | PTE_TABLE;

    uint64_t rodata_end_pa = (uint64_t)_rodata_end;
    for (int i = 0; i < 512; i++) {
        uint64_t pa = (uint64_t)i * PAGE_SIZE;
        uint64_t flags = PTE_PAGE | PTE_AF | PTE_SH_INNER | PTE_ATTR(MT_NORMAL);
        if (pa >= 0x80000 && pa < rodata_end_pa)
            flags |= PTE_AP_ROBOTH;  // EL0+EL1 read-only+exec (kernel text)
        // else: default AP=00 → EL1 rw, EL0 none
        l3_first[i] = pa | flags;
    }

    // remaining 2MB blocks in first 1GB, EL1-only
    for (int i = 1; i < 64; i++) {
        l2_ram[i] = ((uint64_t)i * 0x200000)
                  | PTE_VALID | PTE_BLOCK
                  | PTE_AF | PTE_SH_INNER | PTE_ATTR(MT_NORMAL);
    }

    // L1 entries for device memory regions
    // With 4KB granule + 39-bit VA, L1 can't use block descriptors (1GB blocks
    // only work with 36-bit or larger granule). Must use L2 table with 2MB blocks.

    // L1 entry 65: BCM2712 peripherals (0x107C000000)
    uint64_t *l2_periph = alloc_table();
    l1_table[65] = (uint64_t)l2_periph | PTE_TABLE;
    for (int i = 0; i < 512; i++) {
        l2_periph[i] = (0x107C000000UL + (uint64_t)i * 0x200000)
                     | PTE_VALID | PTE_BLOCK
                     | PTE_AF | PTE_ATTR(MT_DEVICE);
    }

    // L1 entry 66: more peripherals
    uint64_t *l2_periph2 = alloc_table();
    l1_table[66] = (uint64_t)l2_periph2 | PTE_TABLE;
    for (int i = 0; i < 512; i++) {
        l2_periph2[i] = (0x10C0000000UL + (uint64_t)i * 0x200000)
                      | PTE_VALID | PTE_BLOCK
                      | PTE_AF | PTE_ATTR(MT_DEVICE);
    }

    // L1 entry 124: RP1 peripherals (0x1F00000000, UART0 at 0x1F00030000)
    uint64_t *l2_rp1 = alloc_table();
    l1_table[124] = (uint64_t)l2_rp1 | PTE_TABLE;
    for (int i = 0; i < 512; i++) {
        l2_rp1[i] = (0x1F00000000UL + (uint64_t)i * 0x200000)
                   | PTE_VALID | PTE_BLOCK
                   | PTE_AF | PTE_ATTR(MT_DEVICE);
    }

    print("[mmu] 0x00000000-0x001fffff -> normal (L3, text/rodata EL0+EL1 ro)\n");
    print("[mmu] 0x00200000-0x07ffffff -> normal (EL1 only)\n");
    print("[mmu] 0x107C000000 -> device (BCM2712 peripherals)\n");
    print("[mmu] 0x1F00000000 -> device (RP1 peripherals)\n");

    // MAIR_EL1: what our attribute indices actually mean
    // attr0 = 0x00 -> device-nGnRnE (no gathering, no reordering, no early ack)
    // attr1 = 0xFF -> normal, write-back read+write allocate
    uint64_t mair = (0x00UL << (MT_DEVICE * 8))
                  | (0xFFUL << (MT_NORMAL * 8));
    asm volatile("msr mair_el1, %0" : : "r"(mair));

    // TCR_EL1: translation control register
    // T0SZ  = 25 -> 39-bit VA (512GB)
    // TG0   = 0b00 -> 4KB granule
    // SH0   = 0b11 -> inner shareable
    // ORGN0 = 0b01 -> outer write-back cacheable
    // IRGN0 = 0b01 -> inner write-back cacheable
    // IPS   = 0b010 -> 40-bit PA (1TB, needed for peripherals above 4GB)
    uint64_t tcr = (25UL << 0)      // T0SZ
                 | (0b00UL << 14)   // TG0
                 | (0b11UL << 12)   // SH0
                 | (0b01UL << 10)   // ORGN0
                 | (0b01UL << 8)    // IRGN0
                 | (0b010UL << 32); // IPS = 40-bit
    asm volatile("msr tcr_el1, %0" : : "r"(tcr));

    // point TTBR0 at our L1 table
    asm volatile("msr ttbr0_el1, %0" : : "r"((uint64_t)l1_table));

    // invalidate TLB and make sure everything above is visible
    asm volatile("tlbi vmalle1");
    asm volatile("dsb sy");
    asm volatile("isb");

    // clean and invalidate caches before enabling MMU
    asm volatile("ic iallu");   // invalidate instruction cache
    asm volatile("dsb sy");
    asm volatile("isb");

    // clean and invalidate caches before enabling MMU
    asm volatile("ic iallu");
    asm volatile("dsb sy");
    asm volatile("isb");

    // flip the switch: enable MMU + caches
    uint64_t sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1 << 0);   // M  - MMU on
    sctlr |= (1 << 2);   // C  - data cache
    sctlr |= (1 << 12);  // I  - instruction cache
    asm volatile("msr sctlr_el1, %0" : : "r"(sctlr));
    asm volatile("isb");

    print("[mmu] enabled\n");
}
