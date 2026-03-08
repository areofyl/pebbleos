#include <stdint.h>

// ramfb driver
// configures QEMU's ramfb device via the fw_cfg interface
// fw_cfg is an MMIO device that lets the guest read/write config files
// we find the "etc/ramfb" file and write our framebuffer config to it

void print(const char *s);
void print_hex(unsigned long val);
uint64_t alloc_page(void);

#define PAGE_SIZE 4096

// fw_cfg MMIO registers (QEMU virt machine)
#define FWCFG_BASE     0x09020000
#define FWCFG_DATA     (*(volatile uint8_t  *)(FWCFG_BASE + 0x00))
#define FWCFG_SEL      (*(volatile uint16_t *)(FWCFG_BASE + 0x08))
#define FWCFG_DMA_HI   (*(volatile uint32_t *)(FWCFG_BASE + 0x10))
#define FWCFG_DMA_LO   (*(volatile uint32_t *)(FWCFG_BASE + 0x14))

// fw_cfg selectors
#define FWCFG_FILE_DIR 0x0019

// fw_cfg DMA control bits
#define FWCFG_DMA_READ   (1 << 1)
#define FWCFG_DMA_WRITE  (1 << 2)
#define FWCFG_DMA_SELECT (1 << 3)

// byte swap helpers (fw_cfg uses big-endian for DMA and file entries)
static uint16_t be16(uint16_t v) {
    return (v >> 8) | (v << 8);
}

static uint32_t be32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}

static uint64_t be64(uint64_t v) {
    return ((uint64_t)be32((uint32_t)v) << 32) | be32((uint32_t)(v >> 32));
}

// read one byte from currently selected fw_cfg file
static uint8_t fwcfg_read8(void) {
    return FWCFG_DATA;
}

// read big-endian uint16
static uint16_t fwcfg_read_be16(void) {
    uint16_t v = (uint16_t)fwcfg_read8() << 8;
    v |= fwcfg_read8();
    return v;
}

// read big-endian uint32
static uint32_t fwcfg_read_be32(void) {
    uint32_t v = (uint32_t)fwcfg_read8() << 24;
    v |= (uint32_t)fwcfg_read8() << 16;
    v |= (uint32_t)fwcfg_read8() << 8;
    v |= fwcfg_read8();
    return v;
}

// DMA write: write data from memory to the currently selected fw_cfg file
// the DMA access struct must be in physically contiguous memory
struct fw_cfg_dma {
    uint32_t control;  // big-endian
    uint32_t length;   // big-endian
    uint64_t address;  // big-endian
};

static void fwcfg_dma_write(uint16_t selector, void *buf, uint32_t len) {
    // DMA struct must be naturally aligned
    static struct fw_cfg_dma dma __attribute__((aligned(16)));

    dma.control = be32((uint32_t)selector << 16 | FWCFG_DMA_SELECT | FWCFG_DMA_WRITE);
    dma.length = be32(len);
    dma.address = be64((uint64_t)buf);

    // write physical address of DMA struct to the DMA register (big-endian)
    uint64_t dma_addr = (uint64_t)&dma;
    FWCFG_DMA_HI = be32((uint32_t)(dma_addr >> 32));
    FWCFG_DMA_LO = be32((uint32_t)dma_addr);

    // poll until QEMU clears the control field
    asm volatile("dsb sy");
    while (dma.control != 0)
        asm volatile("dsb sy");
}

// find a fw_cfg file by name, returns its selector or -1
static int fwcfg_find_file(const char *name) {
    // select the file directory (selector register is big-endian on MMIO)
    FWCFG_SEL = be16(FWCFG_FILE_DIR);

    // first 4 bytes = file count (big-endian)
    uint32_t count = fwcfg_read_be32();

    // each entry: size(4) + select(2) + reserved(2) + name(56) = 64 bytes
    for (uint32_t i = 0; i < count; i++) {
        uint32_t size = fwcfg_read_be32();
        uint16_t sel = fwcfg_read_be16();
        fwcfg_read_be16(); // skip reserved

        // read name (56 bytes)
        char fname[56];
        for (int j = 0; j < 56; j++)
            fname[j] = fwcfg_read8();

        // compare
        int match = 1;
        for (int j = 0; name[j] || fname[j]; j++) {
            if (name[j] != fname[j]) {
                match = 0;
                break;
            }
        }

        if (match) {
            print("[ramfb] found '");
            print(name);
            print("' selector=");
            print_hex(sel);
            print(" size=");
            print_hex(size);
            print("\n");
            return (int)sel;
        }
    }

    return -1;
}

