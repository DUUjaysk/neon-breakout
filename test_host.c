/*
 * Проверка NEON BREAKOUT на ПК (без PSP).
 *   tcc -o test.exe test_host.c        (Windows, TCC)
 *   gcc -O1 -o test test_host.c -lm    (Linux, GitHub)
 * Робот-игрок проходит уровни, проверки следят за мячом, бонусами и сохранением.
 * Кадры игры сохраняются в out/*.bmp.
 */
#define HOST_TEST
#include "breakout.c"

#include <assert.h>

#define DT (1000.0 / 60.0)

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__); failures++; } } while (0)

static void write_bmp_px(const char *name, const u32 *px)
{
    char path[256];
    FILE *f;
    int x, y, row = SCR_W * 3, size = row * SCR_H + 54;
    unsigned char h[54];
    snprintf(path, sizeof path, "out/%s", name);
    f = fopen(path, "wb");
    if (!f) { printf("не удалось записать %s\n", path); return; }
    memset(h, 0, sizeof h);
    h[0] = 'B'; h[1] = 'M';
    h[2] = (unsigned char)size; h[3] = (unsigned char)(size >> 8); h[4] = (unsigned char)(size >> 16);
    h[10] = 54; h[14] = 40;
    h[18] = (unsigned char)SCR_W; h[19] = (unsigned char)(SCR_W >> 8);
    h[22] = (unsigned char)SCR_H; h[23] = (unsigned char)(SCR_H >> 8);
    h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (y = SCR_H - 1; y >= 0; y--)
        for (x = 0; x < SCR_W; x++) {
            u32 c = px[y * BUF_W + x];
            unsigned char bgr[3];
            bgr[0] = (unsigned char)(c >> 16); bgr[1] = (unsigned char)(c >> 8); bgr[2] = (unsigned char)c;
            fwrite(bgr, 1, 3, f);
        }
    fclose(f);
}

static void write_bmp(const char *name) { write_bmp_px(name, breakout_render()); }

/* Картинки для меню PSP: значок (потом уменьшается до 144x80) и фон PIC1 без надписей. */
static void art_shots(void)
{
    go_title();
    load_level(2);
    T = frame; ox = oy = 0;
    memcpy(frame, bgbuf, sizeof frame);
    draw_bricks();
    pad_x = 240; pad_w = 90; t_laser = t_catch = 0;
    draw_paddle();
    draw_ball(300, 205, 256);
    draw_ball(292, 213, 90);
    write_bmp_px("pic1.bmp", frame);

    memcpy(frame, bgbuf, sizeof frame);
    load_level(1);
    draw_bricks();
    tint(0, 120, SCR_W, 110, C_BLACK, 150);
    title_c(SCR_W / 2, 136, "BREAKOUT", 7, C_YELLOW);
    pad_x = 240; pad_w = 120;
    draw_paddle();
    draw_ball(330, 228, 256);
    write_bmp_px("icon.bmp", frame);
    go_title();
}

static void step(unsigned held, unsigned pressed, float ax)
{
    breakout_update(DT, held, pressed, ax);
}

static int finite_ok(float v) { return v == v && v > -1e6f && v < 1e6f; }

/* Робот: ведёт платформу под самым низким мячом, летящим вниз, чуть смещаясь,
   чтобы менять угол отскока; нажимает X, когда мяч на платформе или есть лазер. */
static void robot(float t, unsigned *held, unsigned *pressed, float *ax)
{
    int i, bi = -1;
    float target = pad_x, d;
    for (i = 0; i < MAX_BALLS; i++) {
        if (!balls[i].alive || balls[i].stuck) continue;
        if (bi < 0 || (balls[i].vy > 0 && balls[i].y > balls[bi].y) || balls[bi].vy <= 0) bi = i;
    }
    if (bi >= 0) target = balls[bi].x + sinf(t * 0.7f) * pad_w * 0.3f;
    else {
        for (i = 0; i < MAX_DROPS; i++) if (drops[i].alive) { target = drops[i].x; break; }
    }
    d = target - pad_x;
    *ax = d / 12.0f;
    if (*ax > 1) *ax = 1;
    if (*ax < -1) *ax = -1;
    *held = 0;
    *pressed = 0;
    if (any_stuck() && ((int)(t * 10) % 7) == 0) *pressed = IN_CROSS;
    if (t_laser > 0) *held = IN_CROSS;
}

