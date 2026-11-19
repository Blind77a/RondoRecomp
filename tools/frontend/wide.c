/* See wide.h. */
#include "wide.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game.h"

/* A change to the game: `len` bytes at physical bank:offset, applied only when the bytes
 * there are `orig` (so other code loaded into the same RAM is left alone). An `orig` of
 * {0} stands for our bank $00's RTS filler ($60s): no real original starts with BRK. */
typedef struct Patch {
    const char *what;
    uint8_t bank;
    uint16_t off;          /* offset within the 8 KiB bank */
    uint8_t len;
    uint8_t orig[64];
    uint8_t wide[96];
    /* a byte of `wide` set while the game runs (VAR_*, at var_at), 0 = none: compared as
     * anything, written with the frame's value (var_value) */
    uint8_t var, var_at;
} Patch;

/* Values that follow the picture each frame (see wide_frame): how far past the screen
 * sprites are built, how far out objects spawn and start moving, and where a room's first
 * drawing starts. */
enum { VAR_NONE, VAR_SPRITE_RIGHT, VAR_SPRITE_LEFT, VAR_ECHO_RIGHT, VAR_SPAWN_RIGHT, VAR_SPAWN_LEFT,
       VAR_MOVE_RIGHT, VAR_MOVE_LEFT, VAR_ROOM_START, VAR_COUNT };
static uint8_t var_value[VAR_COUNT];   /* 0 = the template's byte */

static const uint8_t RTS_FILLER[96] = {
    0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
    0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
    0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
    0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
    0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
    0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60};

#define FILL60 /* 52 bytes of our bank $00's RTS filler */ \
    {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, \
     0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, \
     0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, \
     0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60}

#define E WIDE_MAX         /* every patch is made for the widest supported view */
#define WIDE_STREAMED 64   /* the widest a side shows (wide_extent: background drawn there) */
#ifndef STREAM
#define STREAM 0x50        /* columns streamed this far past the edge (see patches[0]) */
#endif

