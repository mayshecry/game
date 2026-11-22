#include "kernel.h"
#include "net.h"
#include "firewall.h"
#include "game.h"

void yield(void) {
    asm volatile("sti; hlt");
}

struct multiboot_info {
    uint32_t flags, mem_lower, mem_upper, boot_device, cmdline;
    uint32_t mods_count, mods_addr, num, size, addr, shndx;
    uint32_t mmap_length, mmap_addr, drives_length, drives_addr;
    uint32_t config_table, boot_loader_name, apm_table;
    uint32_t vbe_control_info, vbe_mode_info;
    uint16_t vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    uint32_t framebuffer_addr_lo, framebuffer_addr_hi, framebuffer_pitch;
    uint32_t framebuffer_width, framebuffer_height;
    uint8_t framebuffer_bpp, framebuffer_type;
} __attribute__((packed));

struct multiboot_mmap_entry {
    uint32_t size, addr_low, addr_high, len_low, len_high, type;
} __attribute__((packed));

void kmain(uint32_t magic, struct multiboot_info *mb_info) {
    if (magic != 0x2BADB002) {
        return;
    }
    asm volatile("cli");
    serial_init();
    serial_puts("[sharkos-game] boot\n");
    lfbptr = (uint32_t *)(uintptr_t)(((uint64_t)mb_info->framebuffer_addr_hi << 32) |
                                     (uint64_t)mb_info->framebuffer_addr_lo);
    if (!lfbptr) {
        lfbptr = (uint32_t *)(uintptr_t)mb_info->framebuffer_addr_lo;
    }
    screen_width = mb_info->framebuffer_width;
    screen_height = mb_info->framebuffer_height;
    screen_pitch = mb_info->framebuffer_pitch;
    if (screen_width < 320) {
        screen_width = 320;
    }
    if (screen_height < 200) {
        screen_height = 200;
    }
    uint64_t mem_kb = ((uint64_t)mb_info->mem_upper + (uint64_t)mb_info->mem_lower);
    total_system_memory = mem_kb * 1024;
    if (mb_info->flags & (1 << 6)) {
        struct multiboot_mmap_entry *mmap =
            (struct multiboot_mmap_entry *)(uintptr_t)mb_info->mmap_addr;
        uint32_t mmap_len = mb_info->mmap_length;
        uint64_t highest_addr = 0;
        uint32_t entries = mmap_len / sizeof(struct multiboot_mmap_entry);
        for (uint32_t i = 0; i < entries; i++) {
            if (mmap[i].type == 1) {
                uint64_t region_start =
                    ((uint64_t)mmap[i].addr_high << 32) | (uint64_t)mmap[i].addr_low;
                uint64_t region_len =
                    ((uint64_t)mmap[i].len_high << 32) | (uint64_t)mmap[i].len_low;
                uint64_t region_end = region_start + region_len;
                if (region_start < 137438953472ULL && region_end > highest_addr) {
                    highest_addr = region_end;
                }
            }
        }
        if (highest_addr > 0) {
            total_system_memory = highest_addr;
        }
    }
    if (total_system_memory > 137438953472ULL) {
        total_system_memory = 137438953472ULL;
    }
    pmm_init(total_system_memory);
    ui_init_metrics();
    terminal_initialize();
    init_descriptor_tables();
    outb(0x21, 0x11);
    outb(0xA1, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0x00);
    outb(0xA1, 0x00);
    outb(0x21, 0xFE);
    outb(0xA1, 0xFF);
    outb(0x21, inb(0x21) & ~0x02);
    uint16_t divisor = 1193182 / 1000;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    asm volatile("sti");
    keyboard_init();
    rtc_init();
    net_init();
    net_stack_init();
    firewall_init();
    hw_lfbptr = lfbptr;
    framebuffer_enable_write_combining((uintptr_t)hw_lfbptr,
                                       (uint32_t)(screen_pitch * screen_height));
    serial_puts("[sharkos-game] entering game\n");
    game_run();
    while (1) {
        asm volatile("cli; hlt");
    }
}
