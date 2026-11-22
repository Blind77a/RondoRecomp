/* See torchglow.h. */
#include "torchglow.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

/* A wall torch, as the game draws it (Stage 3's dungeon, area $18): a flame of 2 x 2 pixels
 * in pale yellow (9-bit $1FC: R 7, G 7, B 4) on a sconce, whose dark brown ($0B3: R 3, G 2,
 * B 1) shows either side of the flame's lower row. Found by those exact colours, so a fade
 * (other colours) or anything else pale yellow (without the sconce) is left alone. */
#define FLAME 0xFFFFFF91u
#define SCONCE 0xFF6D4824u
#define MAX_TORCHES 16
#define REACH 36      /* the light's largest radius, pixels */

static float clampf(float v) { return v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v; }

/* smooth noise over time: a value every 4 frames, eased between; 0..1 */
static float hash01(uint32_t n)
{
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    n ^= n >> 15;
    return (float)(n & 0xFFFF) / 65535.0f;
}
static float noise(uint32_t seed, uint64_t frame)
{
    uint32_t k = (uint32_t)(frame / 4);
    float t = (float)(frame % 4) / 4.0f;
    t = t * t * (3.0f - 2.0f * t);
    float a = hash01(seed * 7919u + k), b = hash01(seed * 7919u + k + 1);
    return a + (b - a) * t;
}

/* A blue candle's flame, as the game draws it: its tip a white or pink pixel ($1FF / $0FB)
 * with a blue either side ($007 / $00F: 0024B6 / 006DFF) and a blue below; 3 lines down the
 * candle's pale blue-grey body ($1B4: 91B6B6). Its animation moves the tip a pixel. */
#define CANDLE_BODY 0xFF91B6B6u
#define MAX_CANDLES 16
static bool flame_blue(uint32_t p) { return (p >> 16 & 0xFF) == 0 && (p & 0xFF) >= 0xB6; }

int candle_glow(uint32_t *img, int w, int h, int pitch, uint64_t frame, int world_x)
{
    int cx[MAX_CANDLES], cy[MAX_CANDLES], n = 0;
    for (int y = 0; y + 3 < h && n < MAX_CANDLES; y++)
        for (int x = 1; x + 1 < w && n < MAX_CANDLES; x++) {
            uint32_t t = img[y * pitch + x];
            if (t != 0xFFFFFFFFu && t != 0xFFFF6D6Du) continue;
            if (!flame_blue(img[y * pitch + x - 1]) || !flame_blue(img[y * pitch + x + 1]) || !flame_blue(img[(y + 1) * pitch + x]))
                continue;
            if (img[(y + 3) * pitch + x] != CANDLE_BODY) continue;
            cx[n] = x;
            cy[n] = y;
            n++;
        }
    for (int c = 0; c < n; c++) {
        uint32_t seed = (uint32_t)((cx[c] + world_x) >> 2) * 977u + (uint32_t)(cy[c] >> 3) + 5u;
        float f = 0.6f + 0.4f * noise(seed, frame);
        float rad = 13.0f * f;
        int x0 = cx[c] - 14, x1 = cx[c] + 14, y0 = cy[c] - 14, y1 = cy[c] + 14;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > w - 1) x1 = w - 1;
        if (y1 > h - 1) y1 = h - 1;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float dx = (float)(x - cx[c]), dy = (float)(y - cy[c]) * 1.1f, d = sqrtf(dx * dx + dy * dy);
                if (d >= rad) continue;
                float a = (1.0f - d / rad) * (1.0f - d / rad) * 0.8f * f;
                uint32_t p = img[y * pitch + x];
                float r = (float)(p >> 16 & 0xFF) + 60.0f * a, g = (float)(p >> 8 & 0xFF) + 150.0f * a, b = (float)(p & 0xFF) + 255.0f * a;
                img[y * pitch + x] = 0xFF000000u | (uint32_t)clampf(r) << 16 | (uint32_t)clampf(g) << 8 | (uint32_t)clampf(b);
            }
    }
    return n;
}

/* Alcove glow: the same wall torches where an alcove of lit red brick is painted round them
 * (the castle's halls, area $04: dark arches whose bricks are the game's dark reds $240000
 * and $480000; not $482400, which the floor stones use too), often half behind the next column of wall, only the flame's one column
 * showing. Found by a flame pixel with the sconce's brown below one side of it; only those
 * three reds round it are lit (brighter and warmer towards the flame, out to 60 px, the light
 * flickering), so the light keeps to the painted alcove (not the stone round it, nor the
 * HUD's brighter reds). skip_full: torches with their whole flame showing left to Torch glow. */
static bool brick_red(uint32_t p) { return p == 0xFF240000u || p == 0xFF480000u; }