/* (patches without a varying byte leave var and var_at out: 0) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const Patch patches[] = {
    /* Background streaming (bank $6F, the scroll-layer code; every layer and both axes
     * use it): when the camera crosses a 32-pixel column, the game draws the column 64
     * pixels past the edge it is moving towards. The table holds that distance (low
     * byte; the page byte after it stays +1/-1). STREAM = 80 covers WIDE_MAX (a column
     * comes in at least that far past the edge). No further: a scenery band can show map
     * columns other layers stream into (Stage 2-1: the far trees, layer 1, is not streamed
     * itself; the playfield and middle layers' columns, blank at the top, fill its rows),
     * and the game keeps those writes out of its view only near its own 64. At 96 or 112
     * (0.6-0.12) they landed in view: a black strip drifting across the sky. */
    {"background columns streamed further out", 0x6F, 0x0780, 2, {0x40, 0xC0}, {STREAM, 0x100 - STREAM}},
    /* Some layers (the scenery of the Stage 0 carriage chase) are first drawn by stage
     * code out to the old 64 px only, which would leave a 48 px gap the new distance
     * never fills. So each streamed column (6F:A7A5) is drawn twice: at the old distance,
     * then at the new one, by code in free space in our own bank $00 (mapped at $E000
     * throughout) that switches the table above between the two. */
    {"streamed columns drawn at the old and the new distance", 0x6F, 0x07A5, 3, {0xBD, 0x66, 0x3E}, {0x4C, 0x10, 0xFE}},
    {"column drawing at both distances", 0x00, 0x1E10, 52, FILL60,
     {0x5A,                     /* PHY                              */
      0xDA,                     /* PHX                              */
      0xA9, 0x40,               /* LDA #$40      the old distances  */
      0x8D, 0x80, 0xA7,         /* STA $A780                        */
      0xA9, 0xC0,               /* LDA #$C0                         */
      0x8D, 0x81, 0xA7,         /* STA $A781                        */
      0x20, 0x2E, 0xFE,         /* JSR draw                         */
      0xFA,                     /* PLX                              */
      0x7A,                     /* PLY                              */
      0xA9, STREAM,             /* LDA #STREAM   the new distances  */
      0x8D, 0x80, 0xA7,         /* STA $A780                        */
      0xA9, 0x100 - STREAM,     /* LDA #-STREAM                     */
      0x8D, 0x81, 0xA7,         /* STA $A781                        */
      0x4C, 0x2E, 0xFE,         /* JMP draw (returns to the caller) */
      0xBD, 0x66, 0x3E,         /* draw: LDA $3E66,X  (as at A7A5)  */
      0x4C, 0xA8, 0xA7,         /* JMP $A7A8                        */
      0x40, 0xC0,               /* $FE34: the old distances, kept for vertical use */
      0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60}},
    /* The table is also read for vertical distances: where full-height columns start
     * (64 px above the camera, 6F:A7C4) and how far rows are streamed above and below
     * (6F:A8A4). Those stay as they were, read from the copy at $FE34; scenery layers
     * shorter than the map would otherwise be drawn into each other's rows. */
    {"columns start 64 px above the camera, as before", 0x6F, 0x07C5, 2, {0x80, 0xA7}, {0x34, 0xFE}},
    {"rows streamed 64 px above and below, as before", 0x6F, 0x08A5, 2, {0x80, 0xA7}, {0x34, 0xFE}},
    /* Room start (6F:A94C): the game draws 13 columns from 64 px left of the camera by
     * moving a temporary camera 384 px left and stepping it right 32 px at a time. With
     * the table above each column lands STREAM - 64 px further right, so start 480 px left
     * (camera + 0x20 - 0x200) and draw all 16 columns: the whole 512-pixel map, from
     * camera - 224 + STREAM. */
    {"room start draws 16 columns", 0x6F, 0x0963, 7,
     {0xA9, 0x0C, 0x85, 0x16, 0xA9, 0x80, 0x44}, {0xA9, 0x0F, 0x85, 0x16, 0xA9, 0x20, 0x44}, VAR_ROOM_START, 5},
    /* Rows streamed while the camera moves up or down (6F:A8D5) span the original 256
     * pixels from the camera; draw 16 blocks from 112 px left instead (the whole map). */
    {"rows drawn 512 px wide", 0x6F, 0x08D5, 7,
     {0xAD, 0x66, 0x3E, 0x29, 0x7F, 0x85, 0x08}, {0xA9, 0x10, 0x85, 0x08, 0xEA, 0xEA, 0xEA}},
    {"rows start 112 px left", 0x6F, 0x08EC, 2, {0x10, 0x15}, {0xEA, 0xEA}},

    /* Objects. The game spawns them 64 px out (camera + $140 / - $40, bank $69), lets them
     * move from there (70:A0B0 / A0BD) and removes them 128 px out: enough for 16:9 with the
     * TV's pixels (42 a side), where they are left exactly as the game has them. Square
     * pixels show 70: there (and only there, see OBJECT_REACH) objects spawn and start moving
     * 72 px out instead, and the screen-fixed generators at 320 / -64 move out to match
     * (move_generators). Put in or out as a room starts, with the background. (0.6-0.12,
     * going to 112 px for 21:9, moved all of this out to 128 px.) */
    {"objects spawn 72 px right of the screen", 0x69, 0x1E68, 2, {0x69, 0x40}, {0x69, 0x48}, VAR_SPAWN_RIGHT, 1},
    {"objects spawn 72 px left of the screen", 0x69, 0x1E7F, 2, {0x69, 0xC0}, {0x69, 0xB8}, VAR_SPAWN_LEFT, 1},
    {"objects move from 72 px left of the screen", 0x70, 0x00B0, 1, {0xC0}, {0xB8}, VAR_MOVE_LEFT, 0},
    {"objects move from 72 px right of the screen", 0x70, 0x00BD, 1, {0x40}, {0x48}, VAR_MOVE_RIGHT, 0},

    /* ---- drawing: follows each frame's choice of wide or 4:3 (sprites past the 256
     * pixels would only take up the 64 hardware sprites in a 4:3 picture) ---- */

    /* Sprite building (bank $72, 72:8000): each sprite piece is dropped unless its X
     * (screen + 32) is 0..$11F; let pieces through from -E to $11F + E. */
    {"sprites drawn up to WIDE_MAX px right of the screen", 0x72, 0x0133, 1, {0x20}, {0x20 + E}, VAR_SPRITE_RIGHT, 0},
    {"sprites with X 0..15 drawn (unless parked: see $FE62)", 0x72, 0x013A, 3, {0xDD, 0xA1, 0x81}, {0x4C, 0x62, 0xFE}},
    /* The piece's X high byte decides (72:8123: 0 -> 8138, positive -> 812A, negative ->
     * dropped): through $FC40, which does the same, negatives going to the check below,
     * except for the copies of echoed objects (see $FC00). */
    {"piece culling through $FC40 (echoes)", 0x72, 0x0123, 7, {0xF0, 0x13, 0x10, 0x03, 0x4C, 0x8C, 0x81},
     {0x4C, 0x40, 0xFC, 0xEA, 0xEA, 0xEA, 0xEA}},
    /* Each object is drawn (72:803F: STX <$D9, BSR $8066) through $FC00, which draws echoed
     * objects twice more. */
    {"objects drawn through $FC00 (echoes)", 0x72, 0x003F, 6, {0x86, 0xD9, 0x44, 0x23, 0x30, 0x20},
     {0x20, 0x00, 0xFC, 0x30, 0x21, 0xEA}},
    /* Echoes: an object the ECHO_TABLE marks (written before every frame: screen-wide weather
     * such as Stage 2's rain, two objects fixed to the screen whose drops spread about 128 px
     * either side of them, so they only ever cover the 256 pixels) is drawn again 256 px to
     * the right and 256 px to the left, and the rain tiles across the wider picture. Only
     * while 16 sprite entries are left for the objects after it. Generated by a small
     * assembler; the culling below tells the copies apart by $8066's return address. */
    {"echoed objects drawn 256 px either side", 0x00, 0x1C00, 53, {0},
     {0x86, 0xD9,               /* FC00 STX <$D9                                */
      0x20, 0x66, 0x80,         /* FC02 JSR $8066     the object as the game has it */
      0x30, 0x2D,               /* FC05 BMI ret       sprite table full         */
      0xA6, 0xD9,               /* FC07 LDX <$D9                                */
      0xBD, 0xC0, 0xFC,         /* FC09 LDA $FCC0,X   ECHO_TABLE                */
      0xF0, 0x24,               /* FC0C BEQ ok                                  */
      0xA5, 0x17,               /* FC0E LDA <$17      sprite entries left       */
      0xC9, 0x10,               /* FC10 CMP #$10                                */
      0x90, 0x1E,               /* FC12 BCC ok                                  */
      0xFE, 0x70, 0x2B,         /* FC14 INC $2B70,X   X + 256                   */
      0x20, 0x66, 0x80,         /* FC17 JSR $8066     (returns to FC1A)         */
      0x08,                     /* FC1A PHP                                     */
      0xA6, 0xD9,               /* FC1B LDX <$D9                                */
      0xDE, 0x70, 0x2B,         /* FC1D DEC $2B70,X                             */
      0x28,                     /* FC20 PLP                                     */
      0x30, 0x11,               /* FC21 BMI ret                                 */
      0xDE, 0x70, 0x2B,         /* FC23 DEC $2B70,X   X - 256                   */
      0x20, 0x66, 0x80,         /* FC26 JSR $8066     (returns to FC29)         */
      0x08,                     /* FC29 PHP                                     */
      0xA6, 0xD9,               /* FC2A LDX <$D9                                */
      0xFE, 0x70, 0x2B,         /* FC2C INC $2B70,X                             */
      0x28,                     /* FC2F PLP                                     */
      0x30, 0x02,               /* FC30 BMI ret                                 */
      0xA9, 0x00,               /* FC32 ok: LDA #0    N clear: the next object  */
      0x60}},                   /* FC34 ret: RTS                                */
    /* A = the piece's X high byte (flags from it), X = its size index. The copies keep only
     * pieces past the 256 pixels on their own side, so the original picture is untouched:
     * the copy 256 right keeps screen X 256..255+E, the copy 256 left X < 0 (from -E, as
     * the negative check has it). */
    {"echo copies culled to the sides", 0x00, 0x1C40, 78, {0},
     {0x48,                     /* FC40 PHA                                     */
      0xDA,                     /* FC41 PHX                                     */
      0xBA,                     /* FC42 TSX                                     */
      0xBD, 0x04, 0x21,         /* FC43 LDA $2104,X   $8066's return address hi */
      0xC9, 0xFC,               /* FC46 CMP #$FC                                */
      0xD0, 0x0B,               /* FC48 BNE normal                              */
      0xBD, 0x03, 0x21,         /* FC4A LDA $2103,X   ... and lo                */
      0xC9, 0x19,               /* FC4D CMP #$19      the copy 256 right        */
      0xF0, 0x13,               /* FC4F BEQ plus                                */
      0xC9, 0x28,               /* FC51 CMP #$28      the copy 256 left         */
      0xF0, 0x22,               /* FC53 BEQ minus                               */
      0xFA,                     /* FC55 normal: PLX                             */
      0x68,                     /* FC56 PLA                                     */
      0xF0, 0x05,               /* FC57 BEQ n0                                  */
      0x10, 0x06,               /* FC59 BPL npos                                */
      0x4C, 0x00, 0xFE,         /* FC5B JMP $FE00     negative X check          */
      0x4C, 0x38, 0x81,         /* FC5E n0: JMP $8138                           */
      0x4C, 0x2A, 0x81,         /* FC61 npos: JMP $812A                         */
      0xFA,                     /* FC64 plus: PLX                               */
      0x68,                     /* FC65 PLA                                     */
      0xC9, 0x01,               /* FC66 CMP #1                                  */
      0xD0, 0x21,               /* FC68 BNE drop                                */
      0xA5, 0x05,               /* FC6A LDA <$05                                */
      0xC9, 0x20,               /* FC6C CMP #$20      at or past screen X 256   */
      0x90, 0x1B,               /* FC6E BCC drop                                */
      0xC9, 0x20 + E,           /* FC70 CMP #$20+E                              */
      0xB0, 0x17,               /* FC72 BCS drop                                */
      0x4C, 0x3F, 0x81,         /* FC74 JMP $813F     keep                      */
      0xFA,                     /* FC77 minus: PLX                              */
      0x68,                     /* FC78 PLA                                     */
      0xF0, 0x07,               /* FC79 BEQ m0                                  */
      0xC9, 0xFF,               /* FC7B CMP #$FF                                */
      0xD0, 0x0C,               /* FC7D BNE drop                                */
      0x4C, 0x00, 0xFE,         /* FC7F JMP $FE00     negative X check          */
      0xA5, 0x05,               /* FC82 m0: LDA <$05                            */
      0xC9, 0x20,               /* FC84 CMP #$20      left of screen X 0        */
      0xB0, 0x03,               /* FC86 BCS drop                                */
      0x4C, 0x38, 0x81,         /* FC88 JMP $8138                               */
      0x4C, 0x8C, 0x81},        /* FC8B drop: JMP $818C                         */
     VAR_ECHO_RIGHT, 0x31},
    /* ...in free space in our own bank $00 (RTS filler; mapped at $E000 throughout):
     * A = X high byte (negative). Keep the piece if it is $FF and the low byte >= -E. */
    {"negative sprite X check", 0x00, 0x1E00, 16,
     {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60},
     {0xC9, 0xFF,               /* CMP #$FF          */
      0xD0, 0x09,               /* BNE drop          */
      0xA5, 0x05,               /* LDA <$05          */
      0xC9, 0x100 - E,          /* CMP #-E           */
      0x90, 0x03,               /* BCC drop          */
      0x4C, 0x50, 0xFE,         /* JMP $FE50         */
      0x4C, 0x8C, 0x81},        /* drop: JMP $818C   */
     VAR_SPRITE_LEFT, 7},
    /* ...then the piece's object (slot in zp $D9 while the builder draws it) is dropped if
     * the parked-object table (PARKED_TABLE, written before every frame) marks it: something
     * the game parks just off the left of its 256 pixels, such as the Prologue's waiting
     * horses, stays out of sight in the wider picture too. */
    {"objects parked off the left not drawn", 0x00, 0x1E50, 17,
     {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60},
     {0xDA,                     /* PHX               */
      0xA6, 0xD9,               /* LDX <$D9  (slot)  */
      0xBD, 0x80, 0xFE,         /* LDA $FE80,X       */
      0xFA,                     /* PLX               */
      0xC9, 0x00,               /* CMP #0            */
      0xD0, 0x03,               /* BNE drop          */
      0x4C, 0x3F, 0x81,         /* JMP $813F (keep)  */
      0x4C, 0x8C, 0x81}},       /* drop: JMP $818C   */
    /* Pieces up to 32 px left of the screen (X + 32 = 0..31, A = that, X = the size index):
     * kept, except for a parked object, which gets the game's own lower bound again. */
    {"pieces just left of the screen: parked objects culled as before", 0x00, 0x1E62, 27,
     {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
      0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60},
     {0x48,                     /* PHA               */
      0xDA,                     /* PHX               */
      0xA6, 0xD9,               /* LDX <$D9  (slot)  */
      0xBD, 0x80, 0xFE,         /* LDA $FE80,X       */
      0xFA,                     /* PLX               */
      0xC9, 0x00,               /* CMP #0            */
      0xF0, 0x09,               /* BEQ keep          */
      0x68,                     /* PLA               */
      0xDD, 0xA1, 0x81,         /* CMP $81A1,X       */
      0x90, 0x06,               /* BCC drop          */
      0x4C, 0x3F, 0x81,         /* go: JMP $813F     */
      0x68,                     /* keep: PLA         */
      0x80, 0xFA,               /* BRA go            */
      0x4C, 0x8C, 0x81}},       /* drop: JMP $818C   */
};
#pragma GCC diagnostic pop
#define PARKED_TABLE 0x1E80   /* bank $00 offset ($FE80): a byte per object slot, 1 = parked */
#define ECHO_TABLE 0x1CC0     /* bank $00 offset ($FCC0): a byte per object slot, 1 = echoed */
#define N_PATCHES (int)(sizeof patches / sizeof patches[0])
#define N_WORLD 8          /* patches[0..7] change how the room's background is streamed into
                            * the map (put in or out as a room starts) */
