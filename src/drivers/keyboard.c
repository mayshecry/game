#include "kernel.h"

static char key_buffer[256];
static volatile int key_head = 0;
static volatile int key_tail = 0;
static uint8_t extended_scancode = 0;
static volatile uint8_t key_down_state[128];
static volatile uint8_t key_down_ext[128];

static bool keyboard_wait_input_empty(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 0x02)) {
            return true;
        }
    }
    return false;
}

static bool keyboard_wait_output_full(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb(0x64) & 0x01) {
            return true;
        }
    }
    return false;
}

void keyboard_init(void) {
    uint64_t eflags;
    asm volatile("pushfq; popq %0; cli" : "=r"(eflags)::"memory");
    if (inb(0x64) == 0xFF) {
        if (eflags & 0x200) {
            asm volatile("sti");
        }
        return;
    }
    if (keyboard_wait_input_empty()) {
        outb(0x64, 0xAD);
    }
    if (keyboard_wait_input_empty()) {
        outb(0x64, 0x20);
    }
    if (keyboard_wait_output_full()) {
        uint8_t command = inb(0x60);
        command = (uint8_t)((command | 0x41) & (uint8_t)~0x10);
        if (keyboard_wait_input_empty()) {
            outb(0x64, 0x60);
        }
        if (keyboard_wait_input_empty()) {
            outb(0x60, command);
        }
    }
    if (keyboard_wait_input_empty()) {
        outb(0x64, 0xAE);
    }
    for (int i = 0; i < 32 && (inb(0x64) & 0x01); i++) {
        (void)inb(0x60);
    }
    if (eflags & 0x200) {
        asm volatile("sti");
    }
}

void keyboard_handler(uint8_t scancode) {
    if (scancode == 0xE0) {
        extended_scancode = 1;
        return;
    }
    if (scancode & 0x80) {
        uint8_t base = (uint8_t)(scancode & 0x7F);
        if (extended_scancode) {
            key_down_ext[base] = 0;
        } else {
            key_down_state[base] = 0;
        }
        if (base == 0x2A || base == 0x36) {
            shift_pressed = false;
        }
        if (base == 0x1D) {
            ctrl_pressed = false;
        }
        extended_scancode = 0;
        return;
    }
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
        return;
    } else if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = false;
        return;
    } else if (scancode == 0x1D) {
        ctrl_pressed = true;
        return;
    } else if (scancode == 0x9D) {
        ctrl_pressed = false;
        return;
    }
    char c = 0;
    if (extended_scancode) {
        key_down_ext[scancode & 0x7F] = 1;
        if (scancode == 0x48) {
            c = 0x10;
        } else if (scancode == 0x50) {
            c = 0x11;
        } else if (scancode == 0x4B) {
            c = 0x12;
        } else if (scancode == 0x4D) {
            c = 0x13;
        }
        extended_scancode = 0;
    } else {
        if (scancode < 128) {
            key_down_state[scancode] = 1;
            c = shift_pressed ? keyboard_map_shifted[scancode] : keyboard_map[scancode];
        } else {
            c = 0;
        }
    }
    if (c > 0) {
        int next = (key_head + 1) % 256;
        if (next != key_tail) {
            key_buffer[key_head] = c;
            key_head = next;
        }
    }
    outb(0x20, 0x20);
}

int keyboard_down(uint8_t sc) {
    return key_down_state[sc & 0x7F];
}

int keyboard_down_ext(uint8_t sc) {
    return key_down_ext[sc & 0x7F];
}

char keyboard_getchar() {
    int head = key_head;
    int tail = key_tail;
    if (head == tail) {
        return 0;
    }
    char c = key_buffer[tail];
    key_tail = (tail + 1) % 256;
    return c;
}
