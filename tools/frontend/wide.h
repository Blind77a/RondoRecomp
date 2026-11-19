/* Widescreen for Rondo of Blood: in rooms that scroll sideways the picture is wider than the
 * game's 256 pixels (16:9 shows 42 more each side at 4:3), and it moves like a modern game's
 * camera: it stops at the room's walls, so a stage starts with Richter at the left of a wider
 * view and a room ends with its wall or door at the right edge.
 *
 * The game's own camera, and everything it decides from it (walls, doors, room changes,
 * boss arenas), is left exactly as it was. Only the picture moves: it is the game's 256
 * pixels with `ext` more each side, slid toward the room's interior where the room ends
 * (by up to `ext`), so it never shows past a wall. A boss arena that closes in on the camera
 * keeps the room's walls, so the picture stays wide. Each band of rows drawn from a parallax
 * layer is placed the same way between its own walls (the camera's, at the layer's rate), so
 * it never shows more of the layer than the game draws. Objects fixed to the screen (the HUD,
 * a stage's title) keep their place in the picture. Through a door the picture is held, then
 * glides or grows into the next room. Rooms one screen wide (shafts, towers) keep the game's
 * 4:3 picture; the Prologue's carriage chase grows to the wider picture as its title shows
 * (its scenery loops round the whole background map, its rain round 256 pixels).
 *
 * Small changes to the game fill the wider picture: background columns are drawn further
 * out, and objects are started, kept moving, kept alive and drawn further out. Every change
 * is a byte patch checked against the original bytes first (the game loads code from the
 * disc as it goes), put in or taken out only as a room starts (or outside gameplay), so a
 * room is never half drawn the old way. No SDL here: rondo_headless uses it too.
 * See docs/runtime.md, "Widescreen", and docs/findings.md. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pce/pce.h"

/* Largest extension per side. The game's changes draw 112 pixels past its picture, and at a
 * room's edge the picture sits up to twice its extension to one side (16:9 at 4:3 needs 42;
 * with square pixels, 71 is shown as 56 with the rest beside the picture). */
#define WIDE_MAX 56

typedef struct Wide {
    int want;              /* extension per side the window asks for (0 = off) */
    /* the frame about to run (wide_before_frame) */
    int active;            /* extension drawn (0 = the game's own picture) */
    int x0;                /* the picture's left edge, in the game's screen x (-2 * active..0) */
    bool patched;          /* every change for this room is in place */
    /* bookkeeping from before the frame to after it (not in save states: see wide.c) */
    uint16_t before_x[8];  /* scroll layers' X as this frame's picture shows them */
    uint16_t before_l, before_r;   /* the camera's limits */
    bool room_started;     /* a room started during the frame */
    bool rates_set;        /* rate[] and vdrift[] are for this room */
    uint8_t measured;      /* bit i: rate[i] is measured (not a first guess) */
    float rate[8];         /* each layer's speed against the camera's (1 until it is seen moving) */
    int vdrift[8];         /* each layer's velocity (8.8) while the camera is still: its own drift */
    float sub[8];          /* each layer's band: its offset from centered, as the picture's */
    uint8_t kind[0x58];    /* objects: VDC_SPRITE_* for vdc_set_sprite_kinds */
    VdcWide vdc;
} Wide;

/* Pixels per side that fill a window of aspect window_aspect (width / height) when the
 * game's 256-pixel picture is shown at picture_aspect (4:3, or 256:224 with square pixels);
 * rounded down to even, at most WIDE_MAX. */
int wide_extent(double window_aspect, double picture_aspect);

/* Call before every emulated frame: puts game changes in or takes them out, and sets the
 * VDC's picture for the frame (vdc_set_wide). */
void wide_before_frame(Wide *w, Pce *p);
/* Call after every emulated frame: decides the next frame's picture from where the game's
 * camera now is (kept in the machine state, so states and rewind carry it). */
void wide_after_frame(Wide *w, Pce *p);

/* Take every game change out now and draw the game's own picture (e.g. before a state is
 * saved for a build without widescreen, or for hardcore mode). */
void wide_off(Wide *w, Pce *p);

/* After a state load, a rewind or a restart: what was learnt from watching the room (each
 * scroll layer's rate) is learnt again. */
void wide_reset(Wide *w);

/* Diagnostics: one line per game change (bank:offset, in / out / other code). */
int wide_report(Pce *p, char *out, int n);
/* Diagnostics: bit i set = leave change i out, as if it were in (rondo_headless --wide-skip). */
extern uint32_t wide_debug_skip;
