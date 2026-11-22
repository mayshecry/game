#include "kernel.h"
#include "net.h"
#include "game.h"

#define WORLD_W 2400
#define WORLD_D 400
#define GRAV 1
#define JUMP_V 13
#define MOVE_X 4
#define MOVE_Z 3
#define P2P_PORT 31337
#define MAX_PEERS 8
#define MAX_BULLETS 64
#define MAX_EBULLETS 96
#define MAX_HEARTS 8
#define MAX_PLATS 12
#define MAX_ENEMIES 4
#define CHAT_MAX 48
#define MAX_PARTICLES 192
static const int sin_tab[17] = {0,   98,  195, 290, 383, 471, 556, 634, 707,
                                773, 831, 882, 924, 957, 981, 995, 1000};

static int isin(int deg) {
    deg %= 360;
    if (deg < 0) {
        deg += 360;
    }
    int q = deg / 90;
    int idx = (deg % 90) * 16 / 90;
    int v;
    if (q == 0) {
        v = sin_tab[idx];
    } else if (q == 1) {
        v = sin_tab[16 - idx];
    } else if (q == 2) {
        v = -sin_tab[idx];
    } else {
        v = -sin_tab[16 - idx];
    }
    return v;
}

static uint32_t rng_state = 0x9E3779B9u;

static int rnd(int n) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    if (n <= 0) {
        return 0;
    }
    return (int)(rng_state % (uint32_t)n);
}

typedef struct {
    int x, z, y;
    int vx, vy, vz;
    int life, max_life;
    int size;
    uint32_t col;
    int active;
} particle_t;

static particle_t parts[MAX_PARTICLES];
static int shake_t;
static void proj(int wx, int wz, int wy, int *sx, int *sy, int *sc);
static int on_screen(int sx, int pad);
static uint32_t g_stride;

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void fast_px(int x, int y, uint32_t c);

static void spawn_particle(int x, int z, int y, int vx, int vy, int vz, int life, uint32_t col,
                           int size) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (parts[i].active) {
            continue;
        }
        parts[i].x = x;
        parts[i].z = z;
        parts[i].y = y;
        parts[i].vx = vx;
        parts[i].vy = vy;
        parts[i].vz = vz;
        parts[i].life = life;
        parts[i].max_life = life;
        parts[i].col = col;
        parts[i].size = size;
        parts[i].active = 1;
        return;
    }
}

static void burst(int x, int z, int y, int n, uint32_t col, int spread, int up) {
    for (int i = 0; i < n; i++) {
        spawn_particle(x, z, y, rnd(spread * 2 + 1) - spread, up + rnd(up / 2 + 1), rnd(4) - 2,
                       16 + rnd(14), col, 1 + rnd(2));
    }
}

static void update_particles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &parts[i];
        if (!p->active) {
            continue;
        }
        p->x += p->vx;
        p->y += p->vy;
        p->z += p->vz;
        p->vy -= 1;
        if (p->y < -20) {
            p->active = 0;
            continue;
        }
        if (--p->life <= 0) {
            p->active = 0;
        }
    }
}

static void draw_particles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &parts[i];
        if (!p->active) {
            continue;
        }
        int sx, sy, sc;
        proj(p->x, p->z, p->y, &sx, &sy, &sc);
        if (!on_screen(sx, 20)) {
            continue;
        }
        uint32_t c = p->col;
        int fade = p->life * 100 / (p->max_life > 0 ? p->max_life : 1);
        if (fade < 35) {
            c = 0xFF202040;
        } else if (fade < 70) {
            uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
            c = 0xFF000000u | ((r / 2) << 16) | ((g / 2) << 8) | (b / 2);
        }
        int sz = p->size * sc;
        if (sz < 1) {
            sz = 1;
        }
        draw_rect(sx - sz, sy - sz, sz * 2, sz * 2, c);
    }
}

typedef struct {
    int x;
    int z;
    int w;
    int d;
    int h;
    uint32_t top;
    uint32_t front;
    uint32_t side;
} plat_t;

typedef struct {
    int x;
    int z;
    int y;
    int vx;
    int vz;
    int vy;
    int facing;
    int on_ground;
    int hp;
    int max_hp;
    int anim;
    int hurt_t;
    int dead;
    int shoot_cd;
} player_t;

typedef struct {
    int x;
    int z;
    int y;
    int vx;
    int vy;
    int vz;
    int active;
    int friendly;
} bullet_t;

typedef struct {
    int x;
    int z;
    int y;
    int active;
    int bob;
} heart_t;

typedef struct {
    int x;
    int z;
    int y;
    int hp;
    int alive;
    int patrol_a;
    int patrol_b;
    int dir;
    int shoot_t;
    int anim;
} enemy_t;

typedef struct {
    uint8_t ip[4];
    uint32_t id;
    int x;
    int z;
    int y;
    int facing;
    int hp;
    int anim;
    uint32_t last_seen;
    int active;
    char chat[CHAT_MAX];
    uint32_t chat_t;
} peer_t;

static plat_t plats[MAX_PLATS];
static int plat_count;
static player_t pl;
static bullet_t bullets[MAX_BULLETS];
static bullet_t ebullets[MAX_EBULLETS];
static heart_t hearts[MAX_HEARTS];
static enemy_t enemies[MAX_ENEMIES];
static peer_t peers[MAX_PEERS];
static int peer_count;
static uint8_t manual_ip[4];
static int manual_set;
static int state;
static int hearts_got;
static int kills;
static uint32_t frame;
static uint32_t anim_ms;
static uint32_t last_net_send;
static uint32_t last_hello;
static int last_sent_x, last_sent_z, last_sent_y;
static uint32_t last_fps_t;
static int fps;
static int fps_count;
static int dlg_active;
static int dlg_idx;
static int dlg_char;
static uint32_t dlg_t;
static int chat_mode;
static char chat_buf[CHAT_MAX];
static int chat_len;
static int ip_mode;
static char ip_buf[32];
static int ip_len;
static uint32_t player_id;
static int key_left;
static int key_right;
static int key_up;
static int key_down;
static int key_jump;
static int key_talk;
static int key_attack;
static int edge_space;
static int edge_shoot;
static int edge_talk;
static const char *dlg_lines[] = {
    "* HEY. YOU LOOK LIKE YOU COULD USE A HAND.", "* THE SHARKS ARE COMING. STAY DETERMINED.",
    "* PRESS X TO SHOOT. SPACE TO JUMP.", "* COLLECT 5 HEARTS TO WIN. GOOD LUCK, KID."};
#define DLG_COUNT 4
static int cam_x;
static int horizon_y;

static void pc_beep(int freq, int ms) {
    if (freq <= 0) {
        return;
    }
    uint16_t div = (uint16_t)(1193182 / freq);
    outb(0x43, 0xB6);
    outb(0x42, div & 0xFF);
    outb(0x42, (div >> 8) & 0xFF);
    outb(0x61, inb(0x61) | 0x03);
    uint32_t t0 = uptime_ticks;
    while ((int32_t)(uptime_ticks - t0) < ms) {
        asm volatile("hlt");
    }
    outb(0x61, inb(0x61) & (uint8_t)~0x03);
}