static void test_levels(void)
{
    int l, r, c;
    for (l = 0; l < NLEVELS; l++) {
        int breakable = 0;
        for (r = 0; r < ROWS; r++) {
            CHECK(strlen(LEVELS[l][r]) == COLS, "в каждой строке уровня 14 клеток");
            for (c = 0; c < COLS && LEVELS[l][r][c]; c++) {
                char ch = LEVELS[l][r][c];
                CHECK(ch == '.' || ch == 'S' || ch == 'X' || (ch >= '1' && ch <= '7'), "только известные клетки");
                if (ch != '.' && ch != 'X') breakable++;
            }
        }
        CHECK(breakable > 0, "на уровне есть что разбивать");
    }
    printf("уровни: %d шт., разметка в порядке\n", NLEVELS);
}

static void test_robot_play(void)
{
    float t = 0;
    int frames = 0, max_level = 1, games = 1, lost = 0, prev_lives, bad = 0;
    unsigned held, pressed;
    float ax;
    breakout_update(DT, 0, IN_CROSS, 0);                 /* с титульного экрана — в игру */
    CHECK(state == ST_PLAY, "X на титульном экране начинает игру");
    CHECK(any_stuck(), "мяч лежит на платформе до запуска");
    prev_lives = lives;
    while (t < 900.0f) {                                 /* 15 минут игрового времени */
        int i;
        robot(t, &held, &pressed, &ax);
        if (state == ST_OVER && state_t > 0.7f) { pressed = IN_CROSS; games++; }
        step(held, pressed, ax);
        t += (float)(DT / 1000.0);
        frames++;
        if (frames == 200) write_bmp("play.bmp");
        if (state == ST_PLAY && lives < prev_lives) lost++;
        prev_lives = lives;
        if (level > max_level) max_level = level;
        for (i = 0; i < MAX_BALLS; i++) {
            Ball *b = &balls[i];
            if (!b->alive) continue;
            if (!finite_ok(b->x) || !finite_ok(b->y) || !finite_ok(b->vx) || !finite_ok(b->vy)) bad++;
            if (b->x < FX0 + BALL_R - 0.5f || b->x > FX1 - BALL_R + 0.5f) bad++;
            if (b->y < FY0 + BALL_R - 0.5f) bad++;
        }
        CHECK(pad_x - pad_w * 0.5f >= FX0 - 0.01f && pad_x + pad_w * 0.5f <= FX1 + 0.01f, "платформа не уходит за стенки");
        if (bad) break;
    }
    CHECK(bad == 0, "мяч всегда внутри поля и с нормальными числами");
    CHECK(max_level >= 3, "робот проходит хотя бы два уровня за 15 минут");
    printf("робот: %d кадров, уровней пройдено: %d, игр: %d, мячей потеряно: %d, лучший счёт %d\n",
           frames, max_level - 1, games, lost, best);
}

static void fresh_game(void)
{
    go_title();
    breakout_update(DT, 0, IN_CROSS, 0);
}

static void test_powers(void)
{
    int i, before;

    /* мультимяч: один мяч превращается в три */
    fresh_game();
    step(0, IN_CROSS, 0);
    for (i = 0; i < 10; i++) step(0, 0, 0);
    apply_power(P_MULTI);
    CHECK(balls_alive() == 3, "мультимяч даёт три мяча");
    apply_power(P_MULTI);
    CHECK(balls_alive() == MAX_BALLS, "повторный мультимяч упирается в предел мячей");

    /* широкая платформа плавно расширяется и потом сужается */
    fresh_game();
    apply_power(P_WIDE);
    for (i = 0; i < 60; i++) step(0, 0, 0);
    CHECK(pad_w == PAD_WIDE, "бонус E расширяет платформу");
    for (i = 0; i < 60 * 16; i++) step(0, 0, 0);
    CHECK(pad_w == PAD_NORMAL, "через 15 секунд платформа снова обычная");

    /* лазер разбивает кирпичи */
    fresh_game();
    apply_power(P_LASER);
    before = breakable_left();
    for (i = 0; i < 90; i++) step(IN_CROSS, i == 0 ? IN_CROSS : 0, 0);   /* первый X запускает мяч */
    for (i = 0; i < 60; i++) step(IN_CROSS, 0, 0);
    CHECK(breakable_left() < before, "лазер разбивает кирпичи");

    /* замедление */
    fresh_game();
    {
        float s0 = cur_speed();
        apply_power(P_SLOW);
        CHECK(cur_speed() < s0 * 0.75f, "бонус S замедляет мяч");
    }

    /* ловушка: мяч прилипает к платформе и запускается по X */
    fresh_game();
    step(0, IN_CROSS, 0);
    apply_power(P_CATCH);
    balls[0].x = pad_x; balls[0].y = PADDLE_Y - 30; balls[0].vx = 0; balls[0].vy = 200;
    for (i = 0; i < 30 && !balls[0].stuck; i++) step(0, 0, 0);
    CHECK(balls[0].stuck, "с бонусом C мяч прилипает к платформе");
    step(0, IN_CROSS, 0);
    CHECK(!balls[0].stuck, "X отпускает прилипший мяч");

    /* дополнительная жизнь, но не больше предела */
    fresh_game();
    apply_power(P_LIFE);
    CHECK(lives == START_LIVES + 1, "бонус + даёт жизнь");
    for (i = 0; i < 10; i++) apply_power(P_LIFE);
    CHECK(lives == MAX_LIVES, "жизней не больше шести");

    /* бонус ловится платформой */
    fresh_game();
    t_wide = 0;
    memset(drops, 0, sizeof drops);
    drops[0].alive = 1; drops[0].type = P_WIDE; drops[0].x = pad_x; drops[0].y = PADDLE_Y - 40;
    for (i = 0; i < 60; i++) step(0, 0, 0);
    CHECK(t_wide > 0, "падающий бонус ловится платформой");
    printf("бонусы: проверены все шесть\n");
}