int alcove_glow(uint32_t *img, int w, int h, int pitch, uint64_t frame, int world_x, bool skip_full)
{
    int fx[MAX_TORCHES], fy[MAX_TORCHES], n = 0;
    for (int y = 0; y + 1 < h && n < MAX_TORCHES; y++)
        for (int x = 1; x + 1 < w && n < MAX_TORCHES; x++) {
            if (img[y * pitch + x] != FLAME || img[(y + 1) * pitch + x] != FLAME) continue;
            if (img[(y + 1) * pitch + x - 1] != SCONCE && img[(y + 1) * pitch + x + 1] != SCONCE) continue;
            bool dup = false;
            for (int k = 0; k < n; k++) dup |= x - fx[k] < 4 && fx[k] - x < 4 && y - fy[k] < 4 && fy[k] - y < 4;
            if (dup) continue;
            bool full = x + 2 < w && img[y * pitch + x + 1] == FLAME && img[(y + 1) * pitch + x + 1] == FLAME &&
                        img[(y + 1) * pitch + x - 1] == SCONCE && img[(y + 1) * pitch + x + 2] == SCONCE;
            if (skip_full && full) {
                x += 2;
                continue;
            }
            fx[n] = x;
            fy[n] = y;
            n++;
        }
    for (int t = 0; t < n; t++) {
        float cx = (float)fx[t] + 0.5f, cy = (float)fy[t] + 0.5f;
        uint32_t seed = (uint32_t)((fx[t] + world_x) >> 3) * 131u + (uint32_t)(fy[t] >> 3) + 17u;
        float f = 0.55f + 0.45f * noise(seed, frame);
        int x0 = fx[t] - 61, x1 = fx[t] + 61, y0 = fy[t] - 61, y1 = fy[t] + 61;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > w - 1) x1 = w - 1;
        if (y1 > h - 1) y1 = h - 1;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                uint32_t p = img[y * pitch + x];
                if (!brick_red(p)) continue;
                float dx = (float)x - cx, dy = ((float)y - cy) * 0.9f, d = sqrtf(dx * dx + dy * dy);
                if (d >= 60.0f) continue;
                float a = powf(1.0f - d / 60.0f, 1.2f) * f;
                float r = (float)(p >> 16 & 0xFF) * (1.0f + 2.2f * a) + 40.0f * a;
                float g = (float)(p >> 8 & 0xFF) * (1.0f + 1.5f * a) + 18.0f * a;
                img[y * pitch + x] = 0xFF000000u | (uint32_t)clampf(r) << 16 | (uint32_t)clampf(g) << 8 | (p & 0xFF);
            }
    }
    return n;
}

int torch_glow(const uint32_t *src, uint32_t *dst, int w, int h, int pitch, uint64_t frame, int world_x)
{
    if (src != dst)
        for (int y = 0; y < h; y++) memcpy(&dst[y * pitch], &src[y * pitch], (size_t)w * sizeof *dst);
    int tx[MAX_TORCHES], ty[MAX_TORCHES], n = 0;
    for (int y = 0; y + 1 < h && n < MAX_TORCHES; y++) {
        const uint32_t *r0 = &src[y * pitch], *r1 = &src[(y + 1) * pitch];
        for (int x = 1; x + 2 < w && n < MAX_TORCHES; x++) {
            if (r0[x] != FLAME || r0[x + 1] != FLAME || r1[x] != FLAME || r1[x + 1] != FLAME) continue;
            if (r1[x - 1] != SCONCE || r1[x + 2] != SCONCE) continue;
            tx[n] = x;
            ty[n] = y;
            n++;
            x += 2;
        }
    }
    for (int t = 0; t < n; t++) {
        /* the flame's middle; its flicker, its own by where it stands in the level */
        float cx = (float)tx[t] + 1.0f, cy = (float)ty[t] + 1.0f;
        uint32_t seed = (uint32_t)((tx[t] + world_x) >> 3) * 131u + (uint32_t)(ty[t] >> 3);
        float f = 0.65f + 0.35f * noise(seed, frame);
        float rad = (float)REACH * f, core_rad = 7.0f * f;
        int x0 = (int)(cx - REACH) - 1, x1 = (int)(cx + REACH) + 1;
        int y0 = (int)(cy - REACH) - 1, y1 = (int)(cy + REACH) + 1;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > w - 1) x1 = w - 1;
        if (y1 > h - 1) y1 = h - 1;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float dx = (float)x - cx, dy = ((float)y - cy) * 1.15f;
                float d = sqrtf(dx * dx + dy * dy);
                if (d >= rad) continue;
                /* the wall lit: brighter, warmer, towards the flame; and a small bright core */
                float a = powf(1.0f - d / rad, 1.6f) * f;
                uint32_t p = dst[y * pitch + x];
                float r = (float)(p >> 16 & 0xFF), g = (float)(p >> 8 & 0xFF), b = (float)(p & 0xFF);
                r = r * (1.0f + 2.6f * a) + 70.0f * a;
                g = g * (1.0f + 1.7f * a) + 25.0f * a;
                b = b * (1.0f + 0.6f * a);
                if (d < core_rad) {
                    float c = (1.0f - d / core_rad) * (1.0f - d / core_rad) * 0.6f;
                    r += 255.0f * c;
                    g += 170.0f * c;
                    b += 60.0f * c;
                }
                dst[y * pitch + x] = 0xFF000000u | (uint32_t)clampf(r) << 16 | (uint32_t)clampf(g) << 8 | (uint32_t)clampf(b);
            }
    }
    return n;
}
