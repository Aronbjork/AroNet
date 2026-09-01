#ifndef SCREEN_ORIENTATION_H
#define SCREEN_ORIENTATION_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCREEN_ORIENTATION_STANDARD = 0,
    SCREEN_ORIENTATION_FLIPPED = 1
} screen_orientation_t;

bool screen_orientation_init(void);
bool screen_orientation_set(screen_orientation_t orientation);
screen_orientation_t screen_orientation_get_current(void);

#ifdef __cplusplus
}
#endif

#endif // SCREEN_ORIENTATION_H