static int clampi(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static int absi(int v) {
    return v < 0 ? -v : v;
}

static void draw_line(int x0, int y0, int x1, int y1, uint32_t col) {
    int dx = absi(x1 - x0);
    int dy = -absi(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int x = x0;
    int y = y0;
    for (;;) {
        fast_px(x, y, col);
        if (x == x1 && y == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

static inline void fast_px(int x, int y, uint32_t c) {
    if ((uint32_t)x >= screen_width || (uint32_t)y >= screen_height) {
        return;
    }
    lfbptr[(uint32_t)y * g_stride + (uint32_t)x] = c;
}

static void draw_circle(int cx, int cy, int r, uint32_t col) {
    int x = r;
    int y = 0;
    int err = 1 - r;
    while (x >= y) {
        for (int yy = cy - x; yy <= cy + x; yy++) {
            fast_px(cx + y, yy, col);
            fast_px(cx - y, yy, col);
        }
        for (int yy = cy - y; yy <= cy + y; yy++) {
            fast_px(cx + x, yy, col);
            fast_px(cx - x, yy, col);
        }
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

static void draw_circle_outline(int cx, int cy, int r, uint32_t col) {
    int x = r;
    int y = 0;
    int err = 1 - r;
    while (x >= y) {
        fast_px(cx + x, cy + y, col);
        fast_px(cx - x, cy + y, col);
        fast_px(cx + x, cy - y, col);
        fast_px(cx - x, cy - y, col);
        fast_px(cx + y, cy + x, col);
        fast_px(cx - y, cy + x, col);
        fast_px(cx + y, cy - x, col);
        fast_px(cx - y, cy - x, col);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

static void draw_heart(int cx, int cy, int s, uint32_t col) {
    fast_px(cx - s, cy - s, col);
    fast_px(cx + s, cy - s, col);
    fast_px(cx - s, cy - s + 1, col);
    fast_px(cx + s, cy - s + 1, col);
    for (int y = -s + 2; y <= s; y++) {
        int w = s - (y > 0 ? (y * y / (s + 1)) : 0);
        if (w < 1) {
            w = 1;
        }
        int x0 = -w;
        int x1 = w;
        if (y < 0 && x0 == -w) {
            x0 = -w + 1;
        }
        if (y < 0 && x1 == w) {
            x1 = w - 1;
        }
        int yy = cy + y;
        if (yy < 0 || yy >= (int)screen_height) {
            continue;
        }
        int startx = cx + x0;
        int endx = cx + x1;
        if (startx < 0) {
            startx = 0;
        }
        if (endx > (int)screen_width - 1) {
            endx = (int)screen_width - 1;
        }
        int run = endx - startx + 1;
        if (run < 1) {
            continue;
        }
        fill32(&lfbptr[(uint32_t)yy * g_stride + (uint32_t)startx], col, (size_t)run);
    }
    fast_px(cx, cy + s, col);
}

static void draw_text(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    draw_string_px(s, x, y, fg, bg);
}

static void draw_text_center(int cx, int y, const char *s, uint32_t fg, uint32_t bg) {
    int len = 0;
    while (s[len]) {
        len++;
    }
    int w = len * (int)font_cell_w;
    draw_string_px(s, cx - w / 2, y, fg, bg);
}

static int text_w(const char *s) {
    int len = 0;
    while (s[len]) {
        len++;
    }
    return len * (int)font_cell_w;
}

static int zscale(int z) {
    if (z < 0) {
        z = 0;
    }
    if (z > 460) {
        z = 460;
    }
    return 1000 + (z * 1000) / 256;
}

static int ground_py(int z) {
    int sh = (int)screen_height;
    if (z < 0) {
        z = 0;
    }
    return horizon_y + (sh - horizon_y) * z / (z + 150);
}

static void proj(int wx, int wz, int wy, int *sx, int *sy, int *sc) {
    int zs = zscale(wz);
    int px = (int)screen_width / 2 + ((wx - cam_x) * zs) / 1000;
    int py = ground_py(wz) - (wy * zs) / 1000;
    int s = zs / 1000;
    if (s < 1) {
        s = 1;
    }
    if (shake_t > 0) {
        px += isin((int)anim_ms * 7) * shake_t / 160;
        py += isin((int)anim_ms * 5 + 90) * shake_t / 160;
    }
    *sx = px;
    *sy = py;
    *sc = s;
}

static void draw_stickman_screen(int sx, int sy, int facing, int anim, uint32_t col, int s,
                                 int airborne) {
    if (s < 1) {
        s = 1;
    }
    int head_r = 6 * s;
    int bob = 0;
    int walk = anim * 22;
    if (!airborne) {
        bob = isin(walk) * s / 1000;
    }
    int hy = sy - 30 * s + bob;
    int head_cy = hy + head_r;
    int neck_y = head_cy + head_r + s;
    int hip_y = neck_y + 12 * s;
    int f = facing >= 0 ? 1 : -1;
    int swing = airborne ? 5 * s : isin(walk) * 7 * s / 1000;
    int arm = airborne ? 9 * s : isin(walk + 180) * 7 * s / 1000;
    draw_circle_outline(sx, head_cy, head_r, col);
    draw_pixel(sx + f * (head_r - s), head_cy - s, col);
    draw_pixel(sx + f * (head_r - s), head_cy, col);
    draw_line(sx, neck_y, sx, hip_y, col);
    int sh_y = neck_y + 2 * s;
    draw_line(sx, sh_y, sx + f * 4 * s, sh_y + 3 * s, col);
    draw_line(sx + f * 4 * s, sh_y + 3 * s, sx + f * 3 * s + arm, sh_y + 6 * s, col);
    draw_line(sx, sh_y, sx - f * 3 * s, sh_y + 3 * s, col);
    draw_line(sx - f * 3 * s, sh_y + 3 * s, sx - f * 4 * s - arm / 2, sh_y + 7 * s, col);
    int knee_f_x = sx + swing + f * s;
    int knee_f_y = airborne ? hip_y + 3 * s : hip_y + 5 * s;
    int foot_f_x = sx + swing * 2 + f * 2 * s;
    int foot_f_y = airborne ? hip_y + 4 * s : sy + bob;
    int knee_b_x = sx - swing - f * s;
    int knee_b_y = airborne ? hip_y + 2 * s : hip_y + 5 * s;
    int foot_b_x = sx - swing * 2 - f * 2 * s;
    int foot_b_y = airborne ? hip_y + 6 * s : sy + bob;
    draw_line(sx, hip_y, knee_b_x, knee_b_y, col);
    draw_line(knee_b_x, knee_b_y, foot_b_x, foot_b_y, col);
    draw_line(sx, hip_y, knee_f_x, knee_f_y, col);
    draw_line(knee_f_x, knee_f_y, foot_f_x, foot_f_y, col);
    draw_line(foot_b_x - 2 * s, foot_b_y + s, foot_b_x + 2 * s, foot_b_y + s, 0xFF333344);
    draw_line(foot_f_x - 2 * s, foot_f_y + s, foot_f_x + 2 * s, foot_f_y + s, 0xFF333344);
}

static void draw_stickman(int wx, int wz, int wy, int facing, int anim, uint32_t col, int scale_mul,
                          int airborne) {
    int sx, sy, sc;
    proj(wx, wz, wy, &sx, &sy, &sc);
    int ss = sc + 1;
    if (ss > 3) {
        ss = 3;
    }
    draw_stickman_screen(sx, sy, facing, anim, col, ss * scale_mul, airborne);
}

static void draw_platform(plat_t *p) {
    int thick = 42;
    int shade = 1;
    for (int k = 0; k < 8; k++) {
        int z0 = p->z + (p->d * k) / 8;
        int z1 = p->z + (p->d * (k + 1)) / 8;
        int x0, y0, s0, x1, y1, s1;
        proj(p->x, z0, p->h, &x0, &y0, &s0);
        proj(p->x + p->w, z0, p->h, &x1, &y1, &s1);
        int top_y = y0;
        int bot_y;
        int bx, by, bs;
        proj(p->x, z1, p->h, &bx, &by, &bs);
        bot_y = by;
        if (bot_y < top_y) {
            bot_y = top_y;
        }
        uint32_t c = p->top;
        if (k < 3) {
            uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
            c = 0xFF000000u | ((r + 18 > 255 ? 255 : r + 18) << 16) |
                ((g + 18 > 255 ? 255 : g + 18) << 8) | (b + 20 > 255 ? 255 : b + 20);
        }
        draw_rect(x0, top_y, x1 - x0, bot_y - top_y + 1, c);
        if (k == 0) {
            draw_line(x0, top_y, x1, top_y, 0xFF9DB6D4);
        } else if (k == 7) {
            draw_line(x0, bot_y, x1, bot_y, 0xFF6A88AC);
        }
    }
    int fx0, fy0, fs0, fx1, fy1, fs1, fy2, fs2;
    proj(p->x, p->z, p->h, &fx0, &fy0, &fs0);
    proj(p->x + p->w, p->z, p->h, &fx1, &fy1, &fs1);
    proj(p->x, p->z, p->h - thick, &fx0, &fy2, &fs2);
    int fh = fy2 - fy0;
    if (fh < 2) {
        fh = 2;
    }
    uint32_t fr = (p->front >> 16) & 0xFF, fg = (p->front >> 8) & 0xFF, fb = p->front & 0xFF;
    uint32_t lo = 0xFF000000u | ((fr / 2) << 16) | ((fg / 2) << 8) | (fb / 2);
    draw_rect(fx0, fy0, fx1 - fx0, fh, p->front);
    draw_rect(fx0, fy0 + fh - fh / 3, fx1 - fx0, fh / 3, lo);
    draw_rect(fx0, fy0, 2, fh, p->side);
    draw_rect(fx1 - 2, fy0, 2, fh, lo);
    int sx0, sy0, ss0, sx1, sy1, ss1;
    proj(p->x + p->w, p->z, p->h, &sx0, &sy0, &ss0);
    proj(p->x + p->w, p->z + p->d, p->h, &sx1, &sy1, &ss1);
    draw_line(fx1, fy0, sx1, sy1, 0xFF8FA8C8);

    (void)shade;
}

static void draw_background(void) {
    int sw = (int)screen_width;
    int sh = (int)screen_height;
    for (int y = 0; y < horizon_y; y += 3) {
        int r, g, b;
        if (y * 100 / (horizon_y > 0 ? horizon_y : 1) < 55) {
            int t = y * 100 / (horizon_y * 55 / 100 + 1);
            r = 2 + t * 8 / 100;
            g = 4 + t * 18 / 100;
            b = 14 + t * 44 / 100;
        } else {
            int t = (y - horizon_y * 55 / 100) * 100 / (horizon_y * 45 / 100 + 1);
            r = 10 + t * 12 / 100;
            g = 22 + t * 14 / 100;
            b = 58 - t * 14 / 100;
        }
        uint32_t col = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        draw_rect(0, y, sw, 3, col);
    }
    for (int i = 0; i < 110; i++) {
        int sx = (i * 137 - (int)(cam_x / 12)) % (sw + 40);
        if (sx < -20) {
            sx += sw + 40;
        }
        int sy = (i * 89) % (horizon_y - 10);
        int tw = (i * 31 + (int)(anim_ms / 90)) % 11;
        uint32_t sc2 = 0xFF556688;
        if (tw == 0) {
            sc2 = 0xFFFFFFFF;
        } else if (tw < 3) {
            sc2 = 0xFFAABBCC;
        } else if (tw < 6) {
            sc2 = 0xFF7788AA;
        }
        draw_pixel(sx, sy, sc2);
        if (tw == 0) {
            draw_pixel(sx + 1, sy, 0xFF445566);
            draw_pixel(sx, sy + 1, 0xFF445566);
        }
    }
    int shx = (int)(((anim_ms / 2) * 7) % (uint32_t)(sw * 2)) - sw / 2;
    int shy = 40 + (int)((anim_ms / 20) % 90);
    for (int k = 0; k < 7; k++) {
        uint32_t c = 0xFFBBCCEE;
        if (k > 3) {
            c = 0xFF7788AA;
        }
        draw_pixel(shx - k * 6 - (int)cam_x / 40, shy + k * 2, c);
    }
    int mx = sw - 120 - (int)((anim_ms / 300) % 16);
    int my = 60;
    draw_circle(mx, my, 34, 0xFF0E1A3A);
    draw_circle(mx, my, 27, 0xFF16264A);
    draw_circle(mx, my, 22, 0xFFF0F0D0);
    draw_circle(mx - 6, my - 4, 5, 0xFFD8D8B8);
    draw_circle(mx + 7, my + 6, 4, 0xFFD8D8B8);
    draw_circle(mx + 2, my - 9, 3, 0xFFE8E8C8);
    for (int i = 0; i < 9; i++) {
        int hx = (i * 210 - (int)(cam_x / 8) % 210 + sw) % (sw + 160) - 80;
        int hh = 24 + (i * 29) % 52;
        int hw = 70 + (i * 17) % 46;
        draw_rect(hx, horizon_y - hh, hw, hh, 0xFF0A1226);
        draw_rect(hx + 6, horizon_y - hh - 7, hw - 18, 7, 0xFF0C1630);
        for (int wy2 = horizon_y - hh + 7; wy2 < horizon_y - 4; wy2 += 11) {
            for (int wx2 = hx + 7; wx2 < hx + hw - 9; wx2 += 13) {
                if (((wx2 + wy2 + i * 7) % 5) == 0) {
                    draw_rect(wx2, wy2, 3, 4, 0xFF1E3050);
                }
            }
        }
    }
    for (int i = 0; i < 6; i++) {
        int hx = (i * 330 - (int)(cam_x / 4) % 330 + sw) % (sw + 200) - 100;
        int hh = 48 + (i * 41) % 64;
        int hw = 110 + (i * 23) % 60;
        draw_rect(hx, horizon_y - hh, hw, hh, 0xFF060B1A);
        draw_rect(hx + 10, horizon_y - hh - 10, hw - 34, 10, 0xFF080F20);
        for (int wy2 = horizon_y - hh + 9; wy2 < horizon_y - 6; wy2 += 13) {
            for (int wx2 = hx + 9; wx2 < hx + hw - 11; wx2 += 17) {
                if (((wx2 * 3 + wy2 + i * 11) % 6) == 0) {
                    draw_rect(wx2, wy2, 3, 5, 0xFF16264A);
                }
            }
        }
    }
    int z;
    for (z = 0; z < 2400; z += 25) {
        int y0 = ground_py(z);
        int y1 = ground_py(z + 26);
        if (y0 >= sh) {
            break;
        }
        if (y1 <= y0) {
            y1 = y0 + 1;
        }
        int t = z * 100 / 600;
        if (t > 100) {
            t = 100;
        }
        uint32_t gcol = 0xFF000000u | ((uint32_t)(16 + t / 6) << 16) |
                        ((uint32_t)(26 + t / 5) << 8) | (uint32_t)(40 + t / 2);
        draw_rect(0, y0, sw, y1 - y0, gcol);
    }
    for (int i = -14; i <= 14; i++) {
        int wx = i * 150;
        int zf = 0, zn = 380;
        int s_f = zscale(zf), s_n = zscale(zn);
        int xf = sw / 2 + ((wx - cam_x) * s_f) / 1000;
        int xn = sw / 2 + ((wx - cam_x) * s_n) / 1000;
        if ((xf < -400 && xn < -400) || (xf > sw + 400 && xn > sw + 400)) {
            continue;
        }
        draw_line(xf, ground_py(zf), xn, ground_py(zn), 0xFF16304A);
    }
    for (int i = 0; i < 90; i++) {
        int wx = (i * 271) % 3600 - 200;
        int zz = 30 + (i * 53) % 330;
        int gx, gy, gs;
        proj(wx, zz, 0, &gx, &gy, &gs);
        if (gx < -20 || gx > sw + 20) {
            continue;
        }
        uint32_t dc = (i % 3 == 0) ? 0xFF1C3048 : 0xFF12203A;
        int dw = 2 + (i % 3);
        draw_rect(gx, gy, dw * gs, gs, dc);
    }
    for (int i = 0; i < 26; i++) {
        int wx = (i * 419) % 3600 - 300;
        int zz = 60 + (i * 71) % 300;
        int gx, gy, gs, gx2, gy2, gs2;
        proj(wx, zz, 0, &gx, &gy, &gs);
        proj(wx + 26, zz, 0, &gx2, &gy2, &gs2);
        if (gx < -60 || gx > sw + 60) {
            continue;
        }
        draw_rect(gx, gy - 2, gx2 - gx, 3, 0xFF24384F);
        draw_rect(gx, gy - 2, gx2 - gx, 1, 0xFF3A5570);
    }
    int fill_y = ground_py(2000);
    if (fill_y < sh) {
        uint32_t deep = 0xFF000000u | (35u << 16) | (58u << 8) | 78u;
        draw_rect(0, fill_y, sw, sh - fill_y, deep);
    }
    for (z = 40; z < 600; z += 40) {
        int gy = ground_py(z);
        int t = z * 100 / 400;
        uint32_t lc = 0xFF000000u | ((uint32_t)(20 + t / 5) << 16) | ((uint32_t)(34 + t / 4) << 8) |
                      (uint32_t)(52 + t / 2);
        draw_rect(0, gy, sw, 1, lc);
    }
}

static void draw_hud(void) {
    int sw = (int)screen_width;
    draw_rect(0, 0, sw, 28, 0xFF000000);
    draw_rect(0, 28, sw, 1, 0xFFFFFFFF);
    draw_text(8, 6, "HP", 0xFFFF0000, 0xFF000000);
    for (int i = 0; i < pl.max_hp / 5; i++) {
        uint32_t c = (i * 5 < pl.hp) ? 0xFFFF0000 : 0xFF550000;
        draw_heart(40 + i * 20, 14, 5, c);
    }
    char buf[32];
    char nb[8];
    int p = 0;
    const char *s = "HEARTS ";
    for (int i = 0; s[i]; i++) {
        buf[p++] = s[i];
    }
    buf[p++] = (char)('0' + hearts_got);
    buf[p] = 0;
    draw_text(8 + 6 * 20, 6, buf, 0xFFFF00FF, 0xFF000000);
    p = 0;
    s = "KILLS ";
    for (int i = 0; s[i]; i++) {
        buf[p++] = s[i];
    }
    buf[p++] = (char)('0' + kills);
    buf[p] = 0;
    draw_text(200, 6, buf, 0xFF00FFFF, 0xFF000000);
    p = 0;
    s = "PEERS ";
    for (int i = 0; s[i]; i++) {
        buf[p++] = s[i];
    }
    buf[p++] = (char)('0' + peer_count);
    buf[p] = 0;
    draw_text(300, 6, buf, 0xFF00FF00, 0xFF000000);
    if (net_has_nic() && net_configured) {
        p = 0;
        s = "IP ";
        for (int i = 0; s[i]; i++) {
            buf[p++] = s[i];
        }
        int_to_string(net_ip[0], nb);
        for (int i = 0; nb[i]; i++) {
            buf[p++] = nb[i];
        }
        buf[p++] = '.';
        int_to_string(net_ip[1], nb);
        for (int i = 0; nb[i]; i++) {
            buf[p++] = nb[i];
        }
        buf[p++] = '.';
        int_to_string(net_ip[2], nb);
        for (int i = 0; nb[i]; i++) {
            buf[p++] = nb[i];
        }
        buf[p++] = '.';
        int_to_string(net_ip[3], nb);
        for (int i = 0; nb[i]; i++) {
            buf[p++] = nb[i];
        }
        buf[p] = 0;
        draw_text(400, 6, buf, 0xFFFFFFFF, 0xFF000000);
    } else {
        draw_text(400, 6, "NET: OFFLINE/SOLO", 0xFF888888, 0xFF000000);
    }
    p = 0;
    s = "FPS ";
    for (int i = 0; s[i]; i++) {
        buf[p++] = s[i];
    }
    int_to_string((uint32_t)fps, nb);
    for (int i = 0; nb[i]; i++) {
        buf[p++] = nb[i];
    }
    buf[p] = 0;
    draw_text(sw - text_w(buf) - 8, 6, buf, 0xFFAAAAAA, 0xFF000000);
    if (manual_set) {
        draw_text(sw - 200, 20, "MANUAL IP SET", 0xFF00FFFF, 0xFF000000);
    }
}

static void draw_dialogue(void) {
    int sw = (int)screen_width;
    int sh = (int)screen_height;
    int bw = sw - 40;
    int bh = 90;
    int bx = 20;
    int by = sh - bh - 12;
    draw_rect(bx, by, bw, bh, 0xFF000000);
    draw_rect(bx, by, bw, 2, 0xFFFFFFFF);
    draw_rect(bx, by + bh - 2, bw, 2, 0xFFFFFFFF);
    draw_rect(bx, by, 2, bh, 0xFFFFFFFF);
    draw_rect(bx + bw - 2, by, 2, bh, 0xFFFFFFFF);
    const char *line = dlg_lines[dlg_idx];
    int n = dlg_char;
    if (n > (int)strlen(line)) {
        n = (int)strlen(line);
    }
    char tmp[128];
    for (int i = 0; i < n; i++) {
        tmp[i] = line[i];
    }
    tmp[n] = 0;
    draw_text(bx + 12, by + 12, tmp, 0xFFFFFFFF, 0xFF000000);
    if (dlg_char >= (int)strlen(line)) {
        draw_text(bx + 12, by + bh - 24, "Z: NEXT  X: CLOSE", 0xFFAAAAAA, 0xFF000000);
    }
}

static void draw_chat(void) {
    int sw = (int)screen_width;
    int sh = (int)screen_height;
    int bw = sw - 40;
    int bh = 40;
    int bx = 20;
    int by = sh - bh - 12;
    draw_rect(bx, by, bw, bh, 0xFF000000);
    draw_rect(bx, by, bw, 2, 0xFF00FFFF);
    draw_rect(bx, by + bh - 2, bw, 2, 0xFF00FFFF);
    draw_rect(bx, by, 2, bh, 0xFF00FFFF);
    draw_rect(bx + bw - 2, by, 2, bh, 0xFF00FFFF);
    draw_text(bx + 12, by + 12, "CHAT: ", 0xFF00FFFF, 0xFF000000);
    char tmp[64];
    for (int i = 0; i < chat_len; i++) {
        tmp[i] = chat_buf[i];
    }
    tmp[chat_len] = 0;
    draw_text(bx + 12 + 6 * (int)font_cell_w, by + 12, tmp, 0xFFFFFFFF, 0xFF000000);
}

static void draw_title(void) {
    int sw = (int)screen_width;
    int sh = (int)screen_height;
    int saved_horizon = horizon_y;
    horizon_y = sh * 42 / 100;
    draw_background();
    horizon_y = saved_horizon;
    draw_rect(0, 0, sw, 60, 0xFF000000);
    draw_rect(0, 60, sw, 2, 0xFF00FFFF);
    draw_text_center(sw / 2, 10, "SHARKOS GAME", 0xFF00FFFF, 0xFF000000);
    draw_text_center(sw / 2, 30, "A STICKMAN PLATFORMER ON THE SHARKOS KERNEL", 0xFFFFFFFF,
                     0xFF000000);
    draw_text_center(sw / 2, 90, "SEMI-3D  |  UNDERTALE-STYLE BULLET HELL  |  P2P MULTIPLAYER",
                     0xFFAAAAAA, 0xFF000000);
    draw_text_center(sw / 2, 120,
                     "MOVE: WASD / ARROWS   JUMP: SPACE   TALK: Z   SHOOT: X   CHAT: T", 0xFFFFFFFF,
                     0xFF000000);
    draw_text_center(sw / 2, 150, "COLLECT 5 HEARTS TO WIN. DODGE THE BULLETS. STAY DETERMINED.",
                     0xFFFF00FF, 0xFF000000);
    int y = 200;
    int ph = 96;
    draw_rect(sw / 2 - 230, y - 14, 460, ph, 0xFF000814);
    draw_rect(sw / 2 - 230, y - 14, 460, 1, 0xFF1E4670);
    draw_rect(sw / 2 - 230, y - 14 + ph, 460, 1, 0xFF1E4670);
    draw_rect(sw / 2 - 230, y - 14, 1, ph, 0xFF1E4670);
    draw_rect(sw / 2 + 229, y - 14, 1, ph, 0xFF1E4670);
    draw_text_center(sw / 2, y, "ENTER: START (P2P AUTO)", 0xFF00FF00, 0xFF000814);
    y += 24;
    draw_text_center(sw / 2, y, "I: TYPE PEER IP FOR DIRECT P2P", 0xFF00FFFF, 0xFF000814);
    y += 24;
    if (net_has_nic()) {
        if (net_configured) {
            char buf[64];
            int p = 0;
            const char *s = "NET READY  IP ";
            for (int i = 0; s[i]; i++) {
                buf[p++] = s[i];
            }
            char nb[8];
            int_to_string(net_ip[0], nb);
            for (int i = 0; nb[i]; i++) {
                buf[p++] = nb[i];
            }
            buf[p++] = '.';
            int_to_string(net_ip[1], nb);
            for (int i = 0; nb[i]; i++) {
                buf[p++] = nb[i];
            }
            buf[p++] = '.';
            int_to_string(net_ip[2], nb);
            for (int i = 0; nb[i]; i++) {
                buf[p++] = nb[i];
            }
            buf[p++] = '.';
            int_to_string(net_ip[3], nb);
            for (int i = 0; nb[i]; i++) {
                buf[p++] = nb[i];
            }
            buf[p] = 0;
            draw_text_center(sw / 2, y, buf, 0xFF00FF00, 0xFF000814);
        } else {
            draw_text_center(sw / 2, y, "NET: WAITING FOR DHCP...", 0xFFFFFF00, 0xFF000000);
        }
    } else {
        draw_text_center(sw / 2, y, "NET: NO NIC - SOLO MODE", 0xFF888888, 0xFF000000);
    }
    y += 24;
    if (manual_set) {
        char buf[40];
        int p = 0;
        const char *s = "PEER IP: ";
        for (int i = 0; s[i]; i++) {
            buf[p++] = s[i];
        }
        char nb[8];
        int_to_string(manual_ip[0], nb);
        for (int i = 0; nb[i]; i++) {
            buf[p++] = nb[i];
        }
        buf[p++] = '.';
        int_to_string(manual_ip[1], nb);
        for (int i = 0; nb[i]; i++) {
            buf[p++] = nb[i];
        }
        buf[p++] = '.';
        int_to_string(manual_ip[2], nb);
        for (int i = 0; nb[i]; i++) {
            buf[p++] = nb[i];
        }
        buf[p++] = '.';
        int_to_string(manual_ip[3], nb);
        for (int i = 0; nb[i]; i++) {
            buf[p++] = nb[i];
        }
        buf[p] = 0;
        draw_text_center(sw / 2, y, buf, 0xFF00FFFF, 0xFF000000);
    }
    y += 24;
    if (ip_mode) {
        char tmp[40];
        tmp[0] = '>';
        tmp[1] = ' ';
        for (int i = 0; i < ip_len; i++) {
            tmp[2 + i] = ip_buf[i];
        }
        tmp[2 + ip_len] = 0;
        draw_text_center(sw / 2, y, tmp, 0xFFFFFFFF, 0xFF000000);
        draw_text_center(sw / 2, y + 20, "TYPE IP THEN ENTER", 0xFFAAAAAA, 0xFF000000);
    }
    y += 40;
    draw_text_center(sw / 2, y, "BY mayshecry  |  SHARKOS KERNEL  |  STICKMEN EDITION", 0xFFFF00FF,
                     0xFF000000);
    draw_text_center(sw / 2, y + 20, "P2P OVER SHARKOS UDP  PORT 31337", 0xFFAAAAAA, 0xFF000000);
    draw_stickman_screen(sw / 2 - 120, sh - 30, 1, (int)(anim_ms / 55), 0xFFFFFFFF, 3, 0);
    draw_stickman_screen(sw / 2 + 120, sh - 30, -1, (int)(anim_ms / 55 + 12), 0xFF00FFFF, 3, 0);
    (void)sh;
}

static void draw_gameover(void) {
    int sw = (int)screen_width;
    int sh = (int)screen_height;
    draw_background();
    draw_rect(0, 0, sw, sh, 0xAA000000);
    draw_text_center(sw / 2, sh / 2 - 60, "GAME OVER", 0xFFFF0000, 0xFF000000);
    char buf[48];
    int p = 0;
    const char *s = "HEARTS: ";
    for (int i = 0; s[i]; i++) {
        buf[p++] = s[i];
    }
    buf[p++] = (char)('0' + hearts_got);
    buf[p] = 0;
    draw_text_center(sw / 2, sh / 2 - 20, buf, 0xFFFFFFFF, 0xFF000000);
    p = 0;
    s = "KILLS: ";
    for (int i = 0; s[i]; i++) {
        buf[p++] = s[i];
    }
    buf[p++] = (char)('0' + kills);
    buf[p] = 0;
    draw_text_center(sw / 2, sh / 2, buf, 0xFFFFFFFF, 0xFF000000);
    draw_text_center(sw / 2, sh / 2 + 30, "ENTER: RETRY   ESC: MENU", 0xFFAAAAAA, 0xFF000000);
    draw_text_center(sw / 2, sh / 2 + 60, "BY mayshecry", 0xFFFF00FF, 0xFF000000);
}

static void draw_win(void) {
    int sw = (int)screen_width;
    int sh = (int)screen_height;
    draw_background();
    draw_rect(0, 0, sw, sh, 0xAA000000);
    draw_text_center(sw / 2, sh / 2 - 60, "YOU WIN!", 0xFF00FF00, 0xFF000000);
    draw_text_center(sw / 2, sh / 2 - 20, "YOU STAYED DETERMINED.", 0xFFFFFFFF, 0xFF000000);
    draw_text_center(sw / 2, sh / 2 + 10, "ENTER: PLAY AGAIN   ESC: MENU", 0xFFAAAAAA, 0xFF000000);
    draw_text_center(sw / 2, sh / 2 + 40, "BY mayshecry", 0xFFFF00FF, 0xFF000000);
}

static void init_world(void) {
    plat_count = 0;
    plats[plat_count++] = (plat_t){180, 150, 240, 90, 70, 0xFF3A5A7A, 0xFF16283C, 0xFF28425C};
    plats[plat_count++] = (plat_t){520, 240, 220, 80, 120, 0xFF3A5A7A, 0xFF16283C, 0xFF28425C};
    plats[plat_count++] = (plat_t){830, 170, 260, 90, 70, 0xFF40607E, 0xFF182A3E, 0xFF2A445E};
    plats[plat_count++] = (plat_t){1180, 250, 230, 80, 140, 0xFF3A5A7A, 0xFF16283C, 0xFF28425C};
    plats[plat_count++] = (plat_t){1480, 180, 250, 90, 80, 0xFF40607E, 0xFF182A3E, 0xFF2A445E};
    plats[plat_count++] = (plat_t){1810, 250, 220, 80, 150, 0xFF3A5A7A, 0xFF16283C, 0xFF28425C};
    plats[plat_count++] = (plat_t){2090, 180, 240, 90, 90, 0xFF40607E, 0xFF182A3E, 0xFF2A445E};
    plats[plat_count++] = (plat_t){420, 120, 140, 60, 170, 0xFF44647F, 0xFF1A2C40, 0xFF2C4660};
    plats[plat_count++] = (plat_t){1330, 120, 150, 60, 180, 0xFF44647F, 0xFF1A2C40, 0xFF2C4660};
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullets[i].active = 0;
    }
    for (int i = 0; i < MAX_EBULLETS; i++) {
        ebullets[i].active = 0;
    }
    hearts[0].x = 300;
    hearts[0].z = 195;
    hearts[0].y = 96;
    hearts[1].x = 630;
    hearts[1].z = 280;
    hearts[1].y = 146;
    hearts[2].x = 960;
    hearts[2].z = 215;
    hearts[2].y = 96;
    hearts[3].x = 1295;
    hearts[3].z = 290;
    hearts[3].y = 166;
    hearts[4].x = 1605;
    hearts[4].z = 225;
    hearts[4].y = 106;
    hearts[5].x = 1920;
    hearts[5].z = 290;
    hearts[5].y = 176;
    hearts[6].x = 2210;
    hearts[6].z = 225;
    hearts[6].y = 116;
    hearts[7].x = 490;
    hearts[7].z = 150;
    hearts[7].y = 196;
    for (int i = 0; i < MAX_HEARTS; i++) {
        hearts[i].bob = i * 10;
        hearts[i].active = i < 5;
    }
}

static void reset_player(void) {
    pl.x = 220;
    pl.z = 200;
    pl.y = 0;
    pl.vx = 0;
    pl.vz = 0;
    pl.vy = 0;
    pl.facing = 1;
    pl.on_ground = 1;
    pl.hp = 20;
    pl.max_hp = 20;
    pl.anim = 0;
    pl.hurt_t = 0;
    pl.dead = 0;
    pl.shoot_cd = 0;
    hearts_got = 0;
    kills = 0;
    dlg_active = 0;
    dlg_idx = 0;
    dlg_char = 0;
    chat_mode = 0;
    chat_len = 0;
}

static int ground_at(int x, int z) {
    int best = 0;
    for (int i = 0; i < plat_count; i++) {
        plat_t *p = &plats[i];
        if (x >= p->x && x <= p->x + p->w && z >= p->z && z <= p->z + p->d) {
            if (p->h > best) {
                best = p->h;
            }
        }
    }
    return best;
}

static void spawn_bullet(int x, int z, int y, int vx, int vz, int friendly) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) {
            continue;
        }
        bullets[i].x = x;
        bullets[i].z = z;
        bullets[i].y = y;
        bullets[i].vx = vx;
        bullets[i].vz = vz;
        bullets[i].vy = 0;
        bullets[i].active = 1;
        bullets[i].friendly = friendly;
        return;
    }
}

static void spawn_ebullet(int x, int z, int y, int vx, int vz) {
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (ebullets[i].active) {
            continue;
        }
        ebullets[i].x = x;
        ebullets[i].z = z;
        ebullets[i].y = y;
        ebullets[i].vx = vx;
        ebullets[i].vz = vz;
        ebullets[i].vy = 0;
        ebullets[i].active = 1;
        ebullets[i].friendly = 0;
        return;
    }
}

static void update_player(void) {
    int was_air = !pl.on_ground;
    int mx = 0;
    int mz = 0;
    if (key_left) {
        mx -= MOVE_X;
    }
    if (key_right) {
        mx += MOVE_X;
    }
    if (key_up) {
        mz -= MOVE_Z;
    }
    if (key_down) {
        mz += MOVE_Z;
    }
    pl.vx = mx;
    pl.vz = mz;
    if (mx != 0) {
        pl.facing = mx > 0 ? 1 : -1;
    }
    pl.x += pl.vx;
    pl.z += pl.vz;
    pl.x = clampi(pl.x, 0, WORLD_W);
    pl.z = clampi(pl.z, 0, WORLD_D);
    if (key_jump && pl.on_ground) {
        pl.vy = JUMP_V;
        pl.on_ground = 0;
        pc_beep(660, 60);
        burst(pl.x, pl.z, pl.y + 2, 6, 0xFF8090B0, 3, 3);
    }
    int prev_y = pl.y;
    pl.y += pl.vy;
    pl.vy -= GRAV;
    if (pl.vy < -20) {
        pl.vy = -20;
    }
    int g = ground_at(pl.x, pl.z);
    if (pl.y <= g && pl.vy <= 0) {
        if (prev_y >= g - 1 || pl.on_ground) {
            pl.y = g;
            pl.vy = 0;
            pl.on_ground = 1;
        } else {
            pl.on_ground = 0;
        }
    } else {
        if (pl.y > g) {
            pl.on_ground = 0;
        }
    }
    if (pl.y < 0) {
        pl.y = 0;
        pl.vy = 0;
        pl.on_ground = 1;
    }
    if (was_air && pl.on_ground && pl.vy == 0) {
        burst(pl.x, pl.z, pl.y + 2, 5, 0xFF7080A0, 4, 2);
    }
    if (pl.vx != 0 || pl.vz != 0) {
        pl.anim++;
        if (pl.on_ground && (pl.anim % 7) == 0) {
            spawn_particle(pl.x, pl.z, pl.y + 2, -pl.vx / 3, 2, 0, 10, 0xFF6A7A9A, 1);
        }
    } else {
        pl.anim++;
    }
    if (pl.hurt_t > 0) {
        pl.hurt_t--;
    }
    if (pl.shoot_cd > 0) {
        pl.shoot_cd--;
    }
    if (key_attack && pl.shoot_cd == 0) {
        spawn_bullet(pl.x + pl.facing * 20, pl.z, pl.y + 30, pl.facing * 9, 0, 1);
        spawn_particle(pl.x + pl.facing * 22, pl.z, pl.y + 30, pl.facing * 3, 3, 0, 8, 0xFFFFFF80,
                       2);
        pl.shoot_cd = 12;
        pc_beep(880, 40);
    }
}

static void update_enemies(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &enemies[i];
        if (!e->alive) {
            continue;
        }
        e->anim++;
        e->x += e->dir * 2;
        if (e->x > e->patrol_b) {
            e->dir = -1;
        }
        if (e->x < e->patrol_a) {
            e->dir = 1;
        }
        e->y = ground_at(e->x, e->z);
        e->shoot_t--;
        if (e->shoot_t <= 0) {
            int dx = pl.x - e->x;
            int dz = pl.z - e->z;
            int vx = clampi(dx / 30, -4, 4);
            int vz = clampi(dz / 30, -2, 2);
            if (vx == 0) {
                vx = e->dir * 3;
            }
            spawn_ebullet(e->x, e->z, e->y + 30, vx, vz);
            if ((anim_ms / 1500) % 2 == 0) {
                spawn_ebullet(e->x, e->z, e->y + 30, vx, vz + 1);
                spawn_ebullet(e->x, e->z, e->y + 30, vx, vz - 1);
            }
            e->shoot_t = 110;
        }
    }
}

static void update_bullets(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullet_t *b = &bullets[i];
        if (!b->active) {
            continue;
        }
        b->x += b->vx;
        b->z += b->vz;
        if (b->x < -100 || b->x > WORLD_W + 100 || b->z < -20 || b->z > WORLD_D + 20) {
            b->active = 0;
            continue;
        }
        for (int e = 0; e < MAX_ENEMIES; e++) {
            enemy_t *en = &enemies[e];
            if (!en->alive) {
                continue;
            }
            if (absi(b->x - en->x) < 24 && absi(b->z - en->z) < 24 &&
                absi(b->y + 30 - (en->y + 30)) < 40) {
                en->hp--;
                b->active = 0;
                pc_beep(440, 30);
                if (en->hp <= 0) {
                    en->alive = 0;
                    kills++;
                    pc_beep(220, 120);
                    for (int h = 0; h < MAX_HEARTS; h++) {
                        if (!hearts[h].active) {
                            hearts[h].x = en->x;
                            hearts[h].z = en->z;
                            hearts[h].y = en->y + 40;
                            hearts[h].active = 1;
                            break;
                        }
                    }
                }
                break;
            }
        }
    }
    for (int i = 0; i < MAX_EBULLETS; i++) {
        bullet_t *b = &ebullets[i];
        if (!b->active) {
            continue;
        }
        b->x += b->vx;
        b->z += b->vz;
        if (b->x < -100 || b->x > WORLD_W + 100 || b->z < -20 || b->z > WORLD_D + 20) {
            b->active = 0;
            continue;
        }
        if (pl.hurt_t == 0 && absi(b->x - pl.x) < 16 && absi(b->z - pl.z) < 16 &&
            absi(b->y - (pl.y + 25)) < 28) {
            pl.hp -= 2;
            pl.hurt_t = 40;
            shake_t = 16;
            burst(pl.x, pl.z, pl.y + 25, 8, 0xFFFF4444, 5, 4);
            b->active = 0;
            pc_beep(180, 100);
            if (pl.hp <= 0) {
                pl.hp = 0;
                state = 3;
            }
        }
    }
}

static void update_hearts(void) {
    for (int i = 0; i < MAX_HEARTS; i++) {
        heart_t *h = &hearts[i];
        if (!h->active) {
            continue;
        }
        h->bob++;
        if (absi(pl.x - h->x) < 22 && absi(pl.z - h->z) < 22 && absi(pl.y + 25 - h->y) < 30) {
            h->active = 0;
            hearts_got++;
            pl.hp += 5;
            if (pl.hp > pl.max_hp) {
                pl.hp = pl.max_hp;
            }
            pc_beep(990, 80);
            if (hearts_got >= 5) {
                state = 4;
            }
        }
    }
}

static int on_screen(int sx, int pad) {
    return sx > -pad && sx < (int)screen_width + pad;
}

static void draw_shadow(int wx, int wz, int wy, int scale_mul) {
    int sx, sy, sc;
    proj(wx, wz, ground_at(wx, wz), &sx, &sy, &sc);
    int w = 10 * sc * scale_mul - wy / 9;
    if (w < 4) {
        w = 4;
    }
    draw_rect(sx - w, sy - 2, w * 2, 2, 0xFF000000);
    draw_rect(sx - w + 3, sy, w * 2 - 6, 2, 0xFF000000);
    draw_rect(sx - w / 2, sy - 3, w, 2, 0xFF05050C);
}

#define MAX_DIRTY 64
static int dr_prev[MAX_DIRTY][4];
static int dr_prev_n;
static int dr_cur[MAX_DIRTY][4];
static int dr_overflow;
static int dr_cur_n;

static void dr_push(int x0, int y0, int x1, int y1) {
    if (dr_cur_n >= MAX_DIRTY) {
        dr_overflow = 1;
        return;
    }
    dr_cur[dr_cur_n][0] = x0;
    dr_cur[dr_cur_n][1] = y0;
    dr_cur[dr_cur_n][2] = x1;
    dr_cur[dr_cur_n][3] = y1;
    dr_cur_n++;
}

static void dr_collect(void) {
    int sx, sy, sc;
    int sw = (int)screen_width;
    int sh = (int)screen_height;
    dr_cur_n = 0;
    proj(pl.x, pl.z, pl.y, &sx, &sy, &sc);
    {
        int ss = sc + 1;
        int top = sy - 30 * ss;
        int lx, ly, lsc;
        proj(pl.x, pl.z, pl.y + 42, &lx, &ly, &lsc);
        if (ly - 18 < top) {
            top = ly - 18;
        }
        dr_push(sx - 14 * ss - 8, top, sx + 14 * ss + 8, sy + 10);
        int shx, shy, shs;
        proj(pl.x, pl.z, ground_at(pl.x, pl.z), &shx, &shy, &shs);
        dr_push(shx - 16 * shs, shy - 8, shx + 16 * shs, shy + 6);
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) {
            continue;
        }
        proj(enemies[i].x, enemies[i].z, enemies[i].y, &sx, &sy, &sc);
        if (sx < -160 || sx > sw + 160) {
            continue;
        }
        {
            int es = sc + 1;
            dr_push(sx - 14 * es - 8, sy - 30 * es, sx + 14 * es + 8, sy + 10);
            int shx, shy, shs;
            proj(enemies[i].x, enemies[i].z, ground_at(enemies[i].x, enemies[i].z), &shx, &shy,
                 &shs);
            dr_push(shx - 16 * shs, shy - 8, shx + 16 * shs, shy + 6);
        }
    }
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!peers[i].active) {
            continue;
        }
        proj(peers[i].x, peers[i].z, peers[i].y, &sx, &sy, &sc);
        if (sx < -260 || sx > sw + 260) {
            continue;
        }
        dr_push(sx - 90, sy - 110, sx + 90, sy + 46);
    }
    proj(900, 220, ground_at(900, 220), &sx, &sy, &sc);
    if (sx > -160 && sx < sw + 160) {
        dr_push(sx - 100, sy - 96, sx + 100, sy + 30);
    }
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (!ebullets[i].active) {
            continue;
        }
        proj(ebullets[i].x, ebullets[i].z, ebullets[i].y, &sx, &sy, &sc);
        if (sx < -80 || sx > sw + 80) {
            continue;
        }
        dr_push(sx - 30, sy - 30, sx + 30, sy + 30);
    }
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            continue;
        }
        proj(bullets[i].x, bullets[i].z, bullets[i].y, &sx, &sy, &sc);
        if (sx < -80 || sx > sw + 80) {
            continue;
        }
        dr_push(sx - 30, sy - 30, sx + 30, sy + 30);
    }
    {
        int gx0[4], gy0[4], gx1[4], gy1[4], gn[4];
        for (int g = 0; g < 4; g++) {
            gx0[g] = 0;
            gy0[g] = 0;
            gx1[g] = 0;
            gy1[g] = 0;
            gn[g] = 0;
        }
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!parts[i].active) {
                continue;
            }
            proj(parts[i].x, parts[i].z, parts[i].y, &sx, &sy, &sc);
            if (sx < -40 || sx > sw + 40) {
                continue;
            }
            int g = i & 3;
            if (!gn[g]) {
                gx0[g] = sx - 10;
                gy0[g] = sy - 10;
                gx1[g] = sx + 10;
                gy1[g] = sy + 10;
                gn[g] = 1;
            } else {
                if (sx - 10 < gx0[g]) {
                    gx0[g] = sx - 10;
                }
                if (sy - 10 < gy0[g]) {
                    gy0[g] = sy - 10;
                }
                if (sx + 10 > gx1[g]) {
                    gx1[g] = sx + 10;
                }
                if (sy + 10 > gy1[g]) {
                    gy1[g] = sy + 10;
                }
            }
        }
        for (int g = 0; g < 4; g++) {
            if (gn[g]) {
                dr_push(gx0[g], gy0[g], gx1[g], gy1[g]);
            }
        }
    }
    for (int i = 0; i < dr_cur_n; i++) {
        if (dr_cur[i][0] < 0) {
            dr_cur[i][0] = 0;
        }
        if (dr_cur[i][1] < 0) {
            dr_cur[i][1] = 0;
        }
        if (dr_cur[i][2] > sw) {
            dr_cur[i][2] = sw;
        }
        if (dr_cur[i][3] > sh) {
            dr_cur[i][3] = sh;
        }
    }
}

