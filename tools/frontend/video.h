/* Putting the PC Engine picture on any screen: 4:3 / square-pixel / stretched, fit to the
 * screen or whole multiples only, sharp (crisp but even) / nearest / smooth filtering, a
 * screen effect (scanlines, CRT flat or curved, composite; see video.c), and a blurred copy
 * of the picture beside it on 16:9 and ultrawide screens. Also on-screen text. */
#pragma once

#include <SDL3/SDL.h>
#include <stdint.h>

#include "settings.h"

typedef struct Video {
    SDL_Renderer *ren;
    SDL_Texture *frame;          /* the emulated picture, streaming */
    SDL_Texture *sharp;          /* integer-upscaled copy (FILTER_SHARP), in its top left */
    int sharp_w, sharp_h;        /* its size */
    int sharp_k;                 /* the factor */
    SDL_Texture *blur_a, *blur_b;
    int src_w, src_h;
    int src_wide;                /* columns each side of the game's 256 (widescreen; 0 = none) */
    /* screen effects */
    SDL_Texture *fx;             /* the picture at its size on screen, with scanlines, in its top left */
    int fx_w, fx_h;              /* its size */
    SDL_Texture *scan;           /* each screen row's brightness (for scan_h rows showing scan_rows lines) */
    int scan_h, scan_rows;
    float scan_depth;
    SDL_Texture *mask;           /* phosphor stripes, tiled */
    float mask_depth;
    SDL_Texture *glow;           /* the picture at half size, for halation */
    SDL_Texture *vignette;       /* darker edges */
} Video;

bool video_init(Video *v, SDL_Renderer *ren);
void video_free(Video *v);
/* Upload a new picture (ARGB8888, pitch in pixels). wide: columns it has each side of the
 * game's 256 (widescreen, wide.h), drawn at the same pixel shape: a 340-pixel picture with 42
 * a side shows at 16:9 with the 4:3 aspect. */
void video_upload(Video *v, const uint32_t *fb, int w, int h, int pitch, int wide);
/* The shape of the game's 256-pixel picture on screen (width / height): 4:3, or square pixels
 * (stretched counts as 4:3). */
double video_picture_aspect(const Settings *s);
/* Clear and draw the picture for the current window size; returns where it went. */
SDL_FRect video_draw(Video *v, const Settings *s);

/* Text with SDL's built-in 8x8 font. scale = pixels per font pixel. */
int ui_scale(SDL_Renderer *ren);
void ui_text(SDL_Renderer *ren, float x, float y, float scale, SDL_Color c, const char *text);
void ui_box(SDL_Renderer *ren, SDL_FRect r, SDL_Color c);