#define WORLD_PATCHES ((1u << N_WORLD) - 1)
#define OBJECT_PATCHES (((1u << 4) - 1) << N_WORLD)   /* patches[8..11]: objects 72 px out */
#define DRAW_PATCHES (((1u << N_PATCHES) - 1) & ~WORLD_PATCHES & ~OBJECT_PATCHES)   /* the rest */
/* objects reach further out only for a picture wider than the 64 px they already reach */
#define OBJECT_REACH 64
/* How far past the 4:3 screen objects are made and start moving in a widened room: the
 * picture's extension and a margin for wide sprites drawn around their X (the broken
 * bridge's sections and statues, 64 px and more), at most 120 (the game removes objects 128
 * px out). With the game's 64 (or the 72 used before), they appeared in the picture's edges
 * as they were made. */
#define OBJECT_MARGIN 50
static int object_reach(int ext)
{
    int d = ext + OBJECT_MARGIN;
    return d < 0x40 ? 0x40 : d > 0x78 ? 0x78 : d;
}
/* enemies sent in from just past the screen's edges start at most this far out (wide_frame) */
#define EDGE_SPAWN_MAX 80

static uint8_t *at(Pce *p, const Patch *q) { return p->page[q->bank] ? p->page[q->bank] + q->off : NULL; }
static const uint8_t *orig_of(const Patch *q) { return q->orig[0] ? q->orig : RTS_FILLER; }

/* 0.6-0.12 and the first 16:9 build sent negative pieces to $FE00 with a patch at 72:8127
 * alone, now part of the one at 72:8123: a save state from then holds it. Put the game's
 * bytes back, so the patch here finds them. */
static void undo_old_patches(Pce *p)
{
    static const uint8_t old[7] = {0xF0, 0x13, 0x10, 0x03, 0x4C, 0x00, 0xFE};
    uint8_t *m = p->page[0x72] ? p->page[0x72] + 0x0123 : NULL;
    if (m && !memcmp(m, old, sizeof old)) {
        m[5] = 0x8C;
        m[6] = 0x81;
        p->cpu.page_gen[0x72]++;
    }
}

/* Diagnostics: patches to leave out (bit i = patches[i]), counted as in place. */
uint32_t wide_debug_skip;

/* patches[0], the streaming distance, and patches[1], the column drawn at both distances:
 * while a column is drawn at the old distance ($FE10) the table holds the old one, and a
 * frame can end right then (the game running long). */
#define PATCH_DISTANCE 0
#define PATCH_TWICE 1

/* The object patches are little more than their varying byte, so their own bytes cannot
 * tell whether they are in: they are counted in while this is set (set_patches). */
static bool objects_on;

/* The patch's bytes are at m (its varying byte, if any, holding whatever value). */
static bool is_in(const uint8_t *m, const Patch *q)
{
    if (!q->var) return !memcmp(m, q->wide, q->len);
    return !memcmp(m, q->wide, q->var_at) &&
           !memcmp(m + q->var_at + 1, q->wide + q->var_at + 1, (size_t)(q->len - q->var_at - 1));
}

/* Which patches are in place. */
static uint32_t patches_in(Pce *p)
{
    uint32_t mask = wide_debug_skip;
    for (int i = 0; i < N_PATCHES; i++) {
        const uint8_t *m = at(p, &patches[i]);
        if (m && is_in(m, &patches[i]) && (objects_on || !(OBJECT_PATCHES & 1u << i))) mask |= 1u << i;
    }
    /* mid-column: the old distance, put there for a moment by the column drawing */
    const Patch *d = &patches[PATCH_DISTANCE];
    const uint8_t *m = at(p, d);
    if ((mask & 1u << PATCH_TWICE) && m && !memcmp(m, d->orig, d->len)) mask |= 1u << PATCH_DISTANCE;
    return mask;
}

/* Put the patches in `mask` in (or take them out) where the original (or patched) bytes
 * are found. */
static void set_patches(Pce *p, uint32_t mask, bool on)
{
    if (mask & OBJECT_PATCHES) objects_on = on;
    for (int i = 0; i < N_PATCHES; i++) {
        if (!(mask & 1u << i) || (wide_debug_skip & 1u << i)) continue;
        const Patch *q = &patches[i];
        uint8_t *m = at(p, q);
        if (!m) continue;
        if (on ? !memcmp(m, orig_of(q), q->len) : is_in(m, q)) {
            memcpy(m, on ? q->wide : orig_of(q), q->len);
            if (on && q->var && var_value[q->var]) m[q->var_at] = var_value[q->var];
            p->cpu.page_gen[q->bank]++;   /* recompiled code in this bank is re-checked */
        }
    }
}

/* Patches in place get this frame's values (var_value). */
static void update_vars(Pce *p)
{
    for (int i = 0; i < N_PATCHES; i++) {
        const Patch *q = &patches[i];
        if (!q->var || (wide_debug_skip & 1u << i) || (!objects_on && (OBJECT_PATCHES & 1u << i))) continue;
        uint8_t *m = at(p, q);
        uint8_t v = var_value[q->var] ? var_value[q->var] : q->wide[q->var_at];
        if (m && is_in(m, q) && m[q->var_at] != v) {
            m[q->var_at] = v;
            p->cpu.page_gen[q->bank]++;
        }
    }
}

int wide_extent(double window_aspect, double picture_aspect)
{
    double w = 256.0 * window_aspect / picture_aspect;
    int e = (int)floor((w - 256.0) / 2.0);
    if (e < 0) e = 0;
    if (e > WIDE_MAX) e = WIDE_MAX;
    /* no further than the game has its background drawn: columns 32 px wide streamed STREAM
     * px past the edge when the camera crosses one, so (STREAM / 32) * 32 = 64 px are always
     * there; at 70 the last 6 px showed what the map held before every 32 px walked */
    if (e > WIDE_STREAMED) e = WIDE_STREAMED;
    return e & ~1;
}

/* ---- objects ------------------------------------------------------------------------ */