static void erase_prev(int px0, int py0, int px1, int py1) {
    int a[24][4];
    int b[24][4];
    int n = 1;
    a[0][0] = px0;
    a[0][1] = py0;
    a[0][2] = px1;
    a[0][3] = py1;
    for (int c = 0; c < dr_cur_n && n > 0; c++) {
        int ux0 = dr_cur[c][0], uy0 = dr_cur[c][1], ux1 = dr_cur[c][2], uy1 = dr_cur[c][3];
        int m = 0;
        for (int i = 0; i < n; i++) {
            int x0 = a[i][0], y0 = a[i][1], x1 = a[i][2], y1 = a[i][3];
            if (x1 <= ux0 || x0 >= ux1 || y1 <= uy0 || y0 >= uy1) {
                if (m < 24) {
                    b[m][0] = x0;
                    b[m][1] = y0;
                    b[m][2] = x1;
                    b[m][3] = y1;
                    m++;
                }
                continue;
            }
            if (y0 < uy0 && m < 24) {
                b[m][0] = x0;
                b[m][1] = y0;
                b[m][2] = x1;
                b[m][3] = uy0;
                m++;
            }
            if (y1 > uy1 && m < 24) {
                b[m][0] = x0;
                b[m][1] = uy1;
                b[m][2] = x1;
                b[m][3] = y1;
                m++;
            }
            int yy0 = y0 > uy0 ? y0 : uy0;
            int yy1 = y1 < uy1 ? y1 : uy1;
            if (yy0 < yy1) {
                if (x0 < ux0 && m < 24) {
                    b[m][0] = x0;
                    b[m][1] = yy0;
                    b[m][2] = ux0;
                    b[m][3] = yy1;
                    m++;
                }
                if (x1 > ux1 && m < 24) {
                    b[m][0] = ux1;
                    b[m][1] = yy0;
                    b[m][2] = x1;
                    b[m][3] = yy1;
                    m++;
                }
            }
        }
        for (int i = 0; i < m; i++) {
            a[i][0] = b[i][0];
            a[i][1] = b[i][1];
            a[i][2] = b[i][2];
            a[i][3] = b[i][3];
        }
        n = m;
    }
    for (int i = 0; i < n; i++) {
        if (a[i][2] > a[i][0] && a[i][3] > a[i][1]) {
            fb_bg_restore_direct(a[i][0], a[i][1], a[i][2], a[i][3]);
        }
    }
}

