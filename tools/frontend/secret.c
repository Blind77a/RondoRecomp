/* The Konami code door in Stage 1's house and the hidden room behind it (secret.h). */
#include "secret.h"

#include <math.h>
#include <string.h>

#include "game.h"
#include "pce/disc.h"
#include "pce/sound.h"
#include "pce/video.h"

/* work RAM (offsets in bank $F8) */
#define W_EVENT 0x38        /* an object's request to the main loop: 3 leaves the room */
#define W_EXIT 0x8C         /* the way out: bit 7, walking off the room's edge */
#define W_STEP 0x82         /* the route step (the grid comes from it, the room from the route) */
#define W_ROOM 0x83
#define W_AREA 0xA2
#define W_CAM_PAGE 0x1E96   /* the camera's X page and X */
#define W_CAM_X 0x1E08
#define W_PX_HI 0x0B70      /* the player (object 0): X, Y (picture coordinates) */
#define W_PX 0x0BC8
#define W_PY_HI 0x0C78
#define W_PY 0x0CD0

/* Stage 1's first area (area $10), as the USA disc loads it */
#define AREA 0x10
#define HOUSE 1             /* the house's room, and its route step */
#define B_ROOMS 0x6B        /* the area's room tables, at $8000 */
#define GRID_PTRS 0x9C30    /* a grid list for each route step */
#define HEAD_PTRS 0x9C71    /* a header for each room */
#define HOUSE_GRID 0x9C44
#define HOUSE_HEAD 0x9C91
#define B_SCREENS 0x6F      /* the area's screens: 64 block numbers each, at $A9EC, and a bit
                             * more for each block (block numbers are 9 bits) at $AFEC */
#define SCREENS 0xA9EC
#define SCREEN_HI 0xAFEC
/* ours, in the room bank's free space after the room tables (all $FF on the disc) */
#define OUR_GRID 0x9E00
#define OUR_HEAD_IN 0x9E10
#define OUR_HEAD_OUT 0x9E20
#define STASH 0x9F00        /* "RSEC", then the street's two screens and their bits */
#define FREE_END 0x9FE0
#define BORROW_A 1          /* the street's screens lent to the hidden room */
#define N_SCREENS 3        /* the hidden room's screens (level X pages 2-4) */
#define STASH_LEN (N_SCREENS * 64 + N_SCREENS * 8)

static const uint8_t HOUSE_HEADER[14] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00,
                                         0x01, 0x01, 0x20, 0xAA, 0x00, 0x00};
static const uint8_t HOUSE_SCREEN[8] = {0x90, 0x91, 0x92, 0x93, 0x9D, 0x9E, 0x9F, 0xA0};

/* The hidden room: two screens side by side at the level's X pages 2 and 3, Y page 1 (the
 * house's row; the house is page 0), so the house's own objects, all on page 0, stay out of
 * reach. Stage 1's blocks: E8-ED stone and the dark interior's brick (E9 the ceiling, EA/ED
 * the wall's edge), EE/EF the dark brick wall (EF with a pillar, EB/EC above them), F1 a
 * barred window (two blocks tall), F0 a chair, 118 the floor's ledge, 119 below the floor,
 * 9F/A1 the house's arched door (the way back). Row 7 is below the picture. */
static uint16_t layout[8][8 * N_SCREENS];
static void make_layout(void)
{
    static const uint8_t wall[8] = {0xEE, 0xEF, 0xEE, 0xEE, 0xEE, 0xEF, 0xEE, 0xEE};
    for (int c = 0; c < 8 * N_SCREENS; c++) {
        uint16_t lw = wall[c & 7];
        layout[0][c] = 0xE9;
        layout[1][c] = lw == 0xEF ? 0xEC : 0xEB;
        for (int r = 2; r <= 5; r++) layout[r][c] = lw;
        layout[6][c] = 0x118;
        layout[7][c] = 0x119;
    }
    layout[1][5] = layout[2][5] = 0xF1;   /* windows */
    layout[1][12] = layout[2][12] = 0xF1;
    layout[5][6] = 0xF0;                  /* a chair (the far end is the card table's) */
    layout[4][2] = 0x9F;                  /* the door back */
    layout[5][2] = 0xA1;
    layout[1][19] = layout[2][19] = 0xF1;
    for (int r = 0; r <= 5; r++) layout[r][8 * N_SCREENS - 1] = 0xE8;
    layout[0][0] = 0xE8;
    layout[1][0] = 0xEA;
    for (int r = 2; r <= 5; r++) layout[r][0] = 0xED;
}
/* the grid list (6B:8882's format: a grid position, then screen numbers, $FF ends) and the
 * headers (6B:8954's: camera X page, Y page, X, Y, camera mode, columns, left page, right
 * page, top and bottom pages, the player's X and Y, $45, the player's facing) */
static const uint8_t GRID[5] = {0x12, BORROW_A, BORROW_A + 1, BORROW_A + 2, 0xFF};
static const uint8_t HEAD_IN[14] = {0x02, 0x01, 0x00, 0x00, 0x00, 0x08, 0x02, 0x03,
                                    0x01, 0x01, 0x50, 0xAA, 0x00, 0x00};
/* back in the house, by the door, facing the stairs */
static const uint8_t HEAD_OUT[14] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00,
                                     0x01, 0x01, 0xD0, 0x2A, 0x00, 0x01};

/* The door: its tiles (VRAM patterns, palette $B) in five rows of two, 16 x 40 pixels: two
 * wooden leaves hinged at the sides (columns 0-7 and 8-15), a bar across them (rows 19-23)
 * and a padlock on it (columns 9-13). The house's map has the door at map rows 34-38,
 * columns 25-26.
 *
 * Opening (the user's pick of five mock-ups, "creaky"), at 60 frames a second: the padlock's
 * shackle pops, the lock drops out of sight, the bar slides off to the left; then, with the
 * game's own creaking door (the sound of RUN on its title screen), the left leaf swings
 * slowly in, the right one gives a little, sticks, and swings in after it. A leaf turning
 * away narrows toward its hinge and darkens. */
static const uint16_t DOOR_TILES[5][2] = {{0x245, 0x246}, {0x248, 0x249}, {0x24A, 0x24B},
                                          {0x24C, 0x24D}, {0x24E, 0x24F}};
#define DOOR_H 40
#define T_POP 16      /* the shackle pops */
#define T_FALL 24     /* the lock drops */
#define T_BAR 44      /* the bar slides off */
#define T_LEFT 60     /* the left leaf swings; the creak starts */
#define T_PAUSE 150
#define T_GIVE 170    /* the right leaf gives a little and settles back */
#define T_RIGHT 200   /* the right leaf swings */
#define T_OPEN 320
#define DOOR_MAP_ROW 34
#define DOOR_MAP_COL 25

static const uint8_t KONAMI[10] = {PAD_UP, PAD_UP, PAD_DOWN, PAD_DOWN, PAD_LEFT, PAD_RIGHT,
                                   PAD_LEFT, PAD_RIGHT, PAD_II, PAD_I};

static struct {
    uint8_t prev;              /* the pad last frame */
    int code;                  /* how much of the code has been entered */
    bool open;                 /* the door opened (this visit to the house) */
    int anim;                  /* frames into the opening, 0 - T_OPEN */
    bool have_orig;
    uint16_t orig[5][2][16], shown[5][2][16];
    int pending;               /* 1 going in, 2 coming out (the room change under way) */
    int pending_frames;
} s;

static void fight_reset(void);

static bool hidden_seen;   /* (see in_hidden) */

void secret_reset(void)
{
    hidden_seen = false;
    s.prev = 0;
    s.code = 0;
    s.open = false;
    s.anim = 0;
    s.have_orig = false;
    s.pending = 0;
    sound_extra_stop();
    fight_reset();
}

static uint8_t *at(Pce *p, int bank, uint16_t a) { return p->page[bank] ? p->page[bank] + (a & 0x1FFF) : NULL; }
static int rd16(Pce *p, int bank, uint16_t a)
{
    uint8_t *m = at(p, bank, a);
    return m ? m[0] | m[1] << 8 : -1;
}
static void wr16(Pce *p, int bank, uint16_t a, uint16_t v)
{
    uint8_t *m = at(p, bank, a);
    if (m) m[0] = (uint8_t)v, m[1] = (uint8_t)(v >> 8);
}

/* the stage's data is the one this was made for, and our space in it is free or ours */
static bool data_ok(Pce *p)
{
    if (p->wram[W_AREA] != AREA) return false;
    uint8_t *h = at(p, B_ROOMS, HOUSE_HEAD), *sc = at(p, B_SCREENS, SCREENS + 7 * 64), *st = at(p, B_ROOMS, STASH);
    if (!h || !sc || !st || memcmp(h, HOUSE_HEADER, sizeof HOUSE_HEADER) || memcmp(sc, HOUSE_SCREEN, sizeof HOUSE_SCREEN))
        return false;
    int g = rd16(p, B_ROOMS, GRID_PTRS + 2 * HOUSE), hd = rd16(p, B_ROOMS, HEAD_PTRS + 2 * HOUSE);
    if ((g != HOUSE_GRID && g != OUR_GRID) || (hd != HOUSE_HEAD && hd != OUR_HEAD_IN && hd != OUR_HEAD_OUT)) return false;
    if (!memcmp(st, "RSEC", 4)) return true;
    for (uint16_t a = OUR_GRID; a < FREE_END; a++) {
        uint8_t v = *at(p, B_ROOMS, a);
        if (v != 0xFF && !(a >= OUR_GRID && a < STASH)) return false;
    }
    return true;
}

static int cam_page(Pce *p) { return p->wram[W_CAM_PAGE] & 0x0F; }
static bool playing(Pce *p) { return game_state(p) == RONDO_PLAYING && p->wram[RONDO_SUB] == 1; }
static bool in_house(Pce *p) { return p->wram[W_AREA] == AREA && p->wram[W_ROOM] == HOUSE && cam_page(p) == 0; }
/* (walking in from the street, the room is the house's two frames before the camera leaves the
 * street's page 3, so the hidden room counts only once the game has played in it; then it holds
 * through pauses and the like: hidden_seen) */
static bool in_hidden(Pce *p)
{
    int c = cam_page(p);
    if (p->wram[W_AREA] != AREA || p->wram[W_ROOM] != HOUSE || (c != 2 && c != 3)) return hidden_seen = false;
    if (playing(p)) hidden_seen = true;
    return hidden_seen;
}
static int px(Pce *p) { return p->wram[W_PX_HI] << 8 | p->wram[W_PX]; }
static int py(Pce *p) { return p->wram[W_PY_HI] << 8 | p->wram[W_PY]; }

/* the hidden room's screens in the street's two slots (the street's kept in the stash) */
static void borrow(Pce *p)
{
    uint8_t *st = at(p, B_ROOMS, STASH), *sc = at(p, B_SCREENS, SCREENS + BORROW_A * 64),
            *hi = at(p, B_SCREENS, SCREEN_HI + BORROW_A * 8);
    if (!st || !sc || !hi) return;
    if (memcmp(st, "RSEC", 4)) {
        memcpy(st + 4, sc, N_SCREENS * 64);
        memcpy(st + 4 + N_SCREENS * 64, hi, N_SCREENS * 8);
        memcpy(st, "RSEC", 4);
    }
    for (int k = 0; k < N_SCREENS; k++)
        for (int r = 0; r < 8; r++) {
            uint8_t bits = 0;
            for (int c = 0; c < 8; c++) {
                uint16_t b = layout[r][k * 8 + c];
                sc[k * 64 + r * 8 + c] = (uint8_t)b;
                bits |= (uint8_t)((b >> 8) & 1) << c;
            }
            hi[k * 8 + r] = bits;
        }
}

static void give_back(Pce *p)
{
    uint8_t *st = at(p, B_ROOMS, STASH), *sc = at(p, B_SCREENS, SCREENS + BORROW_A * 64),
            *hi = at(p, B_SCREENS, SCREEN_HI + BORROW_A * 8);
    if (!st || !sc || !hi || memcmp(st, "RSEC", 4)) return;
    memcpy(sc, st + 4, N_SCREENS * 64);
    memcpy(hi, st + 4 + N_SCREENS * 64, N_SCREENS * 8);
    memset(st, 0xFF, 4 + STASH_LEN);
}

static void pointers_back(Pce *p)
{
    wr16(p, B_ROOMS, GRID_PTRS + 2 * HOUSE, HOUSE_GRID);
    wr16(p, B_ROOMS, HEAD_PTRS + 2 * HOUSE, HOUSE_HEAD);
}