/* Object arrays (88 slots, work RAM offsets): kind, flags, screen X low/high, type. */
#define OBJ_SLOTS 0x58
#define OBJ_KIND 0x0750      /* 0 = free slot */
#define OBJ_FLAGS 0x10F0     /* bit 0: fixed to the screen (does not scroll) */
#define ECHO_STILL_FRAMES 8  /* an echoed object has moved within this many frames */
#define OBJ_XL 0x0BC8
#define OBJ_XH 0x0B70
#define OBJ_TYPE 0x0A10
#define HUD_SLOT 0x57        /* the HUD (health, hearts, score): type 5, screen X 8 */
#define SUBWEAPON_SLOT 0x56  /* the sub-weapon icon hanging from the HUD: type 3, screen X 28 */
#define OBJ_FRAME 0x0A68     /* sprite frame drawn; 0 = none */

/* Some objects are generators fixed to the screen at the spawn edge (e.g. Stage 1's
 * skeleton generator sits at screen X 320 and drops skeletons there). While objects spawn
 * further out (right / left: pixels past that side of the screen; the game's is 64), move
 * the ones at the game's edge or at the last one we used (gen_r / gen_l) to the new one. */
static void move_generators(Wide *w, Pce *p, int right, int left)
{
    uint8_t *r = p->wram;
    int was_r = w->gen_r ? w->gen_r : 0x40, was_l = w->gen_l ? w->gen_l : 0x40;
    for (int i = 0; i < OBJ_SLOTS; i++) {
        if (!r[OBJ_KIND + i] || !(r[OBJ_FLAGS + i] & 1)) continue;
        int x = r[OBJ_XL + i] | r[OBJ_XH + i] << 8;
        int to = -1;
        if (x == 0x0140 || x == 0x0100 + was_r) to = 0x0100 + right;
        else if (x == 0x10000 - 0x40 || x == 0x10000 - was_l) to = 0x10000 - left;
        if (to >= 0 && to != x) {
            r[OBJ_XL + i] = (uint8_t)to;
            r[OBJ_XH + i] = (uint8_t)(to >> 8);
        }
    }
    w->gen_r = right;
    w->gen_l = left;
}

/* Move one HUD object `by` px from its original screen X (home): only if it is the HUD's
 * (its type) and anywhere from home out to as far as widescreen ever puts it on that side
 * (where the game puts it, or this or an earlier session did: a save state holds it). */
static void place_hud_object(Pce *p, int slot, uint8_t type, int home, int by)
{
    uint8_t *r = p->wram;
    if (r[OBJ_TYPE + slot] != type) return;
    int x = (int16_t)(r[OBJ_XL + slot] | r[OBJ_XH + slot] << 8);
    int reach = 2 * WIDE_MAX;
    if (by <= 0 ? (x > home || x < home - reach) : (x < home || x > home + reach)) return;
    int to = home + by;
    r[OBJ_XL + slot] = (uint8_t)to;
    r[OBJ_XH + slot] = (uint8_t)(to >> 8);
}

/* Keep the HUD at the edges of the picture: at the left the frame (health, hearts, score)
 * and the sub-weapon icon that hangs from it, both set up by each stage as it starts; at the
 * right a boss's health bar (slot $55, type 5, settled at X 216 after sliding in). The bar
 * cannot be moved as an object (past X 253 its own code slides it in again), so its sprites
 * (patterns $1F8-$1FF, the bar's own) are drawn further right instead. The stage's title
 * letters ($54-$55, the one in $55 at X 88) are not the HUD's and stay centred. left / right:
 * how far the picture reaches past the original 256 pixels on each side. */
#define BOSS_BAR_SLOT 0x55
static void place_hud_left(Pce *p, int left)
{
    place_hud_object(p, HUD_SLOT, 0x05, 8, -left);
    place_hud_object(p, SUBWEAPON_SLOT, 0x03, 28, -left);
}

static void place_hud(Pce *p, int left, int right)
{
    place_hud_left(p, left);
    const uint8_t *r = p->wram;
    int bx = r[OBJ_XL + BOSS_BAR_SLOT] | r[OBJ_XH + BOSS_BAR_SLOT] << 8;
    bool bar = r[OBJ_TYPE + BOSS_BAR_SLOT] == 0x05 && r[OBJ_FRAME + BOSS_BAR_SLOT] && bx >= 216 && bx <= 254;
    vdc_set_sprite_move(0x1F8, bar ? 0x1FF : 0, right);
}

/* ---- the room ----------------------------------------------------------------------- */

/* Scroll layers (work RAM): layer 0 is the playfield camera. */
#define CAM_XL 0x1E08        /* X low byte, layers 0-7 */
#define CAM_PAGE 0x1E96      /* X high byte mod 16, layers 0-7 */
#define LIMIT_LEFT 0x1EBE    /* camera's leftmost page */
#define LIMIT_RIGHT 0x1EC6   /* camera's rightmost X: page, low byte */

/* signed distance a - b in the camera's 4096-pixel space */
static int wrap(int d) { d &= 0xFFF; return d >= 0x800 ? d - 0x1000 : d; }

/* the camera can move sideways in this room */
static bool room_scrolls(const uint8_t *r)
{
    return r[LIMIT_RIGHT] * 256 + r[LIMIT_RIGHT + 1] > r[LIMIT_LEFT] * 256;
}

/* The two routines that draw a room when it starts (6F:A94C, 6F:A9B1; checked by their
 * first bytes). The game has just loaded the room's header, camera limits included. */
static const uint8_t ROOM_START_A[] = {0x82, 0xA9, 0x90, 0x9D, 0x66, 0x3E};   /* A94C */
static const uint8_t ROOM_START_B[] = {0xA4, 0x45, 0xB9, 0x94, 0xA9, 0x82};   /* A9B1 */

/* Called at every dispatch point (Pce.on_entry): when a room starts, put the background
 * changes in for rooms the camera scrolls sideways through, and take them out for every
 * other room, whose background map may be shared out between layers in ways the wider
 * drawing would overwrite (the Stage 0 carriage chase is one). */
/* kinds of object moved round from one side of the screen to the other (wide_frame): seen
 * jumping, or three or more of a kind standing exactly 128 px apart (the bridge pillars, so
 * their first time round is covered too). Forgotten as a room starts. */
static bool recycled[256];
static bool jumped[256];          /* a kind seen moved round (recycled for the room) */
static uint32_t row_sig[256];     /* find_rows: each kind's slots, ... */
static int row_travel[256];       /* ... and the camera's net move since they changed */
static int rows_cam;              /* the camera last frame (find_rows) */
static bool scenery_rows[64];   /* map rows seen scrolled apart from the playfield (this room) */

static int edge_shift(int left, int right, int ext);

/* The HUD's sprites are built during a frame (72:8000) and shown with the next one, whose
 * picture is shifted (Level ends) for the camera as it stands once this frame has moved it:
 * so the HUD is put again as the building starts, the camera moved by then (put before the
 * frame it would be a step behind, jittering by a pixel as the camera steps 1 and 2 px). */
static void hud_at_build(Wide *w, Pce *p)
{
    const uint8_t *r = p->wram;
    int ext = p->vdc.wide;
    if (!ext || game_state(p) != RONDO_PLAYING || !w->edges || !room_scrolls(r)) return;
    int cam = (r[CAM_PAGE] & 0x0F) << 8 | r[CAM_XL];
    int left = wrap(r[LIMIT_LEFT] * 256 - cam);
    int right = wrap((r[LIMIT_RIGHT] * 256 + r[LIMIT_RIGHT + 1]) - cam) + 256;
    place_hud_left(p, ext - edge_shift(left, right, ext));
}