static void draw_world_static(void) {
    draw_background();
    for (int i = 0; i < plat_count; i++) {
        draw_platform(&plats[i]);
    }
    for (int i = 0; i < MAX_HEARTS; i++) {
        heart_t *h = &hearts[i];
        if (!h->active) {
            continue;
        }
        int sx, sy, sc;
        int bob = (h->bob / 8) % 2 ? 3 : -3;
        proj(h->x, h->z, h->y + bob, &sx, &sy, &sc);
        if (!on_screen(sx, 90)) {
            continue;
        }
        draw_heart(sx, sy, 9 * sc + 3, 0xFF702040);
        draw_heart(sx, sy, 9 * sc, 0xFFFF00FF);
    }
}

static void draw_world(void) {
    int npc_x = 900;
    int npc_z = 220;
    int nsx, nsy, nsc;
    proj(npc_x, npc_z, ground_at(npc_x, npc_z), &nsx, &nsy, &nsc);
    if (on_screen(nsx, 140)) {
        draw_shadow(npc_x, npc_z, ground_at(npc_x, npc_z), 1);
        draw_stickman(npc_x, npc_z, ground_at(npc_x, npc_z), -1, (int)(anim_ms / 90), 0xFFFFFF00, 1,
                      0);
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &enemies[i];
        if (!e->alive) {
            continue;
        }
        uint32_t c = 0xFFFF0000;
        if ((anim_ms / 120) % 2 == 0 && e->hp <= 1) {
            c = 0xFFFFAAAA;
        }
        int esx, esy, esc;
        proj(e->x, e->z, e->y, &esx, &esy, &esc);
        if (!on_screen(esx, 140)) {
            continue;
        }
        draw_shadow(e->x, e->z, e->y, 1);
        draw_stickman(e->x, e->z, e->y, e->dir, e->anim, c, 1, 0);
        int sx, sy, sc;
        proj(e->x, e->z, e->y + 60, &sx, &sy, &sc);
        draw_rect(sx - 12, sy, 24, 4, 0xFF330000);
        draw_rect(sx - 12, sy, e->hp * 8, 4, 0xFFFF0000);
    }
    for (int i = 0; i < MAX_HEARTS; i++) {
        heart_t *h = &hearts[i];
        if (!h->active) {
            continue;
        }
        int sx, sy, sc;
        int bob = (h->bob / 8) % 2 ? 3 : -3;
        proj(h->x, h->z, h->y + bob, &sx, &sy, &sc);
        if (!on_screen(sx, 90)) {
            continue;
        }
        int pulse = isin((int)(anim_ms / 3) + i * 60) * 2 / 1000;
        draw_heart(sx, sy, 9 * sc + 3 + pulse, 0xFF702040);
        if ((anim_ms % 900) == (uint32_t)(i * 150)) {
            spawn_particle(h->x, h->z, h->y + bob, rnd(5) - 2, 2, 0, 22, 0xFFFF66FF, 1);
        }
        draw_heart(sx, sy, 9 * sc + pulse, 0xFFFF00FF);
    }
    for (int i = 0; i < MAX_PEERS; i++) {
        peer_t *p = &peers[i];
        if (!p->active) {
            continue;
        }
        uint32_t c = 0xFF00FFFF;
        if ((int)(uptime_ticks - p->last_seen) > 3000) {
            c = 0xFF008888;
        }
        int psx, psy, psc;
        proj(p->x, p->z, p->y, &psx, &psy, &psc);
        if (!on_screen(psx, 160)) {
            continue;
        }
        draw_shadow(p->x, p->z, p->y, 1);
        draw_stickman(p->x, p->z, p->y, p->facing, p->anim, c, 1, 0);
        int sx, sy, sc;
        proj(p->x, p->z, p->y + 55, &sx, &sy, &sc);
        char ipb[24];
        int q = 0;
        int_to_string(p->ip[0], ipb + q);
        while (ipb[q]) {
            q++;
        }
        ipb[q++] = '.';
        int_to_string(p->ip[1], ipb + q);
        while (ipb[q]) {
            q++;
        }
        ipb[q++] = '.';
        int_to_string(p->ip[2], ipb + q);
        while (ipb[q]) {
            q++;
        }
        ipb[q++] = '.';
        int_to_string(p->ip[3], ipb + q);
        while (ipb[q]) {
            q++;
        }
        ipb[q] = 0;
        draw_text_center(sx, sy, ipb, 0xFF00FFFF, 0xFF000000);
        if ((int)(uptime_ticks - p->chat_t) < 4000 && p->chat[0]) {
            draw_text_center(sx, sy + 14, p->chat, 0xFFFFFFFF, 0xFF000000);
        }
    }
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullet_t *b = &bullets[i];
        if (!b->active) {
            continue;
        }
        int sx, sy, sc;
        proj(b->x, b->z, b->y, &sx, &sy, &sc);
        if (!on_screen(sx, 40)) {
            continue;
        }
        draw_circle(sx - b->vx, sy + (b->vz > 0 ? 1 : -1), 2 * sc, 0xFF806000);
        draw_circle(sx, sy, 4 * sc, 0xFF806000);
        draw_circle(sx, sy, 3 * sc, 0xFFFFFF00);
        draw_circle(sx, sy, 1 * sc, 0xFFFFFFFF);
    }
    for (int i = 0; i < MAX_EBULLETS; i++) {
        bullet_t *b = &ebullets[i];
        if (!b->active) {
            continue;
        }
        int sx, sy, sc;
        proj(b->x, b->z, b->y, &sx, &sy, &sc);
        if (!on_screen(sx, 40)) {
            continue;
        }
        draw_heart(sx, sy, 5 * sc, 0xFF5A0000);
        draw_heart(sx, sy, 4 * sc, 0xFFFF0000);
    }
    draw_shadow(pl.x, pl.z, pl.y, 1);
    uint32_t pc = 0xFFFFFFFF;
    if (pl.hurt_t > 0 && (anim_ms / 80) % 2 == 0) {
        pc = 0xFFFF0000;
    }
    draw_stickman(pl.x, pl.z, pl.y, pl.facing, pl.anim, pc, 1, !pl.on_ground);
    int sx, sy, sc;
    proj(pl.x, pl.z, pl.y + 42, &sx, &sy, &sc);
    draw_text_center(sx, sy, "YOU", 0xFFFFFFFF, 0xFF000000);
    draw_particles();
    if (absi(pl.x - npc_x) < 80 && absi(pl.z - npc_z) < 60 && !dlg_active) {
        draw_text_center(sx, sy + 14, "Z: TALK", 0xFFFFFF00, 0xFF000000);
    }
}

