/*
 * NEON BREAKOUT — арканоид для PSP в неоновом стиле Neon Arcade.
 *
 * Логика и отрисовка не зависят от PSP: кадр рисуется в буфер 512x272 ABGR8888,
 * а main.c копирует его на экран. test_host.c проверяет игру на ПК.
 *
 * Управление: D-Pad / стик — платформа, X — запустить мяч / лазер,
 * START — пауза, SELECT (в паузе или после игры) — в главное меню.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "breakout.h"

/* У процессора PSP нет быстрой двойной точности, поэтому в игре только float-функции.
   В TCC на Windows части из них нет — для проверки на ПК подставляем аналоги. */
#ifdef HOST_TEST
#undef sinf
#undef cosf
#undef sqrtf
#undef floorf
#undef fabsf
#define sinf(x)   ((float)sin((double)(x)))
#define cosf(x)   ((float)cos((double)(x)))
#define sqrtf(x)  ((float)sqrt((double)(x)))
#define floorf(x) ((float)floor((double)(x)))
#define fabsf(x)  ((float)fabs((double)(x)))
#endif

static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512

#define RGB(r, g, b) (0xFF000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))

#define C_YELLOW  RGB(252, 238, 10)
#define C_CYAN    RGB(0, 240, 255)
#define C_MAGENTA RGB(255, 42, 109)
#define C_RED     RGB(255, 60, 60)
#define C_GREEN   RGB(57, 255, 120)
#define C_TEXT    RGB(236, 235, 245)
#define C_MUTED   RGB(140, 139, 168)
#define C_BLACK   RGB(0, 0, 0)
#define C_WHITE   RGB(255, 255, 255)

/* ------------------------------------------------------------------ */
/* Поле                                                                */
/* ------------------------------------------------------------------ */
#define FX0 16                     /* левая стенка */
#define FX1 464                    /* правая стенка */
#define FY0 24                     /* потолок (выше — табло) */
#define COLS 14
#define ROWS 9
#define BRICK_W 30
#define BRICK_H 10
#define BRICK_SX 32                /* шаг по горизонтали: 14 * 32 = 448 = ширина поля */
#define BRICK_SY 13
#define BRICK_Y0 36

#define PADDLE_Y 246
#define PADDLE_H 7
#define PAD_NORMAL 56.0f
#define PAD_WIDE 88.0f
#define BALL_R 3

#define MAX_BALLS 8
#define MAX_DROPS 16
#define MAX_BOLTS 16
#define MAX_PARTS 192
#define START_LIVES 3
#define MAX_LIVES 6

enum { B_EMPTY = 0, B_STRONG = 8, B_CRACK = 9, B_STEEL = 10 };   /* 1..7 — обычные цветные */
enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_CLEAR, ST_OVER };
enum { P_WIDE, P_MULTI, P_SLOW, P_LASER, P_CATCH, P_LIFE, P_COUNT };

static const u32 PCOL[7] = {
    RGB(0, 240, 255), RGB(60, 120, 255), RGB(176, 70, 255), RGB(255, 42, 109),
    RGB(255, 150, 30), RGB(252, 238, 10), RGB(57, 255, 120),
};

static const char PCHAR[P_COUNT] = { 'E', 'M', 'S', 'L', 'C', '+' };
static const char *const PNAME[P_COUNT] = { "WIDE PADDLE", "MULTIBALL", "SLOW BALL", "LASER", "CATCH", "EXTRA LIFE" };
static const u32 PDCOL[P_COUNT] = {
    RGB(0, 240, 255), RGB(255, 42, 109), RGB(57, 255, 120),
    RGB(255, 80, 40), RGB(252, 238, 10), RGB(236, 235, 245),
};

/* Уровни: '.' — пусто, '1'-'7' — цветной кирпич, 'S' — прочный (2 удара), 'X' — стальной (не бьётся). */
static const char *const LEVELS[][ROWS] = {
    {   "..............",
        "77777777777777",
        "66666666666666",
        "55555555555555",
        "44444444444444",
        "33333333333333",
        "22222222222222",
        "11111111111111",
        ".............." },
    {   "......SS......",
        ".....3333.....",
        "....444444....",
        "...55555555...",
        "..6666666666..",
        ".777777777777.",
        "11111111111111",
        "..............",
        ".............." },
    {   "X............X",
        "X.2222222222.X",
        "X.3333333333.X",
        "X.SSSSSSSSSS.X",
        "X.4444444444.X",
        "X.5555555555.X",
        "XXXX......XXXX",
        "..............",
        ".............." },
    {   "1.2.3.4.5.6.7.",
        ".1.2.3.4.5.6.7",
        "7.6.5.4.3.2.1.",
        ".7.6.5.4.3.2.1",
        "S.S.S.S.S.S.S.",
        ".S.S.S.S.S.S.S",
        "..............",
        "..............",
        ".............." },
    {   "...7......7...",
        "....7....7....",
        "...77777777...",
        "..77.7777.77..",
        ".777777777777.",
        ".7.77777777.7.",
        ".7.7......7.7.",
        "....77..77....",
        ".............." },
    {   "......77......",
        ".....7SS7.....",
        "....7S55S7....",
        "...7S5445S7...",
        "..7S543345S7..",
        "...7S5445S7...",
        "....7S55S7....",
        ".....7SS7.....",
        "......77......" },
    {   "XXXXXXXXXXXXXX",
        "S............S",
        "S.1111111111.S",
        "S.2SSSSSSSS2.S",
        "S.3S444444S3.S",
        "S.4SSSSSSSS4.S",
        "S.5555555555.S",
        "..............",
        ".............." },
    {   "..X........X..",
        "..S...66...S..",
        ".S3S..66..S3S.",
        ".343.5665.343.",
        "S444S5665S444S",
        "45554566545554",
        "56665555556665",
        "..............",
        ".............." },
};
#define NLEVELS ((int)(sizeof LEVELS / sizeof LEVELS[0]))

