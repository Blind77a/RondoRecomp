/* See wide.h. */
#include "wide.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game.h"

/* ---- changes to the game --------------------------------------------------------------- */

/* A change to the game: `len` bytes at physical bank:offset, applied only when the bytes
 * there are `orig` (so other code loaded into the same RAM is left alone). */
typedef struct Patch {
    const char *what;
    uint8_t bank;
    uint16_t off;          /* offset within the 8 KiB bank */
    uint8_t len;
    uint8_t orig[64];
    uint8_t wide[64];
} Patch;

#define FILL60_16 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60
#define FILL60_52 /* 52 bytes of our bank $00's RTS filler */ \
    {FILL60_16, FILL60_16, FILL60_16, 0x60, 0x60, 0x60, 0x60}

#define E 112              /* how far past the game's 256 pixels the changes reach */

static const Patch patches[] = {
    /* ---- the world: put in or taken out as a room starts ---- */

    /* Background streaming (bank $6F, the scroll-layer code; every layer and both axes
     * use it): when the camera crosses a 32-pixel column, the game draws the column 64
     * pixels past the edge it is moving toward. The table holds that distance (low
     * byte; the page byte after it stays +1/-1). 0x70/0x90 draws 112 pixels out, which
     * still fits the 512-pixel background map (256 + 2 * 112 + 32 = 512). */
    {"background columns streamed 112 px out", 0x6F, 0x0780, 2, {0x40, 0xC0}, {0x70, 0x90}},
    /* Some layers are first drawn by stage code out to the old 64 px only, which would leave
     * a 48 px gap the new distance never fills. So each streamed column (6F:A7A5) is drawn
     * twice: at the old distance, then at the new one, by code in free space in our own
     * bank $00 (mapped at $E000 throughout) that switches the table above between the two. */
    {"streamed columns drawn at the old and the new distance", 0x6F, 0x07A5, 3, {0xBD, 0x66, 0x3E}, {0x4C, 0x10, 0xFE}},
    {"column drawing at both distances", 0x00, 0x1E10, 52, FILL60_52,
     {0x5A,                     /* PHY                              */
      0xDA,                     /* PHX                              */
      0xA9, 0x40,               /* LDA #$40      the old distances  */
      0x8D, 0x80, 0xA7,         /* STA $A780                        */
      0xA9, 0xC0,               /* LDA #$C0                         */
      0x8D, 0x81, 0xA7,         /* STA $A781                        */
      0x20, 0x2E, 0xFE,         /* JSR draw                         */
      0xFA,                     /* PLX                              */
      0x7A,                     /* PLY                              */
      0xA9, 0x70,               /* LDA #$70      the new distances  */
      0x8D, 0x80, 0xA7,         /* STA $A780                        */
      0xA9, 0x90,               /* LDA #$90                         */
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
     * the table above each column lands 48 px further right, so start 480 px left
     * (camera + 0x20 - 0x200) and draw all 16 columns. */
    {"room start draws 16 columns", 0x6F, 0x0963, 7,
     {0xA9, 0x0C, 0x85, 0x16, 0xA9, 0x80, 0x44}, {0xA9, 0x0F, 0x85, 0x16, 0xA9, 0x20, 0x44}},
    /* The other room start (6F:A9B1, e.g. Stage 2' on the lake bridge) draws 16 columns from
     * a temporary camera 320 px left: the whole map, from the camera to 512 px right. Some
     * bands are drawn only then and repeat every 512 px (the hedges under the bridge). With
     * the table above each column would land 48 px further right, the last one on the map's
     * first column again with what lies 512 px on; start 368 px left so the columns land
     * where they did. */
    {"other room start draws the map where it did", 0x6F, 0x09BA, 2, {0xA9, 0xC0}, {0xA9, 0x90}},
    /* Rows streamed while the camera moves up or down (6F:A8D5) span the original 256
     * pixels from the camera; draw 16 blocks from 112 px left instead (the whole map). */
    {"rows drawn 512 px wide", 0x6F, 0x08D5, 7,
     {0xAD, 0x66, 0x3E, 0x29, 0x7F, 0x85, 0x08}, {0xA9, 0x10, 0x85, 0x08, 0xEA, 0xEA, 0xEA}},
    {"rows start 112 px left", 0x6F, 0x08EC, 2, {0x10, 0x15}, {0xEA, 0xEA}},

    /* Objects live in screen coordinates. Spawning (bank $69): objects in the room's list
     * (sorted by world X) start when they come within camera + $140 (64 px past the right
     * edge) or camera - $40; start them 128 px out instead (camera + $180 / - $80). */
    {"objects spawn 128 px right of the screen", 0x69, 0x1E68, 2, {0x69, 0x40}, {0x69, 0x80}},
    {"objects spawn 128 px left of the screen", 0x69, 0x1E7F, 2, {0x69, 0xC0}, {0x69, 0x80}},
    /* Object update (bank $70, 70:A000): objects more than 128 px off either side are
     * removed (screen X < -128 or >= 384); keep them to -192 / 448 so ones that just
     * spawned at 384 are not removed at once (but not through a door: wide_before_frame)... */
    {"objects removed 192 px left of the screen", 0x70, 0x0070, 1, {0x80}, {0x40}},
    {"objects removed 192 px right of the screen", 0x70, 0x007D, 1, {0x80}, {0xC0}},
    /* ...and objects that only move near the screen wait at -64 / 320; let them move
     * from -128 / 384, where they now spawn. */
    {"objects move from 128 px left of the screen", 0x70, 0x00B0, 1, {0xC0}, {0x80}},
    {"objects move from 128 px right of the screen", 0x70, 0x00BD, 1, {0x40}, {0x80}},

    /* ---- drawing: follows each frame's choice of a wider picture or the game's own
     * (sprites past the 256 pixels would only take up the 64 hardware sprites) ---- */

    /* Sprite building (bank $72, 72:8000): each sprite piece is dropped unless its X
     * (screen + 32) is 0..$11F, and for X + 32 below 32 unless it shows at all; let pieces
     * through from -E to $11F + E. */
    {"sprites drawn up to 112 px right of the screen", 0x72, 0x0133, 1, {0x20}, {0x20 + E}},
    {"sprites just left of the screen drawn", 0x72, 0x013A, 2, {0xDD, 0xA1}, {0x80, 0x03}},   /* BRA $813F */
    {"negative sprite X goes to the check below", 0x72, 0x0127, 3, {0x4C, 0x8C, 0x81}, {0x4C, 0x00, 0xFE}},
    /* ...in free space in our own bank $00: A = X + 32's high byte (negative). Keep the
     * piece if it is $FF and the low byte >= -E. */
    {"negative sprite X check", 0x00, 0x1E00, 16, {FILL60_16},
     {0xC9, 0xFF,               /* CMP #$FF          */
      0xD0, 0x09,               /* BNE drop          */
      0xA5, 0x05,               /* LDA <$05          */
      0xC9, 0x100 - E,          /* CMP #-E           */
      0x90, 0x03,               /* BCC drop          */
      0x4C, 0x3F, 0x81,         /* JMP $813F (keep)  */
      0x4C, 0x8C, 0x81}},       /* drop: JMP $818C   */
};
#define N_PATCHES (int)(sizeof patches / sizeof patches[0])
#define N_WORLD 15         /* patches[0..14] change the game world, the rest only drawing */
#define WORLD_PATCHES ((1u << N_WORLD) - 1)
#define DRAW_PATCHES (((1u << N_PATCHES) - 1) & ~WORLD_PATCHES)
/* The removal distances (patches[11-12]) are the game's own while it goes through a door:
 * the rest of the world changes are what the wider picture needs. */
#define REMOVAL_PATCHES ((1u << 11) | (1u << 12))
#define CORE_PATCHES (WORLD_PATCHES & ~REMOVAL_PATCHES)

static uint8_t *at(Pce *p, const Patch *q) { return p->page[q->bank] ? p->page[q->bank] + q->off : NULL; }

/* Diagnostics: patches to leave out (bit i = patches[i]), counted as in place. */
uint32_t wide_debug_skip;

/* Which patches are in place. The distance table (patches[0]) holds the old distances
 * while the code drawing a column at both (patches[1-2]) is part-way through, which a
 * frame can end in: that counts as in. */
static uint32_t patches_in(Pce *p)
{
    uint32_t mask = wide_debug_skip;
    for (int i = 0; i < N_PATCHES; i++) {
        const uint8_t *m = at(p, &patches[i]);
        if (m && !memcmp(m, patches[i].wide, patches[i].len)) mask |= 1u << i;
    }
    const uint8_t *t = at(p, &patches[0]);
    if ((mask & 6) == 6 && t && !memcmp(t, patches[0].orig, patches[0].len)) mask |= 1;
    return mask;
}

/* Put the patches in `mask` in (or take them out) where the original (or patched) bytes
 * are found. */
static void set_patches(Pce *p, uint32_t mask, bool on)
{
    for (int i = 0; i < N_PATCHES; i++) {
        if (!(mask & 1u << i) || (wide_debug_skip & 1u << i)) continue;
        const Patch *q = &patches[i];
        uint8_t *m = at(p, q);
        if (!m) continue;
        const uint8_t *from = on ? q->orig : q->wide, *to = on ? q->wide : q->orig;
        if (!memcmp(m, from, q->len)) {
            memcpy(m, to, q->len);
            p->cpu.page_gen[q->bank]++;   /* recompiled code in this bank is re-checked */
        }
    }
}

/* Save states from 0.11-0.12, whose widescreen drew objects parked off the left of the
 * Prologue's picture in its own way, can hold that version's sprite changes: put the game's
 * code back as it was (and clear what they used in our bank $00), so the changes above
 * apply. */
static void undo_old_changes(Pce *p)
{
    static const uint8_t old_left[] = {0x4C, 0x62, 0xFE}, game_left[] = {0xDD, 0xA1, 0x81};
    static const uint8_t old_check_end[] = {0x4C, 0x50, 0xFE, 0x4C, 0x8C, 0x81};
    uint8_t *b72 = p->page[0x72], *b00 = p->page[0x00];
    if (!b72 || !b00 || memcmp(b72 + 0x013A, old_left, 3)) return;
    memcpy(b72 + 0x013A, game_left, 3);
    if (!memcmp(b00 + 0x1E00 + 10, old_check_end, sizeof old_check_end)) memset(b00 + 0x1E00, 0x60, 16);
    memset(b00 + 0x1E50, 0x60, 0x1ED8 - 0x1E50);   /* its drawing checks and parked-object table */
    p->cpu.page_gen[0x72]++;
    p->cpu.page_gen[0x00]++;
}

int wide_extent(double window_aspect, double picture_aspect)
{
    double w = 256.0 * window_aspect / picture_aspect;
    int e = (int)floor((w - 256.0) / 2.0);
    if (e < 0) e = 0;
    if (e > WIDE_MAX) e = WIDE_MAX;
    return e & ~1;
}

/* ---- the game's RAM ------------------------------------------------------------------- */

/* Scroll layers (work RAM): layer 0 is the playfield camera. A layer's X is 12 bits (the
 * level is 16 screens of 256 pixels). Late in each frame (line ~246, 69:4206...) the game
 * copies each layer's X for the next frame's picture, whose raster interrupts set the VDC's
 * X scroll for each band of rows from those copies (69:42FE). */
#define CAM_XL 0x1E08        /* X low byte, layers 0-7 */
#define CAM_PAGE 0x1E96      /* X high byte mod 16, layers 0-7 */
#define SHOWN_XL 0x1E39      /* the copies: X low byte, layers 0-7 */
#define SHOWN_XH 0x1E42      /* X high byte */
#define LIMIT_LEFT 0x1EBE    /* camera's leftmost page */
#define LIMIT_RIGHT 0x1EC6   /* camera's rightmost X: page, low byte */
/* X velocities, 8.8, layers 0-7 (added to the layers each frame by 6F:A6F4). Each stage's
 * scroll code sets a parallax layer's from the camera's every frame (halved, quartered, ...),
 * plus a steady drift for clouds. */
#define VEL_LO 0x1E7E
#define VEL_HI 0x1E86
#define AREA 0xA2            /* zero page: the area being played ($0A / $0B: the Prologue) */

/* Object arrays (88 slots, work RAM offsets) */
#define OBJ_SLOTS 0x58
#define OBJ_KIND 0x0750
#define OBJ_FLAGS 0x10F0     /* bit 0: fixed to the screen (does not scroll) */
#define OBJ_XL 0x0BC8
#define OBJ_XH 0x0B70
#define OBJ_TYPE 0x0A10
#define OBJ_FRAME 0x0A68     /* sprite frame drawn; 0 = none */
#define OBJ_LAYER 0x0F38     /* low 7 bits: the scroll layer the object moves with (0: the camera) */
#define HUD_SLOT 0x57        /* the HUD (health, hearts, score): type 5, screen X 8 */
#define SUBWEAPON_SLOT 0x56  /* the sub-weapon icon hanging from the HUD: type 3, screen X 28 */
#define DRAWN_SLOT 0xD9      /* zero page: the object the sprite builder is drawing */
#define PROLOGUE_RAIN 0x26   /* object kind: the Prologue's rain, cycled around X 0-255 */
#define SUB_TITLE 0x05       /* RONDO_SUB while a stage's title shows */
#define SUB_DOOR 0x03        /* RONDO_SUB while leaving a room, by a door or off its edge (6B:8260) */
#define WAY_OUT 0x84         /* zero page: how the room is being left, 1-3 a door's type */

/* signed distance in the camera's 4096-pixel space */
static int wrap(int d) { d &= 0xFFF; return d >= 0x800 ? d - 0x1000 : d; }

/* going through a door (not walking off a room's edge, which cuts to the next room) */
static bool in_door(const Pce *p)
{
    const uint8_t *r = p->wram;
    return game_state(p) == RONDO_PLAYING && r[RONDO_SUB] == SUB_DOOR && r[WAY_OUT] >= 1 && r[WAY_OUT] <= 3;
}

static uint16_t layer_x(const uint8_t *r, int i) { return (uint16_t)((r[CAM_PAGE + i] & 0x0F) << 8 | r[CAM_XL + i]); }

/* the X scroll the next picture's band from layer i is drawn with */
static uint16_t shown_bxr(const uint8_t *r, int i) { return (uint16_t)((r[SHOWN_XH + i] << 8 | r[SHOWN_XL + i]) & 0x3FF); }

/* where the next picture shows layer i, 12 bits: the layer as it is, less what it has moved
 * since its copy was made. Only the copy's low 9 bits count: the map is 512 pixels wide, and
 * some stages keep the copy modulo 512 (Stage 5 starts with the camera at $600, the copy 0). */
static uint16_t shown_x(const uint8_t *r, int i)
{
    int x = layer_x(r, i), d = (shown_bxr(r, i) - x) & 0x1FF;
    if (d >= 0x100) d -= 0x200;
    return (uint16_t)((x + d) & 0xFFF);
}
static uint16_t limit_l(const uint8_t *r) { return (uint16_t)((r[LIMIT_LEFT] & 0x0F) << 8); }
static uint16_t limit_r(const uint8_t *r) { return (uint16_t)((r[LIMIT_RIGHT] & 0x0F) << 8 | r[LIMIT_RIGHT + 1]); }
static int velocity(const uint8_t *r, int i) { return (int16_t)(r[VEL_HI + i] << 8 | r[VEL_LO + i]); }
static bool in_prologue(const uint8_t *r) { return (r[AREA] & 0xFE) == 0x0A; }

/* Some objects are generators fixed to the screen at the old spawn edge (e.g. Stage 1's
 * skeleton generator sits at screen X 320 and drops skeletons there, which the wider
 * picture would show). Move ones at exactly 320 / -64 to the new edge, 384 / -128 (and
 * back when the changes are out). */
static void move_generators(Pce *p, bool on)
{
    uint8_t *r = p->wram;
    for (int i = 0; i < OBJ_SLOTS; i++) {
        if (!r[OBJ_KIND + i] || !(r[OBJ_FLAGS + i] & 1)) continue;
        int x = r[OBJ_XL + i] | r[OBJ_XH + i] << 8;
        int to = -1;
        if (on && x == 0x0140) to = 0x0180;
        else if (on && x == 0xFFC0) to = 0xFF80;
        else if (!on && x == 0x0180) to = 0x0140;
        else if (!on && x == 0xFF80) to = 0xFFC0;
        if (to >= 0) {
            r[OBJ_XL + i] = (uint8_t)to;
            r[OBJ_XH + i] = (uint8_t)(to >> 8);
        }
    }
}

/* The two routines that draw a room when it starts (6F:A94C, 6F:A9B1; checked by their
 * first bytes). The game has just loaded the room's header, camera limits included. */
static const uint8_t ROOM_START_A[] = {0x82, 0xA9, 0x90, 0x9D, 0x66, 0x3E};   /* A94C */
static const uint8_t ROOM_START_B[] = {0xA4, 0x45, 0xB9, 0x94, 0xA9, 0x82};   /* A9B1 */

/* the camera can move sideways in this room */
static bool room_scrolls(const uint8_t *r) { return wrap(limit_r(r) - limit_l(r)) > 0; }

/* Called at every dispatch point (Pce.on_entry): when a room starts, put the world
 * changes in for rooms the camera scrolls sideways through, and take them out for every
 * other room, whose background map may be shared out between layers in ways the wider
 * drawing would overwrite (the Prologue's carriage chase is one). */
static void on_entry(Pce *p, void *ctx)
{
    uint16_t pc = p->cpu.pc;
    if ((pc != 0xA94C && pc != 0xA9B1) || p->cpu.mpr[5] != 0x6F || !p->page[0x6F]) return;
    const uint8_t *code = p->page[0x6F] + (pc & 0x1FFF);
    if (memcmp(code, pc == 0xA94C ? ROOM_START_A : ROOM_START_B, 6)) return;
    Wide *w = ctx;
    set_patches(p, WORLD_PATCHES, w->want > 0 && room_scrolls(p->wram));
    w->room_started = true;
}

/* ---- the picture ----------------------------------------------------------------------
 * Decided after each frame for the next one, from the camera as the frame left it (the
 * next frame's picture is drawn with the scroll the game sets from it), and kept in the
 * machine state (Vdc.wide_state), so a loaded state or a rewind goes on with the picture
 * as it was:
 *   [0] 'W' once decided (0: decide afresh)     [1] extension a side
 *   [2] the picture's offset from centered on the game's 256 pixels (signed; the picture's
 *       left edge is at screen x -ext + offset)
 *   [3] ST_GLIDE, ST_DOOR, ST_PROLOGUE (see decide)
 *   [4..10] layers 1-7: the same for a band of rows drawn from that layer (signed)
 *   [11] the room's left wall (page)  [12..13] its right wall (low, high; ST_WALLS in [13]):
 *        the camera's limits at their widest since the room started */

#define ST_MAGIC 'W'
#define ST_GLIDE 1         /* the picture is gliding to where it belongs */
#define ST_DOOR 2          /* held while going through a door */
#define ST_PROLOGUE 4      /* the Prologue's title has shown: its carriage chase is wider */
#define ST_WALLS 0x80      /* in [13]: the room's walls are kept */
#define GLIDE_STEP 3       /* pixels a frame a glide moves the picture, beyond the camera's own */
#define EXT_STEP 2         /* pixels a frame the extension grows or shrinks within a room */
#define PROLOGUE_STEP 1    /* pixels a frame each side grows by as the Prologue widens */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The extension this room allows: the window's, at most half the camera's sideways
 * travel between the room's walls (the picture never shows past both at once), 0 in a room
 * the changes are not in (or one screen wide), and outside gameplay. */
static int room_extent(const Wide *w, const Pce *p, int left, int right)
{
    if (w->want <= 0 || game_state(p) != RONDO_PLAYING || !w->patched) return 0;
    if (((p->vdc.reg[9] >> 4) & 3) < 1) return 0;   /* background map 64+ tiles wide */
    int e = (right - left) / 2;
    if (e > w->want) e = w->want;
    return e < 0 ? 0 : e & ~1;
}

/* The room's walls for the picture: the camera's limits, at their widest since the room
 * started. A boss arena closes the limits in on the camera; the picture keeps the room's
 * walls, so it stays wide there, showing the room the player came through. */
static void room_walls(Pce *p, bool fresh, uint16_t *wl, uint16_t *wr)
{
    const uint8_t *r = p->wram;
    uint8_t *st = p->vdc.wide_state;
    uint16_t l = limit_l(r), rr = limit_r(r);
    if (!fresh && st[0] == ST_MAGIC && (st[13] & ST_WALLS)) {
        uint16_t kl = (uint16_t)((st[11] & 0x0F) << 8), kr = (uint16_t)((st[13] & 0x0F) << 8 | st[12]);
        if (wrap(kl - l) < 0) l = kl;
        if (wrap(kr - rr) > 0) rr = kr;
    }
    st[11] = (uint8_t)(l >> 8);
    st[12] = (uint8_t)rr;
    st[13] = (uint8_t)(ST_WALLS | rr >> 8);
    *wl = l;
    *wr = rr;
}

/* The Prologue's carriage chase: one screen (the camera never moves) whose scenery layers
 * loop round the whole background map, and whose rain cycles round the game's 256 pixels
 * (drawn wrapping: sprite_kinds). Before it drives in, the carriage waits off both
 * edges of the picture and its horses off the left, so the picture stays 4:3 until the
 * stage's title shows; then it grows out, centered on the carriage, a pixel each side a
 * frame, the bands centered with it. Returns false outside the Prologue. */
static bool prologue_picture(Wide *w, Pce *p)
{
    const uint8_t *r = p->wram;
    uint8_t *st = p->vdc.wide_state;
    if (w->want <= 0 || game_state(p) != RONDO_PLAYING || !in_prologue(r)) return false;
    if (st[0] != ST_MAGIC) memset(st, 0, 14);
    if (r[RONDO_SUB] == SUB_TITLE) st[3] |= ST_PROLOGUE;
    int ext = 0;
    if (st[3] & ST_PROLOGUE) ext = clampi(w->want, 0, st[1] + PROLOGUE_STEP);
    st[0] = ST_MAGIC;
    st[1] = (uint8_t)ext;
    st[2] = 0;
    st[3] &= ST_PROLOGUE;
    for (int i = 1; i < 8; i++) st[3 + i] = 0, w->sub[i] = 0;
    return true;
}

/* Decide the next frame's picture (see above). fresh: no history to go on (a room just
 * started, a state was loaded from before widescreen, gameplay just began). */
static void decide(Wide *w, Pce *p, bool fresh)
{
    const uint8_t *r = p->wram;
    uint8_t *st = p->vdc.wide_state;
    if (st[0] != ST_MAGIC) fresh = true;
    if (prologue_picture(w, p)) return;

    uint16_t wall_l, wall_r;
    room_walls(p, fresh, &wall_l, &wall_r);
    /* a layer's rate is learnt again in each room, from 1 (as the camera) */
    if (fresh || !w->rates_set) {
        for (int i = 0; i < 8; i++) w->rate[i] = 1.0f, w->vdrift[i] = 0;
        w->measured = 0;
    }
    w->rates_set = true;

    /* Through a door the camera scrolls a screen on past the room's wall, the next room
     * starts (its camera stepped across the level for a dozen frames to draw it, with the
     * picture still showing) and the door closes behind the player. Hold the picture as it
     * was until play goes on, then grow or glide it into the new room from there, so the
     * columns only that room start draws are never shown half done and nothing jumps. */
    if (st[0] == ST_MAGIC && w->want > 0 && in_door(p)) {
        st[3] |= ST_DOOR;
        return;
    }
    bool after_door = st[0] == ST_MAGIC && (st[3] & ST_DOOR);
    if (after_door) fresh = false;

    uint16_t x[8];   /* where the next picture shows each layer */
    for (int i = 0; i < 8; i++) x[i] = shown_x(r, i);
    int d0 = wrap(x[0] - w->before_x[0]);
    if (d0 > 64 || d0 < -64) fresh = true;   /* the camera jumped */
    /* nothing shown (background and sprites off): a room being set up, its camera stepped
     * across the level to draw it */
    if (!(p->vdc.reg[5] & 0xC0)) fresh = true;
    bool walls_moved = wall_l != w->before_l || wall_r != w->before_r;

    /* the room's walls, from the camera (left <= 0 <= right) */
    int left = wrap(wall_l - x[0]), right = wrap(wall_r - x[0]);
    if (left > 0) left = 0;
    if (right < 0) right = 0;

    int ext_was = st[1], off_was = (int8_t)st[2];
    int target = room_extent(w, p, left, right);
    if (after_door && target == 0) fresh = true;   /* into a room kept at 4:3: as the game shows it */
    int ext = fresh ? target : clampi(target, ext_was - EXT_STEP, ext_was + EXT_STEP);

    /* the picture's left edge (screen x): centered on the camera, slid in from a wall */
    int xl = right - left >= 2 * ext ? clampi(-ext, left, right - 2 * ext) : (left + right) / 2 - ext;
    int off = xl + ext;
    bool glide = !fresh && (st[3] & ST_GLIDE);
    /* the walls moved, or a door was just gone through: no jump */
    if (!fresh && (walls_moved || after_door)) glide = true;
    int step = (d0 < 0 ? -d0 : d0) + GLIDE_STEP;
    if (glide) {
        off = clampi(off, off_was - step, off_was + step);
        if (off == xl + ext) glide = false;
    }
    if (ext == 0) off = 0, glide = false;

    /* Bands drawn from the other layers. The game moves a parallax layer at its own rate k
     * against the camera (it sets the layer's velocity from the camera's each frame, plus a
     * drift for clouds), so the layer's art reaches only as far as the game ever shows it:
     * from where the layer is with the camera at the left wall to where it is at the right,
     * plus 256 pixels. Its band is placed as the picture is, between its own walls (k times
     * the camera's): centered, slid in at a wall, or centered between them where the layer
     * moves less than the wider picture needs. It moves there no faster than the picture
     * glides. k is the velocity's share of the camera's, less what the layer had while the
     * camera stood still. */
    int v0 = velocity(r, 0);
    for (int i = 1; i < 8; i++) {
        int vi = velocity(r, i);
        if (v0 == 0) {
            w->vdrift[i] = vi;
            /* a layer moving on its own (clouds) is guessed not to follow the camera */
            if (vi && !(w->measured & 1u << i)) w->rate[i] = 0.0f;
        } else if (v0 >= 0x20 || v0 <= -0x20) {
            w->rate[i] = clampf((float)(vi - w->vdrift[i]) / (float)v0, -1.0f, 2.0f);
            w->measured |= (uint8_t)(1u << i);
        }
        float li = w->rate[i] * (float)left, ri = w->rate[i] * (float)right;
        if (li > ri) { float t = li; li = ri; ri = t; }
        float place = (ri - li >= 2.0f * (float)ext ? clampf((float)-ext, li, ri - 2.0f * (float)ext) : (li + ri) / 2.0f - (float)ext) + (float)ext;
        /* after a state was loaded or rewound to: from where its picture had the layer */
        if (!fresh && lrintf(w->sub[i]) != (int8_t)st[3 + i]) w->sub[i] = (int8_t)st[3 + i];
        w->sub[i] = fresh ? place : clampf(place, w->sub[i] - (float)step, w->sub[i] + (float)step);
        /* within what the background changes draw: E pixels past the game's picture */
        float lim = (float)(E - ext);
        w->sub[i] = clampf(w->sub[i], -lim, lim);
        if (ext == 0) w->sub[i] = 0;
    }

    st[0] = ST_MAGIC;
    st[1] = (uint8_t)ext;
    st[2] = (uint8_t)(int8_t)off;
    st[3] = glide ? ST_GLIDE : 0;
    for (int i = 1; i < 8; i++) st[3 + i] = (uint8_t)(int8_t)lrintf(w->sub[i]);
}

/* Where each object's sprites go (VDC_SPRITE_*). The object update (70:A000) moves the
 * objects in slots $10-$57 that have a kind with the camera, or with the scroll layer
 * OBJ_LAYER names, unless they are fixed to the screen; slots below $10 are the player's
 * (moved by the player's code, with the camera). What it leaves alone, kind 0 with a
 * sprite, is fixed to the screen: the HUD (kept at the picture's left edge), a stage's
 * title (centered in it; its letters slide in from X -104) and a boss's health gauge (at X
 * 216: kept as far from the picture's right edge). The Prologue's rain is cycled around X
 * 0-255 by its own code (24 pixels a frame), its drops repeating every 256 pixels: drawn
 * wrapping, it falls past both edges of the game's picture into the wider one. */
static void sprite_kinds(Wide *w, const uint8_t *r)
{
    bool prologue = in_prologue(r);
    for (int i = 0; i < OBJ_SLOTS; i++) {
        uint8_t k = VDC_SPRITE_LEVEL;
        int x = (int16_t)(r[OBJ_XH + i] << 8 | r[OBJ_XL + i]);
        if ((i == HUD_SLOT && r[OBJ_TYPE + i] == 0x05) || (i == SUBWEAPON_SLOT && r[OBJ_TYPE + i] == 0x03))
            k = VDC_SPRITE_LEFT;
        else if (i >= 0x10 && (!r[OBJ_KIND + i] || (r[OBJ_FLAGS + i] & 1)))
            k = x >= 192 && x < 256 ? VDC_SPRITE_RIGHT : VDC_SPRITE_CENTER;
        else if (i >= 0x10 && (r[OBJ_LAYER + i] & 0x7F) < 8)
            k = (uint8_t)(VDC_SPRITE_BAND + (r[OBJ_LAYER + i] & 0x7F));
        if (prologue && i >= 0x10 && r[OBJ_KIND + i] == PROLOGUE_RAIN) k |= VDC_SPRITE_WRAP;
        w->kind[i] = k;
    }
}

void wide_before_frame(Wide *w, Pce *p)
{
    uint8_t *r = p->wram;
    uint8_t s = game_state(p);
    if (!p->on_entry) {
        p->on_entry = on_entry;
        p->on_entry_ctx = w;
    }

    /* outside gameplay the game is as it was; rooms put the changes back as they start */
    if (s != RONDO_PLAYING || w->want <= 0)
        set_patches(p, WORLD_PATCHES, false);
    /* Going through a door (6B:8260) the game first waits for every enemy to be gone
     * (6B:846F: no object in slots 1-$50 of a kind below $80). Objects it would have removed
     * already, kept by the wider removal distances where the camera stopped, would hold it up
     * for good (two at -160 past Stage 4's second bone serpent): the game's own distances
     * then, so it removes them itself. */
    else if ((patches_in(p) & CORE_PATCHES) == CORE_PATCHES)
        set_patches(p, REMOVAL_PATCHES, r[RONDO_SUB] != SUB_DOOR);
    uint32_t in = patches_in(p);
    w->patched = (in & CORE_PATCHES) == CORE_PATCHES;
    if (s == RONDO_PLAYING) move_generators(p, (in & WORLD_PATCHES) == WORLD_PATCHES);

    /* this frame's picture, decided after the last one (or now) */
    uint8_t *st = p->vdc.wide_state;
    for (int i = 0; i < 8; i++) w->before_x[i] = shown_x(r, i);
    bool kept = st[0] == ST_MAGIC && (st[13] & ST_WALLS);
    w->before_l = kept ? (uint16_t)((st[11] & 0x0F) << 8) : limit_l(r);
    w->before_r = kept ? (uint16_t)((st[13] & 0x0F) << 8 | st[12]) : limit_r(r);
    w->room_started = false;

    if (st[0] != ST_MAGIC || s != RONDO_PLAYING || w->want <= 0) decide(w, p, true);
    int ext = st[1], off = (int8_t)st[2];

    /* sprites past the game's 256 pixels are built only while the picture is wider */
    undo_old_changes(p);
    set_patches(p, DRAW_PATCHES, ext > 0);
    if (ext > 0 && (patches_in(p) & DRAW_PATCHES) != DRAW_PATCHES) {
        set_patches(p, DRAW_PATCHES, false);
        ext = 0;
    }

    w->active = ext;
    w->x0 = ext ? off - ext : 0;
    if (!ext) {
        vdc_set_wide(NULL);
        vdc_set_sprite_kinds(NULL, NULL, 0);
        return;
    }
    VdcWide *v = &w->vdc;
    v->ext = ext;
    v->x0 = off - ext;
    v->n_bands = 8;
    for (int i = 0; i < 8; i++) {
        v->band[i].bxr = shown_bxr(r, i);
        v->band[i].x0 = (int16_t)((i ? (int8_t)st[3 + i] : off) - ext);
    }
    vdc_set_wide(v);
    sprite_kinds(w, r);
    vdc_set_sprite_kinds(&r[DRAWN_SLOT], w->kind, OBJ_SLOTS);
}

void wide_after_frame(Wide *w, Pce *p)
{
    w->patched = (patches_in(p) & CORE_PATCHES) == CORE_PATCHES;   /* a room may have started */
    decide(w, p, w->room_started || game_state(p) != RONDO_PLAYING);
}

void wide_off(Wide *w, Pce *p)
{
    set_patches(p, WORLD_PATCHES | DRAW_PATCHES, false);
    move_generators(p, false);
    memset(p->vdc.wide_state, 0, sizeof p->vdc.wide_state);
    w->patched = false;
    w->active = 0;
    w->x0 = 0;
    vdc_set_wide(NULL);
    vdc_set_sprite_kinds(NULL, NULL, 0);
}

void wide_reset(Wide *w) { w->rates_set = false; }

int wide_report(Pce *p, char *out, int n)
{
    int used = 0;
    for (int i = 0; i < N_PATCHES && used < n; i++) {
        const Patch *q = &patches[i];
        const uint8_t *m = at(p, q);
        const char *st = !m ? "unmapped" : !memcmp(m, q->wide, q->len) ? "in" : !memcmp(m, q->orig, q->len) ? "out" : "other code";
        used += snprintf(out + used, (size_t)(n - used), "  %02X:%04X %-10s %s\n", q->bank, q->off, st, q->what);
    }
    return used;
}