static int net_service_cb(const uint8_t *frame_data, uint32_t frame_len, const uint8_t *src_ip,
                          const uint8_t *dst_ip, uint16_t src_port, uint16_t dst_port,
                          uint8_t protocol) {
    (void)dst_ip;
    (void)protocol;
    if (dst_port != P2P_PORT) {
        return 0;
    }
    if (src_port != P2P_PORT) {
        return 0;
    }
    if (frame_len < 14 + 20 + 8 + 15) {
        return 0;
    }
    uint32_t ihl = (uint32_t)((frame_data[14] & 0x0F) * 4);
    if (ihl < 20) {
        return 0;
    }
    if (frame_len < 14 + ihl + 8 + 15) {
        return 0;
    }
    const uint8_t *pkt = frame_data + 14 + ihl + 8;
    uint32_t plen = frame_len - (14 + ihl + 8);
    if (plen < 15) {
        return 0;
    }
    if (pkt[0] != 'S' || pkt[1] != 'H') {
        return 0;
    }
    uint8_t type = pkt[2];
    uint32_t id = (uint32_t)pkt[3] | ((uint32_t)pkt[4] << 8) | ((uint32_t)pkt[5] << 16) |
                  ((uint32_t)pkt[6] << 24);
    if (id == player_id) {
        return 1;
    }
    int px = (int)pkt[7] | ((int)pkt[8] << 8);
    int pz = (int)pkt[9];
    int py = (int)pkt[10] | ((int)pkt[11] << 8);
    int facing = (pkt[12] == 255) ? -1 : 1;
    int hp = (int)pkt[13];
    int anim = (int)pkt[14];
    int slot = -1;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peers[i].active && peers[i].id == id) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < MAX_PEERS; i++) {
            if (!peers[i].active) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        return 1;
    }
    peer_t *p = &peers[slot];
    if (!p->active) {
        p->active = 1;
        peer_count++;
        for (int i = 0; i < 4; i++) {
            p->ip[i] = src_ip[i];
        }
        p->id = id;
        p->chat[0] = 0;
    }
    p->x = px;
    p->z = pz;
    p->y = py;
    p->facing = facing;
    p->hp = hp;
    p->anim = anim;
    p->last_seen = uptime_ticks;
    for (int i = 0; i < 4; i++) {
        p->ip[i] = src_ip[i];
    }
    if (type == 3 && plen > 15) {
        uint32_t n = plen - 15;
        if (n >= CHAT_MAX) {
            n = CHAT_MAX - 1;
        }
        for (uint32_t i = 0; i < n; i++) {
            p->chat[i] = (char)pkt[15 + i];
        }
        p->chat[n] = 0;
        p->chat_t = uptime_ticks;
    }
    return 1;
}