/* the game's own room change into the house's route step, with our grid and header */
static void change_room(Pce *p, uint16_t grid, uint16_t head)
{
    memcpy(at(p, B_ROOMS, OUR_GRID), GRID, sizeof GRID);
    memcpy(at(p, B_ROOMS, OUR_HEAD_IN), HEAD_IN, sizeof HEAD_IN);
    memcpy(at(p, B_ROOMS, OUR_HEAD_OUT), HEAD_OUT, sizeof HEAD_OUT);
    wr16(p, B_ROOMS, GRID_PTRS + 2 * HOUSE, grid);
    wr16(p, B_ROOMS, HEAD_PTRS + 2 * HOUSE, head);
    p->wram[W_STEP] = HOUSE - 1;   /* (leaving steps it to the house's again) */
    p->wram[W_EXIT] = 0x80;
    p->wram[W_EVENT] = 3;
    s.pending_frames = 0;
}

/* a pattern's pixel (colour 0-15) */
static int pix(const uint16_t *t, int x, int y)
{
    int b = 7 - x;
    return (t[y] >> b & 1) | (t[y] >> (8 + b) & 1) << 1 | (t[8 + y] >> b & 1) << 2 | (t[8 + y] >> (8 + b) & 1) << 3;
}
static void set_pix(uint16_t *t, int x, int y, int v)
{
    int b = 7 - x;
    t[y] = (uint16_t)((t[y] & ~(1u << b | 1u << (8 + b))) | (v & 1) << b | (v >> 1 & 1) << (8 + b));
    t[8 + y] = (uint16_t)((t[8 + y] & ~(1u << b | 1u << (8 + b))) | (v >> 2 & 1) << b | (v >> 3 & 1) << (8 + b));
}
static int door_orig(int x, int y) { return pix(s.orig[y >> 3][x >> 3], x & 7, y & 7); }

static double ease(double t)
{
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return 0.5 - 0.5 * cos(3.14159265358979 * t);
}
static bool in_lock(int x, int y) { return x >= 9 && x <= 13 && y >= 19 && y <= 23 && door_orig(x, y) != 1; }
/* a leaf's own pixels (behind the bar: its boards, as above it) */
static int leaf_px(int x, int y) { return door_orig(x, y >= 19 && y <= 23 ? (y & 1 ? 17 : 18) : y); }
/* the bar (behind the lock: the bar as beside it) */
static int bar_px(int x, int y) { return in_lock(x, y) ? door_orig(6, y) : door_orig(x, y); }
static int darker(int c)
{
    switch (c) {
    case 7: return 5;
    case 5: return 6;
    case 6: case 2: return 1;
    case 3: return 2;
    case 4: return 3;
    default: return c;
    }
}

/* the door `t` frames into its opening, as colours (16 x 40) */
static void door_at(int t, uint8_t g[DOOR_H][16])
{
    double lw = 8, rw = 8;
    if (t >= T_LEFT) lw = 8 * (1 - ease((t - T_LEFT) / (double)(T_PAUSE - T_LEFT - 1)));
    if (t >= T_GIVE && t < T_RIGHT) rw = 8 - 1.8 * sin(3.14159265358979 * (t - T_GIVE) / (T_RIGHT - T_GIVE));
    if (t >= T_RIGHT) rw = 8 * (1 - ease((t - T_RIGHT) / (double)(T_OPEN - T_RIGHT - 1)));
    for (int side = 0; side < 2; side++) {
        double w = side ? rw : lw;
        int dark = w < 8 ? (int)((1 - w / 8) * 3 + 0.5) : 0;
        for (int y = 0; y < DOOR_H; y++)
            for (int j = 0; j < 8; j++) {
                int x = side ? 15 - j : j, c = 1;
                if (j < w) {   /* (the leaf squeezed toward its hinge) */
                    int sj = (int)(j * 8 / w);
                    if (sj > 7) sj = 7;
                    c = leaf_px(side ? 15 - sj : sj, y);
                    for (int k = 0; k < dark && c != 1; k++) c = darker(c);
                }
                g[y][x] = (uint8_t)c;
            }
    }
    if (t < T_LEFT) {   /* the bar, sliding off to the left */
        int bx = t < T_BAR ? 0 : -(int)(16 * ease((t - T_BAR) / 15.0));
        for (int y = 19; y <= 23; y++)
            for (int x = 0; x < 16; x++) {
                int sx = x - bx;
                if (sx < 16 && (bx == 0 || bar_px(sx, y) != 1)) g[y][x] = (uint8_t)bar_px(sx, y);
            }
    }
    if (t < T_BAR) {    /* the lock: its shackle popped, then falling */
        int dy = 0;
        if (t >= T_FALL) {
            int k = (t - T_FALL) / 2;
            dy = (int)(0.3 * (k + 1) * (k + 2));
        }
        for (int y = 19; y <= 23; y++)
            for (int x = 9; x <= 13; x++) {
                if (!in_lock(x, y)) continue;
                int ny = y + dy - (t >= T_POP && t < T_FALL && y <= 20 ? 1 : 0);
                if (ny < DOOR_H) g[ny][x] = (uint8_t)door_orig(x, y);
            }
    }
}

/* the door's tiles in VRAM `t` frames into its opening (0: the game's own) */
static void door_show(Pce *p, int t)
{
    uint16_t *v = p->vdc.vram;
    bool reloaded = false;
    for (int r = 0; r < 5; r++)
        for (int k = 0; k < 2; k++)
            if (!s.have_orig || memcmp(&v[DOOR_TILES[r][k] * 16], s.shown[r][k], 32)) reloaded = true;
    if (reloaded) {   /* (first time, or the game loaded the tiles again: they are its own) */
        for (int r = 0; r < 5; r++)
            for (int k = 0; k < 2; k++) memcpy(s.orig[r][k], &v[DOOR_TILES[r][k] * 16], 32);
        s.have_orig = true;
    }
    for (int r = 0; r < 5; r++)
        for (int k = 0; k < 2; k++) memcpy(s.shown[r][k], s.orig[r][k], 32);
    if (t > 0) {
        uint8_t g[DOOR_H][16];
        door_at(t, g);
        for (int y = 0; y < DOOR_H; y++)
            for (int x = 0; x < 16; x++) set_pix(s.shown[y >> 3][x >> 3], x & 7, y & 7, g[y][x]);
    }
    for (int r = 0; r < 5; r++)
        for (int k = 0; k < 2; k++) memcpy(&v[DOOR_TILES[r][k] * 16], s.shown[r][k], 32);
}

/* The creak: the ADPCM clip the game plays for RUN on its title screen, read from the disc
 * (the title's sound bank: data track 2, record $ED on, loaded whole into ADPCM RAM; the clip
 * at $50CA, $5B8A bytes, rate 12: 8 kHz). Its first 0.55 s are silence, skipped. Checked by
 * its CRC: on another disc there is no creak. */
#define CREAK_REC 0xED
#define CREAK_AT 0x50CA
#define CREAK_LEN 0x5B8A
#define CREAK_CRC 0xFA003458u
#define CREAK_SKIP 2200
static uint8_t creak[CREAK_LEN];
static int creak_state;   /* 0 not read yet, 1 read, -1 not there */