/* ------------------------------------------------------------------ */
/* Состояние игры                                                      */
/* ------------------------------------------------------------------ */
typedef struct { float x, y, vx, vy, stick; int alive, stuck; } Ball;
typedef struct { float x, y; int type, alive; } Drop;
typedef struct { float x, y; int alive; } Bolt;
typedef struct { float x, y, vx, vy, life, max; u32 c; } Part;

static unsigned char bricks[ROWS][COLS];
static Ball balls[MAX_BALLS];
static Drop drops[MAX_DROPS];
static Bolt bolts[MAX_BOLTS];
static Part parts[MAX_PARTS];

static int state = ST_TITLE;
static int score, best, best_level, lives, level, new_best;
static float pad_x, pad_w;
static float t_wide, t_slow, t_laser, t_catch, laser_cd;
static float speed_mul;
static float shake, flash, state_t, gt;
static char msg[32];
static float msg_t;
static char savepath[320] = "ms0:/PSP/GAME/NeonBreakout/breakout.sav";

/* ------------------------------------------------------------------ */
/* Случайные числа                                                     */
/* ------------------------------------------------------------------ */
static u32 rng = 0x2545F491u;
static u32 rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static float frand(void) { return (float)(rnd() & 0xFFFFFF) / 16777216.0f; }

/* ------------------------------------------------------------------ */
/* Примитивы рисования                                                 */
/* ------------------------------------------------------------------ */
static u32 frame[SCR_H * BUF_W];
static u32 bgbuf[SCR_H * BUF_W];
static u32 *T = frame;
static int ox, oy;                 /* сдвиг для тряски экрана */

static u32 mix(u32 d, u32 s, int a)
{
    int dr = d & 255, dg = (d >> 8) & 255, db = (d >> 16) & 255;
    int sr = s & 255, sg = (s >> 8) & 255, sb = (s >> 16) & 255;
    dr += ((sr - dr) * a) >> 8;
    dg += ((sg - dg) * a) >> 8;
    db += ((sb - db) * a) >> 8;
    return RGB(dr, dg, db);
}

static void rect(int x, int y, int w, int h, u32 c, int a)
{
    int i, j;
    x += ox; y += oy;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    if (w <= 0 || h <= 0 || a <= 0) return;
    for (j = 0; j < h; j++) {
        u32 *p = T + (y + j) * BUF_W + x;
        if (a >= 256) for (i = 0; i < w; i++) p[i] = c;
        else          for (i = 0; i < w; i++) p[i] = mix(p[i], c, a);
    }
}
#define fill(x, y, w, h, c)      rect(x, y, w, h, c, 256)
#define tint(x, y, w, h, c, a)   rect(x, y, w, h, c, a)

static void outline(int x, int y, int w, int h, u32 c, int a)
{
    rect(x, y, w, 1, c, a);
    rect(x, y + h - 1, w, 1, c, a);
    rect(x, y + 1, 1, h - 2, c, a);
    rect(x + w - 1, y + 1, 1, h - 2, c, a);
}

static void disc(int cx, int cy, int r, u32 c, int a)
{
    int dy;
    for (dy = -r; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r * r - dy * dy) + 0.3f);
        rect(cx - dx, cy + dy, 2 * dx + 1, 1, c, a);
    }
}

/* капсула: прямоугольник со скруглёнными концами */
static void capsule(int x, int y, int w, int h, u32 c, int a)
{
    int r = h / 2;
    rect(x + r, y, w - 2 * r, h, c, a);
    disc(x + r, y + r, r, c, a);
    disc(x + w - 1 - r, y + r, r, c, a);
}

static const unsigned char font5x7[59][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x56,0x20,0x50}, {0x00,0x08,0x07,0x03,0x00}, {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00}, {0x2A,0x1C,0x7F,0x1C,0x2A}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x80,0x70,0x30,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x00,0x60,0x60,0x00},
    {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x72,0x49,0x49,0x49,0x46}, {0x21,0x41,0x49,0x4D,0x33}, {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x31}, {0x41,0x21,0x11,0x09,0x07},
    {0x36,0x49,0x49,0x49,0x36}, {0x46,0x49,0x49,0x29,0x1E}, {0x00,0x00,0x14,0x00,0x00},
    {0x00,0x40,0x34,0x00,0x00}, {0x00,0x08,0x14,0x22,0x41}, {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x59,0x09,0x06}, {0x3E,0x41,0x5D,0x59,0x4E},
    {0x7C,0x12,0x11,0x12,0x7C}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x41,0x3E}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x41,0x51,0x73}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x1C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x26,0x49,0x49,0x49,0x32}, {0x03,0x01,0x7F,0x01,0x03}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03}, {0x61,0x59,0x49,0x4D,0x43},
};

static void text(int x, int y, const char *s, int sc, u32 c)
{
    for (; *s; s++, x += 6 * sc) {
        int ch = (unsigned char)*s, col, row;
        const unsigned char *g;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 90) continue;
        g = font5x7[ch - 32];
        for (col = 0; col < 5; col++)
            for (row = 0; row < 7; row++)
                if ((g[col] >> row) & 1)
                    rect(x + col * sc, y + row * sc, sc, sc, c, 256);
    }
}
static int text_w(const char *s, int sc) { int n = (int)strlen(s); return n ? n * 6 * sc - sc : 0; }
static void text_c(int cx, int y, const char *s, int sc, u32 c) { text(cx - text_w(s, sc) / 2, y, s, sc, c); }

