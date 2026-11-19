/* Sound tests.
 *
 *   sound_test               LFO: channel 1 modulates channel 0's pitch between two rates;
 *                            band-limiting: a note has no tones besides its harmonics
 *   sound_test <game.cue>    also: AD_CPLAY streaming from the CD produces exactly the same
 *                            output as AD_PLAY of the same data from ADPCM RAM
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pce/pce.h"

#define MASTER_PER_SAMPLE (21477270.0 / SND_RATE)

/* render in pieces smaller than the output ring */
static int render(Sound *s, uint64_t *t, int16_t *out, int frames)
{
    memset(out, 0, (size_t)frames * 4);
    int done = 0;
    while (done < frames) {
        int n = frames - done < 4096 ? frames - done : 4096;
        double target = (double)(done + n) * MASTER_PER_SAMPLE;
        uint64_t start = *t - (uint64_t)((double)done * MASTER_PER_SAMPLE);
        sound_advance(s, start + (uint64_t)target + 1);
        done += sound_take(s, out + done * 2, n);
        *t = start + (uint64_t)target + 1;
        if (!n) break;
    }
    return done;
}

static int crossings(const int16_t *pcm, int frames)
{
    int n = 0;
    for (int i = 1; i < frames; i++) n += (pcm[i * 2] >= 0) != (pcm[(i - 1) * 2] >= 0);
    return n;
}

static int test_lfo(void)
{
    static Sound s;
    static uint8_t ram[0x10000];
    sound_reset(&s, ram, NULL);
    uint64_t t = 0;
    sound_psg_write(&s, t, 1, 0xFF);                      /* global balance */
    for (int ch = 0; ch < 2; ch++) {
        sound_psg_write(&s, t, 0, (uint8_t)ch);
        sound_psg_write(&s, t, 4, 0x00);                  /* off: write the waveform */
        for (int i = 0; i < 32; i++) sound_psg_write(&s, t, 6, i < 16 ? 0x1F : 0x00);
        sound_psg_write(&s, t, 2, ch == 0 ? 0x00 : 0x00);
        sound_psg_write(&s, t, 3, ch == 0 ? 0x02 : 0x01); /* ch0 period $200, ch1 $100 */
        sound_psg_write(&s, t, 5, 0xFF);
        sound_psg_write(&s, t, 4, 0x9F);                  /* on, full volume */
    }
    sound_psg_write(&s, t, 8, 0xFF);                      /* LFO clocked 255x slower than ch1 */
    sound_psg_write(&s, t, 9, 0x02);                      /* x16: period $200 +/- 240 */

    static int16_t pcm[SND_RATE * 2];
    int frames = render(&s, &t, pcm, SND_RATE / 2);      /* 0.5 s */
    /* LFO cycle ~0.58 s (high half, then low half): compare 0.1 s windows inside each half */
    int a = crossings(pcm + 2 * (int)(0.05 * SND_RATE), (int)(0.1 * SND_RATE));
    int b = crossings(pcm + 2 * (int)(0.38 * SND_RATE), (int)(0.1 * SND_RATE));
    int lo = a < b ? a : b, hi = a < b ? b : a;
    printf("LFO: %d frames, zero crossings per 0.1 s: %d and %d\n", frames, a, b);
    if (lo == 0 || hi < 2 * lo) { printf("FAIL: pitch does not alternate\n"); return 1; }

    /* LFO off: channel 0 alone at a steady pitch, channel 1 audible again */
    sound_psg_write(&s, t, 9, 0x80);
    render(&s, &t, pcm, (int)(0.2 * SND_RATE));
    int c = crossings(pcm, (int)(0.1 * SND_RATE)), d = crossings(pcm + 2 * (int)(0.1 * SND_RATE), (int)(0.1 * SND_RATE));
    printf("LFO off: zero crossings %d and %d\n", c, d);
    if (abs(c - d) > c / 10 + 2) { printf("FAIL: pitch not steady with the LFO off\n"); return 1; }
    return 0;
}

/* in place, n a power of 2 */
static void fft(double *re, double *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j |= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double a = -2 * 3.14159265358979323846 / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; k++) {
                double wr = cos(a * k), wi = sin(a * k);
                double *ur = &re[i + k], *ui = &im[i + k], *vr = &re[i + k + len / 2], *vi = &im[i + k + len / 2];
                double xr = *vr * wr - *vi * wi, xi = *vr * wi + *vi * wr;
                *vr = *ur - xr; *vi = *ui - xi;
                *ur += xr; *ui += xi;
            }
    }
}