static uint32_t crc32_of(const uint8_t *b, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= b[i];
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static void play_creak(Pce *p)
{
    if (creak_state == 0) {
        static uint8_t buf[12 * 2048];
        const DiscTrack *tr = disc_track(&p->disc, 2);
        int first = CREAK_AT / 2048;
        creak_state = -1;
        if (tr && disc_read_data(&p->disc, tr->lba + CREAK_REC + first, 12, buf)) {
            memcpy(creak, buf + (CREAK_AT - first * 2048), CREAK_LEN);
            if (crc32_of(creak, CREAK_LEN) == CREAK_CRC) creak_state = 1;
        }
    }
    if (creak_state == 1) sound_extra_play(creak + CREAK_SKIP, CREAK_LEN - CREAK_SKIP, 12);
}

/* the house's door is where it should be in the map (the house as drawn) */
static bool door_in_map(Pce *p)
{
    const uint16_t *v = p->vdc.vram;
    for (int r = 0; r < 5; r++)
        for (int k = 0; k < 2; k++)
            if ((v[(DOOR_MAP_ROW + r) * 64 + DOOR_MAP_COL + k] & 0x7FF) != DOOR_TILES[r][k]) return false;
    return true;
}

static void scene_hits(Pce *p);
static void fight_frame(Pce *p);

void secret_frame(Pce *p, bool on)
{
    static bool made;
    if (!made) make_layout(), made = true;
    uint8_t pad = p->pad, press = pad & (uint8_t)~s.prev;
    s.prev = pad;
    if (p->wram[W_AREA] != AREA || !data_ok(p)) {
        if (s.pending == 0) { s.open = false; s.code = 0; }
        return;
    }
    /* the room change under way: our pointers back once the room it went to is playing */
    if (s.pending) {
        s.pending_frames++;
        bool done = playing(p) && (s.pending == 1 ? in_hidden(p) : in_house(p));
        if (done || s.pending_frames > 600) {
            pointers_back(p);
            s.pending = 0;
        }
    } else if (rd16(p, B_ROOMS, HEAD_PTRS + 2 * HOUSE) != HOUSE_HEAD || rd16(p, B_ROOMS, GRID_PTRS + 2 * HOUSE) != HOUSE_GRID) {
        pointers_back(p);   /* (a state saved in the middle of one) */
    }
    /* the street's screens lent only while the hidden room is played (or being entered) */
    if (s.pending == 1 || (in_hidden(p) && playing(p))) borrow(p);
    else if (s.pending == 0 && !in_hidden(p)) give_back(p);
    if (!on) {
        s.code = 0;
        if (s.open && s.have_orig) door_show(p, 0);
        sound_extra_stop();
        s.open = false;
        return;
    }
    if (p->wram[W_ROOM] != HOUSE) {
        s.open = false;
        s.code = 0;
        return;
    }
    if (in_hidden(p)) {
        fight_frame(p);
        scene_hits(p);
        s.open = true;   /* (it is open on this side, and stays open behind) */
        s.anim = T_OPEN;
        door_show(p, s.anim);
        /* Up at the door back */
        int wx = cam_page(p) * 256 + p->wram[W_CAM_X] + px(p);
        if (s.pending == 0 && playing(p) && (press & PAD_UP) && wx >= 576 && wx <= 608 && py(p) >= 0x80)
            s.pending = 2, change_room(p, HOUSE_GRID, OUR_HEAD_OUT);
        return;
    }
    if (!in_house(p) || !door_in_map(p)) return;
    bool by_door = px(p) >= 160 && px(p) <= 240 && py(p) < 0x50;
    if (!s.open) {
        if (!by_door || !playing(p)) s.code = 0;
        else if (press) {
            /* (a press holding the button wanted counts: the frontend's Sub-weapon control,
             * a gamepad's B, reaches the game as Up and II together; for a direction, no other
             * direction with it) */
            uint8_t want = KONAMI[s.code], dirs = PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT;
            bool ok = (press & want) && (!(want & dirs) || !(press & dirs & (uint8_t)~want));
            if (want & (PAD_I | PAD_II)) ok = press & want;
            if (ok) s.code++;
            else s.code = press == KONAMI[0] ? 1 : 0;
            if (s.code == 10) s.open = true, s.anim = 0, s.code = 0;
        }
    }
    if (s.open) {
        if (s.anim < T_OPEN && ++s.anim == T_LEFT) play_creak(p);
        door_show(p, s.anim);
        if (s.pending == 0 && s.anim == T_OPEN && playing(p) && (press & PAD_UP) && px(p) >= 192 && px(p) <= 224 && py(p) < 0x50) {
            borrow(p);
            s.pending = 1;
            change_room(p, OUR_GRID, OUR_HEAD_IN);
        }
    }
}

/* ---- the scene: Death and Maria at cards --------------------------------------------- */
/* At the room's far right a card table (the host's own drawing), Death hovering at its left
 * end and Maria standing at its right, facing him, each holding cards. They are drawn by the
 * host over the picture after each frame, behind the game's sprites (Richter walks in front),
 * in the game's own sprite graphics and palettes read from the disc's data track: Death as
 * the boss of the Death fight, Maria's standing frame from her own sprite set. Whipping Death
 * makes him flash. */

/* where on the data track (byte offsets from its start): 16 x 16 sprite cells, 128 bytes each,
 * by cell number from a set's start, and palettes (16 entries packed in 18 bytes: a byte of
 * the 8 entries' bit 8, MSB first, then their low bytes, twice) */
#define DEATH_CELLS 0x838800
#define DEATH_CELLS2 0x84E800
#define DEATH_PAL 0x8614B7
#define DEATH_PAL2 0x861523
#define MARIA_CELLS 0xEC600
#define MARIA_PAL 0x1293C9
#define DEATH_CRC 0x5AF32AD3u
#define MARIA_CRC 0xE27CAA86u

/* a frame: sprites as the game's sprite table has them (x, y from the frame's top left;
 * cell number; attributes: palette, width and height, flips); listed front to back */
typedef struct { int8_t x, y; uint16_t cell, attr; } Piece;
/* Death as he hovers in his fight (palette 0 here: his palette 9; 1: his palette 15, the
 * glint in his ribs; cell 0x1F4 stands for cell 0xF4 of his second set) */
static const Piece DEATH[] = {
    {16, 48, 0x14D, 0x1800}, {0, 48, 0x14D, 0x1000}, {13, 24, 0x145, 0x0000}, {7, 28, 0x147, 0x0000},
    {0, 0, 0x140, 0x1100},   {0, 32, 0x14B, 0x0000}, {16, 32, 0x14B, 0x0800}, {9, 36, 0x1F4, 0x0001},
};
#define DEATH_W 32
#define DEATH_H 80
/* Maria standing (facing right; drawn flipped, facing left) */
static const Piece MARIA[] = {{0, 0, 0x185, 0x0000}, {0, 16, 0x184, 0x1000}};
#define MARIA_W 16
#define MARIA_H 48

static struct {
    int state;                       /* 0 not read, 1 read, -1 not there */
    uint8_t death[16 * 128 + 128], maria[4 * 128];
    uint32_t death_pal[2][16], maria_pal[1][16];
    int flash;                       /* frames of Death's white flash left */
    int hit_wait;                    /* frames before he can be hit again */
    /* the card game (host-side, not in save states) */
    uint64_t last_frame;
    uint32_t rng;
    int hand[2];                     /* cards in Death's (0) and Maria's (1) hands */
    int deck;                        /* cards in the deck */
    int n_table;                     /* cards played on the table: their slots and faces */
    int8_t table_x[6];
    uint8_t table_kind[6];
    int wait;                        /* frames to the next move */
    int turn;                        /* whose move */
    /* a card on its way: from (fx, fy) to (tx, ty) over `len` frames, relative to the table's
     * middle at the table top; `to`: what it lands as (0 a hand, 1 the table, 2 the deck) */
    bool moving;
    int mv_t, mv_len, mv_from_x, mv_from_y, mv_to_x, mv_to_y, mv_to, mv_who, mv_kind, mv_sweep;
    int glow;                        /* the candle's light this frame, 0-255 */
} sc;

static bool disc_bytes(Pce *p, uint32_t off, uint32_t len, uint8_t *out)
{
    static uint8_t buf[4 * 2048];
    const DiscTrack *tr = disc_track(&p->disc, 2);
    uint32_t first = off / 2048, n = (off % 2048 + len + 2047) / 2048;
    if (!tr || n > 4 || !disc_read_data(&p->disc, tr->lba + (int)first, (int)n, buf)) return false;
    memcpy(out, buf + off % 2048, len);
    return true;
}

static uint32_t argb9(uint16_t c)
{
    uint32_t b = c & 7, r = (c >> 3) & 7, g = (c >> 6) & 7;
    return 0xFF000000u | (r * 255 / 7) << 16 | (g * 255 / 7) << 8 | (b * 255 / 7);
}
static void unpack_pal(const uint8_t *b, uint32_t *out)
{
    for (int g = 0; g < 2; g++)
        for (int i = 0; i < 8; i++) out[g * 8 + i] = argb9((uint16_t)(b[g * 9 + 1 + i] | ((b[g * 9] >> (7 - i)) & 1) << 8));
}

static bool scene_load(Pce *p)
{
    if (sc.state) return sc.state > 0;
    sc.state = -1;
    uint8_t dp[2][18], mp[18], all[16 * 128 + 128 + 36], mall[4 * 128 + 18];
    if (!disc_bytes(p, DEATH_CELLS + 0x140 * 128, 16 * 128, sc.death) ||
        !disc_bytes(p, DEATH_CELLS2 + 0xF4 * 128, 128, sc.death + 16 * 128) || !disc_bytes(p, DEATH_PAL, 18, dp[0]) ||
        !disc_bytes(p, DEATH_PAL2, 18, dp[1]) || !disc_bytes(p, MARIA_CELLS + 0x184 * 128, 4 * 128, sc.maria) ||
        !disc_bytes(p, MARIA_PAL, 18, mp))
        return false;
    memcpy(all, sc.death, sizeof sc.death);
    memcpy(all + sizeof sc.death, dp[0], 18);
    memcpy(all + sizeof sc.death + 18, dp[1], 18);
    memcpy(mall, sc.maria, sizeof sc.maria);
    memcpy(mall + sizeof sc.maria, mp, 18);
    if (crc32_of(all, sizeof all) != DEATH_CRC || crc32_of(mall, sizeof mall) != MARIA_CRC) return false;
    unpack_pal(dp[0], sc.death_pal[0]);
    unpack_pal(dp[1], sc.death_pal[1]);
    unpack_pal(mp, sc.maria_pal[0]);
    sc.state = 1;
    return true;
}

/* a sprite cell's pixel (colour 0-15): the game's 4 planes of 16 rows */
static int cell_px(const uint8_t *cell, int x, int y)
{
    int b = 15 - x, c = 0;
    for (int pl = 0; pl < 4; pl++) {
        uint16_t w = (uint16_t)(cell[(pl * 16 + y) * 2] | cell[(pl * 16 + y) * 2 + 1] << 8);
        c |= ((w >> b) & 1) << pl;
    }
    return c;
}

/* the picture: a pixel at screen x, y (the game's 256 from x 0), unless a sprite of the
 * game's is there */
typedef struct { uint32_t *fb; const uint8_t *spr; int org, w, h; } Canvas;
static void plot(const Canvas *cv, int x, int y, uint32_t c)
{
    int fx = x + cv->org;
    if (fx < 0 || fx >= cv->w || y < 0 || y >= cv->h) return;
    if (cv->spr[y * PCE_FB_MAX_W + fx]) return;
    cv->fb[y * PCE_FB_MAX_W + fx] = c;
}

/* (0-16: how much of a frame draw_frame leaves out, by a 4 x 4 ordered dither) */
static int g_dissolve;
static const uint8_t DITHER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

/* a frame of pieces with its top left at screen x, y; flip: mirrored within width w; cells
 * from `base` in `cells`, from `base2` in `cells2` */
static void draw_frame(const Canvas *cv, const Piece *pc, int n, const uint8_t *cells, uint16_t base, uint16_t base2,
                       const uint8_t *cells2, uint32_t pal[][16], int x, int y, int w, bool flip, bool white)
{
    for (int i = n - 1; i >= 0; i--) {   /* (back to front) */
        const Piece *e = &pc[i];
        int cgy = (e->attr >> 12) & 3, cw = e->attr & 0x100 ? 2 : 1, ch = cgy == 0 ? 1 : cgy == 1 ? 2 : 4;
        int sw = cw * 16, sh = ch * 16;
        bool xf = ((e->attr & 0x800) != 0) != flip;
        int ex = flip ? w - (e->x + sw) : e->x;
        uint16_t first = (uint16_t)(e->cell & ~((cw - 1) | (ch - 1) << 1));
        for (int yy = 0; yy < sh; yy++)
            for (int xx = 0; xx < sw; xx++) {
                int cx = xf ? sw - 1 - xx : xx;
                uint16_t cell = (uint16_t)(first + (cx >> 4) + (yy >> 4) * 2);
                const uint8_t *src = cells2 && cell >= base2 ? cells2 + (cell - base2) * 128 : cells + (cell - base) * 128;
                int c = cell_px(src, cx & 15, yy & 15);
                int px = x + ex + xx, py = y + e->y + yy;
                if (c && DITHER[py & 3][px & 3] >= g_dissolve) plot(cv, px, py, white ? 0xFFFFFFFFu : pal[e->attr & 15][c]);
            }
    }
}

/* the table and the cards: the host's own drawing */
static void rect(const Canvas *cv, int x0, int y0, int x1, int y1, uint32_t c)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) plot(cv, x, y, c);
}
#define WOOD_D 0xFF482400u
#define WOOD 0xFF6D4824u
#define WOOD_L 0xFF916D24u
static void card(const Canvas *cv, int x, int y, int w, int h, int kind)
{
    if (kind == 0) {   /* face down */
        rect(cv, x, y, x + w - 1, y + h - 1, 0xFFDADAECu);
        if (w > 2 && h > 2) rect(cv, x + 1, y + 1, x + w - 2, y + h - 2, 0xFF242491u);
    } else {           /* face up: a red or a black pip */
        rect(cv, x, y, x + w - 1, y + h - 1, 0xFFECECECu);
        plot(cv, x + w / 2, y + h / 2, kind == 1 ? 0xFFC80000u : 0xFF000000u);
    }
}

/* the scene's places in the room (room X: the level's X - 512, from the room's left wall) */
#define DEATH_X 350
#define DEATH_BOTTOM 186
#define TABLE_X0 390
#define TABLE_X1 444
#define TABLE_Y 166
#define MARIA_X 452
#define ROOM_FLOOR 192

static int scene_cam(Pce *p) { return cam_page(p) * 256 + p->wram[W_CAM_X] - 512; }

/* Death's place on screen this frame (his top left; he hovers up and down a little) */
static void death_at(Pce *p, int cam, int *x, int *y)
{
    *x = DEATH_X - cam;
    *y = DEATH_BOTTOM - DEATH_H + (int)lround(2 * sin((double)p->frame * 0.05));
}

/* the whip striking Death (before a frame: its box as the last frame left it) */
static void fight_start(Pce *p);
static bool fight_busy(void);

static void scene_hits(Pce *p)
{
    if (sc.hit_wait) sc.hit_wait--;
    if (sc.flash) sc.flash--;
    if (sc.state <= 0 || sc.hit_wait || fight_busy()) return;
    int dx, dy;
    death_at(p, scene_cam(p), &dx, &dy);
    int x0 = dx + 2, x1 = dx + DEATH_W - 2, y0 = dy + 6, y1 = dy + DEATH_H - 6;
    for (int sl = 1; sl < 16; sl++) {
        if (!p->wram[0x0A10 + sl] || p->wram[0x1828 + sl] != 5) continue;   /* (the whip) */
        int fr = p->wram[0x0A68 + sl];
        if (fr < 5 || fr > 7) continue;                                         /* (its strike) */
        int ox = (int16_t)(p->wram[W_PX_HI + sl] << 8 | p->wram[W_PX + sl]);
        int oy = (int16_t)(p->wram[W_PY_HI + sl] << 8 | p->wram[W_PY + sl]);
        int ahead = (int8_t)p->wram[0x1930 + sl], hw = p->wram[0x1988 + sl];
        int below = (int8_t)p->wram[0x19E0 + sl], hh = p->wram[0x1A38 + sl];
        int cx = ox + (p->wram[0x0AC0] ? -ahead : ahead), cy = oy + below;
        if (cx + hw >= x0 && cx - hw <= x1 && cy + hh >= y0 && cy - hh <= y1) {
            sc.flash = 8;
            sc.hit_wait = 20;
            fight_start(p);
            return;
        }
    }
}

/* The card game: each in turn plays a card from their hand onto the table, or draws one from
 * the deck; with enough on the table, the trick is swept back onto the deck. A move every
 * second or three, a card flying over in a little arc. */
static const int8_t SLOTS[5] = {-13, -7, 6, 12, 18};
static int rnd(int n)
{
    sc.rng = sc.rng * 1103515245u + 12345u;
    return (int)((sc.rng >> 16) % (uint32_t)n);
}
static void move_card(int fx, int fy, int tx, int ty, int to, int who, int kind, int sweep, int len)
{
    sc.moving = true;
    sc.mv_t = 0;
    sc.mv_len = len;
    sc.mv_from_x = fx, sc.mv_from_y = fy, sc.mv_to_x = tx, sc.mv_to_y = ty;
    sc.mv_to = to, sc.mv_who = who, sc.mv_kind = kind, sc.mv_sweep = sweep;
}
static void cards_step(const int hand_x[2], const int hand_y[2])
{
    if (!sc.deck && !sc.hand[0] && !sc.hand[1] && !sc.n_table) {   /* (a new game) */
        sc.rng = 0x2D2A5u;
        sc.hand[0] = sc.hand[1] = 3;
        sc.deck = 10;
        sc.n_table = 2;
        sc.table_x[0] = SLOTS[1], sc.table_kind[0] = 2;
        sc.table_x[1] = SLOTS[3], sc.table_kind[1] = 1;
        sc.wait = 60;
    }
    if (sc.moving) {
        if (++sc.mv_t < sc.mv_len) return;
        sc.moving = false;
        if (sc.mv_to == 0) sc.hand[sc.mv_who]++;
        else if (sc.mv_to == 2) sc.deck += sc.mv_sweep;
        else if (sc.n_table < 5) {
            sc.table_x[sc.n_table] = (int8_t)sc.mv_to_x;
            sc.table_kind[sc.n_table++] = (uint8_t)sc.mv_kind;
        }
        return;
    }
    if (--sc.wait > 0) return;
    int w = sc.turn;
    if (sc.n_table >= 4 || (sc.n_table >= 2 && rnd(4) == 0)) {   /* the trick swept up */
        move_card(-4, -2, -22, -2, 2, w, 0, sc.n_table, 24);
        sc.n_table = 0;
    } else if (sc.deck && (!sc.hand[w] || (sc.hand[w] < 4 && rnd(10) < 3))) {   /* a card drawn */
        sc.deck--;
        move_card(-22, -2, hand_x[w], hand_y[w], 0, w, 0, 0, 20);
    } else if (sc.hand[w]) {   /* a card played, face up */
        int x, tries = 0;
        bool used;
        do {
            x = SLOTS[rnd(5)];
            used = false;
            for (int i = 0; i < sc.n_table; i++) used |= sc.table_x[i] == x;
        } while (used && ++tries < 20);
        sc.hand[w]--;
        move_card(hand_x[w], hand_y[w], x, -2, 1, w, 1 + rnd(2), 0, 18);
    }
    sc.turn ^= 1;
    sc.wait = 50 + rnd(120);
}

