#include "kernel.h"
#include "term_font16_data.h"

long inflate_raw(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len) {
    (void)in;
    (void)in_len;
    (void)out;
    (void)out_len;
    return -1;
}

long inflate_zlib(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len) {
    (void)in;
    (void)in_len;
    (void)out;
    (void)out_len;
    return -1;
}

long inflate_gzip(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len) {
    (void)in;
    (void)in_len;
    (void)out;
    (void)out_len;
    return -1;
}

static uint32_t *backbuf = 0;
static uint32_t *bgbuf = 0;
static uint32_t bufs_px = 0;
static uint32_t fb_stride_words = 0;

static void fb_stride_init(void) {
    fb_stride_words = screen_pitch / 4u;
    if (fb_stride_words == 0) {
        fb_stride_words = (uint32_t)screen_width;
    }
}

static uint32_t stride_words(void) {
    return fb_stride_words;
}

static int fb_alloc(void) {
    uint32_t need = (uint32_t)(screen_pitch * screen_height) / 4u;
    if (backbuf && bgbuf && bufs_px >= need) {
        return 1;
    }
    uint32_t *a = (uint32_t *)kmalloc((size_t)need * 4u + 64u);
    uint32_t *b = (uint32_t *)kmalloc((size_t)need * 4u + 64u);
    if (!a || !b) {
        return 0;
    }
    backbuf = a;
    bgbuf = b;
    bufs_px = need;
    for (uint32_t i = 0; i < need; i++) {
        backbuf[i] = 0;
        bgbuf[i] = 0;
    }
    return 1;
}

static void copy_words(uint32_t *dst, const uint32_t *src, uint32_t n) {
    if (n == 0) {
        return;
    }
    uint64_t *d8 = (uint64_t *)dst;
    const uint64_t *s8 = (const uint64_t *)src;
    uint32_t q = n / 2u;
    if (q) {
        asm volatile("rep movsq" : "+D"(d8), "+S"(s8), "+c"(q)::"memory");
    }
    if (n & 1u) {
        dst[n - 1] = src[n - 1];
    }
}

static void copy_rect(uint32_t *dst, const uint32_t *src, int x0, int y0, int x1, int y1) {
    int sw = (int)screen_width;
    int sh = (int)screen_height;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > sw) {
        x1 = sw;
    }
    if (y1 > sh) {
        y1 = sh;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    uint32_t st = stride_words();
    uint32_t run = (uint32_t)(x1 - x0);
    for (int y = y0; y < y1; y++) {
        uint32_t off = (uint32_t)y * st + (uint32_t)x0;
        copy_words(dst + off, src + off, run);
    }
}

uint32_t *fb_frame_begin(void) {
    if (!fb_stride_words) {
        fb_stride_init();
    }
    if (!fb_alloc()) {
        return lfbptr;
    }
    lfbptr = backbuf;
    return backbuf;
}

void fb_layer_begin(void) {
    if (!fb_alloc()) {
        return;
    }
    lfbptr = bgbuf;
}

void fb_layer_end(void) {
    if (!bgbuf) {
        return;
    }
    lfbptr = backbuf;
}

void fb_bg_to_back(void) {
    if (!bgbuf || !backbuf) {
        return;
    }
    uint32_t n = (uint32_t)(screen_pitch * screen_height) / 4u;
    copy_words(backbuf, bgbuf, n);
}

void fb_bg_restore(int x0, int y0, int x1, int y1) {
    if (!bgbuf || !backbuf) {
        return;
    }
    copy_rect(backbuf, bgbuf, x0, y0, x1, y1);
}

void fb_present_all(void) {
    if (!backbuf) {
        return;
    }
    uint32_t *hw = (uint32_t *)hw_lfbptr;
    if (!hw) {
        return;
    }
    copy_words(hw, backbuf, (uint32_t)(screen_pitch * screen_height) / 4u);
}

