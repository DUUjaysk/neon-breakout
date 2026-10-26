#ifndef BREAKOUT_H
#define BREAKOUT_H
#include <stdint.h>
#include "input.h"

typedef uint32_t u32;

void breakout_set_save_dir(const char *dir);   /* dir заканчивается на '/' */
void breakout_init(u32 seed);                  /* один раз при запуске */
/* dt_ms — время кадра, held/pressed — маски кнопок, analog_x — стик от -1 до 1 */
void breakout_update(double dt_ms, unsigned held, unsigned pressed, float analog_x);
const u32 *breakout_render(void);              /* кадр 512x272 (stride 512), ABGR8888 */
#endif