/* the candle's light: warm, flickering, over everything near it (Richter too) */
static void candle_glow(const Canvas *cv, int x, int y, uint64_t frame)
{
    if ((frame % 3) == 0 || !sc.glow) sc.glow = 150 + rnd(60);
    int r = 30 + (sc.glow - 150) / 12;
    for (int yy = y - r; yy <= y + r; yy++) {
        if (yy < 0 || yy >= cv->h) continue;
        for (int xx = x - r; xx <= x + r; xx++) {
            int fx = xx + cv->org;
            if (fx < 0 || fx >= cv->w) continue;
            double dd = sqrt((double)(xx - x) * (xx - x) + (double)(yy - y) * (yy - y)) / r;
            if (dd >= 1) continue;
            double f = (1 - dd) * (1 - dd) * sc.glow / 255.0;
            uint32_t *px = &cv->fb[yy * PCE_FB_MAX_W + fx];
            int cr = (int)(*px >> 16 & 255) + (int)(f * 120), cg = (int)(*px >> 8 & 255) + (int)(f * 62),
                cb = (int)(*px & 255) + (int)(f * 14);
            *px = 0xFF000000u | (uint32_t)(cr > 255 ? 255 : cr) << 16 | (uint32_t)(cg > 255 ? 255 : cg) << 8 |
                  (uint32_t)(cb > 255 ? 255 : cb);
        }
    }
}


/* ---- the fight: Death's end and the arena ------------------------------------------------ */
/* Whipping Death ends the card game: he shakes and flashes, the game's blood sprays from him
 * and he comes apart (FIGHT_DYING); the room then fades into the arena (FIGHT_FADE), the
 * camera locked at the room's right end so the arena is exactly the 16:9 picture: the title
 * screen's sky (its castle, moon and lightning, the logo left out; widened by the title art's
 * sides) behind the castle approach's brown wall, gate and cobbles. Maria stands facing
 * Richter with a boss gauge (FIGHT_ARENA). All of it is the host's drawing behind the game's
 * sprites, from the disc: the title's map and tiles unpacked by the game's own run-length
 * scheme (6C:81CD), the wall's 32-pixel blocks and tiles, sprites and palettes as they are
 * stored. Not in save states (a state loaded mid-fight shows the card game again). */
enum { FIGHT_NONE, FIGHT_DYING, FIGHT_FADE, FIGHT_ARENA };
#define DYING_T 150
#define FADE_T 60
#define ARENA_W 396
#define ARENA_H 224
#define SKY_LIFT 24          /* the sky raised: the castle over the wall */
#define DROP 17              /* the arena's ground this much lower than the room's: the floor's last
                              * row the picture's last; the camera as much higher in the fight */
#define WALL_TOP (108 + DROP) /* the wall band's top row in the arena */
#define ARENA_X0 698         /* the level's X of the arena's x 0 */
#define ARENA_FLOOR (ROOM_FLOOR + DROP)
#define MARIA_ARENA_X 300
#define GAUGE_ARENA_X 352
#define LIMIT_LEFT_PAGE 0x1EBE
#define CAM_LO 0x1E00

/* the title's packed streams: where on the data track, the VRAM address they unpack to, and
 * the flag the game passes (bit 0 clear: an address step wraps in 4 K words). The two that use
 * the second scheme ($FD: the logo's sprites) are left out. */
static const struct { uint32_t off; uint16_t vram; uint8_t flag; } TITLE_STREAMS[] = {
    {0x430B9, 0x1100, 1}, {0x3C800, 0x2000, 1}, {0x3DBFC, 0x3000, 1}, {0x64800, 0x4000, 1},
    {0x65DEE, 0x5000, 1}, {0x47792, 0x6000, 1}, {0x3F46D, 0x7800, 1}, {0x33B46, 0x0000, 0},
    {0x340D5, 0x0800, 0}, {0x340D5, 0x0020, 0}, {0x345DD, 0x0820, 0},
};
#define TITLE_STREAM_MAX 6144
#define TITLE_VRAM_CRC 0x569D9C32u
#define TITLE_PALS 0x63604        /* 8 palettes, then the lightning's 4-7 */
#define TITLE_PALS_CRC 0x996A041Du
/* the castle approach's wall: its blocks (16 map entries each), tiles and palettes */
#define WALL_BLOCKS_AT 0x19A000
#define WALL_TILES 0x1AC600       /* tile n at + n * 32 (n 0x110-0x20F) */
#define WALL_TILES2 0x2CE600      /* (n 0x210-0x216: the gate's) */
#define WALL_PALS 0x191340
#define WALL_CRC 0x6E9C0312u
/* the band of the map shown: block rows from map row 44, block columns from map column 12;
 * the arena's x 0 is the map's x 100, its row WALL_TOP the map's y 380 */
static const uint8_t WALL_BLOCK[5][13] = {
    {72, 80, 81, 82, 83, 88, 89, 90, 91, 64, 64, 64, 64},
    {93, 93, 93, 93, 98, 93, 93, 93, 101, 108, 93, 93, 93},
    {95, 95, 95, 95, 99, 95, 102, 103, 104, 109, 110, 111, 112},
    {97, 97, 97, 97, 100, 105, 105, 106, 107, 113, 106, 105, 105},
    {64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64},
};
/* the title's lightning bolt (sprites, as the title draws them; x, y on the title) */
#define BOLT_CELLS 0xAD800
#define BOLT_PAL 0x63342
#define BOLT_CRC 0x54665E56u
static const Piece BOLT[] = {{88, 28, 0x199, 0x8000}, {104, 32, 0x19F, 0x0000}, {88, 16, 0x19C, 0x0100},
                             {120, 80, 0x199, 0x0800}, {104, 64, 0x19A, 0x0000}, {104, 48, 0x19B, 0x0000}};
/* the game's blood (the splash of a beaten boss: 8 frames, 16 x 16) */
#define BLOOD_CELLS 0x23DC00
#define BLOOD_PAL 0x8614DB
#define BLOOD_CRC 0xF1D822E8u
/* a boss's gauge (the Death fight's, as it is stored: its column is the bar) */
#define GAUGE_CELLS 0x1B0800
#define GAUGE_PAL 0x861493
#define GAUGE_CRC 0x6E0C1153u
/* the thunder: the game's own, the Prologue's (its carriage chase's, with the lightning: its
 * sound effect $77 there, an ADPCM clip, 4 kHz, 3 s), read from the disc and played over the
 * game's sound by the host. (Stage 1's effect $77 plays whatever its ADPCM RAM holds at the
 * same place, not thunder.) */
#define THUNDER_DELAY 10      /* frames after the flash */
#define THUNDER_AT 0x146804
#define THUNDER_LEN 0x17A2
#define THUNDER_CRC 0x18AD4485u
#define THUNDER_VOLUME 256    /* (the ADPCM's own level) */
#define THUNDER_SOFTEN 0
static const Piece GAUGE[] = {{16, 32, 0x1F9, 0x3000}, {16, 0, 0x1FC, 0x1000}, {0, 0, 0x1F8, 0x1000}};
/* the castle approach's standing torches: cells 0xB0-0xBB (with the wall's tiles), the flame in
 * its palette 9, the stand in its 14 */
#define TORCH_CELLS (WALL_TILES + 0xB0 * 128)
#define TORCH_PAL_FLAME 0x1915A2
#define TORCH_PAL_STAND 0x191364
#define TORCH_CRC 0xAF8C6D51u
static const uint16_t FLAME_CELLS[4] = {0xB1, 0xB3, 0xB4, 0xBB};
static const int TORCH_ARENA_X[2] = {44, 236};

static struct {
    int state, t;
    int loaded;                       /* 0 not yet, 1 ready, -1 not there */
    const TitleArt *arts;
    int n_arts;
    uint32_t sky[2][ARENA_H * ARENA_W];   /* the arena: its lightning's palette off and on */
    uint8_t solid[ARENA_H * ARENA_W];     /* (the wall's pixels: the sky is behind them) */
    uint8_t bolt[7 * 128], blood[8 * 128], gauge[8 * 128], torch[12 * 128];
    uint32_t torch_pal[2][16];
    uint32_t bolt_pal[1][16], blood_pal[1][16], gauge_pal[1][16];
    struct { float x, y, vx, vy; int age; bool on; } drop[24];
    /* the music: the stage's, kept while the fight's plays */
    bool music_ours;
    int music_start, music_end;
    int64_t music_pos;
    bool music_repeat, music_playing;
    uint8_t thunder[THUNDER_LEN];
    bool have_thunder;
    /* the lightning: a strike now and then (7-15 s after the last), the bolt
     * somewhere over the sky, the game's thunder after it */
    int strike_at, next_strike, strike_dx;
    bool strike_flip;

} fs;

void secret_set_title_arts(const TitleArt *arts, int n)
{
    fs.arts = arts;
    fs.n_arts = n;
}

/* any length from the data track */
static bool disc_span(Pce *p, uint32_t off, uint32_t len, uint8_t *out)
{
    while (len) {
        uint32_t k = len > 4096 ? 4096 : len;
        if (!disc_bytes(p, off, k, out)) return false;
        off += k, out += k, len -= k;
    }
    return true;
}

/* the game's run-length scheme into VRAM (6C:81CD): $00 n hi: n bytes, each with that high
 * byte; $01-$7F: a word that many times; $80-$FC: that many words (& $7F); $FE lo hi: the
 * address from the stream's base; $FF: the end */
static void unpack(const uint8_t *s, uint32_t n, uint16_t base, uint8_t flag, uint16_t *vram)
{
    uint32_t p = 0;
    uint16_t a = base;
    while (p < n) {
        uint8_t op = s[p];
        if (op == 0xFF || op == 0xFD) return;
        if (op == 0) {
            if (p + 3 > n) return;
            int k = s[p + 1];
            uint8_t hi = s[p + 2];
            for (int i = 0; i < k && p + 3 + i < n; i++) vram[a++ & 0x7FFF] = (uint16_t)(s[p + 3 + i] | hi << 8);
            p += 3 + (uint32_t)k;
        } else if (op < 0x80) {
            if (p + 3 > n) return;
            uint16_t w = (uint16_t)(s[p + 1] | s[p + 2] << 8);
            for (int i = 0; i < op; i++) vram[a++ & 0x7FFF] = w;
            p += 3;
        } else if (op == 0xFE) {
            if (p + 3 > n) return;
            int lo = s[p + 1] + (base & 0xFF), hi = s[p + 2] + (base >> 8) + (lo >> 8);
            if (!(flag & 1)) hi &= 0x0F;
            a = (uint16_t)((hi & 0xFF) << 8 | (lo & 0xFF));
            p += 3;
        } else {
            int k = op & 0x7F;
            for (int i = 0; i < k && p + 2 + 2 * i < n; i++, a++)
                vram[a & 0x7FFF] = (uint16_t)(s[p + 1 + 2 * i] | s[p + 2 + 2 * i] << 8);
            p += 1 + 2 * (uint32_t)k;
        }
    }
}

/* a background tile's pixel (colour 0-15): 4 planes, 8 rows */
static int tile_px(const uint16_t *t, int x, int y)
{
    int b = 7 - x;
    return (t[y] >> b & 1) | (t[y] >> (8 + b) & 1) << 1 | (t[8 + y] >> b & 1) << 2 | (t[8 + y] >> (8 + b) & 1) << 3;
}


/* ---- Maria, the arena's boss -------------------------------------------------------------- */
/* Maria fights as the game draws her when she is played: her own sprites (her poses, read from
 * the disc: where each piece of each is, as the game's sprite table has it), moving as she does
 * (walk, jump and somersault, crouch and slide, her doves). The host plays her: an AI picks her
 * moves by Richter's distance, dodges his whip now and then, and keeps to the arena. Her hits
 * on Richter are the game's own (the player's hit, as an enemy's touch makes it: its type, its
 * damage, which way he is knocked: the game's code does the rest, his flinch, blinking and
 * health); his whip and sub-weapons hit her (her gauge empties). Facing left as stored; drawn
 * mirrored facing right. Arena coordinates: x 0-395 across the picture, y the picture's. */