static void test_states(void)
{
    int i, r, c, saved;
    FILE *f;

    /* пауза */
    fresh_game();
    step(0, IN_START, 0);
    CHECK(state == ST_PAUSE, "START ставит паузу");
    write_bmp("pause.bmp");
    step(0, IN_START, 0);
    CHECK(state == ST_PLAY, "START снимает паузу");

    /* прохождение уровня: оставляем один кирпич и разбиваем его */
    fresh_game();
    for (r = 0; r < ROWS; r++) for (c = 0; c < COLS; c++) if (bricks[r][c] != B_STEEL) bricks[r][c] = B_EMPTY;
    bricks[0][0] = 1;
    hit_brick(0, 0);
    step(0, 0, 0);
    CHECK(state == ST_CLEAR, "после последнего кирпича — LEVEL CLEAR");
    for (i = 0; i < 20; i++) step(0, 0, 0);
    write_bmp("clear.bmp");
    for (i = 0; i < 130; i++) step(0, 0, 0);
    CHECK(state == ST_PLAY && level == 2, "через 2 секунды начинается уровень 2");

    /* проигрыш и рекорд */
    fresh_game();
    score = best + 12345;
    lives = 1;
    memset(balls, 0, sizeof balls);
    step(0, 0, 0);
    CHECK(state == ST_OVER, "без мячей и жизней — GAME OVER");
    CHECK(new_best && best == score, "новый рекорд запоминается");
    for (i = 0; i < 30; i++) step(0, 0, 0);
    write_bmp("gameover.bmp");
    saved = best;
    best = 0;
    load_save();
    CHECK(best == saved, "рекорд сохраняется в файл и читается обратно");
    f = fopen("out/breakout.sav", "r");
    CHECK(f != NULL, "файл рекорда создаётся рядом с игрой");
    if (f) fclose(f);
    step(0, IN_CROSS, 0);
    CHECK(state == ST_OVER, "X сразу после проигрыша не начинает новую игру случайно");
    for (i = 0; i < 40; i++) step(0, 0, 0);
    step(0, IN_CROSS, 0);
    CHECK(state == ST_PLAY && score == 0 && lives == START_LIVES, "X после паузы начинает новую игру");
    printf("состояния: пауза, уровень, проигрыш, сохранение — в порядке\n");
}

/* кадр «в самой гуще»: несколько мячей, лазер, бонусы, искры */
static void action_shot(void)
{
    int i;
    fresh_game();
    level = 6;
    start_level();
    step(0, IN_CROSS, 0);
    for (i = 0; i < 40; i++) step(0, 0, 0);
    apply_power(P_MULTI);
    apply_power(P_LASER);
    apply_power(P_CATCH);
    t_catch = 0;
    for (i = 0; i < 25; i++) step(IN_CROSS, 0, 0);
    spawn_drop(140, 170); spawn_drop(330, 150);
    hit_brick(4, 6);
    msg_t = 0;
    write_bmp("action.bmp");
}

int main(void)
{
    breakout_set_save_dir("out/");
    remove("out/breakout.sav");
    breakout_init(12345);
    write_bmp("title.bmp");

    test_levels();
    test_robot_play();
    test_powers();
    test_states();
    action_shot();
    art_shots();

    if (failures) { printf("\nОШИБОК: %d\n", failures); return 1; }
    printf("\nВСЕ ПРОВЕРКИ ПРОЙДЕНЫ\n");
    return 0;
}