void fb_compose_begin(void) {
    if (!fb_alloc()) {
        return;
    }
    lfbptr = backbuf;
}

void fb_compose_end(void) {
    lfbptr = backbuf;
}

void fb_direct_begin(void) {
    if (!fb_alloc()) {
        return;
    }
    lfbptr = (uint32_t *)hw_lfbptr;
}

void fb_direct_end(void) {
    if (!backbuf) {
        return;
    }
    lfbptr = backbuf;
}

void fb_bg_restore_direct(int x0, int y0, int x1, int y1) {
    if (!bgbuf) {
        return;
    }
    copy_rect((uint32_t *)hw_lfbptr, bgbuf, x0, y0, x1, y1);
}

void fb_present_bg_all(void) {
    if (!bgbuf) {
        return;
    }
    uint32_t *hw = (uint32_t *)hw_lfbptr;
    if (!hw) {
        return;
    }
    copy_words(hw, bgbuf, (uint32_t)(screen_pitch * screen_height) / 4u);
}

void fb_present(int x0, int y0, int x1, int y1) {
    if (!backbuf) {
        return;
    }
    uint32_t *hw = (uint32_t *)hw_lfbptr;
    if (!hw) {
        return;
    }
    copy_rect(hw, backbuf, x0, y0, x1, y1);
}

uint32_t *fb_back_begin(void) {
    return fb_frame_begin();
}

void fb_back_end(void) {
    if (!backbuf) {
        return;
    }
    fb_present(0, 0, (int)screen_width, (int)screen_height);
    lfbptr = (uint32_t *)hw_lfbptr;
}

void fb_stride_ready(void) {
    fb_stride_init();
}

void ui_init_metrics(void) {
    fb_stride_init();
    uint32_t w = (uint32_t)screen_width;
    uint32_t h = (uint32_t)screen_height;
    font_scale = (w >= 1280) ? 2 : 1;
    if (font_scale < 1) {
        font_scale = 1;
    }
    if (font_scale > 3) {
        font_scale = 3;
    }
    font_cell_w = 8 * font_scale;
    font_cell_h = 16 * font_scale;
    ui_tab_y = font_cell_h + 8;
    ui_chrome_top = ui_tab_y + font_cell_h;
    ui_footer_h = font_cell_h + 4;
    ui_footer_y = h - ui_footer_h;
    term_cols = w / font_cell_w;
    if (term_cols < 20) {
        term_cols = 20;
    }
    if (term_cols > 160) {
        term_cols = 160;
    }
    term_max_row = (ui_footer_y / font_cell_h) - 1;
    if (term_max_row < 3) {
        term_max_row = 3;
    }
    content_first_row = ui_chrome_top / font_cell_h;
}

uint32_t col_px(size_t col) {
    return (uint32_t)(col * font_cell_w);
}

uint32_t row_px(size_t row) {
    return (uint32_t)(row * font_cell_h);
}

void draw_string_px(const char *s, int x, int y, uint32_t fg, uint32_t bg) {
    for (size_t i = 0; s[i] != '\0'; i++) {
        draw_char(s[i], x + (int)(i * font_cell_w), y, fg, bg);
    }
}

void draw_char(char c, int x, int y, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 126) {
        return;
    }
    uint32_t font_idx = (uint32_t)(c - 32);
    uint32_t stride = fb_stride_words;
    int scale = (int)font_scale;
    if (scale < 1) {
        scale = 1;
    }
    int sw = (int)screen_width;
    int sh = (int)screen_height;
    int cw = 8 * scale;
    int chh = 16 * scale;
    if (x >= sw || y >= sh || x + cw <= 0 || y + chh <= 0) {
        return;
    }
    const unsigned char *glyph = term_font8x16[font_idx];
    for (int row = 0; row < 16; row++) {
        uint8_t font_byte = glyph[row];
        int py0 = y + row * scale;
        if (py0 < 0 || py0 >= sh) {
            continue;
        }
        int rows = scale;
        if (py0 + rows > sh) {
            rows = sh - py0;
        }
        for (int sy = 0; sy < rows; sy++) {
            uint32_t *row_ptr = &lfbptr[(uint32_t)(py0 + sy) * stride];
            for (int col = 0; col < 8; col++) {
                uint32_t color = ((font_byte >> (7 - col)) & 1) ? fg : bg;
                int px = x + col * scale;
                if (px < 0 || px >= sw) {
                    continue;
                }
                int cols = scale;
                if (px + cols > sw) {
                    cols = sw - px;
                }
                for (int sx = 0; sx < cols; sx++) {
                    row_ptr[px + sx] = color;
                }
            }
        }
    }
}