/* her poses: the game's animation frames she uses (MP_xx is frame $xx) */
enum { MP_01, MP_02, MP_03, MP_08, MP_09, MP_0A, MP_0B, MP_0C, MP_0D, MP_0E, MP_0F, MP_10, MP_11, MP_12,
       MP_13, MP_14, MP_15, MP_17, MP_18, MP_19, MP_1A, MP_1B, MP_1C, MP_1D, MP_1F, MP_20, MP_25, MP_26,
       MP_27, MP_29, MP_2A, MP_2B, MP_33, MP_34, MP_35, MP_COUNT };
static const uint8_t MP_FRAME[MP_COUNT] = {
    0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
    0x13, 0x14, 0x15, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1F, 0x20, 0x25, 0x26,
    0x27, 0x29, 0x2A, 0x2B, 0x33, 0x34, 0x35};
/* The game's tables for her, read from the disc:
 *  - her sprite frames (as at $9000): per frame a pointer to [count][5-byte pieces: flags (bit 0
 *    32 wide, bits 4-5 the height, bit 3 flipped), y, x (facing right), pattern low, pattern high
 *    in bits 4-6];
 *  - her cell uploads (as at $9800): per frame a pointer to a list of [bank][flags][VRAM address]
 *    [source address][32-byte blocks, when flags bit 7], $FF ending it; frames from 3 on reach
 *    their list through a byte table (MARIA_REMAP), frames 0-2 directly;
 *  - her cells (MARIA_GFX, the uploads' source: bank $1C at its start). */
#define MARIA_FRAMES 0xF2800
#define MARIA_FRAMES_LEN 0x400
#define MARIA_FRAMES_CRC 0x4CF29EECu
#define MARIA_LISTS 0xF3C00
#define MARIA_LISTS_LEN 0x400
#define MARIA_LISTS_CRC 0xEDBFC6EAu
#define MARIA_REMAP 0xF09A5
#define MARIA_REMAP_LEN 0x36
#define MARIA_REMAP_CRC 0xE369A5BCu
#define MP_MAXP 96
typedef struct { int8_t x, y; uint16_t attr; uint8_t n; uint16_t cell[8]; } MPiece;
static MPiece mpieces[MP_MAXP];
static uint16_t mpose_first[MP_COUNT + 1];
#define MARIA_GFX 0xF8800
#define MARIA_GFX_LEN 18432
#define MARIA_GFX_CRC 0xBEB819AEu
#define DOVE_CELLS 0x105800        /* 3 cells: the wings' beats */
#define DOVE_PAL 0x129392
#define DOVE_CRC 0x6B48488Cu
#define MARIA_FEET 22              /* her feet below her position */
#define MARIA_HP 32
#define WHIP_DAMAGE 2
#define WEAPON_DAMAGE 3

enum { M_INTRO, M_IDLE, M_WALK, M_THROW, M_JUMP, M_CROUCH, M_SLIDE, M_HURT, M_DOWN };
static const uint8_t WALK_CYCLE[8] = {MP_09, MP_0A, MP_0B, MP_0C, MP_0D, MP_0E, MP_0F, MP_08};
static const uint8_t FLIP_CYCLE[6] = {MP_10, MP_12, MP_13, MP_14, MP_15, MP_18};

static struct {
    uint8_t gfx[MARIA_GFX_LEN], dove[3 * 128];
    uint32_t dove_pal[1][16];
    float x, y, vx, vy;       /* her feet, arena coordinates */
    bool right;               /* facing right */
    int state, t, pose, hp, inv, flash;
    bool second;              /* the somersault (second jump) done */
    int walk_for, throws, dir;
    int dove_wait;            /* frames before she may throw again */
    int flinch_wait;          /* frames before a hit makes her flinch again */
    int org, fb0;             /* the picture's, last frame */
    struct { float x, y, vx, vy; int t; bool on; } dove_[4];
} mf;

static void maria_reset(void)
{
    mf.x = MARIA_ARENA_X + 8;
    mf.y = ARENA_FLOOR;
    mf.vx = mf.vy = 0;
    mf.right = false;
    mf.state = M_INTRO;
    mf.t = 0;
    mf.pose = MP_01;
    mf.hp = MARIA_HP;
    mf.inv = mf.flash = 0;
    mf.dove_wait = mf.flinch_wait = 0;
    memset(mf.dove_, 0, sizeof mf.dove_);
}

/* her poses from the game's tables: each piece's place, size and flip, and which of her cells
 * (128 bytes each, from MARIA_GFX) the frame's uploads put in the VRAM cells it shows */
static bool maria_poses(const uint8_t *fr, const uint8_t *ls, const uint8_t *rm)
{
    int np = 0;
    for (int k = 0; k < MP_COUNT; k++) {
        int f = MP_FRAME[k], vram[16];
        for (int i = 0; i < 16; i++) vram[i] = -1;
        int la = f < 3 ? 0x19 + 2 * f : 0x1F + 2 * rm[f];
        if (f >= MARIA_REMAP_LEN || la + 1 >= MARIA_LISTS_LEN) return false;
        int a = (ls[la] | ls[la + 1] << 8) - 0x9800;
        for (;;) {
            if (a < 0 || a >= MARIA_LISTS_LEN) return false;
            if (ls[a] == 0xFF) break;
            if (a + 6 > MARIA_LISTS_LEN) return false;
            int bank = ls[a], fl = ls[a + 1], v = ls[a + 2] | ls[a + 3] << 8, src = ls[a + 4] | ls[a + 5] << 8, cnt = 0;
            a += 6;
            if (fl & 0x80) {
                if (a >= MARIA_LISTS_LEN) return false;
                cnt = ls[a++];
            }
            long off = (long)(bank - 0x1C) * 0x2000 + (src - 0xA000);
            for (int c = 0; c < cnt / 4; c++) {
                int cell = v / 64 + c - 0x180;
                long o = off + c * 128L;
                if (cell < 0 || cell >= 16 || o < 0 || o % 128 || o + 128 > MARIA_GFX_LEN) return false;
                vram[cell] = (int)(o / 128);
            }
        }
        int pa = (fr[2 * f] | fr[2 * f + 1] << 8) - 0x9000;
        if (pa < 0 || pa >= MARIA_FRAMES_LEN) return false;
        int n = fr[pa];
        if (pa + 1 + 5 * n > MARIA_FRAMES_LEN || np + n > MP_MAXP) return false;
        mpose_first[k] = (uint16_t)np;
        for (int i = 0; i < n; i++) {
            const uint8_t *e = fr + pa + 1 + 5 * i;
            int cw = e[0] & 1 ? 2 : 1, cg = e[0] >> 4 & 3, ch = cg == 0 ? 1 : cg == 1 ? 2 : 4;
            int pat = e[3] | (e[4] >> 4 & 7) << 8, base = (pat >> 1) & ~((cw - 1) | (ch - 1) << 1);
            MPiece *m = &mpieces[np++];
            m->x = (int8_t)-((int8_t)e[2] + cw * 16);          /* facing left */
            m->y = (int8_t)e[1];
            m->attr = (uint16_t)((cw == 2 ? 0x100 : 0) | cg << 12 | (e[0] & 8 ? 0 : 0x800));
            m->n = (uint8_t)(cw * ch);
            for (int c = 0; c < cw * ch; c++) {
                int v = base + c % cw + c / cw * 2 - 0x180;
                if (v < 0 || v >= 16 || vram[v] < 0) return false;
                m->cell[c] = (uint16_t)vram[v];
            }
        }
    }
    mpose_first[MP_COUNT] = (uint16_t)np;
    return true;
}

static bool maria_load(Pce *p)
{
    uint8_t dv[3 * 128 + 18], fr[MARIA_FRAMES_LEN], ls[MARIA_LISTS_LEN], rm[MARIA_REMAP_LEN];
    if (!disc_span(p, MARIA_GFX, MARIA_GFX_LEN, mf.gfx) || crc32_of(mf.gfx, MARIA_GFX_LEN) != MARIA_GFX_CRC) return false;
    if (!disc_span(p, MARIA_FRAMES, sizeof fr, fr) || crc32_of(fr, sizeof fr) != MARIA_FRAMES_CRC ||
        !disc_span(p, MARIA_LISTS, sizeof ls, ls) || crc32_of(ls, sizeof ls) != MARIA_LISTS_CRC ||
        !disc_span(p, MARIA_REMAP, sizeof rm, rm) || crc32_of(rm, sizeof rm) != MARIA_REMAP_CRC ||
        !maria_poses(fr, ls, rm))
        return false;
    if (!disc_span(p, DOVE_CELLS, 3 * 128, dv) || !disc_span(p, DOVE_PAL, 18, dv + 3 * 128) || crc32_of(dv, sizeof dv) != DOVE_CRC)
        return false;
    memcpy(mf.dove, dv, 3 * 128);
    unpack_pal(dv + 3 * 128, mf.dove_pal[0]);
    return true;
}

/* a pose at screen x, y (her position), mirrored when facing right */
static void draw_mpose(const Canvas *cv, int pose, int x, int y, bool right, bool white)
{
    for (int i = mpose_first[pose + 1] - 1; i >= (int)mpose_first[pose]; i--) {
        const MPiece *e = &mpieces[i];
        int cgy = (e->attr >> 12) & 3, cw = e->attr & 0x100 ? 2 : 1, ch = cgy == 0 ? 1 : cgy == 1 ? 2 : 4;
        int sw = cw * 16, sh = ch * 16;
        bool xf = ((e->attr & 0x800) != 0) != right, yf = (e->attr & 0x8000) != 0;
        int ox = right ? -(e->x + sw) : e->x;
        for (int yy = 0; yy < sh; yy++)
            for (int xx = 0; xx < sw; xx++) {
                int sx = xf ? sw - 1 - xx : xx, sy = yf ? sh - 1 - yy : yy;
                uint16_t c = e->cell[(sy >> 4) * cw + (sx >> 4)];
                if (c == 0xFFFF) continue;
                int v = cell_px(mf.gfx + c * 128, sx & 15, sy & 15);
                int px = x + ox + xx, py = y + e->y + yy;
                if (v && DITHER[py & 3][px & 3] >= g_dissolve) plot(cv, px, py, white ? 0xFFFFFFFFu : sc.maria_pal[0][v]);
            }
    }
}

/* Richter in arena coordinates (his feet), from the game's (screen) */
static void richter_at(Pce *p, float *x, float *y)
{
    int sx = (int16_t)(p->wram[W_PX_HI] << 8 | p->wram[W_PX]), sy = (int16_t)(p->wram[W_PY_HI] << 8 | p->wram[W_PY]);
    *x = (float)(sx + mf.org - mf.fb0);
    *y = (float)(sy + MARIA_FEET);
}

/* the game's own hit on the player: type 2, `dmg`, knocked away from x (as an enemy's touch
 * makes it: 6B:9855); not while he is already hit or blinking */
static void hurt_richter(Pce *p, int dmg, float from_x)
{
    if (p->wram[0x13B0] || p->wram[0x1460]) return;
    float rx, ry;
    richter_at(p, &rx, &ry);
    p->wram[0x13B0] = 2;
    p->wram[0x12A8] = (uint8_t)dmg;
    p->wram[0x1778] = from_x > rx ? 1 : 0;   /* (0: knocked right, 1: left) */
}

static bool overlap(float ax0, float ay0, float ax1, float ay1, float bx0, float by0, float bx1, float by1)
{
    return ax0 < bx1 && bx0 < ax1 && ay0 < by1 && by0 < ay1;
}

/* her box: low while crouched or sliding */
static void maria_box(float *x0, float *y0, float *x1, float *y1)
{
    bool low = mf.state == M_SLIDE || mf.state == M_CROUCH;
    *x0 = mf.x - 8, *x1 = mf.x + 8, *y1 = mf.y, *y0 = mf.y - (low ? 20 : 44);
}