static void on_entry(Pce *p, void *ctx)
{
    uint16_t pc = p->cpu.pc;
    if (pc == 0x8000 && p->cpu.mpr[4] == 0x72 && p->page[0x72]) {
        hud_at_build(ctx, p);
        return;
    }
    if ((pc != 0xA94C && pc != 0xA9B1) || p->cpu.mpr[5] != 0x6F || !p->page[0x6F]) return;
    const uint8_t *code = p->page[0x6F] + (pc & 0x1FFF);
    if (memcmp(code, pc == 0xA94C ? ROOM_START_A : ROOM_START_B, 6)) return;
    Wide *w = ctx;
    const uint8_t *r = p->wram;
    bool scrolls = room_scrolls(r);
    /* The room is first drawn as the whole 512-pixel map from 144 px left of the camera. A
     * room starting within the extension of its left end draws from the camera instead,
     * and one at its right end from 176 px left, so the side the picture is shifted to
     * (wide_frame) is all there. */
    uint8_t start = 0x20;
    if (w->edges && w->want > 0 && scrolls) {
        int cam = (r[CAM_PAGE] & 0x0F) << 8 | r[CAM_XL];
        int from_left = wrap(cam - r[LIMIT_LEFT] * 256);
        int to_right = wrap(r[LIMIT_RIGHT] * 256 + r[LIMIT_RIGHT + 1] - cam);
        if (from_left < w->want) start = 0xB0;
        else if (to_right < w->want) start = 0x00;
    }
    var_value[VAR_ROOM_START] = start;
    memset(recycled, 0, sizeof recycled);
    memset(jumped, 0, sizeof jumped);
    memset(row_sig, 0, sizeof row_sig);
    memset(row_travel, 0, sizeof row_travel);
    memset(scenery_rows, 0, sizeof scenery_rows);
    set_patches(p, WORLD_PATCHES, w->want > 0 && scrolls);
    set_patches(p, OBJECT_PATCHES, w->want > 0 && scrolls);
    update_vars(p);
}

/* ---- menus ------------------------------------------------------------------------ */

/* The menus (title, file select) are drawn on a map that holds nothing of theirs past the
 * 256 pixels, but their backgrounds are rows of a few tiles that join seamlessly (the file
 * select's waves: 144-147 and 148-149 in any order). Each row is carried on past both
 * edges with the tile that most often follows (precedes) its edge tile across the picture,
 * seen at least twice. Art that does not repeat (the title's clouds, story scenes) has no
 * such tile and stays 4:3. */
static uint16_t side_l[64 * VDC_SIDE_TILES], side_r[64 * VDC_SIDE_TILES];   /* drawn */
static uint16_t try_l[64 * VDC_SIDE_TILES], try_r[64 * VDC_SIDE_TILES];     /* worked out */

#define PAIRS 4096
static uint32_t pair_key[PAIRS];
static uint16_t pair_n[PAIRS];
static uint16_t pair_used[PAIRS], n_used;   /* the slots in use */

static void pair_add(uint16_t a, uint16_t b)
{
    uint32_t key = (uint32_t)a << 16 | b, i = (key * 2654435761u) >> 20;
    while (pair_n[i] && pair_key[i] != key) i = (i + 1) & (PAIRS - 1);
    if (!pair_n[i]) {
        if (n_used >= PAIRS / 2) return;   /* (a map has far fewer) */
        pair_used[n_used++] = (uint16_t)i;
    }
    pair_key[i] = key;
    if (pair_n[i] < 0xFFFF) pair_n[i]++;
}

/* the tile seen most often after a (before it: before = true), at least twice (at least
 * once: once = true); or none */
static uint16_t pair_next_n(uint16_t a, bool before, bool once)
{
    uint16_t best = VDC_SIDE_NONE;
    int n = once ? 0 : 1;
    for (int u = 0; u < n_used; u++) {
        int i = pair_used[u];
        uint16_t x = (uint16_t)(pair_key[i] >> 16), y = (uint16_t)pair_key[i];
        if ((before ? y : x) == a && pair_n[i] > n) {
            n = pair_n[i];
            best = before ? x : y;
        }
    }
    return best;
}
static uint16_t pair_next(uint16_t a, bool before) { return pair_next_n(a, before, false); }

/* Works out the side tiles for the last frame's picture (ext a side); true when nearly all
 * of them could be, and then they are the ones drawn. */
static bool menu_sides(Pce *p, int ext)
{
    static const int map_w[4] = {32, 64, 128, 128};
    const Vdc *d = &p->vdc;
    int mw = map_w[(d->reg[9] >> 4) & 3], mh = (d->reg[9] & 0x40) ? 64 : 32;
    int rows = d->fb_h < PCE_FB_MAX_H ? d->fb_h : PCE_FB_MAX_H;
    if (rows <= 0 || d->fb_w - 2 * d->fb_wide != 256) return false;
    /* one sideways scroll for the whole picture */
    const uint16_t *rs = vdc_row_scroll(), *my = vdc_row_map_y();
    for (int y = 1; y < rows; y++)
        if (rs[y] != rs[0]) return false;
    int first = rs[0] >> 3, last = (rs[0] + 255) >> 3;
    bool shown[64] = {false};
    for (int y = 0; y < rows; y++) shown[(my[y] & (mh * 8 - 1)) >> 3] = true;
#define TILE(r, c) d->vram[(r) * mw + ((c) & (mw - 1))]
    for (int u = 0; u < n_used; u++) pair_n[pair_used[u]] = 0;
    n_used = 0;
    for (int r = 0; r < mh; r++)
        if (shown[r])
            for (int c = first; c < last; c++) pair_add(TILE(r, c), TILE(r, c + 1));
    int need = (ext + 7) / 8 + 1, filled = 0, total = 0;
    if (need > VDC_SIDE_TILES) need = VDC_SIDE_TILES;
    for (int r = 0; r < 64; r++) {
        uint16_t *l = &try_l[r * VDC_SIDE_TILES], *rt = &try_r[r * VDC_SIDE_TILES];
        uint16_t a = r < mh ? TILE(r, first) : VDC_SIDE_NONE, b = r < mh ? TILE(r, last) : VDC_SIDE_NONE;
        for (int k = 0; k < VDC_SIDE_TILES; k++) {
            a = a == VDC_SIDE_NONE ? a : pair_next(a, true);
            b = b == VDC_SIDE_NONE ? b : pair_next(b, false);
            l[k] = a;
            rt[k] = b;
            if (r < mh && shown[r] && k < need) {
                total += 2;
                filled += (a != VDC_SIDE_NONE) + (b != VDC_SIDE_NONE);
            }
        }
    }
#undef TILE
    if (!total || filled * 100 < total * 97) return false;
    memcpy(side_l, try_l, sizeof side_l);
    memcpy(side_r, try_r, sizeof side_r);
    return true;
}


/* a row: every object of a kind (not fixed to the screen), three or four of them, standing
 * exactly 128 px apart (the bridge pillars; not, say, a splash's many particles, three of
 * which can happen to be 128 px apart) */
static void find_rows(const uint8_t *r, int cam_step)
{
    /* each kind's objects (which slots hold it) and how far the camera has gone since they
     * last changed: a row the game moves round keeps its objects; pillars it makes and
     * removes as the camera goes (the broken bridge's, kind $A0) change them within 128 px */
    uint32_t sig[256] = {0};
    for (int i = 0; i < OBJ_SLOTS; i++)
        if (r[OBJ_KIND + i]) sig[r[OBJ_KIND + i]] = sig[r[OBJ_KIND + i]] * 31u + (uint32_t)i + 1u;
    /* (row_travel: the camera's net move since then, so going back and forth does not add
     * up: pillars made 128 px apart cannot get 160 px away without a new one) */
    for (int k = 1; k < 256; k++) {
        if (sig[k] != row_sig[k]) {
            row_sig[k] = sig[k];
            row_travel[k] = 0;
            if (!jumped[k]) recycled[k] = false;   /* (a kind seen made or removed: not a row) */
        } else if (row_travel[k] > -10000 && row_travel[k] < 10000) {
            row_travel[k] += cam_step;
        }
    }
    for (int i = 0; i < OBJ_SLOTS; i++) {
        uint8_t k = r[OBJ_KIND + i];
        if (!k || recycled[k] || (row_travel[k] < 160 && row_travel[k] > -160) || (r[OBJ_FLAGS + i] & 1)) continue;
        int xs[5], n = 0;
        bool fixed = false;
        for (int j = 0; j < OBJ_SLOTS && n <= 4; j++) {
            if (r[OBJ_KIND + j] != k) continue;
            fixed |= (r[OBJ_FLAGS + j] & 1) != 0;
            xs[n++] = (int16_t)(r[OBJ_XL + j] | r[OBJ_XH + j] << 8);
        }
        if (fixed || n < 3 || n > 4) continue;
        for (int a = 1; a < n; a++)   /* (sorted) */
            for (int b = a; b > 0 && xs[b] < xs[b - 1]; b--) {
                int t = xs[b]; xs[b] = xs[b - 1]; xs[b - 1] = t;
            }
        bool row = true;
        for (int a = 1; a < n; a++) row &= xs[a] - xs[a - 1] <= 130 && xs[a] - xs[a - 1] >= 126;
        if (row) recycled[k] = true;
    }
}