static void p2p_send(uint8_t type, const uint8_t *dst_ip, const uint8_t *extra,
                     uint32_t extra_len) {
    if (!net_has_nic() || !net_configured) {
        return;
    }
    uint8_t pkt[128];
    uint32_t o = 0;
    pkt[o++] = 'S';
    pkt[o++] = 'H';
    pkt[o++] = type;
    pkt[o++] = (uint8_t)(player_id & 0xFF);
    pkt[o++] = (uint8_t)((player_id >> 8) & 0xFF);
    pkt[o++] = (uint8_t)((player_id >> 16) & 0xFF);
    pkt[o++] = (uint8_t)((player_id >> 24) & 0xFF);
    pkt[o++] = (uint8_t)(pl.x & 0xFF);
    pkt[o++] = (uint8_t)((pl.x >> 8) & 0xFF);
    pkt[o++] = (uint8_t)pl.z;
    pkt[o++] = (uint8_t)(pl.y & 0xFF);
    pkt[o++] = (uint8_t)((pl.y >> 8) & 0xFF);
    pkt[o++] = (uint8_t)(pl.facing < 0 ? 255 : 1);
    pkt[o++] = (uint8_t)pl.hp;
    pkt[o++] = (uint8_t)pl.anim;
    if (extra && extra_len) {
        for (uint32_t i = 0; i < extra_len && o < 120; i++) {
            pkt[o++] = extra[i];
        }
    }
    uint8_t mac[6];
    uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t bcip[4] = {255, 255, 255, 255};
    if (dst_ip) {
        if (net_route_mac(dst_ip, mac)) {
            net_send_udp(mac, net_ip, dst_ip, P2P_PORT, P2P_PORT, pkt, o);
        }
    } else {
        net_send_udp(bc, net_ip, bcip, P2P_PORT, P2P_PORT, pkt, o);
    }
}