/* Richter's weapons on her: the whip's strike and his sub-weapons (his objects 4-15 with a box) */
static void maria_hits(Pce *p)
{
    if (mf.inv || mf.state == M_INTRO || mf.state == M_DOWN) return;
    float mx0, my0, mx1, my1;
    maria_box(&mx0, &my0, &mx1, &my1);
    for (int sl = 1; sl < 16; sl++) {
        if (!p->wram[0x0A10 + sl]) continue;
        int hw = p->wram[0x1988 + sl], hh = p->wram[0x1A38 + sl];
        if (!hw || !hh) continue;
        bool whip = p->wram[0x1828 + sl] == 5;   /* (its box is set only while it strikes: standing
                                                   * frames 05-07, crouching 0C-0E, in the air) */
        if (!whip && sl < 4) continue;
        int ox = (int16_t)(p->wram[W_PX_HI + sl] << 8 | p->wram[W_PX + sl]);
        int oy = (int16_t)(p->wram[W_PY_HI + sl] << 8 | p->wram[W_PY + sl]);
        int ahead = (int8_t)p->wram[0x1930 + sl], below = (int8_t)p->wram[0x19E0 + sl];
        float cx = (float)(ox + (whip && p->wram[0x0AC0] ? -ahead : ahead) + mf.org - mf.fb0), cy = (float)(oy + below);
        if (overlap(cx - hw, cy - hh, cx + hw, cy + hh, mx0, my0, mx1, my1)) {
            mf.hp -= whip ? WHIP_DAMAGE : WEAPON_DAMAGE;
            mf.inv = 40;
            mf.flash = 8;
            float rx, ry;
            richter_at(p, &rx, &ry);
            mf.dir = rx < mf.x ? 1 : -1;
            /* (a flinch now and then: hit again soon after, she only blinks and goes on;
             * in the air she only flashes) */
            if (mf.state != M_JUMP && !mf.flinch_wait) {
                mf.flinch_wait = 150;
                mf.state = M_HURT, mf.t = 0;
                mf.vx = 1.6f * (float)mf.dir;
            }
            if (mf.hp <= 0) mf.hp = 0, mf.state = M_DOWN, mf.t = 0, mf.vx = 0;
            return;
        }
    }
}

/* away from Richter if there is room behind her; cornered, forward over him: a somersault
 * across (or now and then a slide past) */
#define CORNER 70
static void maria_escape(float rx)
{
    float away = rx < mf.x ? 1.0f : -1.0f;
    bool cornered = away > 0 ? mf.x > ARENA_W - CORNER : mf.x < CORNER;
    mf.t = 0;
    mf.second = false;
    if (!cornered) {
        mf.state = M_JUMP, mf.vy = -5.0f, mf.vx = 1.8f * away;
    } else if (rnd(3)) {
        mf.state = M_JUMP, mf.vy = -5.4f, mf.vx = -2.6f * away;   /* (over him) */
        mf.right = away < 0;
    } else {
        mf.state = M_CROUCH, mf.dir = away > 0 ? -1 : 1;           /* (a slide past him) */
    }
}

static void maria_choose(Pce *p)
{
    float rx, ry;
    richter_at(p, &rx, &ry);
    float d = rx - mf.x, ad = d < 0 ? -d : d;
    mf.right = d > 0;
    int r = rnd(100);
    mf.t = 0;
    /* (her doves spaced out: not while one is flying or soon after the last) */
    bool flying = false;
    for (int i = 0; i < 4; i++) flying |= mf.dove_[i].on;

    if (ad > 150) {
        if (r < 60) mf.state = M_WALK, mf.walk_for = 30 + rnd(40), mf.dir = d > 0 ? 1 : -1;
        else mf.state = M_THROW, mf.throws = 1;
    } else if (ad > 80) {
        if (r < 45) mf.state = M_THROW, mf.throws = 1;
        else if (r < 70) mf.state = M_CROUCH, mf.dir = d > 0 ? 1 : -1;
        else mf.state = M_JUMP, mf.vy = -5.0f, mf.vx = (d > 0 ? 1.6f : -1.6f), mf.second = false;
    } else {
        if (r < 40) maria_escape(rx);
        else if (r < 70) mf.state = M_CROUCH, mf.dir = d > 0 ? 1 : -1;
        else mf.state = M_THROW, mf.throws = 1;
    }
    if (mf.state == M_THROW && (mf.dove_wait || flying)) {   /* (too soon: in close instead) */
        if (ad > 120) mf.state = M_WALK, mf.walk_for = 30 + rnd(40), mf.dir = d > 0 ? 1 : -1;
        else mf.state = M_CROUCH, mf.dir = d > 0 ? 1 : -1;
    }
}

/* a dove thrown from her hand toward Richter */
static void throw_dove(Pce *p)
{
    float rx, ry;
    richter_at(p, &rx, &ry);
    for (int i = 0; i < 4; i++)
        if (!mf.dove_[i].on) {
            mf.dove_[i].on = true;
            mf.dove_[i].t = 0;
            mf.dove_[i].x = mf.x + (mf.right ? 12.0f : -12.0f);
            mf.dove_[i].y = mf.y - 22;   /* (waist high, straight: to be jumped) */
            mf.dove_[i].vx = mf.right ? 2.2f : -2.2f;
            mf.dove_[i].vy = 0;
            return;
        }
}

/* before a frame: her move, her doves, the hits */
static void maria_step(Pce *p)
{
    float rx, ry;
    richter_at(p, &rx, &ry);
    if (mf.inv) mf.inv--;
    if (mf.dove_wait) mf.dove_wait--;
    if (mf.flinch_wait) mf.flinch_wait--;
    if (mf.flash) mf.flash--;
    maria_hits(p);
    mf.t++;
    switch (mf.state) {
    case M_INTRO:
        mf.pose = MP_01;
        mf.right = rx > mf.x;
        if (mf.t > 20) maria_choose(p);   /* (straight into the fight) */
        break;
    case M_IDLE:
        mf.pose = (mf.t / 40) % 3 == 2 ? MP_02 : MP_01;
        mf.right = rx > mf.x;
        if (mf.t > ((rx - mf.x) * (rx - mf.x) < 80 * 80 ? 6 : 24 + rnd(30))) maria_choose(p);
        break;
    case M_WALK:
        mf.pose = WALK_CYCLE[(mf.t / 5) & 7];
        mf.right = mf.dir > 0;
        mf.x += 1.3f * (float)mf.dir;
        if (mf.t > mf.walk_for || (mf.dir > 0 ? rx - mf.x : mf.x - rx) < 70) mf.state = M_IDLE, mf.t = 0;
        break;
    case M_THROW:
        mf.pose = mf.t < 6 ? MP_29 : mf.t < 12 ? MP_2A : MP_2B;
        if (mf.t == 8) throw_dove(p), mf.dove_wait = 150;
        if (mf.t >= 24) {
            if (--mf.throws > 0) mf.t = 0;
            else mf.state = M_IDLE, mf.t = 0;
        }
        break;
    case M_JUMP:
        mf.x += mf.vx;
        mf.y += mf.vy;
        mf.vy += 0.25f;
        if (!mf.second && mf.vy > -1.0f && rnd(3)) mf.second = true, mf.vy = -4.2f, mf.t = 0;   /* (the somersault) */
        if (mf.second) mf.pose = FLIP_CYCLE[mf.t / 4 < 5 ? mf.t / 4 : 5];
        else mf.pose = mf.vy < 0 ? MP_17 : MP_11;
        if (mf.y >= ARENA_FLOOR) {
            mf.y = ARENA_FLOOR;
            mf.vx = mf.vy = 0;
            mf.state = M_CROUCH, mf.t = 100;   /* (a moment crouched on landing) */
        }
        if (mf.second && overlap(rx - 7, ry - 44, rx + 7, ry, mf.x - 10, mf.y - 40, mf.x + 10, mf.y)) hurt_richter(p, 8, mf.x);
        break;
    case M_CROUCH:
        if (mf.t >= 100) {   /* (landing) */
            mf.pose = MP_1B;
            if (mf.t >= 108) mf.state = M_IDLE, mf.t = 0;
            break;
        }
        mf.pose = mf.t < 4 ? MP_19 : mf.t < 8 ? MP_1A : MP_1B;
        mf.right = mf.dir > 0;
        if (mf.t >= 14) mf.state = M_SLIDE, mf.t = 0, mf.vx = 3.4f * (float)mf.dir;
        break;
    case M_SLIDE:
        mf.pose = mf.t < 3 ? MP_1F : mf.t < 6 ? MP_20 : mf.t < 10 ? MP_25 : mf.t < 30 ? MP_26 : MP_27;
        mf.x += mf.vx;
        mf.vx *= 0.97f;
        if (overlap(rx - 7, ry - 44, rx + 7, ry, mf.x - 14, mf.y - 16, mf.x + 14, mf.y)) hurt_richter(p, 8, mf.x);
        if (mf.t >= 40) mf.state = M_IDLE, mf.t = 0;
        break;
    case M_HURT:
        mf.pose = mf.t < 6 ? MP_33 : mf.t < 12 ? MP_34 : MP_35;
        mf.x += mf.vx;
        mf.vx *= 0.9f;
        if (mf.t >= 20) maria_escape(rx);   /* (straight away from more hits) */
        break;
    case M_DOWN:   /* (her gauge empty: the second phase comes in the next stage of this) */
        mf.pose = MP_35;
        mf.flash = (mf.t / 4) & 1 ? 2 : 0;
        if (mf.t >= 90) mf.hp = MARIA_HP, mf.state = M_IDLE, mf.t = 0;
        break;
    }
    /* the whip coming: now and then she jumps it */
    if ((mf.state == M_IDLE || mf.state == M_WALK) && p->wram[0x0EE0] == 0x03 && (rx - mf.x) * (rx - mf.x) < 90 * 90 && rnd(100) < 4)
        maria_escape(rx);
    if (mf.x < 14) mf.x = 14, mf.vx = 0;
    if (mf.x > ARENA_W - 14) mf.x = ARENA_W - 14, mf.vx = 0;
    /* touching her hurts, as an enemy does */
    if (mf.state != M_DOWN && mf.state != M_INTRO && mf.state != M_HURT &&
        overlap(rx - 7, ry - 44, rx + 7, ry, mf.x - 6, mf.y - 40, mf.x + 6, mf.y))
        hurt_richter(p, 4, mf.x);
    /* her doves: flying straight on */
    for (int i = 0; i < 4; i++) {
        if (!mf.dove_[i].on) continue;
        mf.dove_[i].x += mf.dove_[i].vx;
        mf.dove_[i].y += mf.dove_[i].vy;
        mf.dove_[i].t++;
        if (mf.dove_[i].x < -16 || mf.dove_[i].x > ARENA_W + 16) mf.dove_[i].on = false;
        else if (overlap(rx - 7, ry - 44, rx + 7, ry, mf.dove_[i].x - 6, mf.dove_[i].y - 6, mf.dove_[i].x + 6, mf.dove_[i].y + 6)) {
            hurt_richter(p, 6, mf.dove_[i].x);
            mf.dove_[i].on = false;
        }
    }
}

/* after a frame: her doves, her, her gauge's fill */
static void maria_draw(const Canvas *cv, int base)
{
    for (int i = 0; i < 4; i++)
        if (mf.dove_[i].on) {
            Piece dv = {0, 0, (uint16_t)(0x1A0 + (mf.dove_[i].t / 4) % 3), (uint16_t)(mf.dove_[i].vx < 0 ? 0x0800 : 0x0000)};
            draw_frame(cv, &dv, 1, mf.dove, 0x1A0, 0xFFFF, NULL, mf.dove_pal, base + (int)mf.dove_[i].x - 8, (int)mf.dove_[i].y - 8, 16, false, false);
        }
    if (!(mf.inv && (mf.inv & 2) && !mf.flash))   /* (blinking while she cannot be hurt) */
        draw_mpose(cv, mf.pose, base + (int)mf.x, (int)mf.y - MARIA_FEET, mf.right, mf.flash > 0);
}

static void gauge_fill(const Canvas *cv, int gx, int gy)
{
    static const uint32_t TUBE[4] = {0xFFB6B6DAu, 0xFFFFFFFFu, 0xFFDADAFFu, 0xFF9191B6u};
    int h = mf.hp * 86 / MARIA_HP;
    for (int ly = 94 - h; ly <= 94; ly++)
        for (int k = 0; k < 4; k++) plot(cv, gx + 24 + k, gy + ly, TUBE[k]);
}

