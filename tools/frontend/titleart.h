/* Title art (Video > Title art, with widescreen): the player's own art beside the title
 * screen. The game's map holds nothing of the title's past its 256 pixels (wide.c's menus
 * cannot carry it on: its clouds do not repeat), so a picture the player made is drawn
 * there instead: title_sides.png in the profile folder, 396 x 225, the title's 256 x 225
 * picture in the middle (columns 70-325) and 70 columns of art each side, drawn in colours
 * of the title's own. It is the player's own file and is never shipped (art made from
 * Konami's title).
 *
 * The middle is the reference. Each of its colours is one of the game's palette entries,
 * and the game's fades and the lightning change those entries, not the picture: so each
 * frame, every reference colour is looked up where it is in the reference, in the frame,
 * and the sides are drawn with what it has become there. A frame is the title when its
 * pixels group the way the reference's colours do (the same colour where the reference
 * has the same colour), whatever the palette. No other screen is changed. No SDL here (the
 * game loads the PNG: main.c), so the headless tool can use it too. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TITLEART_W 396
#define TITLEART_H 225
#define TITLEART_SIDE 70   /* columns of art each side */
#define TITLEART_COLOURS 255

typedef struct TitleArt {
    bool loaded;
    uint32_t px[TITLEART_H][TITLEART_W];   /* 0xAARRGGBB: filled by the caller, then loaded */
    /* worked out from px on first use */
    bool prepared;
    int ncol;
    uint32_t col[TITLEART_COLOURS];        /* the reference's colours */
    uint8_t id[TITLEART_H][TITLEART_W];    /* each pixel's (sides: the nearest reference colour) */
    bool shown;                            /* the last frame was the title */
} TitleArt;

/* raw: the game's picture as the VDC drew it, 256 x h, `pitch` pixels a row; fb: the same
 * as shown (Smooth dithering's, or raw). If it is the title, writes fb with `ext` columns of
 * art each side (ext <= TITLEART_SIDE) to out (256 + 2 ext wide, the same pitch) and returns
 * true. */
bool titleart_compose(TitleArt *t, const uint32_t *raw, const uint32_t *fb, int h, int pitch, int ext, uint32_t *out);