/* Level ends: how far the picture moves over (positive: right) for a room whose walls are at
 * screen X left and right (right: just past its last pixel), ext a side */
static int edge_shift(int left, int right, int ext)
{
    int bl = left + ext > 0 ? left + ext : 0;              /* black at the left */
    int br = 256 + ext - right > 0 ? 256 + ext - right : 0; /* ... at the right */
    int shift = bl && br ? (bl - br) / 2 : bl - br;
    if (shift > ext) shift = ext;
    if (shift < -ext) shift = -ext;
    return shift;
}

/* The game moves a pillar round by 383 px, not 384, so after it the row stands 127 / 129 px
 * apart, and the copies 256 px either side land a pixel off the pillars they meet at the
 * picture's edges (a pillar seen twice, a pixel apart). Each row is kept exactly 128 px
 * apart: a pillar a pixel or two out of step with the rest is put back in step (scenery: a
 * pixel unseen in the 4:3 picture, where the moving round happens). */
static void align_rows(uint8_t *r)
{
    for (int k = 1; k < 256; k++) {
        if (!recycled[k]) continue;
        int slot[5], phase[5], n = 0;
        for (int i = 0; i < OBJ_SLOTS && n < 5; i++) {
            if (r[OBJ_KIND + i] != k || (r[OBJ_FLAGS + i] & 1)) continue;
            slot[n] = i;
            phase[n++] = (int16_t)(r[OBJ_XL + i] | r[OBJ_XH + i] << 8) & 127;
        }
        if (n < 3 || n > 4) continue;
        /* the phase most of the row has */
        int best = phase[0], most = 0;
        for (int a = 0; a < n; a++) {
            int c = 0;
            for (int b = 0; b < n; b++) c += phase[b] == phase[a];
            if (c > most) { most = c; best = phase[a]; }
        }
        if (most < 2) continue;
        for (int a = 0; a < n; a++) {
            int d = ((best - phase[a] + 64) & 127) - 64;   /* (-64..63) */
            if (d == 0 || d < -2 || d > 2) continue;
            int i = slot[a], x = (int16_t)(r[OBJ_XL + i] | r[OBJ_XH + i] << 8) + d;
            r[OBJ_XL + i] = (uint8_t)x;
            r[OBJ_XH + i] = (uint8_t)(x >> 8);
        }
    }
}

/* ---- scenery ------------------------------------------------------------------------ */

/* Scenery bands (rows scrolled apart from the playfield: Stage 2's distant hills, castle and
 * trees under the bridge) are sometimes drawn only as far across their map as the 4:3
 * picture ever shows of them; the rest of the map is blank tiles, which the wider picture
 * would show as bare sky. Each such map row's longest blank stretch is filled from both
 * ends with the tile most often after (before) the one at its end, seen at least twice in
 * the band, meeting in the middle: fixed to the map, so it scrolls with the band, and drawn
 * only beside the original 256 pixels (vdc_set_map_fill). Shorter gaps, the playfield's
 * rows and rows any playfield line shows are left as they are. */
#define SCENERY_MIN_GAP 12   /* tiles: the shortest blank stretch filled (96 px) */
static uint16_t scenery[64 * VDC_FILL_W];

static bool tile_blank(const Vdc *d, uint16_t e)
{
    const uint16_t *t = &d->vram[((e & 0x7FF) * 16) & 0x7FFF];
    for (int i = 0; i < 16; i++)
        if (t[i]) return false;
    return true;
}

static bool scenery_fill(Pce *p, int cam)
{
    static const int map_w[4] = {32, 64, 128, 128};
    const Vdc *d = &p->vdc;
    int mw = map_w[(d->reg[9] >> 4) & 3], mh = (d->reg[9] & 0x40) ? 64 : 32;
    if (mw < 64) return false;
    int rows = d->fb_h < PCE_FB_MAX_H ? d->fb_h : PCE_FB_MAX_H;
    const uint16_t *rs = vdc_row_scroll(), *my = vdc_row_map_y();
    /* a map row is scenery once it has been seen scrolled apart from the playfield in this
     * room (at a room's end every layer can stand at the same scroll for a moment); one a
     * playfield line shows in the same frame is not */
    uint8_t band[64] = {0};   /* 1 scenery, 2 shown by a playfield line */
    bool pf_line[64] = {false}, apart[64] = {false};
    for (int y = 0; y < rows; y++) {
        int r = (my[y] & (mh * 8 - 1)) >> 3;
        if ((rs[y] & 0x1FF) == (cam & 0x1FF)) pf_line[r] = true;
        else apart[r] = true;
    }
    for (int r = 0; r < 64; r++) {
        if (apart[r]) scenery_rows[r] = true;
        band[r] = !scenery_rows[r] ? 0 : (apart[r] && pf_line[r]) ? 2 : 1;
    }
#define TILE(r, c) d->vram[(r) * mw + ((c) & (mw - 1))]
    for (int u = 0; u < n_used; u++) pair_n[pair_used[u]] = 0;
    n_used = 0;
    static bool blank[64][VDC_FILL_W];
    bool any_band = false;
    for (int r = 0; r < mh; r++) {
        if (band[r] != 1) continue;
        any_band = true;
        for (int c = 0; c < mw; c++) blank[r][c] = tile_blank(d, TILE(r, c));
        for (int c = 0; c < mw; c++)
            if (!blank[r][c] && !blank[r][(c + 1) & (mw - 1)]) pair_add(TILE(r, c), TILE(r, c + 1));
    }
    if (!any_band) return false;
    for (int i = 0; i < 64 * VDC_FILL_W; i++) scenery[i] = VDC_SIDE_NONE;
    bool any = false;
    for (int r = 0; r < mh; r++) {
        if (band[r] != 1) continue;
        /* the longest blank stretch, going round the map */
        int best_s = 0, best_l = 0;
        for (int c = 0; c < mw; c++) {
            if (!blank[r][c] || blank[r][(c - 1) & (mw - 1)]) continue;
            int l = 0;
            while (l < mw && blank[r][(c + l) & (mw - 1)]) l++;
            if (l > best_l) { best_l = l; best_s = c; }
        }
        /* (a stretch the art leaves out is long; gaps between clouds and hills are short; and
         * a band is mostly drawn: not a row that is empty but for a tower, say) */
        int drawn = 0;
        for (int c = 0; c < mw; c++) drawn += !blank[r][c];
        if (best_l < SCENERY_MIN_GAP || best_l >= mw || drawn * 2 < mw) continue;
        int half = (best_l + 1) / 2;
        uint16_t e = TILE(r, best_s - 1);
        for (int k = 0; k < half && (e = pair_next_n(e, false, true)) != VDC_SIDE_NONE; k++) {
            scenery[r * VDC_FILL_W + ((best_s + k) & (mw - 1))] = e;
            any = true;
        }
        e = TILE(r, best_s + best_l);
        for (int k = 0; k < best_l - half && (e = pair_next_n(e, true, true)) != VDC_SIDE_NONE; k++) {
            scenery[r * VDC_FILL_W + ((best_s + best_l - 1 - k) & (mw - 1))] = e;
            any = true;
        }
    }
#undef TILE
    return any;
}

