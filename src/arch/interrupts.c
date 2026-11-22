#include "kernel.h"

void irq_handler(struct registers *r) {
    if (r->int_no >= 40) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
    if (r->int_no == 33) {
        uint8_t scancode = inb(0x60);
        keyboard_handler(scancode);
    }
    if (r->int_no == 32) {
        uptime_ticks++;
    }
    if (r->int_no >= 40) {
        outb(0x20, 0x20);
    }
}

void syscall_handler(struct registers *r) {
    (void)r;
}

void isr_handler(struct registers *r) {
    if (r->int_no == 0x80) {
        syscall_handler(r);
        return;
    }
    static const char *names[] = {"Divide by zero",
                                  "Debug",
                                  "NMI",
                                  "Breakpoint",
                                  "Overflow",
                                  "Bound range",
                                  "Invalid opcode",
                                  "No FPU",
                                  "Double fault",
                                  "Coprocessor overrun",
                                  "Invalid TSS",
                                  "Segment not present",
                                  "Stack fault",
                                  "General protection fault",
                                  "Page fault",
                                  "Reserved",
                                  "x87 FP",
                                  "Alignment check",
                                  "Machine check",
                                  "SIMD FP"};
    char buf[11];
    draw_rect(0, 0, (int)screen_width, 8 * (int)font_cell_h + 16, 0xFF800000);
    int y = 8;
    draw_string_px("SharkOS - CPU EXCEPTION, SYSTEM HALTED", 8, y, 0xFFFFFFFF, 0xFF800000);
    y += font_cell_h;
    draw_string_px("Vector: ", 8, y, 0xFFFFFF80, 0xFF800000);
    hex_to_string((uint32_t)r->int_no, buf);
    draw_string_px(buf, 8 + 8 * font_cell_w, y, 0xFFFFFFFF, 0xFF800000);
    if (r->int_no < 20) {
        draw_string_px(names[r->int_no], 8 + 20 * font_cell_w, y, 0xFFFFFFFF, 0xFF800000);
    }
    y += font_cell_h;
    draw_string_px("Error:  ", 8, y, 0xFFFFFF80, 0xFF800000);
    hex_to_string((uint32_t)r->err_code, buf);
    draw_string_px(buf, 8 + 8 * font_cell_w, y, 0xFFFFFFFF, 0xFF800000);
    y += font_cell_h;
    draw_string_px("RIP:    ", 8, y, 0xFFFFFF80, 0xFF800000);
    hex_to_string((uint32_t)r->rip, buf);
    draw_string_px(buf, 8 + 8 * font_cell_w, y, 0xFFFFFFFF, 0xFF800000);
    y += font_cell_h;
    draw_string_px("RSP:    ", 8, y, 0xFFFFFF80, 0xFF800000);
    hex_to_string((uint32_t)r->rsp, buf);
    draw_string_px(buf, 8 + 8 * font_cell_w, y, 0xFFFFFFFF, 0xFF800000);
    if (r->int_no == 14) {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        y += font_cell_h;
        draw_string_px("CR2:    ", 8, y, 0xFFFFFF80, 0xFF800000);
        hex_to_string((uint32_t)cr2, buf);
        draw_string_px(buf, 8 + 8 * font_cell_w, y, 0xFFFFFFFF, 0xFF800000);
    }
    y += font_cell_h * 2;
    draw_string_px("Power off or reset the machine.", 8, y, 0xFFFFFFFF, 0xFF800000);
    while (1) {
        asm volatile("cli; hlt");
    }
}
