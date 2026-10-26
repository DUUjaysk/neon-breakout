/*
 * NEON BREAKOUT — платформенный слой PSP: экран, кнопки, стик, выход по HOME.
 * Сама игра — в breakout.c. Основа взята из Neon Arcade.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspge.h>

#include "breakout.h"

PSP_MODULE_INFO("NeonBreakout", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1; (void)arg2; (void)common;
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    int cbid;
    (void)args; (void)argp;
    cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
}

/* папка, где лежит EBOOT.PBP (с '/' на конце) — там же хранится рекорд */
static void get_save_dir(const char *argv0, char *out, size_t n)
{
    const char *slash = argv0 ? strrchr(argv0, '/') : NULL;
    if (slash && (size_t)(slash - argv0) + 2 < n) {
        size_t len = (size_t)(slash - argv0) + 1;
        memcpy(out, argv0, len);
        out[len] = 0;
    } else {
        snprintf(out, n, "ms0:/PSP/GAME/NeonBreakout/");
    }
}

int main(int argc, char **argv)
{
    SceCtrlData pad;
    char dir[256];
    unsigned int old = 0;
    u32 *vram_cached = (u32 *)sceGeEdramGetAddr();
    u32 *vram_uncached = (u32 *)((unsigned int)vram_cached | 0x40000000);
    unsigned long long last;

    setup_callbacks();
    get_save_dir(argc > 0 ? argv[0] : NULL, dir, sizeof dir);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    sceDisplaySetMode(0, SCR_W, SCR_H);
    sceDisplaySetFrameBuf(vram_cached, BUF_W, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);

    last = (unsigned long long)sceKernelGetSystemTimeWide();
    breakout_set_save_dir(dir);
    breakout_init((u32)last);
    last = (unsigned long long)sceKernelGetSystemTimeWide();

    for (;;) {
        unsigned long long now;
        unsigned int held = 0, pressed, b;
        float ax;
        double dt;

        sceCtrlPeekBufferPositive(&pad, 1);
        b = pad.Buttons;
        if (b & PSP_CTRL_LEFT)     held |= IN_LEFT;
        if (b & PSP_CTRL_RIGHT)    held |= IN_RIGHT;
        if (b & PSP_CTRL_DOWN)     held |= IN_DOWN;
        if (b & PSP_CTRL_UP)       held |= IN_UP;
        if (b & PSP_CTRL_CROSS)    held |= IN_CROSS;
        if (b & PSP_CTRL_CIRCLE)   held |= IN_CIRCLE;
        if (b & PSP_CTRL_SQUARE)   held |= IN_SQUARE;
        if (b & PSP_CTRL_TRIANGLE) held |= IN_TRIANGLE;
        if (b & PSP_CTRL_LTRIGGER) held |= IN_LTRIG;
        if (b & PSP_CTRL_RTRIGGER) held |= IN_RTRIG;
        if (b & PSP_CTRL_START)    held |= IN_START;
        if (b & PSP_CTRL_SELECT)   held |= IN_SELECT;

        /* стик по горизонтали: плавная скорость, мёртвая зона в центре */
        ax = ((float)pad.Lx - 128.0f) / 127.0f;
        if (ax > -0.25f && ax < 0.25f) ax = 0;
        else ax = (ax > 0 ? ax - 0.25f : ax + 0.25f) / 0.75f;
        if (ax > 1) ax = 1;
        if (ax < -1) ax = -1;

        pressed = held & ~old;
        old = held;

        now = (unsigned long long)sceKernelGetSystemTimeWide();
        dt = (double)(now - last) / 1000.0;
        last = now;
        if (dt > 100.0) dt = 100.0;

        breakout_update(dt, held, pressed, ax);

        sceDisplayWaitVblankStart();
        memcpy(vram_uncached, breakout_render(), SCR_H * BUF_W * sizeof(u32));
    }
    return 0;
}
