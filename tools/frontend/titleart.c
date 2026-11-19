/* Title art: see titleart.h. */
#include "titleart.h"

#include <string.h>

/* rows of the title where its prompt is (PUSH ... BUTTON / KEY, which blinks and is worded
 * for the controller or the keyboard): left out of the comparison */
#define PROMPT_Y0 138
#define PROMPT_Y1 157

static int dist2(uint32_t a, uint32_t b)
{
    int d = 0;
    for (int s = 0; s < 24; s += 8) {
        int x = (int)(a >> s & 0xFF) - (int)(b >> s & 0xFF);
        d += x * x;
    }
    return d;
}

static void prepare(TitleArt *t)
{
    t->prepared = true;
    t->ncol = 0;
    for (int y = 0; y < TITLEART_H; y++)
        for (int x = 0; x < TITLEART_W; x++) {
            uint32_t c = t->px[y][x] & 0xFFFFFF;
            bool mid = x >= TITLEART_SIDE && x < TITLEART_SIDE + 256;
            int k = 0;
            while (k < t->ncol && t->col[k] != c) k++;
            if (k == t->ncol) {
                if (mid && t->ncol < TITLEART_COLOURS) t->col[t->ncol++] = c;
                else k = -1;
            }
            t->id[y][x] = (uint8_t)(k < 0 ? 0xFF : k);
        }
    /* side colours not in the reference (and any past the table): the nearest that is */
    for (int y = 0; y < TITLEART_H; y++)
        for (int x = 0; x < TITLEART_W; x++) {
            if (t->id[y][x] != 0xFF) continue;
            int best = 0, bd = 1 << 30;
            for (int k = 0; k < t->ncol; k++) {
                int d = dist2(t->px[y][x], t->col[k]);
                if (d < bd) { bd = d; best = k; }
            }
            t->id[y][x] = (uint8_t)best;
        }
}

bool titleart_compose(TitleArt *t, const uint32_t *raw, const uint32_t *fb, int h, int pitch, int ext, uint32_t *out)
{
    if (!t->loaded || h < 200 || ext <= 0) return t->shown = false;
    if (!t->prepared) prepare(t);
    if (t->ncol == 0) return t->shown = false;
    if (ext > TITLEART_SIDE) ext = TITLEART_SIDE;
    if (h > TITLEART_H) h = TITLEART_H;

    /* what each reference colour has become: the colour the frame has most often where the
     * reference has it (every 2nd pixel each way; a few candidates a colour) */
    enum { CAND = 4 };
    static uint32_t cand[TITLEART_COLOURS][CAND];
    static int count[TITLEART_COLOURS][CAND];
    memset(count, 0, sizeof count);
    int total = 0;
    for (int y = 1; y < h; y += 2) {
        if (y >= PROMPT_Y0 && y < PROMPT_Y1) continue;
        const uint32_t *f = raw + y * pitch;
        const uint8_t *ids = &t->id[y][TITLEART_SIDE];
        for (int x = 1; x < 256; x += 2) {
            int k = ids[x];
            uint32_t c = f[x] & 0xFFFFFF;
            int j = 0, low = 0;
            while (j < CAND && count[k][j] && cand[k][j] != c) {
                if (count[k][j] < count[k][low]) low = j;
                j++;
            }
            if (j == CAND) j = low, count[k][j] = 0;   /* (replaces the rarest) */
            cand[k][j] = c;
            count[k][j]++;
            total++;
        }
    }
    uint32_t map[TITLEART_COLOURS], seen[TITLEART_COLOURS];
    int agree = 0, distinct = 0;
    for (int k = 0; k < t->ncol; k++) {
        int b = 0;
        for (int j = 1; j < CAND; j++)
            if (count[k][j] > count[k][b]) b = j;
        map[k] = count[k][b] ? cand[k][b] : 0;
        agree += count[k][b];
        if (!count[k][b] || !map[k]) continue;
        int q = 0;
        while (q < distinct && seen[q] != map[k]) q++;
        if (q == distinct) seen[distinct++] = map[k];   /* (colours other than black) */
    }
    /* a colour with no samples this frame (one only in the prompt's rows, say): as the
     * nearest colour that has, so it fades with the rest */
    for (int k = 0; k < t->ncol; k++) {
        if (count[k][0] || count[k][1] || count[k][2] || count[k][3]) continue;
        int best = -1, bd = 1 << 30;
        for (int q = 0; q < t->ncol; q++) {
            if (!(count[q][0] || count[q][1] || count[q][2] || count[q][3])) continue;
            int d = dist2(t->col[k], t->col[q]);
            if (d < bd) { bd = d; best = q; }
        }
        map[k] = best >= 0 ? map[best] : 0;
    }
    /* the title: the frame's pixels group as the reference's colours do. Shown once some of
     * its colours are up (not a black screen, which groups any way at all), then kept through
     * a fade to black and through a little more disagreement (sprites such as the lightning) */
    bool title = total && agree * 100 >= total * (t->shown ? 85 : 92);
    if (!t->shown && distinct < 6) title = false;
    t->shown = title;
    if (!title) return false;

    int w = 256 + 2 * ext;
    for (int y = 0; y < h; y++) {
        uint32_t *o = out + y * pitch;
        memcpy(o + ext, fb + y * pitch, 256 * sizeof *o);
        /* the title's first column is a shade darker than the rest (unseen at a screen's edge,
         * a line beside the art): drawn as the next */
        o[ext] = o[ext + 1];
        for (int x = 0; x < ext; x++) {
            o[x] = 0xFF000000u | map[t->id[y][TITLEART_SIDE - ext + x]];
            o[w - 1 - x] = 0xFF000000u | map[t->id[y][TITLEART_SIDE + 256 + ext - 1 - x]];
        }
    }
    return true;
}