static bool fight_load(Pce *p)
{
    if (fs.loaded) return fs.loaded > 0;
    fs.loaded = -1;
    static uint16_t vram[0x8000];
    static uint8_t buf[TITLE_STREAM_MAX];
    memset(vram, 0, sizeof vram);
    for (size_t i = 0; i < sizeof TITLE_STREAMS / sizeof TITLE_STREAMS[0]; i++) {
        if (!disc_span(p, TITLE_STREAMS[i].off, TITLE_STREAM_MAX, buf)) return false;
        unpack(buf, TITLE_STREAM_MAX, TITLE_STREAMS[i].vram, TITLE_STREAMS[i].flag, vram);
    }
    uint8_t vb[0x10000];
    for (int i = 0; i < 0x8000; i++) vb[i * 2] = (uint8_t)vram[i], vb[i * 2 + 1] = (uint8_t)(vram[i] >> 8);
    if (crc32_of(vb, sizeof vb) != TITLE_VRAM_CRC) return false;
    uint8_t tp[12 * 18];
    if (!disc_span(p, TITLE_PALS, sizeof tp, tp) || crc32_of(tp, sizeof tp) != TITLE_PALS_CRC) return false;
    uint32_t pal[2][128];
    for (int k = 0; k < 8; k++) {
        unpack_pal(tp + 18 * k, pal[0] + 16 * k);
        unpack_pal(tp + 18 * (k >= 4 ? k + 4 : k), pal[1] + 16 * k);
    }
    static uint8_t wall[0x2000 + 0x100 * 32 + 7 * 32 + 8 * 18];
    if (!disc_span(p, WALL_BLOCKS_AT, 0x2000, wall) || !disc_span(p, WALL_TILES + 0x110 * 32, 0x100 * 32, wall + 0x2000) ||
        !disc_span(p, WALL_TILES2 + 0x210 * 32, 7 * 32, wall + 0x2000 + 0x100 * 32) ||
        !disc_span(p, WALL_PALS, 8 * 18, wall + 0x2000 + 0x100 * 32 + 7 * 32) || crc32_of(wall, sizeof wall) != WALL_CRC)
        return false;
    uint8_t sp[8 * 128 + 18];
    if (!disc_span(p, BOLT_CELLS + 0x199 * 128, 7 * 128, sp) || !disc_span(p, BOLT_PAL, 18, sp + 7 * 128) ||
        crc32_of(sp, 7 * 128 + 18) != BOLT_CRC)
        return false;
    memcpy(fs.bolt, sp, 7 * 128);
    unpack_pal(sp + 7 * 128, fs.bolt_pal[0]);
    if (!disc_span(p, BLOOD_CELLS + 0x178 * 128, 8 * 128, sp) || !disc_span(p, BLOOD_PAL, 18, sp + 8 * 128) ||
        crc32_of(sp, sizeof sp) != BLOOD_CRC)
        return false;
    memcpy(fs.blood, sp, 8 * 128);
    unpack_pal(sp + 8 * 128, fs.blood_pal[0]);
    if (!disc_span(p, GAUGE_CELLS + 0x1F8 * 128, 8 * 128, sp) || !disc_span(p, GAUGE_PAL, 18, sp + 8 * 128) ||
        crc32_of(sp, sizeof sp) != GAUGE_CRC)
        return false;
    memcpy(fs.gauge, sp, 8 * 128);
    unpack_pal(sp + 8 * 128, fs.gauge_pal[0]);
    uint8_t tb[12 * 128 + 36];
    if (!disc_span(p, TORCH_CELLS, 12 * 128, tb) || !disc_span(p, TORCH_PAL_FLAME, 18, tb + 12 * 128) ||
        !disc_span(p, TORCH_PAL_STAND, 18, tb + 12 * 128 + 18) || crc32_of(tb, sizeof tb) != TORCH_CRC)
        return false;
    memcpy(fs.torch, tb, 12 * 128);
    fs.have_thunder = disc_span(p, THUNDER_AT, THUNDER_LEN, fs.thunder) && crc32_of(fs.thunder, THUNDER_LEN) == THUNDER_CRC;
    unpack_pal(tb + 12 * 128, fs.torch_pal[0]);
    unpack_pal(tb + 12 * 128 + 18, fs.torch_pal[1]);
    /* the title art that is the title's (its layout: the title's map) */
    const TitleArt *art = NULL;
    for (int i = 0; i < fs.n_arts && !art; i++) {
        int same = 0;
        for (int r = 0; r < TITLEART_ROWS; r++)
            for (int c = 0; c < TITLEART_COLS; c++) same += fs.arts[i].layout[r][c] == vram[r * 64 + c];
        if (fs.arts[i].loaded && same > TITLEART_ROWS * TITLEART_COLS * 3 / 4) art = &fs.arts[i];
    }
    /* the sky: the title (its map, palette entries), its sides from the art (else its edges
     * carried on, mirrored), raised */
    for (int y = 0; y < ARENA_H; y++) {
        int ty = y + SKY_LIFT > 223 ? 223 : y + SKY_LIFT;
        for (int x = 0; x < ARENA_W; x++) {
            int e = 0;
            int mx = x - TITLEART_SIDE;
            if (mx >= 0 && mx < 256) {
                uint16_t m = vram[(ty >> 3) * 64 + (mx >> 3)];
                int c = tile_px(&vram[(m & 0x7FF) * 16], mx & 7, ty & 7);
                e = c ? (m >> 12) * 16 + c : 0;
            } else if (art) {
                uint16_t a = art->side[ty][mx < 0 ? x : TITLEART_SIDE + (mx - 256)];
                e = a == TITLEART_NONE ? 0 : a & 0x7F;
            } else {
                int sx = mx < 0 ? -mx - 1 : 511 - mx;
                uint16_t m = vram[(ty >> 3) * 64 + (sx >> 3)];
                int c = tile_px(&vram[(m & 0x7FF) * 16], sx & 7, ty & 7);
                e = c ? (m >> 12) * 16 + c : 0;
            }
            fs.sky[0][y * ARENA_W + x] = pal[0][e & 0x7F];
            fs.sky[1][y * ARENA_W + x] = pal[1][e & 0x7F];
        }
    }
    /* the wall over it: map rows from y 380 (above its top course, only its brown merlons) */
    uint32_t wp[8][16];
    for (int k = 0; k < 8; k++) unpack_pal(wall + 0x2000 + 0x100 * 32 + 7 * 32 + 18 * k, wp[k]);
    memset(fs.solid, 0, sizeof fs.solid);
    for (int y = WALL_TOP; y < ARENA_H; y++) {
        int my = 380 + y - WALL_TOP, br = (my - 352) / 32;
        if (br > 4) break;
        for (int x = 0; x < ARENA_W; x++) {
            int mxx = 100 + x, bc = (mxx - 96) / 32;
            int blk = WALL_BLOCK[br][bc], ti = ((my >> 3) & 3) * 4 + ((mxx >> 3) & 3);
            uint16_t m = (uint16_t)(wall[blk * 32 + ti * 2] | wall[blk * 32 + ti * 2 + 1] << 8);
            int t = m & 0x7FF;
            if (t < 0x110 || t > 0x216) continue;
            const uint8_t *tb = wall + 0x2000 + (t - 0x110) * 32;
            uint16_t tw[16];
            for (int i = 0; i < 16; i++) tw[i] = (uint16_t)(tb[i * 2] | tb[i * 2 + 1] << 8);
            int c = tile_px(tw, mxx & 7, my & 7);
            if (!c) continue;
            uint32_t col = wp[(m >> 12) & 7][c];
            int r = col >> 16 & 255, g = col >> 8 & 255, b = col & 255;
            bool brown = r >= g && g >= b && r > 40;
            if (my < 393 && !brown && !fs.solid[(y - 1) * ARENA_W + x]) continue;   /* (above a merlon) */
            fs.sky[0][y * ARENA_W + x] = fs.sky[1][y * ARENA_W + x] = col;
            fs.solid[y * ARENA_W + x] = 1;
        }
    }
    /* black behind the wall (its gaps), from its top course down */
    for (int y = WALL_TOP + 13; y < ARENA_H; y++)
        for (int x = 0; x < ARENA_W; x++)
            if (!fs.solid[y * ARENA_W + x]) fs.sky[0][y * ARENA_W + x] = fs.sky[1][y * ARENA_W + x] = 0xFF000000u;
    if (!maria_load(p)) return false;
    fs.loaded = 1;
    return true;
}


/* the camera to the room's right end and held there (the arena: the whole picture) */
static void lock_camera(Pce *p)
{
    int cam = cam_page(p) * 256 + p->wram[W_CAM_X], d = 768 - cam;
    p->wram[W_CAM_PAGE] = 3;
    p->wram[0x1E10] = 1;
    p->wram[W_CAM_X] = 0;
    p->wram[CAM_LO] = 0;
    for (int s = 0; s < 16; s++) {   /* (the player's objects: in screen coordinates) */
        if (!p->wram[0x0A10 + s] && s) continue;
        int x = (int16_t)(p->wram[W_PX_HI + s] << 8 | p->wram[W_PX + s]) - d;
        p->wram[W_PX_HI + s] = (uint8_t)(x >> 8);
        p->wram[W_PX + s] = (uint8_t)x;
    }
    p->wram[LIMIT_LEFT_PAGE] = 3;
    p->wram[0x1E8E] = 0;                 /* (Y: page 1 less DROP) */
    p->wram[0x1E28] = 0;
    p->wram[0x1E20] = (uint8_t)(256 - DROP);
}

/* the camera's Y back to the room's (and the player's objects with it) */
static void unlock_camera(Pce *p)
{
    if (p->wram[0x1E8E] != 0) return;
    p->wram[0x1E8E] = 1;
    p->wram[0x1E28] = 1;
    p->wram[0x1E20] = 0;
    for (int s = 0; s < 16; s++) {
        if (!p->wram[0x0A10 + s] && s) continue;
        int y = (int16_t)(p->wram[W_PY_HI + s] << 8 | p->wram[W_PY + s]) - DROP;
        p->wram[W_PY_HI + s] = (uint8_t)(y >> 8);
        p->wram[W_PY + s] = (uint8_t)y;
    }
}

static void music_fight(Pce *p);
static void music_back(Pce *p);

/* before a frame */
static void fight_frame(Pce *p)
{
    if (!in_hidden(p)) {
        if (fs.state != FIGHT_NONE) music_back(p);
        fs.state = FIGHT_NONE;
        return;
    }
    if (fs.state == FIGHT_NONE) {
        if (p->wram[LIMIT_LEFT_PAGE] == 3) {   /* (a state loaded mid-fight): the room's own limits */
            p->wram[LIMIT_LEFT_PAGE] = 2;
            p->wram[0x1EC6] = 3;
            p->wram[0x1EC7] = 0;
            unlock_camera(p);
        }
        return;
    }
    if (!playing(p)) return;
    fs.t++;
    if (fs.state == FIGHT_DYING && fs.t >= DYING_T) {
        fs.state = FIGHT_FADE, fs.t = 0;
    } else if (fs.state == FIGHT_FADE && fs.t == FADE_T / 2) {
        lock_camera(p);
    } else if (fs.state == FIGHT_FADE && fs.t >= FADE_T) {
        fs.state = FIGHT_ARENA, fs.t = 0;
        maria_reset();
        music_fight(p);
        fs.strike_at = -1000;
        fs.next_strike = 120 + rnd(120);   /* (the first soon after the arena comes in) */
    }
    if (fs.state >= FIGHT_FADE && cam_page(p) == 3) p->wram[LIMIT_LEFT_PAGE] = 3;
    if (fs.state == FIGHT_ARENA) {
        maria_step(p);
        if (fs.t >= fs.next_strike) {
            fs.strike_at = fs.t;
            fs.next_strike = fs.t + 420 + rnd(481);   /* (7-15 s on) */
            fs.strike_dx = rnd(200) - 120;
            fs.strike_flip = rnd(2);
        }
        if (fs.t == fs.strike_at + THUNDER_DELAY && fs.have_thunder)
            sound_extra_play_at(fs.thunder, THUNDER_LEN, 8, THUNDER_VOLUME, THUNDER_SOFTEN);
    }
}

/* the arena's pixel at its x, y */
static uint32_t arena_px(int ax, int y, bool flash)
{
    if (ax < 0) ax = 0;
    if (ax >= ARENA_W) ax = ARENA_W - 1;
    return fs.sky[flash][y * ARENA_W + ax];
}

/* the arena over the picture, behind the game's sprites; `a` 0-256: how much of it */
static void draw_arena(const Canvas *cv, int a, bool flash, int fb0)
{
    for (int y = 0; y < cv->h; y++)   /* (the picture's last row too: the arena's carried on) */
        for (int fx = 0; fx < cv->w; fx++) {
            if (cv->spr[y * PCE_FB_MAX_W + fx]) continue;
            uint32_t c = arena_px(fx - fb0, y < ARENA_H ? y : ARENA_H - 1, flash), *px = &cv->fb[y * PCE_FB_MAX_W + fx];
            if (a >= 256) {
                *px = c;
                continue;
            }
            uint32_t o = *px;
            int r = ((int)(o >> 16 & 255) * (256 - a) + (int)(c >> 16 & 255) * a) >> 8;
            int g = ((int)(o >> 8 & 255) * (256 - a) + (int)(c >> 8 & 255) * a) >> 8;
            int b = ((int)(o & 255) * (256 - a) + (int)(c & 255) * a) >> 8;
            *px = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        }
}


/* the torches, at their places in the arena (its x 0 at screen x `base`) */
static void draw_torches(const Canvas *cv, int base, uint64_t frame)
{
    for (int i = 0; i < 2; i++) {
        Piece stand = {0, 16, 0xB0, 0x1001}, flame = {0, 0, FLAME_CELLS[(frame / 5) % 4], 0x0000};
        int x = base + TORCH_ARENA_X[i], y = ARENA_FLOOR - 48;
        draw_frame(cv, &stand, 1, fs.torch, 0xB0, 0xFFFF, NULL, fs.torch_pal, x, y, 16, false, false);
        draw_frame(cv, &flame, 1, fs.torch, 0xB0, 0xFFFF, NULL, fs.torch_pal, x, y, 16, false, false);
    }
}