/* заголовок с «хроматическим» сдвигом, как в меню Neon Arcade */
static void title_c(int cx, int y, const char *s, int sc, u32 c)
{
    int sh = sc / 2 < 1 ? 1 : sc / 2;
    text_c(cx + sh, y, s, sc, C_MAGENTA);
    text_c(cx - sh, y, s, sc, C_CYAN);
    text_c(cx, y, s, sc, c);
}

/* ------------------------------------------------------------------ */
/* Фон: собирается один раз                                            */
/* ------------------------------------------------------------------ */
static void build_bg(void)
{
    int x, y;
    T = bgbuf; ox = oy = 0;
    for (y = 0; y < SCR_H; y++) {
        for (x = 0; x < SCR_W; x++) {
            u32 c = mix(RGB(12, 9, 28), RGB(4, 4, 10), y * 256 / SCR_H);
            float d1 = sqrtf((float)(x * x + y * y)) / 360.0f;
            float d2 = sqrtf((float)((SCR_W - 1 - x) * (SCR_W - 1 - x) + (SCR_H - 1 - y) * (SCR_H - 1 - y))) / 360.0f;
            if (d1 < 1.0f) c = mix(c, C_MAGENTA, (int)((1.0f - d1) * 0.16f * 256));
            if (d2 < 1.0f) c = mix(c, C_CYAN,    (int)((1.0f - d2) * 0.12f * 256));
            if ((x % 16) == 0 || (y % 16) == 0) c = mix(c, C_WHITE, 6);
            bgbuf[y * BUF_W + x] = c;
        }
    }
    tint(FX0, FY0, FX1 - FX0, SCR_H - FY0, C_BLACK, 110);          /* поле чуть темнее */
    tint(0, 0, SCR_W, FY0 - 4, C_BLACK, 120);                       /* табло */

    /* неоновые стенки: левая голубая, правая розовая, потолок между ними */
    tint(FX0 - 7, FY0 - 3, 4, SCR_H, C_CYAN, 40);
    fill(FX0 - 3, FY0 - 3, 3, SCR_H, C_CYAN);
    tint(FX0, FY0, 3, SCR_H, C_CYAN, 35);
    tint(FX1 + 3, FY0 - 3, 4, SCR_H, C_MAGENTA, 40);
    fill(FX1, FY0 - 3, 3, SCR_H, C_MAGENTA);
    tint(FX1 - 3, FY0, 3, SCR_H, C_MAGENTA, 35);
    for (x = FX0 - 3; x < FX1 + 3; x++) {
        u32 c = mix(C_CYAN, C_MAGENTA, (x - FX0 + 3) * 256 / (FX1 - FX0 + 6));
        fill(x, FY0 - 3, 1, 3, c);
        tint(x, FY0 - 7, 1, 4, c, 40);
        tint(x, FY0, 1, 3, c, 35);
    }
    T = frame;
}

/* ------------------------------------------------------------------ */
/* Частицы и сообщения                                                 */
/* ------------------------------------------------------------------ */
static void burst(float x, float y, u32 c, int n, float speed)
{
    int i, k = 0;
    for (i = 0; i < MAX_PARTS && k < n; i++) {
        Part *p = &parts[i];
        float a, v;
        if (p->life > 0) continue;
        a = frand() * 6.2831853f;
        v = speed * (0.3f + frand() * 0.7f);
        p->x = x; p->y = y;
        p->vx = cosf(a) * v; p->vy = sinf(a) * v - speed * 0.2f;
        p->max = p->life = 0.35f + frand() * 0.4f;
        p->c = c;
        k++;
    }
}

static void update_parts(float dt)
{
    int i;
    for (i = 0; i < MAX_PARTS; i++) {
        Part *p = &parts[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vy += 260.0f * dt;                      /* искры падают */
    }
}

static void show_msg(const char *s)
{
    snprintf(msg, sizeof msg, "%s", s);
    msg_t = 1.4f;
}

/* ------------------------------------------------------------------ */
/* Сохранение рекорда                                                  */
/* ------------------------------------------------------------------ */
static void load_save(void)
{
    FILE *f = fopen(savepath, "r");
    int b = 0, l = 0;
    if (f) { if (fscanf(f, "%d %d", &b, &l) < 2) { /* битый файл — с нуля */ } fclose(f); }
    best = b < 0 ? 0 : b;
    best_level = l < 0 ? 0 : l;
}

static void write_save(void)
{
    FILE *f = fopen(savepath, "w");
    if (f) { fprintf(f, "%d %d\n", best, best_level); fclose(f); }
}

/* ------------------------------------------------------------------ */
/* Уровни, мячи, платформа                                             */
/* ------------------------------------------------------------------ */
static void load_level(int n)
{
    int r, c;
    const char *const *L = LEVELS[(n - 1) % NLEVELS];
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++) {
            char ch = L[r][c];
            bricks[r][c] = ch >= '1' && ch <= '7' ? (unsigned char)(ch - '0')
                         : ch == 'S' ? B_STRONG : ch == 'X' ? B_STEEL : B_EMPTY;
        }
}

static int breakable_left(void)
{
    int r, c, n = 0;
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++)
            if (bricks[r][c] != B_EMPTY && bricks[r][c] != B_STEEL) n++;
    return n;
}

static float cur_speed(void)
{
    float base = 200.0f + 12.0f * (float)(level - 1);
    if (base > 300.0f) base = 300.0f;
    return base * speed_mul * (t_slow > 0 ? 0.7f : 1.0f);
}

static int balls_alive(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_BALLS; i++) n += balls[i].alive;
    return n;
}

static int any_stuck(void)
{
    int i;
    for (i = 0; i < MAX_BALLS; i++) if (balls[i].alive && balls[i].stuck) return 1;
    return 0;
}