// ramfb config struct (written to fw_cfg)
struct ramfb_cfg {
    uint64_t addr;    // framebuffer physical address (big-endian)
    uint32_t fourcc;  // pixel format (big-endian)
    uint32_t flags;   // must be 0 (big-endian)
    uint32_t width;   // pixels (big-endian)
    uint32_t height;  // pixels (big-endian)
    uint32_t stride;  // bytes per row (big-endian)
} __attribute__((packed));

// framebuffer state
static uint32_t *fb_addr;
static uint32_t fb_width;
static uint32_t fb_height;
static uint32_t fb_stride;

// XR24 = 32-bit XRGB (8 bits pad, 8 red, 8 green, 8 blue)
#define DRM_FORMAT_XRGB8888 0x34325258

int ramfb_init(uint32_t width, uint32_t height) {
    print("[ramfb] initializing ");
    print_hex(width);
    print("x");
    print_hex(height);
    print("\n");

    // find the ramfb config file
    int sel = fwcfg_find_file("etc/ramfb");
    if (sel < 0) {
        print("[ramfb] not found! is -device ramfb enabled?\n");
        return -1;
    }

    // allocate framebuffer memory
    uint32_t stride = width * 4; // 4 bytes per pixel (XRGB)
    uint32_t fb_size = stride * height;
    uint32_t pages_needed = (fb_size + PAGE_SIZE - 1) / PAGE_SIZE;

    // allocate contiguous pages
    // since our PMM hands out sequential pages from the bitmap,
    // allocating them in order gives us a contiguous block
    uint64_t first_page = alloc_page();
    if (!first_page) {
        print("[ramfb] out of memory\n");
        return -1;
    }
    for (uint32_t i = 1; i < pages_needed; i++) {
        uint64_t p = alloc_page();
        if (!p) {
            print("[ramfb] out of memory\n");
            return -1;
        }
    }

    fb_addr = (uint32_t *)first_page;
    fb_width = width;
    fb_height = height;
    fb_stride = stride;

    // zero the framebuffer (black screen)
    uint32_t *p = fb_addr;
    for (uint32_t i = 0; i < fb_size / 4; i++)
        p[i] = 0;

    print("[ramfb] framebuffer at ");
    print_hex(first_page);
    print(" (");
    print_hex(pages_needed);
    print(" pages)\n");

    // write the config to ramfb
    static struct ramfb_cfg cfg __attribute__((aligned(16)));
    cfg.addr = be64(first_page);
    cfg.fourcc = be32(DRM_FORMAT_XRGB8888);
    cfg.flags = 0;
    cfg.width = be32(width);
    cfg.height = be32(height);
    cfg.stride = be32(stride);

    fwcfg_dma_write((uint16_t)sel, &cfg, sizeof(cfg));

    print("[ramfb] configured\n");
    return 0;
}

// ---- drawing functions ----

void fb_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x < fb_width && y < fb_height)
        fb_addr[y * (fb_stride / 4) + x] = color;
}

void fb_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t row = y; row < y + h && row < fb_height; row++)
        for (uint32_t col = x; col < x + w && col < fb_width; col++)
            fb_addr[row * (fb_stride / 4) + col] = color;
}

void fb_clear(uint32_t color) {
    uint32_t total = (fb_stride / 4) * fb_height;
    for (uint32_t i = 0; i < total; i++)
        fb_addr[i] = color;
}

uint32_t fb_get_width(void) { return fb_width; }
uint32_t fb_get_height(void) { return fb_height; }