void term_put_char_win(char c, int x, int y, uint32_t fg, uint32_t bg, int cx0, int cy0, int cx1,
                       int cy1) {
    (void)bg;
    if (c < 32 || c > 126) {
        return;
    }
    if (c == ' ') {
        return;
    }
    const unsigned char *glyph = term_font8x16[c - 32];
    uint32_t stride = fb_stride_words;
    int sw = (int)screen_width;
    int sh = (int)screen_height;
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        if (!bits) {
            continue;
        }
        int py = y + row;
        if (py < cy0 || py >= cy1 || py < 0 || py >= sh) {
            continue;
        }
        uint32_t *row_ptr = &lfbptr[(uint32_t)py * stride];
        for (int col = 0; col < 8; col++) {
            if (!((bits >> (7 - col)) & 1)) {
                continue;
            }
            int px = x + col;
            if (px < cx0 || px >= cx1 || px < 0 || px >= sw) {
                continue;
            }
            row_ptr[px] = fg;
        }
    }
}

void draw_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0) {
        return;
    }
    if ((uint32_t)x >= screen_width || (uint32_t)y >= screen_height) {
        return;
    }
    uint32_t stride = fb_stride_words;
    lfbptr[(uint32_t)y * stride + (uint32_t)x] = color;
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    int sw = (int)screen_width;
    int sh = (int)screen_height;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    if (x1 > sw) {
        x1 = sw;
    }
    int y1 = y + h;
    if (y1 > sh) {
        y1 = sh;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    uint32_t stride = fb_stride_words;
    int run = x1 - x0;
    for (int py = y0; py < y1; py++) {
        fill32(&lfbptr[(uint32_t)py * stride + (uint32_t)x0], color, (size_t)run);
    }
}

int terminal_capture_cap = 0;
bool terminal_capture_cleared = false;
uint8_t *terminal_capture_colorbuf = NULL;

void terminal_capture_begin(char *buf, uint8_t *colors, int cap) {
    terminal_capture_buffer = buf;
    terminal_capture_colorbuf = colors;
    terminal_capture_cap = cap;
    terminal_capture_len = 0;
    terminal_capture_cleared = false;
}

void terminal_capture_end(void) {
    if (terminal_capture_buffer && terminal_capture_cap > 0) {
        if (terminal_capture_len >= terminal_capture_cap) {
            terminal_capture_len = terminal_capture_cap - 1;
        }
        terminal_capture_buffer[terminal_capture_len] = '\0';
    }
    terminal_capture_buffer = NULL;
    terminal_capture_colorbuf = NULL;
    terminal_capture_cap = 0;
}

static int fb_cx = 0;
static int fb_cy = 0;

void terminal_initialize(void) {
    fb_cx = 0;
    fb_cy = 0;
}

void terminal_clear(void) {
    draw_rect(0, 0, (int)screen_width, (int)screen_height, 0xFF000000);
    fb_cx = 0;
    fb_cy = 0;
}