static void p2p_poll(void) {
    net_poll();
    uint32_t now = uptime_ticks;
    for (int i = 0; i < MAX_PEERS; i++) {
        peer_t *p = &peers[i];
        if (!p->active) {
            continue;
        }
        if ((int)(now - p->last_seen) > 6000) {
            p->active = 0;
            peer_count--;
        }
    }
    if (!net_has_nic() || !net_configured) {
        return;
    }
    int moved = absi(pl.x - last_sent_x) >= 6 || absi(pl.z - last_sent_z) >= 3 ||
                absi(pl.y - last_sent_y) >= 6;
    int due = (int)(now - last_net_send) >= 50;
    int urgent = moved && (int)(now - last_net_send) >= 16;
    if (due || urgent) {
        last_net_send = now;
        last_sent_x = pl.x;
        last_sent_z = pl.z;
        last_sent_y = pl.y;
        p2p_send(2, NULL, NULL, 0);
        if (manual_set) {
            p2p_send(2, manual_ip, NULL, 0);
        }
    }
    if ((int)(now - last_hello) >= 1200) {
        last_hello = now;
        p2p_send(1, NULL, NULL, 0);
        if (manual_set) {
            p2p_send(1, manual_ip, NULL, 0);
        }
    }
}

static void handle_key(char c) {
    if (state == 0) {
        if (ip_mode) {
            if (c == 27) {
                ip_mode = 0;
                ip_len = 0;
                return;
            }
            if (c == '\n' || c == '\r') {
                uint8_t ip[4] = {0, 0, 0, 0};
                int part = 0;
                int val = 0;
                int ok = 1;
                for (int i = 0; i < ip_len; i++) {
                    char ch = ip_buf[i];
                    if (ch == '.') {
                        if (part > 3) {
                            ok = 0;
                            break;
                        }
                        ip[part++] = (uint8_t)val;
                        val = 0;
                    } else if (ch >= '0' && ch <= '9') {
                        val = val * 10 + (ch - '0');
                        if (val > 255) {
                            ok = 0;
                            break;
                        }
                    } else {
                        ok = 0;
                        break;
                    }
                }
                if (ok && part == 3) {
                    ip[3] = (uint8_t)val;
                    for (int i = 0; i < 4; i++) {
                        manual_ip[i] = ip[i];
                    }
                    manual_set = 1;
                    pc_beep(990, 60);
                }
                ip_mode = 0;
                ip_len = 0;
                return;
            }
            if (c == '\b' || c == 127) {
                if (ip_len > 0) {
                    ip_len--;
                }
                return;
            }
            if ((c >= '0' && c <= '9') || c == '.') {
                if (ip_len < 30) {
                    ip_buf[ip_len++] = c;
                }
            }
            return;
        }
        if (c == 'i' || c == 'I') {
            ip_mode = 1;
            ip_len = 0;
            return;
        }
        if (c == '\n' || c == '\r') {
            init_world();
            reset_player();
            state = 1;
            pc_beep(660, 60);
            return;
        }
        if (c == 27) {
            shutdown();
            return;
        }
        return;
    }
    if (state == 3 || state == 4) {
        if (c == '\n' || c == '\r') {
            init_world();
            reset_player();
            state = 1;
            return;
        }
        if (c == 27) {
            state = 0;
            return;
        }
        return;
    }
    if (chat_mode) {
        if (c == 27) {
            chat_mode = 0;
            chat_len = 0;
            return;
        }
        if (c == '\n' || c == '\r') {
            if (chat_len > 0) {
                char tmp[CHAT_MAX];
                for (int i = 0; i < chat_len; i++) {
                    tmp[i] = chat_buf[i];
                }
                tmp[chat_len] = 0;
                p2p_send(3, NULL, (uint8_t *)tmp, (uint32_t)chat_len);
                if (manual_set) {
                    p2p_send(3, manual_ip, (uint8_t *)tmp, (uint32_t)chat_len);
                }
            }
            chat_mode = 0;
            chat_len = 0;
            return;
        }
        if (c == '\b' || c == 127) {
            if (chat_len > 0) {
                chat_len--;
            }
            return;
        }
        if (c >= 32 && c <= 126) {
            if (chat_len < CHAT_MAX - 1) {
                chat_buf[chat_len++] = c;
            }
        }
        return;
    }
    if (dlg_active) {
        if (c == 'z' || c == 'Z') {
            if (dlg_char < (int)strlen(dlg_lines[dlg_idx])) {
                dlg_char = (int)strlen(dlg_lines[dlg_idx]);
            } else {
                dlg_idx++;
                dlg_char = 0;
                if (dlg_idx >= DLG_COUNT) {
                    dlg_active = 0;
                    dlg_idx = 0;
                }
            }
            pc_beep(520, 30);
            return;
        }
        if (c == 'x' || c == 'X') {
            dlg_active = 0;
            dlg_idx = 0;
            dlg_char = 0;
            return;
        }
        return;
    }
    if (c == 't' || c == 'T') {
        chat_mode = 1;
        chat_len = 0;
    }
    if (c == 27) {
        state = 0;
    }
}