/* Band-limiting (sound.c): a note on one of the game's waveforms (the boss music's) has only
 * its harmonics up to 16 kHz, not the tones point sampling folded down from above 22 kHz
 * (11-27 dB under the notes before). Measured with a Blackman-Harris window: the energy away
 * from the note's harmonics against the energy on them. */
static int test_alias(void)
{
    static const uint8_t wave[32] = {15, 27, 23, 17, 23, 27, 21, 15, 23, 30, 26, 17, 21, 23, 15, 6,
                                     15, 24, 15, 7, 9, 13, 4, 0, 7, 15, 9, 3, 7, 13, 7, 3};
    static const int periods[] = {0x38A, 0x097, 0x050};    /* 123, 741, 1398 Hz */
    enum { N = 16384 };
    static Sound s;
    static uint8_t ram[0x10000];
    static int16_t pcm[(N + 4096) * 2];
    static double re[N], im[N];
    int fail = 0;
    for (size_t p = 0; p < sizeof periods / sizeof periods[0]; p++) {
        sound_reset(&s, ram, NULL);
        uint64_t t = 0;
        sound_psg_write(&s, t, 1, 0xFF);
        sound_psg_write(&s, t, 0, 0);
        sound_psg_write(&s, t, 4, 0x40);                  /* off + DDA: the wave index back to 0 */
        sound_psg_write(&s, t, 4, 0x00);
        for (int i = 0; i < 32; i++) sound_psg_write(&s, t, 6, wave[i]);
        sound_psg_write(&s, t, 2, (uint8_t)periods[p]);
        sound_psg_write(&s, t, 3, (uint8_t)(periods[p] >> 8));
        sound_psg_write(&s, t, 5, 0xFF);
        sound_psg_write(&s, t, 4, 0x9F);
        render(&s, &t, pcm, N + 4096);                    /* the first 4096 frames: the note starting */
        for (int i = 0; i < N; i++) {
            double x = 2 * 3.14159265358979323846 * i / N;
            double w = 0.35875 - 0.48829 * cos(x) + 0.14128 * cos(2 * x) - 0.01168 * cos(3 * x);
            re[i] = pcm[(4096 + i) * 2] * w;
            im[i] = 0;
        }
        fft(re, im, N);
        double f = 21477270.0 / 6 / 32 / periods[p], on = 0, off = 0, bin = (double)SND_RATE / N;
        for (int k = 1; k < N / 2; k++) {
            double hz = k * bin;
            if (hz < 20 || hz > 16000) continue;
            double e = re[k] * re[k] + im[k] * im[k], h = hz / f;
            if (fabs(h - floor(h + 0.5)) * f < 6 * bin) on += e;
            else off += e;
        }
        double db = off > 0 && on > 0 ? 10 * log10(off / on) : on > 0 ? -999 : 0;
        printf("band-limiting: %6.1f Hz note: energy off its harmonics %.1f dB\n", f, db);
        if (!(db < -60)) { printf("FAIL: tones that are not the note's (above -60 dB)\n"); fail = 1; }
    }
    return fail;
}

static int test_stream(const char *cue)
{
    static Pce p;
    char err[256];
    if (!pce_init(&p, cue, err, sizeof err) || !pce_boot(&p, err, sizeof err)) { printf("%s\n", err); return 1; }
    int lba = p.cd_base[0] + 237;                         /* the ADPCM block the game loads at boot */
    if (!disc_read_data(&p.disc, lba, 32, p.adpcm)) { printf("cannot read ADPCM sectors\n"); return 1; }

    static int16_t a[SND_RATE * 4], b[SND_RATE * 4];
    int frames = SND_RATE * 2;                            /* 2 s at rate 12 (8 kHz) */
    uint64_t t = 0;
    sound_reset(&p.snd, p.adpcm, &p.disc);
    sound_adpcm_play(&p.snd, t, 0, 0xFFFF, 12, false);
    render(&p.snd, &t, a, frames);
    t = 0;
    sound_reset(&p.snd, p.adpcm, &p.disc);
    sound_adpcm_stream(&p.snd, t, lba, 0xFFFF, 12);
    render(&p.snd, &t, b, frames);
    long diff = 0, energy = 0;
    for (int i = 0; i < frames * 2; i++) { diff += a[i] != b[i]; energy += abs(a[i]); }
    printf("ADPCM stream vs RAM: %d frames, %ld differing samples, mean level %ld\n", frames, diff, energy / (frames * 2));
    if (diff || !energy) { printf("FAIL\n"); return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    int fail = test_lfo();
    fail |= test_alias();
    if (argc > 1) fail |= test_stream(argv[1]);
    printf(fail ? "sound tests FAILED\n" : "sound tests passed\n");
    return fail;
}
