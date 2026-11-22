/* The Konami code door in Stage 1's house and the hidden room behind it (secret.h). */
#include "secret.h"

#include <math.h>
#include <string.h>

#include "game.h"
#include "pce/disc.h"
#include "pce/sound.h"

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
#define FREE_END 0x9FA0
#define BORROW_A 1          /* the street's screens lent to the hidden room */
#define STASH_LEN (2 * 64 + 2 * 8)

static const uint8_t HOUSE_HEADER[14] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00,
                                         0x01, 0x01, 0x20, 0xAA, 0x00, 0x00};
static const uint8_t HOUSE_SCREEN[8] = {0x90, 0x91, 0x92, 0x93, 0x9D, 0x9E, 0x9F, 0xA0};

/* The hidden room: two screens side by side at the level's X pages 2 and 3, Y page 1 (the
 * house's row; the house is page 0), so the house's own objects, all on page 0, stay out of
 * reach. Stage 1's blocks: E8-ED stone and the dark interior's brick (E9 the ceiling, EA/ED
 * the wall's edge), EE/EF the dark brick wall (EF with a pillar, EB/EC above them), F1 a
 * barred window (two blocks tall), F0 a chair, 118 the floor's ledge, 119 below the floor,
 * 9F/A1 the house's arched door (the way back). Row 7 is below the picture. */
static uint16_t layout[8][16];
static void make_layout(void)
{
    static const uint8_t wall[8] = {0xEE, 0xEF, 0xEE, 0xEE, 0xEE, 0xEF, 0xEE, 0xEE};
    for (int c = 0; c < 16; c++) {
        uint16_t lw = wall[c & 7];
        layout[0][c] = 0xE9;
        layout[1][c] = lw == 0xEF ? 0xEC : 0xEB;
        for (int r = 2; r <= 5; r++) layout[r][c] = lw;
        layout[6][c] = 0x118;
        layout[7][c] = 0x119;
    }
    layout[1][5] = layout[2][5] = 0xF1;   /* windows */
    layout[1][12] = layout[2][12] = 0xF1;
    layout[5][6] = layout[5][10] = 0xF0;  /* chairs */
    layout[4][2] = 0x9F;                  /* the door back */
    layout[5][2] = 0xA1;
    for (int r = 0; r <= 5; r++) layout[r][15] = 0xE8;
    layout[0][0] = 0xE8;
    layout[1][0] = 0xEA;
    for (int r = 2; r <= 5; r++) layout[r][0] = 0xED;
}
/* the grid list (6B:8882's format: a grid position, then screen numbers, $FF ends) and the
 * headers (6B:8954's: camera X page, Y page, X, Y, camera mode, columns, left page, right
 * page, top and bottom pages, the player's X and Y, $45, the player's facing) */
static const uint8_t GRID[4] = {0x12, BORROW_A, BORROW_A + 1, 0xFF};
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

void secret_reset(void)
{
    s.prev = 0;
    s.code = 0;
    s.open = false;
    s.anim = 0;
    s.have_orig = false;
    s.pending = 0;
    sound_extra_stop();
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
static bool in_hidden(Pce *p)
{
    int c = cam_page(p);
    return p->wram[W_AREA] == AREA && p->wram[W_ROOM] == HOUSE && (c == 2 || c == 3);
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
        memcpy(st + 4, sc, 128);
        memcpy(st + 4 + 128, hi, 16);
        memcpy(st, "RSEC", 4);
    }
    for (int k = 0; k < 2; k++)
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
    memcpy(sc, st + 4, 128);
    memcpy(hi, st + 4 + 128, 16);
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
