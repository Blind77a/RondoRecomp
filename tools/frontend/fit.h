/* Windows that fit the screen, for Setup and the game: on a small screen (an arcade cabinet's
 * 4:3 one at 800 x 600, a laptop's at 1366 x 768) a window opened at its usual size would
 * leave part of itself, Setup's buttons for one, off the screen. */
#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

/* The shape of the screen a window is about to open on (width / height): the primary
 * display's desktop mode; 0 if it is not known. Below 1.4: a 4:3 or 5:4 screen. */
static inline float rondo_screen_aspect(void)
{
    const SDL_DisplayMode *m = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    return m && m->h > 0 ? (float)m->w / (float)m->h : 0.0f;
}

/* Sizes the window `win` for a picture w x h: `most` times it at most, smaller (the same
 * shape) until the window and its frame fit on its screen's usable area, the taskbar left
 * out, with a little room around it; then centers it there. False (nothing changed) when
 * even `least` times the picture does not fit: on that screen, better full screen. Best
 * called on a hidden window, whose frame Windows already knows (elsewhere it is guessed). */
static inline bool rondo_fit_window(SDL_Window *win, int w, int h, float most, float least)
{
    SDL_DisplayID d = SDL_GetDisplayForWindow(win);
    SDL_Rect area;
    if (!d) d = SDL_GetPrimaryDisplay();
    if (w <= 0 || h <= 0 || !d || !SDL_GetDisplayUsableBounds(d, &area)) {
        SDL_SetWindowSize(win, (int)((float)w * most), (int)((float)h * most));
        return true;   /* nothing to go by: its usual size */
    }
    int top = 0, left = 0, bottom = 0, right = 0;
    if (!SDL_GetWindowBordersSize(win, &top, &left, &bottom, &right) || top <= 0) {
        float s = SDL_GetDisplayContentScale(d);
        if (s <= 0) s = 1;
        top = (int)(40 * s);   /* a title bar, and a frame */
        left = right = bottom = (int)(8 * s);
    }
    float room_w = (float)(area.w - left - right), room_h = (float)(area.h - top - bottom);
    float k = SDL_min(room_w / (float)w, room_h / (float)h);
    if (k < least) return false;
    float k_snug = SDL_min(k * 0.95f, most);   /* a little room around it when there is some */
    k = k_snug >= least ? k_snug : SDL_min(k, most);
    int ww = (int)((float)w * k), wh = (int)((float)h * k);
    SDL_SetWindowSize(win, ww, wh);
    SDL_SetWindowPosition(win, area.x + left + ((int)room_w - ww) / 2, area.y + top + ((int)room_h - wh) / 2);
    return true;
}