void wide_frame(Wide *w, Pce *p)
{
    uint8_t *r = p->wram;
    uint8_t s = game_state(p);
    bool want = w->want > 0;

    if (!p->on_entry) {
        p->on_entry = on_entry;
        p->on_entry_ctx = w;
    }
    /* outside gameplay the game is as it was; rooms put the changes back as they start */
    undo_old_patches(p);
    if (s != RONDO_PLAYING) set_patches(p, WORLD_PATCHES | OBJECT_PATCHES, false);
    /* a save state made in a widened room holds the object changes (their varying bytes not
     * the game's own: theirs are 0x40/0xC0): taken as in, so they follow this frame's values */
    if (!objects_on && s == RONDO_PLAYING) {
        bool all = true, moved = false;
        for (int i = 0; i < N_PATCHES; i++) {
            if (!(OBJECT_PATCHES & 1u << i)) continue;
            const uint8_t *m = at(p, &patches[i]);
            all &= m && is_in(m, &patches[i]);
            moved |= m && m[patches[i].var_at] != orig_of(&patches[i])[patches[i].var_at];
        }
        if (all && moved) objects_on = true;
    }
    uint32_t in = patches_in(p);
    w->patched = (in & WORLD_PATCHES) == WORLD_PATCHES;
    bool objects_out = (in & OBJECT_PATCHES) == OBJECT_PATCHES;

    /* scenery that keeps scrolling sideways (the carriage chase): a layer moved on at
     * least 45 of the last 60 frames; changing rooms starts the count again */
    bool moved = false;
    for (int i = 1; i < 8; i++) {
        if (r[CAM_XL + i] != w->layer_x[i]) moved = true;
        w->layer_x[i] = r[CAM_XL + i];
    }
    if (s != RONDO_PLAYING || r[0x31] == 0x03 || r[0x31] == 0x00) w->move_history = 0;
    else w->move_history = (w->move_history << 1 | moved) & ((1ull << 60) - 1);
    int moving = 0;
    for (uint64_t h = w->move_history; h; h &= h - 1) moving++;
    w->scenery_moving = moving;

    /* this frame shows the camera as it is now (it moves during the frame and is shown
     * from the next one) */
    int ext = w->want;
    int cam = (r[CAM_PAGE] & 0x0F) << 8 | r[CAM_XL];
    int left = wrap(r[LIMIT_LEFT] * 256 - cam);
    int right = wrap((r[LIMIT_RIGHT] * 256 + r[LIMIT_RIGHT + 1]) - cam) + 256;
    bool scrolls = room_scrolls(r);
    int mw = (p->vdc.reg[9] >> 4) & 3;         /* background map width: 64+ tiles needed */
    /* wide: rooms that scroll sideways, drawn with the changes; and rooms one screen wide
     * whose scenery scrolls past (their layers loop round the whole map), drawn as the game
     * draws them. Others (vertical shafts, boss rooms, rooms entered before widescreen was
     * turned on) keep the 4:3 picture. */
    bool show = want && s == RONDO_PLAYING && mw >= 1 &&
                (scrolls ? w->patched : (!w->patched && w->scenery_moving >= 45));
    /* sprites past the 256 pixels are built only while the picture is wide */
    set_patches(p, DRAW_PATCHES, show);
    if (show && (patches_in(p) & DRAW_PATCHES) != DRAW_PATCHES) {
        set_patches(p, DRAW_PATCHES, false);
        show = false;
    }
    /* menus (title, file select): wide once the sides have filled for a few frames in a
     * row; then kept, with the last sides that filled, through a while they do not (a page
     * of the menu sliding over: its frames reach the edges), so it does not flick back */
    bool menu = false;
    if (!show && want && w->menus && (s == RONDO_TITLE || s == RONDO_SAVE_MENU)) {
        bool good = menu_sides(p, ext);
        if (good) {
            w->menu_good = w->menu_good < MENU_FRAMES ? w->menu_good + 1 : MENU_FRAMES;
            w->menu_bad = 0;
        } else if (w->menu_good >= MENU_FRAMES && ++w->menu_bad > MENU_HOLD) {
            w->menu_good = 0;
        } else if (w->menu_good < MENU_FRAMES) {
            w->menu_good = 0;
        }
        menu = w->menu_good >= MENU_FRAMES;
    } else {
        w->menu_good = w->menu_bad = 0;
    }
    vdc_set_side_tiles(menu ? side_l : NULL, menu ? side_r : NULL);
    /* a menu sliding over to another page (its scroll moved in the last few frames): the
     * pages themselves across the whole picture (vdc_set_side_real) */
    {
        int bxr = vdc_row_scroll()[0];
        if (bxr != w->menu_bxr) w->menu_moved = 0;
        else if (w->menu_moved < 255) w->menu_moved++;
        w->menu_bxr = bxr;
    }
    vdc_set_side_real(menu && w->menu_moved < 4);
    if (!show && !menu) ext = 0;

    /* In a scene drawn wide over its looping scenery, a band of rows that stands still with
     * nothing in the original 256 pixels holds something parked in the part of its 512-pixel
     * map those pixels do not show: the Prologue's carriage (background) waits there just
     * off both edges, its horses (sprite objects) just off the left, while the forest
     * scrolls past. Such a band shows only the original 256 pixels, and objects standing
     * still off the left are not drawn (the HUD excepted). When the band moves again, the
     * parked thing is coming in from the left: the left side is shown, the right (where the
     * map wraps round to its rear) is not, until it has come in clear of the picture's left
     * edge (on the widest screens the picture shows 480 of the map's 512 pixels, so what is
     * still off the left wraps round into the right side). Decided per band (rows drawn with
     * the same scroll) from the last frame's rows. */
    const uint16_t *rs = vdc_row_scroll();
    const uint8_t *blank = vdc_row_bg_blank();
    const uint8_t *edge = vdc_row_bg_edge();
    int rows = p->vdc.fb_h < PCE_FB_MAX_H ? p->vdc.fb_h : PCE_FB_MAX_H;
    bool clip_parked = ext && !scrolls && !menu, any_parked = false;
    for (int y = 0; y < PCE_FB_MAX_H; y++) {
        bool same = y < rows && rs[y] == w->row_x[y];
        w->row_still[y] = same ? (uint8_t)(w->row_still[y] < 255 ? w->row_still[y] + 1 : 255) : 0;
    }
    for (int y0 = 0; y0 < PCE_FB_MAX_H;) {
        int y1 = y0 + 1;
        /* same scroll now and last frame: two bands whose scrolls just happen to meet (the
         * trees passing the parked carriage's) stay apart */
        while (y0 < rows && y1 < rows && rs[y1] == rs[y0] && w->row_x[y1] == w->row_x[y0]) y1++;
        bool all_blank = y0 < rows, all_still = true, at_edge = false;
        uint8_t was = 0;
        for (int y = y0; y < y1; y++) {
            all_blank &= y < rows && blank[y];
            all_still &= w->row_still[y] >= PARKED_FRAMES;
            at_edge |= y < rows && edge[y];
            if (w->row_clip[y] > was) was = w->row_clip[y];
        }
        uint8_t mode = 0;
        if (clip_parked && all_blank && all_still) mode = VDC_CLIP_BOTH;
        else if (clip_parked && was && (all_blank || at_edge)) mode = VDC_CLIP_RIGHT;
        for (int y = y0; y < y1; y++) w->row_clip[y] = mode;
        any_parked |= mode == VDC_CLIP_BOTH;
        y0 = y1;
    }
    for (int y = 0; y < rows; y++) w->row_x[y] = rs[y];
    vdc_set_row_clip(w->row_clip);

    /* objects standing still off the left while a band is parked; and screen-wide weather to
     * echo 256 px either side: objects fixed to the screen (not the HUD) that stay on it but
     * keep moving, such as Stage 2's rain (two objects sweeping down and left across it,
     * their drops about 128 px either side). A banner or a generator does not move. */
    uint8_t *table = p->page[0x00] ? p->page[0x00] + PARKED_TABLE : NULL;
    uint8_t *echo = p->page[0x00] ? p->page[0x00] + ECHO_TABLE : NULL;
    if (ext && s == RONDO_PLAYING) {
        int cam_now = (r[CAM_PAGE] & 0x0F) << 8 | r[CAM_XL], st = wrap(cam_now - rows_cam);
        rows_cam = cam_now;
        find_rows(r, st < -32 || st > 32 ? 0 : st);
        align_rows(r);
    }
    for (int i = 0; i < OBJ_SLOTS; i++) {
        int16_t x = (int16_t)(r[OBJ_XL + i] | r[OBJ_XH + i] << 8);
        w->obj_still[i] = x == w->obj_x[i] ? (uint8_t)(w->obj_still[i] < 255 ? w->obj_still[i] + 1 : 255) : 0;
        /* scenery the game keeps as a few objects it moves round from one side of its screen
         * to the other (Stage 2's bridge pillars: three, 128 px apart, kind $44, jumping 384):
         * noticed by such a jump, and drawn 256 px either side too (the echoes below), which
         * puts the copies where the next ones round would be */
        int jump = x - w->obj_x[i];
        if (r[OBJ_KIND + i] && r[OBJ_KIND + i] == w->obj_kind[i] && ((jump >= 376 && jump <= 392) || (jump <= -376 && jump >= -392)))
            recycled[r[OBJ_KIND + i]] = jumped[r[OBJ_KIND + i]] = true;
        w->obj_x[i] = x;
        bool hud = i == HUD_SLOT || i == SUBWEAPON_SLOT;
        bool parked = any_parked && !hud && r[OBJ_FRAME + i] && x < 0 && w->obj_still[i] >= PARKED_FRAMES;
        if (table) table[i] = parked;
        bool weather = ext && !hud && r[OBJ_KIND + i] && (r[OBJ_FLAGS + i] & 1) && x >= 0 && x < 256 &&
                       w->obj_still[i] < ECHO_STILL_FRAMES;
        bool round = ext && !hud && r[OBJ_KIND + i] && !(r[OBJ_FLAGS + i] & 1) && recycled[r[OBJ_KIND + i]];
        /* ...but not onto one of the row the game is hiding (frame 0: a pillar it leaves out
         * where the bridge ends): the copy 256 px on would stand in for it there. (The other
         * copy is 512 px from it, further than the picture is wide: never wanted at once.) */
        for (int j = 0; round && j < OBJ_SLOTS; j++) {
            if (j == i || r[OBJ_KIND + j] != r[OBJ_KIND + i] || r[OBJ_FRAME + j]) continue;
            int xj = (int16_t)(r[OBJ_XL + j] | r[OBJ_XH + j] << 8), d = xj - x;
            if ((d >= 254 && d <= 258) || (d <= -254 && d >= -258)) round = false;
        }
        /* ...nor near a room's end walls (the bridge's end piers stand where the next pillar
         * would: the game only ever moves its pillars round out of the 4:3 picture's sight) */
        if (round && scrolls && ((x + 256 > right - 96 && x + 256 < 256 + 2 * WIDE_MAX + 32) ||
                                 (x - 256 < left + 96 && x - 256 > -2 * WIDE_MAX - 32)))
            round = false;   /* (only a copy that would be seen: the other one may be wanted) */
        if (echo) echo[i] = weather || round;
    }

    /* Level edges (w->edges): within the extension of a room's end the picture moves over,
     * all the extra width going to the side the room goes on, so nothing beyond its walls
     * shows; it stays put there until the camera has come that far from the end. (A room
     * narrower than the picture is centred between its walls.) */
    int shift = 0;
    if (ext && scrolls && w->edges) shift = edge_shift(left, right, ext);
    vdc_set_wide_shift(shift);
    w->shift = shift;

    /* Enemies some rooms send in from just past the screen's edges, whatever the camera
     * does (the castle hall's ghouls at X -24 / 280, its kind-31 creatures at 272): put at
     * those places they would appear inside the wider picture, so on the frame one first
     * shows it moves out by the picture's extra width on that side, keeping its distance
     * from the edge, but no more than EDGE_SPAWN_MAX past the screen's edge (the creatures
     * remove themselves further out: kind 31 from 86 px). Only at those exact places (16, 24
     * or 32 px past the edge; things thrown from off screen come out where the thrower is),
     * and not objects fixed to the screen. */
    for (int i = 0; i < OBJ_SLOTS; i++) {
        uint8_t kind = r[OBJ_KIND + i];
        bool first = kind && r[OBJ_FRAME + i] && !(kind == w->obj_kind[i] && w->obj_shown[i]);
        w->obj_kind[i] = kind;
        w->obj_shown[i] = kind && r[OBJ_FRAME + i];
        if (!first || !ext || s != RONDO_PLAYING || !scrolls || (r[OBJ_FLAGS + i] & 1) ||
            i == HUD_SLOT || i == SUBWEAPON_SLOT)
            continue;
        int x = (int16_t)(r[OBJ_XL + i] | r[OBJ_XH + i] << 8), to = x;
        if (x == -16 || x == -24 || x == -32) {
            int d = -x + ext - shift;
            to = -(d < EDGE_SPAWN_MAX ? d : EDGE_SPAWN_MAX);
            if (to > x) to = x;
        } else if (x == 256 + 16 || x == 256 + 24 || x == 256 + 32) {
            int d = x - 256 + ext + shift;
            to = 256 + (d < EDGE_SPAWN_MAX ? d : EDGE_SPAWN_MAX);
            if (to < x) to = x;
        }
        if (to != x) {
            r[OBJ_XL + i] = (uint8_t)to;
            r[OBJ_XH + i] = (uint8_t)(to >> 8);
        }
    }

    /* sprites are built out to the picture's edges; objects spawn and start moving past the
     * side the picture reaches further on (no further than 120 px: the game removes them
     * 128 px out) */
    var_value[VAR_SPRITE_RIGHT] = var_value[VAR_ECHO_RIGHT] = (uint8_t)(0x20 + E + (shift > 0 ? shift : 0));
    var_value[VAR_SPRITE_LEFT] = (uint8_t)(0x100 - E - (shift < 0 ? -shift : 0));
    int base = object_reach(w->want);
    int reach_r = shift > 0 ? ext + shift + 8 : 0, reach_l = shift < 0 ? ext - shift + 8 : 0;
    int dr = reach_r > base ? (reach_r < 0x78 ? reach_r : 0x78) : base;
    int dl = reach_l > base ? (reach_l < 0x78 ? reach_l : 0x78) : base;
    var_value[VAR_SPAWN_RIGHT] = var_value[VAR_MOVE_RIGHT] = (uint8_t)dr;
    var_value[VAR_SPAWN_LEFT] = var_value[VAR_MOVE_LEFT] = (uint8_t)(0x100 - dl);
    update_vars(p);
    if (s == RONDO_PLAYING && (objects_out || w->gen_r || w->gen_l)) {
        /* (new generators come at the game's edge) */
        if (objects_out) move_generators(w, p, dr, dl);
        else {
            move_generators(w, p, 0x40, 0x40);
            w->gen_r = w->gen_l = 0;
        }
    }

    /* scenery bands carried on beside the picture (scenery_fill) */
    bool filled = ext && show && scrolls && s == RONDO_PLAYING && scenery_fill(p, cam);
    vdc_set_map_fill(filled ? scenery : NULL);

    p->vdc.wide = ext;
    if (menu) {
        p->vdc.wide_x0 = 0;
        p->vdc.wide_x1 = 256 + 2 * ext;
    } else if (scrolls) {
        p->vdc.wide_x0 = left + ext - shift;
        p->vdc.wide_x1 = right + ext - shift;
    } else {
        p->vdc.wide_x0 = 0;
        p->vdc.wide_x1 = 256 + 2 * ext;
    }
    /* (the HUD is put again as its sprites are built: hud_at_build) */
    if (s == RONDO_PLAYING) place_hud(p, ext - shift, ext + shift);
    else vdc_set_sprite_move(0, -1, 0);
    w->active = ext;
}

void wide_off(Wide *w, Pce *p)
{
    set_patches(p, WORLD_PATCHES | OBJECT_PATCHES | DRAW_PATCHES, false);
    move_generators(w, p, 0x40, 0x40);
    w->gen_r = w->gen_l = 0;
    vdc_set_wide_shift(0);
    vdc_set_side_tiles(NULL, NULL);
    vdc_set_side_real(false);
    vdc_set_map_fill(NULL);
    vdc_set_sprite_move(0, -1, 0);
    w->shift = 0;
    w->menu_good = 0;
    place_hud(p, 0, 0);
    w->patched = false;
    w->active = 0;
    p->vdc.wide = 0;
}

int wide_report(Pce *p, char *out, int n)
{
    int used = 0;
    for (int i = 0; i < N_PATCHES && used < n; i++) {
        const Patch *q = &patches[i];
        const uint8_t *m = at(p, q);
        const char *st = !m ? "unmapped" : !memcmp(m, q->wide, q->len) ? "in" : !memcmp(m, orig_of(q), q->len) ? "out" : "other code";
        used += snprintf(out + used, (size_t)(n - used), "  %02X:%04X %-10s %s\n", q->bank, q->off, st, q->what);
    }
    return used;
}
