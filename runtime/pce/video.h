/* PC Engine video: HuC6260 VCE (palette, dot clock) and HuC6270 VDC
 * (VRAM, background, sprites, DMA, raster/vblank interrupts). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PCE_FB_MAX_W 512
#define PCE_FB_MAX_H 256
#define PCE_LINES_PER_FRAME 263

typedef struct Vce {
    uint16_t pal[512];      /* 9-bit GRB entries */
    uint32_t argb[512];     /* cached 0xAARRGGBB */
    uint16_t addr;
    uint8_t ctrl;           /* bits 0-1 dot clock, bit 7 grayscale */
} Vce;

void vce_reset(Vce *v);
void vce_write(Vce *v, uint16_t off, uint8_t val);
uint8_t vce_read(Vce *v, uint16_t off);

enum {
    VDC_ST_CR = 0x01,   /* sprite 0 collision */
    VDC_ST_OR = 0x02,   /* sprite overflow */
    VDC_ST_RR = 0x04,   /* raster compare */
    VDC_ST_DS = 0x08,   /* VRAM -> SATB DMA done */
    VDC_ST_DV = 0x10,   /* VRAM -> VRAM DMA done */
    VDC_ST_VD = 0x20,   /* vertical blank */
};

typedef struct Vdc {
    uint16_t vram[0x8000];
    uint16_t satb[256];
    uint16_t reg[32];
    uint8_t ar;
    uint8_t status;
    uint16_t read_latch;
    bool irq;               /* IRQ1 line */
    bool satb_pending;      /* DVSSR written: transfer at next vblank */

    /* per-frame state, latched at frame start */
    int display_start, display_end;
    int bg_y;               /* y scroll of the previously rendered line */
    bool first_line;

    /* framebuffer of the active display */
    uint32_t fb[PCE_FB_MAX_W * PCE_FB_MAX_H];
    int fb_w, fb_h;

    /* Widescreen's own bookkeeping (tools/frontend/wide.c), kept with the machine state so
     * that a save state or a rewind carries on with the picture as it was. Never read by the
     * emulation; all 0 when widescreen has not run (and in states from 1.2 and earlier). The
     * same 16 bytes held widescreen's settings until 0.13: the state keeps its size. */
    uint8_t wide_state[16];
} Vdc;

void vdc_reset(Vdc *d);
void vdc_write(Vdc *d, uint16_t off, uint8_t val);
uint8_t vdc_read(Vdc *d, uint16_t off);

/* Called at the start of each of the 263 lines (before the CPU runs that line): the raster
 * compare. */
void vdc_line_start(Vdc *d, int line);
/* The end of the picture: the sprite table DMA and the vertical blank interrupt. Called
 * part-way into line display_end, after that line's raster compare has been taken. */
void vdc_vblank(Vdc *d);
/* Called after the CPU has run the line: renders it if it is an active display line. */
void vdc_line_end(Vdc *d, const Vce *vce, int line);

/* Host-side, kept outside the machine state (save states are unchanged; one picture is
 * drawn at a time per process):
 * - vdc_row_scroll: the background X scroll (BXR) each displayed row of the last frame was
 *   drawn with; raster splits give bands of rows their own scroll (rondo_headless
 *   --row-scroll).
 * - vdc_set_sprite_hide: sprites (a bit per sprite table entry) left out of the picture
 *   from now on; 0 = none. They still count toward each line's 16 and sprite 0's
 *   collision, as if drawn: only their pixels are not put in the picture. */
const uint16_t *vdc_row_scroll(void);
void vdc_set_sprite_hide(uint64_t mask);

/* Widescreen, host side like the above (tools/frontend/wide.c decides it before each frame):
 * a frame whose picture is 256 pixels wide is drawn `ext` pixels wider on each side
 * (256 + 2 * ext in all). Column c of the picture shows the game's screen x = x0 + c, for
 * the sprites and for any row drawn with an X scroll (BXR) not in `band`; a row drawn with
 * the scroll band[i].bxr starts at band[i].x0 instead (a parallax layer, which moves
 * across the screen at its own rate). x0 is -ext for a picture centered on the game's 256
 * pixels, down to -2 * ext or up to 0 where it stops at a room's edge while the game's
 * camera goes on. In a wider picture sprites past the hardware's 16 a line are drawn too.
 * Latched at the start of each frame; NULL (or ext 0): the game's own picture. */
#define VDC_WIDE_MAX 128
typedef struct VdcWide {
    int ext;
    int x0;
    int n_bands;
    struct { uint16_t bxr; int16_t x0; } band[8];
} VdcWide;
void vdc_set_wide(const VdcWide *w);
/* The game's screen x that column 0 of the last frame's picture shows the game's sprites at:
 * 0, or in a wider picture its x0. Drawing over the picture at a screen x (as the Super
 * Castlevania IV whip does) goes in column x - vdc_picture_x0(). */
int vdc_picture_x0(void);

/* In a wider picture, sprites the game drew for objects fixed to the screen (the HUD, a
 * stage's title) keep their place in the picture rather than in the level, and those of
 * objects that move with a parallax layer move with its band. *slot is the game's byte
 * holding the object its sprite builder is drawing, kind[slot] (n slots) that object's
 * VDC_SPRITE_*: each sprite table entry written to VRAM takes the kind of the object being
 * drawn, and the sprite table DMA takes the kinds with the table. NULL: none.
 * VDC_SPRITE_LEVEL: at x0; LEFT: where the game put it, from the picture's left edge;
 * CENTER: as far from the picture's center as from the game's; RIGHT: as far from the
 * picture's right edge as from the game's; BAND + i: at band[i].x0.
 * | VDC_SPRITE_WRAP: also 256 pixels either side (scenery the game cycles round its 256
 * pixels, such as the Prologue's rain, goes on into the wider picture). */
enum { VDC_SPRITE_LEVEL, VDC_SPRITE_LEFT, VDC_SPRITE_CENTER, VDC_SPRITE_RIGHT, VDC_SPRITE_BAND, VDC_SPRITE_WRAP = 0x80 };
void vdc_set_sprite_kinds(const uint8_t *slot, const uint8_t *kind, int n);
