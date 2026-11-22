/* See torchglow.h. */
#include "torchglow.h"

#include <math.h>
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

int torch_glow(const uint32_t *src, uint32_t *dst, int w, int h, int pitch, uint64_t frame, int world_x)
{
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