/* the game's sprites (Richter, his weapons) faded toward the arena behind them, `shown` 0-256;
 * the HUD (top left) left as it is */
static void fade_sprites(const Canvas *cv, int shown, int fb0)
{
    for (int y = 0; y < cv->h; y++)
        for (int fx = 0; fx < cv->w; fx++) {
            if (!cv->spr[y * PCE_FB_MAX_W + fx]) continue;
            int sx = fx - cv->org;
            if (y < 112 && sx < 112) continue;
            uint32_t c = arena_px(fx - fb0, y < ARENA_H ? y : ARENA_H - 1, false), *px = &cv->fb[y * PCE_FB_MAX_W + fx], o = *px;
            int r = ((int)(c >> 16 & 255) * (256 - shown) + (int)(o >> 16 & 255) * shown) >> 8;
            int g = ((int)(c >> 8 & 255) * (256 - shown) + (int)(o >> 8 & 255) * shown) >> 8;
            int b = ((int)(c & 255) * (256 - shown) + (int)(o & 255) * shown) >> 8;
            *px = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        }
}

/* the blood: drops thrown from Death as he dies, each through the splash's 8 frames */
static void blood_step(int cx, int cy)
{
    if (fs.state == FIGHT_DYING && fs.t < 110 && fs.t % 3 == 0)
        for (int i = 0; i < 24; i++)
            if (!fs.drop[i].on) {
                fs.drop[i].on = true;
                fs.drop[i].age = 0;
                fs.drop[i].x = (float)(cx - 8 + rnd(20) - 10);
                fs.drop[i].y = (float)(cy - 8 + rnd(30) - 15);
                fs.drop[i].vx = (float)(rnd(50) - 15) / 20.0f;
                fs.drop[i].vy = (float)(rnd(30) - 25) / 20.0f;
                break;
            }
    for (int i = 0; i < 24; i++) {
        if (!fs.drop[i].on) continue;
        fs.drop[i].x += fs.drop[i].vx;
        fs.drop[i].y += fs.drop[i].vy;
        fs.drop[i].vy += 0.06f;
        if (++fs.drop[i].age >= 48) fs.drop[i].on = false;
    }
}

static bool fight_busy(void) { return fs.state != FIGHT_NONE; }

/* The fight's music: the disc's own Bloody Tears (audio track 11), looping, played as the game
 * plays its music; the stage's put back (where it was) once the room is left. */
#define FIGHT_TRACK 11
static void music_fade_out(Pce *p)
{
    if (!fs.music_ours) {
        fs.music_start = p->cdda.start_lba, fs.music_end = p->cdda.end_lba, fs.music_pos = p->cdda.pos;
        fs.music_repeat = p->cdda.repeat, fs.music_playing = p->cdda.playing;
        fs.music_ours = true;
    }
    p->cdda.fade_step = -3;   /* (as the game's CD_FADE $0C: about 2.5 s) */
}
static void music_fight(Pce *p)
{
    const DiscTrack *t = disc_track(&p->disc, FIGHT_TRACK);
    if (!t || t->data) return;
    p->cdda.start_lba = t->lba;
    p->cdda.end_lba = t->lba + t->sectors;
    p->cdda.pos = (int64_t)t->lba * 588;
    p->cdda.repeat = true;
    p->cdda.playing = true;
    p->cdda.volume = 256;
    p->cdda.fade_step = 0;
}
static void music_back(Pce *p)
{
    if (!fs.music_ours) return;
    p->cdda.start_lba = fs.music_start, p->cdda.end_lba = fs.music_end, p->cdda.pos = fs.music_pos;
    p->cdda.repeat = fs.music_repeat, p->cdda.playing = fs.music_playing;
    p->cdda.volume = 256;
    p->cdda.fade_step = 0;
    fs.music_ours = false;
}
static void fight_reset(void) { fs.state = FIGHT_NONE; fs.music_ours = false; }
static void fight_start(Pce *p)
{
    if (fs.state == FIGHT_NONE && fight_load(p)) {
        fs.state = FIGHT_DYING, fs.t = 0;
        music_fade_out(p);
        memset(fs.drop, 0, sizeof fs.drop);
    }
}

void secret_after_frame(Pce *p)
{
    if (p->wram[W_AREA] != AREA || !in_hidden(p) || !scene_load(p)) return;
    Vdc *d = &p->vdc;
    Canvas cv = {d->fb, vdc_sprite_pixels(), d->fb_wide - vdc_fb_shift(), d->fb_w, d->fb_h};
    int cam = scene_cam(p);
    /* (the room's scene by the camera its picture was drawn with: the game draws a frame
     * with the scroll it set the frame before, so the camera in RAM is a step ahead while it
     * moves and the table shook as Richter walked; the background's X scroll, low 9 bits,
     * tells which) */
    cam += ((((int)vdc_row_scroll()[ROOM_FLOOR - 40] - (cam + 512)) & 0x1FF) ^ 0x100) - 0x100;
    /* (the arena in the level, by the camera its picture was drawn with: with Level ends the
     * walkable arena's picture is moved back as its camera follows Richter, so it stands still
     * on the screen; without, it scrolls as a room does) */
    int fb0 = ARENA_X0 - (cam + 512) + cv.org;   /* (the picture's column of the arena's x 0) */
    int lt = fs.t - fs.strike_at;   /* (a strike flashes as the title's: 3 frames, 3 dark, 3) */
    bool flash = fs.state == FIGHT_ARENA && ((lt >= 0 && lt <= 2) || (lt >= 6 && lt <= 8));
    if (fs.state == FIGHT_ARENA) {
        int base = fb0 - cv.org;   /* (the arena's x 0 on the screen) */
        draw_arena(&cv, 256, flash, fb0);
        if (lt >= 0 && lt <= 2)   /* (the bolt, its place and side each strike's own) */
            draw_frame(&cv, BOLT, 6, fs.bolt, 0x199, 0xFFFF, NULL, fs.bolt_pal, base + TITLEART_SIDE + fs.strike_dx,
                       -SKY_LIFT, 224, fs.strike_flip, false);
        draw_torches(&cv, base, p->frame);
        mf.org = cv.org, mf.fb0 = fb0;
        maria_draw(&cv, base);
        draw_frame(&cv, GAUGE, 3, fs.gauge, 0x1F8, 0xFFFF, NULL, fs.gauge_pal, base + GAUGE_ARENA_X, 16, 32, false, false);
        gauge_fill(&cv, base + GAUGE_ARENA_X, 16);
        return;
    }
    /* the table, its candle (flickering) and the cards played */
    int tx0 = TABLE_X0 - cam, tx1 = TABLE_X1 - cam, ty = TABLE_Y;
    for (int k = 0; k < 2; k++) {
        int lx = k ? tx1 - 7 : tx0 + 4;
        rect(&cv, lx, ty + 8, lx + 3, ROOM_FLOOR - 1, WOOD);
        rect(&cv, lx, ty + 8, lx, ROOM_FLOOR - 1, WOOD_L);
        rect(&cv, lx + 3, ty + 8, lx + 3, ROOM_FLOOR - 1, WOOD_D);
    }
    rect(&cv, tx0, ty, tx1, ty + 1, WOOD_L);
    rect(&cv, tx0, ty + 2, tx1, ty + 4, WOOD);
    rect(&cv, tx0 + 2, ty + 5, tx1 - 2, ty + 7, WOOD_D);
    int cx = (tx0 + tx1) / 2;
    rect(&cv, cx - 1, ty - 7, cx + 1, ty - 1, 0xFFDADAB6u);
    bool flick = (p->frame / 5) % 3 == 0;
    plot(&cv, cx, ty - 8, 0xFFFFB600u);
    plot(&cv, cx, ty - 9, 0xFFFFB600u);
    plot(&cv, cx + (flick ? 1 : 0), ty - 10, 0xFFFFFF91u);
    plot(&cv, cx - 1, ty - 9, 0xFFFF9100u);
    plot(&cv, cx + 1, ty - 9, 0xFFFF9100u);
    /* where the hands are (relative to the table's middle, at its top) */
    int dx, dy, mx = MARIA_X - cam, my = ROOM_FLOOR - MARIA_H;
    death_at(p, cam, &dx, &dy);
    int hand_x[2] = {dx + 26 - cx, mx - cx}, hand_y[2] = {dy + 46 - ty, my + 20 - ty};
    if (p->frame != sc.last_frame) {
        sc.last_frame = p->frame;
        if (fs.state == FIGHT_NONE) cards_step(hand_x, hand_y);
        else blood_step(dx + DEATH_W / 2, dy + DEATH_H / 2);
    }
    /* the deck and the cards played */
    for (int i = 0; i < (sc.deck > 4 ? 3 : sc.deck > 0 ? sc.deck < 3 ? sc.deck : 2 : 0); i++)
        card(&cv, cx - 22, ty - 2 - i, 5, 2, 0);
    for (int i = 0; i < sc.n_table; i++) card(&cv, cx + sc.table_x[i], ty - 2, 5, 2, sc.table_kind[i]);
    /* Death, hovering, his cards held toward the table */
    if (fs.state == FIGHT_NONE) {
        draw_frame(&cv, DEATH, 8, sc.death, 0x140, 0x1F4, sc.death + 16 * 128, sc.death_pal, dx, dy, DEATH_W, false, sc.flash > 0);
        for (int i = 0; i < sc.hand[0] && i < 4; i++) card(&cv, dx + 24 + i * 2, dy + 46 - (i & 1), 3, 5, 0);
    } else if (fs.state == FIGHT_DYING) {   /* shaking, flashing, coming apart */
        int t = fs.t;
        g_dissolve = t < 60 ? 0 : (t - 60) * 17 / (DYING_T - 60);
        draw_frame(&cv, DEATH, 8, sc.death, 0x140, 0x1F4, sc.death + 16 * 128, sc.death_pal,
                   dx + (t < 70 ? ((t >> 1) & 1 ? 2 : -2) : 0), dy, DEATH_W, false, t < 48 && !((t >> 2) & 1));
        g_dissolve = 0;
    }
    /* Maria, facing him, her hand of cards up */
    draw_frame(&cv, MARIA, 2, sc.maria, 0x184, 0xFFFF, NULL, sc.maria_pal, mx, my, MARIA_W, true, false);
    for (int i = 0; i < sc.hand[1] && i < 4; i++) card(&cv, mx - 2 + i * 2, my + 20 - (i & 1), 3, 5, 0);
    /* a card (or the swept trick) on its way */
    if (sc.moving) {
        double t = (double)sc.mv_t / sc.mv_len;
        int x = cx + sc.mv_from_x + (int)lround((sc.mv_to_x - sc.mv_from_x) * t);
        int y = ty + sc.mv_from_y + (int)lround((sc.mv_to_y - sc.mv_from_y) * t - (sc.mv_sweep ? 0 : 8 * sin(3.14159265 * t)));
        if (sc.mv_sweep)
            for (int i = 0; i < sc.mv_sweep && i < 3; i++) card(&cv, x + i * 2, y, 5, 2, 0);
        else card(&cv, x, y, sc.mv_to == 1 ? 5 : 3, sc.mv_to == 1 ? 2 : 5, sc.mv_to == 1 ? sc.mv_kind : 0);
    }
    candle_glow(&cv, cx, ty - 9, p->frame);
    for (int i = 0; i < 24; i++)   /* the blood */
        if (fs.drop[i].on) {
            Piece d1 = {0, 0, 0x178, 0x0000};
            d1.cell = (uint16_t)(0x178 + fs.drop[i].age / 6);
            draw_frame(&cv, &d1, 1, fs.blood, 0x178, 0xFFFF, NULL, fs.blood_pal, (int)fs.drop[i].x, (int)fs.drop[i].y, 16, false, false);
        }
    if (fs.state == FIGHT_FADE) {
        mf.org = cv.org, mf.fb0 = fb0;
        draw_arena(&cv, fs.t * 256 / FADE_T, false, fb0);
        int half = FADE_T / 2, shown = fs.t < half ? 256 - fs.t * 256 / half : (fs.t - half) * 256 / half;
        fade_sprites(&cv, shown, fb0);
        if (fs.t >= half) {   /* (the arena's torches and Maria coming in) */
            int base = fb0 - cv.org;
            g_dissolve = 16 - shown * 16 / 256;
            draw_torches(&cv, base, p->frame);
            draw_frame(&cv, MARIA, 2, sc.maria, 0x184, 0xFFFF, NULL, sc.maria_pal, base + MARIA_ARENA_X, ARENA_FLOOR - MARIA_H, MARIA_W, true, false);
            g_dissolve = 0;
        }
    }
}
