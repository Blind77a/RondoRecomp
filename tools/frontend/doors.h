/* Smooth doors (Video > Smooth doors): past a door the camera scrolls into black while the
 * game puts the next part of the level up (its art, colours and map: the console's video
 * memory holds one part at a time), then the whole part appears at once. Instead, it fades
 * in over a few frames. Only the picture changes: the game runs and plays exactly as before.
 *
 * Told by the picture: during play, a frame whose black columns (nothing drawn top to
 * bottom) shrink by a good part of the picture's width at once, from the frame before. No
 * SDL here, so the headless tool uses it too. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pce/video.h"

#define DOORS_FADE 10        /* frames the next part takes to fade in */
#define DOORS_APPEAR 0.30    /* the share of the width that must come out of black at once */

typedef struct DoorPic {
    const uint32_t *px;     /* pitch PCE_FB_MAX_W */
    int w, h, wide;
} DoorPic;

typedef struct Doors {
    uint32_t last[PCE_FB_MAX_W * PCE_FB_MAX_H];   /* the frame before (the fade's start) */
    uint32_t out[PCE_FB_MAX_W * PCE_FB_MAX_H];    /* the fade */
    DoorPic last_pic, shown;
    bool have_last;
    double black;           /* share of the last frame's width black top to bottom */
    int fade;               /* frames of the fade still to come */
} Doors;

/* Called once a game frame with the picture as it would be shown (pitch PCE_FB_MAX_W; wide:
 * the columns each side of the original 256); playing: the game is in play. Returns the
 * picture to show instead (pic itself when nothing is fading). When no new game frame ran
 * (the screen refreshed again), call doors_again for the same answer. */
DoorPic doors_frame(Doors *d, DoorPic pic, bool playing, bool on);
DoorPic doors_again(const Doors *d, DoorPic pic);

/* Fast doors (Video > Fast doors): the walk past a door into black, while the next part is
 * put up (about 2 s), run DOORS_FAST times as fast, unseen and unheard (main.c). In black:
 * at least DOORS_BLACK of the last picture's width black from top to bottom (also the
 * moment of black after losing a life). */
#define DOORS_FAST 4
#define DOORS_BLACK 0.30
bool doors_in_black(const Vdc *v);