static void spawn_stuck_ball(void)
{
    memset(balls, 0, sizeof balls);
    balls[0].alive = 1;
    balls[0].stuck = 1;
    balls[0].stick = (frand() - 0.5f) * 12.0f;
    balls[0].x = pad_x + balls[0].stick;
    balls[0].y = PADDLE_Y - BALL_R - 1;
}

static void launch(Ball *b)
{
    /* угол зависит от того, где мяч лежит на платформе: от -60 до 60 градусов от вертикали */
    float rel = b->stick / (pad_w * 0.5f), a, s = cur_speed();
    if (rel < -1) rel = -1;
    if (rel > 1) rel = 1;
    a = rel * 1.05f;
    if (fabsf(a) < 0.12f) a = (frand() < 0.5f ? -0.25f : 0.25f);
    b->vx = s * sinf(a);
    b->vy = -s * cosf(a);
    b->stuck = 0;
}

static void launch_stuck(void)
{
    int i;
    for (i = 0; i < MAX_BALLS; i++)
        if (balls[i].alive && balls[i].stuck) launch(&balls[i]);
}

static void clear_flying(void)
{
    memset(drops, 0, sizeof drops);
    memset(bolts, 0, sizeof bolts);
    t_wide = t_slow = t_laser = t_catch = laser_cd = 0;
}

static void start_level(void)
{
    load_level(level);
    clear_flying();
    speed_mul = 1.0f;
    pad_x = (FX0 + FX1) * 0.5f;
    spawn_stuck_ball();
    state = ST_PLAY;
}

static void new_game(void)
{
    score = 0;
    lives = START_LIVES;
    level = 1;
    new_best = 0;
    pad_w = PAD_NORMAL;
    memset(parts, 0, sizeof parts);
    start_level();
    show_msg("LEVEL 1");
}

static void go_title(void)
{
    state = ST_TITLE;
    level = 1;
    load_level(1);
    memset(balls, 0, sizeof balls);
    clear_flying();
}

static void game_over(void)
{
    state = ST_OVER;
    state_t = 0;
    if (score > best) { best = score; new_best = 1; }
    if (level > best_level) best_level = level;
    write_save();
}

static void lose_life(void)
{
    lives--;
    shake = 0.35f;
    flash = 1.0f;
    clear_flying();
    if (lives <= 0) { game_over(); return; }
    spawn_stuck_ball();
    show_msg(lives == 1 ? "LAST BALL!" : "BALL LOST");
}

static void level_clear(void)
{
    state = ST_CLEAR;
    state_t = 2.0f;
    score += 500 + 100 * level;
    memset(balls, 0, sizeof balls);
    clear_flying();
    burst((FX0 + FX1) * 0.5f, 120, C_YELLOW, 60, 220);
}

/* ------------------------------------------------------------------ */
/* Бонусы                                                              */
/* ------------------------------------------------------------------ */
static void spawn_drop(float x, float y)
{
    int i, t;
    u32 r = rnd() % 100;
    t = r < 24 ? P_WIDE : r < 44 ? P_MULTI : r < 59 ? P_SLOW : r < 77 ? P_LASER : r < 93 ? P_CATCH : P_LIFE;
    for (i = 0; i < MAX_DROPS; i++)
        if (!drops[i].alive) {
            drops[i].alive = 1;
            drops[i].type = t;
            drops[i].x = x;
            drops[i].y = y;
            return;
        }
}

