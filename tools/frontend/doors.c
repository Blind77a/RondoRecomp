/* See doors.h. */
#include "doors.h"

#include <string.h>

/* the share of the picture's width black from top to bottom (every 2nd column, every 4th
 * line; black: no channel above 8) */
static double black_share(DoorPic p)
{
    int n = 0, dark = 0;
    for (int x = 0; x < p.w; x += 2) {
        bool black = true;
        for (int y = 0; y < p.h && black; y += 4) {
            uint32_t c = p.px[y * PCE_FB_MAX_W + x];
            black = (c >> 16 & 255) <= 8 && (c >> 8 & 255) <= 8 && (c & 255) <= 8;
        }
        dark += black;
        n++;
    }
    return n ? (double)dark / n : 0;
}

static void keep(Doors *d, DoorPic p)
{
    for (int y = 0; y < p.h; y++)
        memcpy(&d->last[y * PCE_FB_MAX_W], &p.px[y * PCE_FB_MAX_W], (size_t)p.w * sizeof *d->last);
    d->last_pic = p;
    d->last_pic.px = d->last;
    d->have_last = true;
}

DoorPic doors_frame(Doors *d, DoorPic pic, bool playing, bool on)
{
    double black = black_share(pic);
    bool same = d->have_last && pic.w == d->last_pic.w && pic.h == d->last_pic.h;
    if (!on || !playing || !same) d->fade = 0;
    else if (!d->fade && d->black - black >= DOORS_APPEAR) d->fade = DOORS_FADE;
    DoorPic show = pic;
    if (d->fade) {
        /* the picture now, up from black (not from the frame before: the camera jumps as the
         * part appears, and a crossfade showed the hero twice) */
        int t = (DOORS_FADE - d->fade + 1) * 256 / (DOORS_FADE + 1);
        for (int y = 0; y < pic.h; y++) {
            const uint32_t *b = &pic.px[y * PCE_FB_MAX_W];
            uint32_t *o = &d->out[y * PCE_FB_MAX_W];
            for (int x = 0; x < pic.w; x++) {
                uint32_t c = b[x];
                o[x] = 0xFF000000u | ((c & 0xFF00FF) * t >> 8 & 0xFF00FF) | ((c & 0xFF00) * t >> 8 & 0xFF00);
            }
        }
        show.px = d->out;
        d->fade--;
    } else {
        keep(d, pic);
    }
    d->black = black;
    d->shown = show;
    return show;
}

bool doors_in_black(const Vdc *v)
{
    DoorPic p = {v->fb, v->fb_w, v->fb_h, v->fb_wide};
    return black_share(p) >= DOORS_BLACK;
}

DoorPic doors_again(const Doors *d, DoorPic pic)
{
    return d->shown.px == d->out ? d->shown : pic;
}