void terminal_putchar(char c) {
    serial_putc(c);
    if (c == '\n') {
        fb_cx = 0;
        fb_cy += (int)font_cell_h;
        return;
    }
    if (c == '\r') {
        fb_cx = 0;
        return;
    }
    if (c == '\b') {
        if (fb_cx > 0) {
            fb_cx -= (int)font_cell_w;
        }
        return;
    }
    if (c < 32 || c > 126) {
        return;
    }
    draw_char(c, fb_cx, fb_cy, 0xFFFFFFFF, 0xFF000000);
    fb_cx += (int)font_cell_w;
    if (fb_cx >= (int)screen_width - (int)font_cell_w) {
        fb_cx = 0;
        fb_cy += (int)font_cell_h;
    }
    if (fb_cy >= (int)screen_height - (int)font_cell_h) {
        fb_cy = 0;
    }
}

void terminal_writestring(const char *data) {
    while (*data) {
        terminal_putchar(*data++);
    }
}

void terminal_write_direct(const char *data) {
    terminal_writestring(data);
}

void terminal_putchar_cli(char c) {
    terminal_putchar(c);
}

void terminal_putchar_editor(char c) {
    terminal_putchar(c);
}

void terminal_write_char_internal(char c) {
    terminal_putchar(c);
}

void terminal_set_color(uint8_t color) {
    (void)color;
}

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    (void)color;
    draw_char(c, (int)col_px(x), (int)row_px(y), 0xFFFFFFFF, 0xFF000000);
}

void terminal_scroll(void) {
}

void draw_cursor(void) {
}

void terminal_draw_scrollback(void) {
}

void draw_pane_tabs(void) {
}

void split_active_pane(void) {
}

void close_active_pane(void) {
}

void redraw_all_panes(void) {
}

void print_prompt(void) {
}

void ui_draw_footer(void) {
}

void faq_open(void) {
}

void faq_close(void) {
}

void settings_open(void) {
}

void settings_close(void) {
}

void settings_draw(void) {
}

void apply_theme(int theme_idx) {
    (void)theme_idx;
}

void show_fastfetch(void) {
}

void show_welcome_tour(void) {
}

void mouse_init(void) {
}

void mouse_handler(void) {
}

void mouse_wait(void) {
}

uint8_t mouse_read(void) {
    return 0;
}

void mouse_update_cursor(void) {
}

void mouse_draw_cursor_only(void) {
}

void fs_initialize(void) {
}

void fs_cache_clear(void) {
}

void execute_command(char *cmd) {
    (void)cmd;
}

struct fs_node *find_node(struct fs_node *parent, const char *name) {
    (void)parent;
    (void)name;
    return NULL;
}

struct fs_node *search_path(const char *name) {
    (void)name;
    return NULL;
}

struct fs_node *fs_resolve_path(const char *path) {
    (void)path;
    return NULL;
}

struct fs_node *create_node(const char *name, node_type_t type, struct fs_node *parent) {
    (void)name;
    (void)type;
    (void)parent;
    return NULL;
}

const char *fs_node_data(const struct fs_node *node) {
    (void)node;
    return "";
}

int fs_node_write(struct fs_node *node, uint32_t offset, const void *data, uint32_t length) {
    (void)node;
    (void)offset;
    (void)data;
    (void)length;
    return -1;
}

int fs_node_truncate(struct fs_node *node, uint32_t length) {
    (void)node;
    (void)length;
    return -1;
}

int fs_node_remove(struct fs_node *node) {
    (void)node;
    return -1;
}

int fs_node_sync(struct fs_node *node) {
    (void)node;
    return -1;
}

int fat16_mount(int drive, struct fs_node *mountpoint) {
    (void)drive;
    (void)mountpoint;
    return -1;
}

int fat16_unmount(struct fs_node *mountpoint) {
    (void)mountpoint;
    return -1;
}

int fat16_is_mounted(struct fs_node *node) {
    (void)node;
    return 0;
}

void pmm_reserve(uintptr_t base, size_t len) {
    (void)base;
    (void)len;
}

size_t ksize(void *ptr) {
    (void)ptr;
    return 0;
}
