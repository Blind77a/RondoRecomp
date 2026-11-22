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

/* Alcove glow (Video > Improvements > Alcove glow): wall torches in the castle halls' painted
 * alcoves, whole or half behind the wall, light only the alcove's red bricks round them,
 * flickering. In place; skip_full leaves torches with the whole flame showing (Torch glow's).
 * Run before torch_glow (it finds flames by colours Torch glow changes). */
/* Moon glow (Video > Improvements > Moon glow): a faint rim of light round the moon (Stage 2's
 * docks), only in the sky round it, breathing very slowly. In place; 1 if the moon was found. */
/* Shield shine (Video > Improvements > Shield shine): the wall of heraldic shields (Stage 2's
 * docks): a glint runs round each shield's rim. In place; returns how many were found. */
/* Sail sway (Video > Improvements > Sail sway): the purple sails over Stage 2's docks move in
 * the wind, the tall ones swaying, the short ones' lower halves fluttering. In place; run
 * before the other passes. 1 if sails were found. */
int sail_sway(uint32_t *img, int w, int h, int pitch, uint64_t frame, int world_x);

/* Night sky (Video > Improvements > Night sky): the stars over Stage 2's docks twinkle
 * faintly, and rarely (about once a minute of starry sky, at least 20 s apart) a small, dim
 * shooting star crosses it. In place; run after sail_sway and before moon_glow. 1 if the
 * starry sky was found. night_sky_streak_at: a shooting star from that frame on (tests). */
int night_sky(uint32_t *img, int w, int h, int pitch, uint64_t frame);
void night_sky_streak_at(int frame);

int shield_shine(uint32_t *img, int w, int h, int pitch, uint64_t frame);

int moon_glow(uint32_t *img, int w, int h, int pitch, uint64_t frame);

int alcove_glow(uint32_t *img, int w, int h, int pitch, uint64_t frame, int world_x, bool skip_full);