static void poll_input(void) {
    int sp;
    int sx;
    int sz;
    key_left = keyboard_down(0x1E) || keyboard_down_ext(0x4B);
    key_right = keyboard_down(0x20) || keyboard_down_ext(0x4D);
    key_up = keyboard_down(0x11) || keyboard_down_ext(0x48);
    key_down = keyboard_down(0x1F) || keyboard_down_ext(0x50);
    sp = keyboard_down(0x39);
    if (sp && !edge_space) {
        key_jump = 1;
    }
    edge_space = sp;
    sx = keyboard_down(0x2D);
    if (sx && !edge_shoot) {
        key_attack = 1;
    }
    edge_shoot = sx;
    sz = keyboard_down(0x2C);
    if (sz && !edge_talk) {
        key_talk = 1;
    }
    edge_talk = sz;
}

void game_init(void) {
    g_stride = screen_pitch / 4;
    net_set_service_hook(net_service_cb);
    state = 0;
    frame = 0;
    last_net_send = 0;
    last_hello = 0;
    last_fps_t = uptime_ticks;
    fps = 0;
    fps_count = 0;
    dlg_active = 0;
    dlg_idx = 0;
    dlg_char = 0;
    chat_mode = 0;
    chat_len = 0;
    ip_mode = 0;
    ip_len = 0;
    manual_set = 0;
    hearts_got = 0;
    kills = 0;
    peer_count = 0;
    player_id = 0;
    for (int i = 0; i < 6; i++) {
        player_id = player_id * 31 + net_mac[i] + 7;
    }
    uint64_t id_seed = rdtsc();
    player_id = player_id * 2654435761u + (uint32_t)(id_seed & 0xFFFF);
    if (!player_id) {
        player_id = 1;
    }
    player_id ^= uptime_ticks;
    if (player_id == 0) {
        player_id = 12345;
    }
    init_world();
    reset_player();
    horizon_y = (int)screen_height * 58 / 100;
    if (horizon_y < 80) {
        horizon_y = 80;
    }
}

void game_run(void) {
    game_init();
    uint32_t last = uptime_ticks;
    uint32_t last_fps_t = uptime_ticks;
    int bg_cam = -999999;
    int full = 1;
    int last_hp = -1, last_hearts = -1, last_kills = -1, last_fpsv = -1;
    int last_peer_sig = 0;
    while (1) {
        char c;
        int n = 0;
        poll_input();
        while (n < 16 && (c = keyboard_getchar()) != 0) {
            n++;
            handle_key(c);
        }
        uint32_t now = uptime_ticks;
        anim_ms = now;
        static uint32_t last_net = 0;
        if ((int)(now - last_net) >= 10) {
            last_net = now;
            p2p_poll();
        }
        if ((int)(now - last_fps_t) >= 1000) {
            fps = fps_count;
            fps_count = 0;
            last_fps_t = now;
        }
        if ((int)(now - last) >= 16) {
            last = now;
            if (state == 1) {
                update_player();
                update_enemies();
                update_bullets();
                update_hearts();
                update_particles();
                if (shake_t > 0) {
                    shake_t--;
                }
                if (key_talk && !dlg_active && !chat_mode) {
                    if (absi(pl.x - 900) < 80 && absi(pl.z - 220) < 60) {
                        dlg_active = 1;
                        dlg_idx = 0;
                        dlg_char = 0;
                        pc_beep(700, 40);
                    }
                }
                key_jump = 0;
                key_attack = 0;
                key_talk = 0;
                if (dlg_active) {
                    dlg_t++;
                    if (dlg_t % 2 == 0) {
                        if (dlg_char < (int)strlen(dlg_lines[dlg_idx])) {
                            dlg_char++;
                            if (dlg_char % 4 == 0) {
                                pc_beep(700, 15);
                            }
                        }
                    }
                }
                cam_x = pl.x + (int)(screen_width / 10) * 1000 / zscale(pl.z);
                if (cam_x < -400) {
                    cam_x = -400;
                }
                if (cam_x > WORLD_W - 400) {
                    cam_x = WORLD_W - 400;
                }
            }
        }
        frame++;
        fps_count++;
        if (state == 1) {
            if (fps != last_fpsv || pl.hp != last_hp || hearts_got != last_hearts ||
                kills != last_kills) {
                last_fpsv = fps;
                last_hp = pl.hp;
                last_hearts = hearts_got;
                last_kills = kills;
                full = 1;
            }
            if (shake_t > 0 || dlg_active || chat_mode) {
                full = 1;
            }
            if (bg_cam != cam_x) {
                full = 1;
            }
            {
                int sig = 0;
                for (int i = 0; i < MAX_PEERS; i++) {
                    peer_t *p = &peers[i];
                    if (!p->active) {
                        continue;
                    }
                    sig =
                        sig * 131 + p->x * 3 + p->y * 7 + p->z * 13 + p->anim * 17 + p->facing + 1;
                }
                if (sig != last_peer_sig) {
                    last_peer_sig = sig;
                    full = 1;
                }
            }
            fb_frame_begin();
            dr_collect();
            if (dr_overflow) {
                dr_overflow = 0;
                full = 1;
            }
            if (full) {
                fb_layer_begin();
                draw_world_static();
                draw_hud();
                fb_layer_end();
                fb_bg_to_back();
                fb_compose_begin();
                draw_world();
                if (dlg_active) {
                    draw_dialogue();
                }
                if (chat_mode) {
                    draw_chat();
                }
                fb_compose_end();
                fb_present_all();
                bg_cam = cam_x;
                full = 0;
                dr_prev_n = 0;
            } else {
                fb_direct_begin();
                draw_world();
                if (dlg_active) {
                    draw_dialogue();
                }
                if (chat_mode) {
                    draw_chat();
                }
                if (dr_prev_n) {
                    int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
                    if (dr_cur_n) {
                        bx0 = dr_cur[0][0];
                        by0 = dr_cur[0][1];
                        bx1 = dr_cur[0][2];
                        by1 = dr_cur[0][3];
                        for (int i = 1; i < dr_cur_n; i++) {
                            if (dr_cur[i][0] < bx0) {
                                bx0 = dr_cur[i][0];
                            }
                            if (dr_cur[i][1] < by0) {
                                by0 = dr_cur[i][1];
                            }
                            if (dr_cur[i][2] > bx1) {
                                bx1 = dr_cur[i][2];
                            }
                            if (dr_cur[i][3] > by1) {
                                by1 = dr_cur[i][3];
                            }
                        }
                    }
                    for (int i = 0; i < dr_prev_n; i++) {
                        int qx0 = dr_prev[i][0], qy0 = dr_prev[i][1], qx1 = dr_prev[i][2],
                            qy1 = dr_prev[i][3];
                        if (!dr_cur_n || qx1 <= bx0 || qx0 >= bx1 || qy1 <= by0 || qy0 >= by1) {
                            fb_bg_restore_direct(qx0, qy0, qx1, qy1);
                        } else {
                            erase_prev(qx0, qy0, qx1, qy1);
                        }
                    }
                }
                fb_direct_end();
            }
            for (int i = 0; i < dr_cur_n; i++) {
                dr_prev[i][0] = dr_cur[i][0];
                dr_prev[i][1] = dr_cur[i][1];
                dr_prev[i][2] = dr_cur[i][2];
                dr_prev[i][3] = dr_cur[i][3];
            }
            dr_prev_n = dr_cur_n;
        } else {
            fb_frame_begin();
            if (state == 0) {
                draw_title();
            } else if (state == 3) {
                draw_gameover();
            } else {
                draw_win();
            }
            fb_present(0, 0, (int)screen_width, (int)screen_height);
            full = 1;
        }
        if ((frame & 31) == 0) {
            yield();
        } else {
            asm volatile("pause");
        }
    }
}
