/* Torch glow (Video > Improvements > Torch glow): the wall torches' flames light the wall
 * round them, the light flickering. A pass over the finished picture before it is shown, as
 * Smooth dithering's: only the picture changes (the game, its RAM and save states are the
 * same with it on or off). No SDL here, so the headless tool uses it too. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The torches found in src (w x h pixels, ARGB, pitch pixels a row) lit into dst (the same
 * size; the whole picture is copied). frame: the game's frame count, the flicker's clock;
 * world_x: the camera's X, so each torch keeps its own flicker as the picture scrolls.
 * Returns how many torches were lit (0: dst is a plain copy). */
int torch_glow(const uint32_t *src, uint32_t *dst, int w, int h, int pitch, uint64_t frame, int world_x);

/* Candle glow (Video > Improvements > Candle glow): the blue-flamed candles (Stage 4's: a
 * candle object, whipped for hearts) get a soft blue halo round their flame, flickering.
 * Worked on img in place (a copy of the picture); returns how many were lit. */
int candle_glow(uint32_t *img, int w, int h, int pitch, uint64_t frame, int world_x);