static void apply_power(int t)
{
    int i, orig[MAX_BALLS];
    score += 100;
    show_msg(PNAME[t]);
    switch (t) {
    case P_WIDE:  t_wide = 15.0f; break;
    case P_SLOW:  t_slow = 10.0f; break;
    case P_LASER: t_laser = 12.0f; break;
    case P_CATCH: t_catch = 15.0f; break;
    case P_LIFE:  if (lives < MAX_LIVES) lives++; break;
    case P_MULTI:
        launch_stuck();
        /* каждый мяч, что был до бонуса, делится на три: два новых — под углом ±20 градусов */
        for (i = 0; i < MAX_BALLS; i++) orig[i] = balls[i].alive;
        for (i = 0; i < MAX_BALLS; i++) {
            int j, side;
            Ball src;
            if (!orig[i]) continue;
            src = balls[i];
            for (side = -1; side <= 1; side += 2) {
                float a = 0.35f * (float)side, ca = cosf(a), sa = sinf(a);
                for (j = 0; j < MAX_BALLS; j++)
                    if (!balls[j].alive) {
                        balls[j] = src;
                        balls[j].vx = src.vx * ca - src.vy * sa;
                        balls[j].vy = src.vx * sa + src.vy * ca;
                        break;
                    }
            }
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Удары по кирпичам                                                   */
/* ------------------------------------------------------------------ */
static void hit_brick(int r, int c)
{
    int code = bricks[r][c];
    float cx = FX0 + c * BRICK_SX + BRICK_W * 0.5f, cy = BRICK_Y0 + r * BRICK_SY + BRICK_H * 0.5f;
    if (code == B_EMPTY) return;
    if (code == B_STEEL) { burst(cx, cy, C_WHITE, 4, 90); return; }
    if (code == B_STRONG) {
        bricks[r][c] = B_CRACK;
        score += 20;
        burst(cx, cy, RGB(196, 204, 224), 6, 110);
        return;
    }
    bricks[r][c] = B_EMPTY;
    score += code == B_CRACK ? 100 : 50;
    burst(cx, cy, code == B_CRACK ? RGB(196, 204, 224) : PCOL[code - 1], 14, 150);
    if (speed_mul < 1.35f) speed_mul += 0.004f;
    if (frand() < 0.14f) spawn_drop(cx, cy);
}

/* Мяч против кирпичей: выталкиваем по оси наименьшего перекрытия и отражаем. */
static int collide_bricks(Ball *b)
{
    float l = b->x - BALL_R, rr = b->x + BALL_R, t = b->y - BALL_R, bt = b->y + BALL_R;
    int c0 = (int)floorf((l - FX0) / BRICK_SX), c1 = (int)floorf((rr - FX0) / BRICK_SX);
    int r0 = (int)floorf((t - BRICK_Y0) / BRICK_SY), r1 = (int)floorf((bt - BRICK_Y0) / BRICK_SY);
    int r, c;
    if (c0 < 0) c0 = 0;
    if (c1 > COLS - 1) c1 = COLS - 1;
    if (r0 < 0) r0 = 0;
    if (r1 > ROWS - 1) r1 = ROWS - 1;
    for (r = r0; r <= r1; r++)
        for (c = c0; c <= c1; c++) {
            float bx, by, ovx, ovy;
            if (bricks[r][c] == B_EMPTY) continue;
            bx = (float)(FX0 + c * BRICK_SX);
            by = (float)(BRICK_Y0 + r * BRICK_SY);
            ovx = minf(rr, bx + BRICK_W) - maxf(l, bx);
            ovy = minf(bt, by + BRICK_H) - maxf(t, by);
            if (ovx <= 0 || ovy <= 0) continue;
            if (ovx < ovy) {
                if (b->x < bx + BRICK_W * 0.5f) { b->x -= ovx; b->vx = -fabsf(b->vx); }
                else                            { b->x += ovx; b->vx =  fabsf(b->vx); }
            } else {
                if (b->y < by + BRICK_H * 0.5f) { b->y -= ovy; b->vy = -fabsf(b->vy); }
                else                            { b->y += ovy; b->vy =  fabsf(b->vy); }
            }
            hit_brick(r, c);
            return 1;
        }
    return 0;
}

static void bounce_paddle(Ball *b)
{
    float rel = (b->x - pad_x) / (pad_w * 0.5f), a, s = cur_speed();
    b->y = PADDLE_Y - BALL_R;
    if (t_catch > 0) {
        b->stuck = 1;
        b->stick = b->x - pad_x;
        b->vx = b->vy = 0;
        return;
    }
    if (rel < -1) rel = -1;
    if (rel > 1) rel = 1;
    a = rel * 1.05f;
    b->vx = s * sinf(a);
    b->vy = -s * cosf(a);
    burst(b->x, (float)PADDLE_Y, C_CYAN, 4, 70);
}

static void update_balls(float dt)
{
    int i, k;
    float s = cur_speed();
    for (i = 0; i < MAX_BALLS; i++) {
        Ball *b = &balls[i];
        float len, n_steps, step;
        int n;
        if (!b->alive) continue;
        if (b->stuck) {
            float lim = pad_w * 0.5f - BALL_R;
            if (b->stick < -lim) b->stick = -lim;
            if (b->stick > lim) b->stick = lim;
            b->x = pad_x + b->stick;
            b->y = PADDLE_Y - BALL_R - 1;
            continue;
        }
        /* держим скорость постоянной и не даём мячу лететь почти горизонтально */
        len = sqrtf(b->vx * b->vx + b->vy * b->vy);
        if (len < 1e-3f) { b->vx = 0; b->vy = -s; len = s; }
        b->vx *= s / len;
        b->vy *= s / len;
        if (fabsf(b->vy) < 0.3f * s) {
            float sy = b->vy < 0 ? -1.0f : 1.0f, sx = b->vx < 0 ? -1.0f : 1.0f;
            b->vy = sy * 0.3f * s;
            b->vx = sx * sqrtf(s * s - b->vy * b->vy);
        }
        /* мелкие шаги, чтобы быстрый мяч не проскакивал сквозь кирпичи */
        n_steps = s * dt / 2.0f;
        n = (int)n_steps + 1;
        step = dt / (float)n;
        for (k = 0; k < n; k++) {
            b->x += b->vx * step;
            b->y += b->vy * step;
            if (b->x - BALL_R < FX0) { b->x = (float)(FX0 + BALL_R); b->vx = fabsf(b->vx); }
            if (b->x + BALL_R > FX1) { b->x = (float)(FX1 - BALL_R); b->vx = -fabsf(b->vx); }
            if (b->y - BALL_R < FY0) { b->y = (float)(FY0 + BALL_R); b->vy = fabsf(b->vy); }
            if (b->vy > 0 && b->y + BALL_R >= PADDLE_Y && b->y + BALL_R <= PADDLE_Y + PADDLE_H + 4 &&
                b->x >= pad_x - pad_w * 0.5f - BALL_R && b->x <= pad_x + pad_w * 0.5f + BALL_R) {
                bounce_paddle(b);
                if (b->stuck) break;
            }
            collide_bricks(b);
            if (b->y - BALL_R > SCR_H) { b->alive = 0; break; }
        }
    }
}

static void update_drops(float dt)
{
    int i;
    for (i = 0; i < MAX_DROPS; i++) {
        Drop *d = &drops[i];
        if (!d->alive) continue;
        d->y += 75.0f * dt;
        if (d->y + 4 >= PADDLE_Y && d->y - 4 <= PADDLE_Y + PADDLE_H &&
            fabsf(d->x - pad_x) <= pad_w * 0.5f + 9) {
            d->alive = 0;
            burst(d->x, d->y, PDCOL[d->type], 12, 120);
            apply_power(d->type);
        } else if (d->y > SCR_H + 8) {
            d->alive = 0;
        }
    }
}

static void fire_laser(void)
{
    int i, fired = 0;
    float xs[2];
    xs[0] = pad_x - pad_w * 0.5f + 4;
    xs[1] = pad_x + pad_w * 0.5f - 4;
    for (i = 0; i < MAX_BOLTS && fired < 2; i++)
        if (!bolts[i].alive) {
            bolts[i].alive = 1;
            bolts[i].x = xs[fired];
            bolts[i].y = PADDLE_Y - 2;
            fired++;
        }
    laser_cd = 0.25f;
}

static void update_bolts(float dt)
{
    int i;
    for (i = 0; i < MAX_BOLTS; i++) {
        Bolt *b = &bolts[i];
        int c, r;
        float lx, ly;
        if (!b->alive) continue;
        b->y -= 430.0f * dt;
        if (b->y < FY0) { b->alive = 0; continue; }
        c = (int)floorf((b->x - FX0) / BRICK_SX);
        r = (int)floorf((b->y - BRICK_Y0) / BRICK_SY);
        if (c < 0 || c >= COLS || r < 0 || r >= ROWS) continue;
        lx = b->x - (FX0 + c * BRICK_SX);
        ly = b->y - (BRICK_Y0 + r * BRICK_SY);
        if (lx < BRICK_W && ly < BRICK_H && bricks[r][c] != B_EMPTY) {
            hit_brick(r, c);
            b->alive = 0;
        }
    }
}

static void move_paddle(float dt, unsigned held, float ax)
{
    float v = 0, target = t_wide > 0 ? PAD_WIDE : PAD_NORMAL, lo, hi;
    if (held & IN_LEFT)  v -= 330.0f;
    if (held & IN_RIGHT) v += 330.0f;
    if (v == 0) v = ax * 380.0f;
    pad_x += v * dt;
    /* плавно меняем ширину при «расширении» */
    if (pad_w < target) { pad_w += 160.0f * dt; if (pad_w > target) pad_w = target; }
    if (pad_w > target) { pad_w -= 160.0f * dt; if (pad_w < target) pad_w = target; }
    lo = FX0 + pad_w * 0.5f;
    hi = FX1 - pad_w * 0.5f;
    if (pad_x < lo) pad_x = lo;
    if (pad_x > hi) pad_x = hi;
}

static void play_update(float dt, unsigned held, unsigned pressed, float ax)
{
    if (pressed & IN_START) { state = ST_PAUSE; return; }
    move_paddle(dt, held, ax);

    if (t_wide > 0) t_wide -= dt;
    if (t_slow > 0) t_slow -= dt;
    if (t_laser > 0) t_laser -= dt;
    if (laser_cd > 0) laser_cd -= dt;
    if (t_catch > 0) { t_catch -= dt; if (t_catch <= 0) launch_stuck(); }

    if (pressed & IN_CROSS) {
        if (any_stuck()) launch_stuck();
        else if (t_laser > 0 && laser_cd <= 0) fire_laser();
    } else if ((held & IN_CROSS) && t_laser > 0 && laser_cd <= 0 && !any_stuck()) {
        fire_laser();                                   /* зажатый X — стреляет очередью */
    }

    update_balls(dt);
    update_drops(dt);
    update_bolts(dt);

    if (breakable_left() == 0) { level_clear(); return; }
    if (balls_alive() == 0) lose_life();
}

/* ------------------------------------------------------------------ */
/* Публичный интерфейс                                                 */
/* ------------------------------------------------------------------ */
void breakout_set_save_dir(const char *dir)
{
    snprintf(savepath, sizeof savepath, "%sbreakout.sav", dir);
}

void breakout_init(u32 seed)
{
    rng = seed | 1u;
    load_save();
    build_bg();
    pad_x = (FX0 + FX1) * 0.5f;
    pad_w = PAD_NORMAL;
    go_title();
}

void breakout_update(double dt_ms, unsigned held, unsigned pressed, float ax)
{
    float dt = (float)(dt_ms / 1000.0);
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0) dt = 0;
    gt += dt;
    update_parts(dt);
    if (shake > 0) shake -= dt;
    if (flash > 0) flash -= dt * 2.5f;
    if (msg_t > 0) msg_t -= dt;

    switch (state) {
    case ST_TITLE:
        if (pressed & (IN_CROSS | IN_START)) new_game();
        break;
    case ST_PAUSE:
        if (pressed & IN_START) state = ST_PLAY;
        else if (pressed & IN_SELECT) { game_over(); go_title(); }
        break;
    case ST_CLEAR:
        move_paddle(dt, held, ax);
        state_t -= dt;
        if (state_t <= 0) {
            char buf[24];
            level++;
            start_level();
            snprintf(buf, sizeof buf, "LEVEL %d", level);
            show_msg(buf);
        }
        break;
    case ST_OVER:
        state_t += dt;
        if (state_t > 0.6f) {                           /* чтобы не начать случайно */
            if (pressed & (IN_CROSS | IN_START)) new_game();
            else if (pressed & IN_SELECT) go_title();
        }
        break;
    default:
        play_update(dt, held, pressed, ax);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Отрисовка                                                           */
/* ------------------------------------------------------------------ */
static void draw_brick(int x, int y, int code)
{
    int i;
    u32 c;
    if (code == B_STEEL) {
        fill(x, y, BRICK_W, BRICK_H, RGB(58, 62, 80));
        tint(x, y, BRICK_W, 2, C_WHITE, 70);
        fill(x, y + BRICK_H - 1, BRICK_W, 1, RGB(28, 30, 40));
        for (i = 4; i < BRICK_W - 4; i += 5) tint(x + i, y + 3, 2, BRICK_H - 5, C_WHITE, 22);
        fill(x + 2, y + 4, 2, 2, RGB(150, 156, 180));
        fill(x + BRICK_W - 4, y + 4, 2, 2, RGB(150, 156, 180));
        return;
    }
    c = (code == B_STRONG || code == B_CRACK) ? RGB(196, 204, 224) : PCOL[code - 1];
    tint(x - 1, y - 1, BRICK_W + 2, BRICK_H + 2, c, 40);                 /* свечение */
    fill(x, y, BRICK_W, BRICK_H, mix(c, C_BLACK, 80));
    fill(x + 1, y + 1, BRICK_W - 2, BRICK_H - 2, c);
    tint(x + 1, y + 1, BRICK_W - 2, 2, C_WHITE, 115);                    /* блик сверху */
    tint(x + 1, y + BRICK_H - 3, BRICK_W - 2, 2, C_BLACK, 70);           /* тень снизу */
    if (code == B_STRONG || code == B_CRACK)
        outline(x + 3, y + 3, BRICK_W - 6, BRICK_H - 6, RGB(110, 120, 150), 220);
    if (code == B_CRACK) {                                               /* трещина */
        u32 k = RGB(40, 44, 60);
        fill(x + 9, y + 1, 1, 3, k);  fill(x + 10, y + 3, 1, 3, k); fill(x + 11, y + 5, 3, 1, k);
        fill(x + 14, y + 6, 1, 3, k); fill(x + 21, y + 2, 1, 4, k); fill(x + 19, y + 5, 2, 1, k);
    }
}

static void draw_bricks(void)
{
    int r, c;
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++)
            if (bricks[r][c] != B_EMPTY)
                draw_brick(FX0 + c * BRICK_SX + 1, BRICK_Y0 + r * BRICK_SY, bricks[r][c]);
}

static void draw_paddle(void)
{
    int w = (int)(pad_w + 0.5f), x = (int)(pad_x - pad_w * 0.5f + 0.5f), y = PADDLE_Y, i;
    float pulse = (sinf(gt * 5.0f) + 1.0f) * 0.5f;
    for (i = 1; i <= 4; i++)                                             /* мягкое свечение */
        capsule(x - i, y - i, w + 2 * i, PADDLE_H + 2 * i, C_CYAN, 22 - i * 4);
    for (i = 0; i < w; i++) {                                            /* градиент голубой -> розовый */
        u32 c = mix(C_CYAN, C_MAGENTA, i * 256 / (w > 1 ? w - 1 : 1));
        int edge = (i < 3 || i >= w - 3) ? 1 : 0;
        fill(x + i, y + edge, 1, PADDLE_H - 2 * edge, c);
    }
    tint(x + 3, y + 1, w - 6, 2, C_WHITE, 140);
    tint(x + 3, y + PADDLE_H - 2, w - 6, 1, C_BLACK, 90);
    if (t_laser > 0) {                                                   /* пушки по краям */
        fill(x + 2, y - 4, 4, 4, C_RED);
        fill(x + w - 6, y - 4, 4, 4, C_RED);
        tint(x + 1, y - 6, 6, 2, C_RED, (int)(80 + pulse * 120));
        tint(x + w - 7, y - 6, 6, 2, C_RED, (int)(80 + pulse * 120));
    }
    if (t_catch > 0)                                                     /* «липкая» полоса */
        tint(x + 6, y + 2, w - 12, 3, C_YELLOW, (int)(120 + pulse * 110));
}

static void draw_ball(int cx, int cy, int a)
{
    static const int span[6] = { 2, 4, 6, 6, 4, 2 };
    int j;
    disc(cx, cy, 6, C_CYAN, a * 30 / 256);
    disc(cx, cy, 4, C_CYAN, a * 60 / 256);
    for (j = 0; j < 6; j++) rect(cx - span[j] / 2, cy - 3 + j, span[j], 1, C_WHITE, a);
}

static void draw_balls(void)
{
    int i;
    for (i = 0; i < MAX_BALLS; i++) {
        Ball *b = &balls[i];
        if (!b->alive) continue;
        if (!b->stuck) {                                                  /* шлейф */
            draw_ball((int)(b->x - b->vx * 0.024f), (int)(b->y - b->vy * 0.024f), 40);
            draw_ball((int)(b->x - b->vx * 0.012f), (int)(b->y - b->vy * 0.012f), 90);
        }
        draw_ball((int)(b->x + 0.5f), (int)(b->y + 0.5f), 256);
    }
}

static void draw_drops(void)
{
    int i;
    char s[2] = { 0, 0 };
    for (i = 0; i < MAX_DROPS; i++) {
        Drop *d = &drops[i];
        int x, y;
        u32 c;
        if (!d->alive) continue;
        x = (int)d->x - 10; y = (int)d->y - 4;
        c = PDCOL[d->type];
        capsule(x - 2, y - 2, 24, 13, c, 50);
        capsule(x, y, 20, 9, c, 256);
        tint(x + 3, y + 1, 14, 2, C_WHITE, 120);
        s[0] = PCHAR[d->type];
        text(x + 8, y + 1, s, 1, C_BLACK);
    }
}

static void draw_bolts(void)
{
    int i;
    for (i = 0; i < MAX_BOLTS; i++) {
        if (!bolts[i].alive) continue;
        tint((int)bolts[i].x - 2, (int)bolts[i].y - 4, 5, 10, C_RED, 70);
        fill((int)bolts[i].x - 1, (int)bolts[i].y - 3, 2, 8, RGB(255, 200, 160));
    }
}

static void draw_parts(void)
{
    int i;
    for (i = 0; i < MAX_PARTS; i++) {
        Part *p = &parts[i];
        if (p->life <= 0) continue;
        rect((int)p->x, (int)p->y, 2, 2, p->c, (int)(256 * p->life / p->max));
    }
}

static void draw_hud(void)
{
    char buf[32];
    int i;
    text(10, 7, "SCORE", 1, C_MUTED);
    snprintf(buf, sizeof buf, "%06d", score);
    text(44, 7, buf, 1, C_YELLOW);
    snprintf(buf, sizeof buf, "LEVEL %02d", level);
    text_c(SCR_W / 2, 7, buf, 1, C_CYAN);
    for (i = 0; i < lives && i < MAX_LIVES; i++) {                       /* жизни — мини-платформы */
        int x = SCR_W - 22 - i * 18;
        capsule(x, 8, 14, 5, C_MAGENTA, 256);
        tint(x + 2, 8, 10, 1, C_WHITE, 120);
    }
    /* активные бонусы — значки с полоской оставшегося времени */
    {
        float tl[4];
        int types[4], n = 0, k;
        if (t_wide > 0)  { types[n] = P_WIDE;  tl[n++] = t_wide / 15.0f; }
        if (t_slow > 0)  { types[n] = P_SLOW;  tl[n++] = t_slow / 10.0f; }
        if (t_laser > 0) { types[n] = P_LASER; tl[n++] = t_laser / 12.0f; }
        if (t_catch > 0) { types[n] = P_CATCH; tl[n++] = t_catch / 15.0f; }
        for (k = 0; k < n; k++) {
            char s[2] = { 0, 0 };
            int x = FX0 + 6 + k * 34, y = SCR_H - 12;
            capsule(x, y, 14, 9, PDCOL[types[k]], 256);
            s[0] = PCHAR[types[k]];
            text(x + 5, y + 1, s, 1, C_BLACK);
            fill(x + 17, y + 3, 14, 3, RGB(40, 40, 60));
            fill(x + 17, y + 3, (int)(14 * tl[k] + 0.5f), 3, PDCOL[types[k]]);
        }
    }
}

static void band(int y, int h)
{
    tint(0, y, SCR_W, h, C_BLACK, 170);
    tint(0, y, SCR_W, 1, C_CYAN, 160);
    tint(0, y + h - 1, SCR_W, 1, C_MAGENTA, 160);
}

static int blink(void) { return ((int)(gt * 2.4f)) % 2 == 0; }

static void draw_title(void)
{
    char buf[48];
    tint(FX0, FY0, FX1 - FX0, SCR_H - FY0, C_BLACK, 120);
    band(78, 120);
    title_c(SCR_W / 2, 90, "NEON", 3, C_CYAN);
    title_c(SCR_W / 2, 116, "BREAKOUT", 5, C_YELLOW);
    if (blink()) text_c(SCR_W / 2, 164, "PRESS X TO START", 1, C_TEXT);
    snprintf(buf, sizeof buf, "BEST %06d   MAX LEVEL %02d", best, best_level);
    text_c(SCR_W / 2, 180, buf, 1, C_CYAN);
    text_c(SCR_W / 2, 218, "D-PAD / STICK: MOVE    X: LAUNCH / LASER", 1, C_MUTED);
    text_c(SCR_W / 2, 230, "START: PAUSE    HOME: EXIT", 1, C_MUTED);
}

const u32 *breakout_render(void)
{
    char buf[48];
    T = frame;
    memcpy(frame, bgbuf, sizeof frame);

    ox = oy = 0;
    if (shake > 0) {
        float k = shake / 0.35f;
        ox = (int)((frand() - 0.5f) * 8.0f * k);
        oy = (int)((frand() - 0.5f) * 6.0f * k);
    }
    draw_bricks();
    if (state != ST_TITLE) {
        draw_drops();
        draw_bolts();
        draw_paddle();
        draw_balls();
    }
    draw_parts();
    ox = oy = 0;

    if (state == ST_TITLE) { draw_title(); return frame; }
    draw_hud();

    if (state == ST_PLAY && any_stuck() && t_catch <= 0 && blink())
        text_c(SCR_W / 2, 200, "PRESS X TO LAUNCH", 1, C_TEXT);
    if (msg_t > 0 && state == ST_PLAY) {
        int a = msg_t > 0.3f ? 256 : (int)(msg_t / 0.3f * 256);
        int w = text_w(msg, 2);
        tint(SCR_W / 2 - w / 2 - 8, 164, w + 16, 22, C_BLACK, a * 150 / 256);
        text_c(SCR_W / 2 + 1, 168, msg, 2, mix(C_BLACK, C_MAGENTA, a));
        text_c(SCR_W / 2, 168, msg, 2, mix(C_BLACK, C_YELLOW, a));
    }
    if (state == ST_PAUSE) {
        band(96, 80);
        title_c(SCR_W / 2, 108, "PAUSED", 4, C_YELLOW);
        text_c(SCR_W / 2, 146, "START: RESUME", 1, C_TEXT);
        text_c(SCR_W / 2, 158, "SELECT: QUIT TO TITLE", 1, C_MUTED);
    } else if (state == ST_CLEAR) {
        band(96, 74);
        title_c(SCR_W / 2, 108, "LEVEL CLEAR!", 4, C_GREEN);
        snprintf(buf, sizeof buf, "BONUS +%d", 500 + 100 * level);
        text_c(SCR_W / 2, 146, buf, 1, C_YELLOW);
    } else if (state == ST_OVER) {
        band(80, 112);
        title_c(SCR_W / 2, 92, "GAME OVER", 4, C_MAGENTA);
        snprintf(buf, sizeof buf, "SCORE %06d", score);
        text_c(SCR_W / 2, 130, buf, 2, C_YELLOW);
        if (new_best && blink()) text_c(SCR_W / 2, 152, "NEW BEST!", 1, C_GREEN);
        else if (!new_best) {
            snprintf(buf, sizeof buf, "BEST %06d", best);
            text_c(SCR_W / 2, 152, buf, 1, C_CYAN);
        }
        text_c(SCR_W / 2, 170, "X: PLAY AGAIN    SELECT: TITLE", 1, C_TEXT);
    }
    if (flash > 0) tint(0, 0, SCR_W, SCR_H, C_RED, (int)(flash * 70));
    return frame;
}
