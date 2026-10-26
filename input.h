#ifndef INPUT_H
#define INPUT_H
/* Маска кнопок: платформенный слой собирает её из кнопок PSP,
   игра сама решает, что означает каждая кнопка. */
enum {
    IN_LEFT     = 1 << 0,
    IN_RIGHT    = 1 << 1,
    IN_DOWN     = 1 << 2,
    IN_UP       = 1 << 3,
    IN_CROSS    = 1 << 5,
    IN_CIRCLE   = 1 << 6,
    IN_SQUARE   = 1 << 7,
    IN_TRIANGLE = 1 << 8,
    IN_LTRIG    = 1 << 9,
    IN_RTRIG    = 1 << 10,
    IN_START    = 1 << 11,
    IN_SELECT   = 1 << 12
};
#endif
