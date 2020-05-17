/* -*- mode: c; -*- */

#ifndef WM_DRAWABLE_H
#define WM_DRAWABLE_H

#include <defs.h>
#include <display.h>
#include <geometry.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

typedef struct draw_state draw_state_t;

draw_state_t *make_draw_state(int width, int height);
void free_draw_state(draw_state_t *state);

void fill(draw_state_t * state, const rect_t *r, unsigned long bg);

void draw_text(draw_state_t *state, const char *s, int x, XftColor *fg);
void draw_rect(draw_state_t *state, const rect_t *r, XftColor *fg, int fill);

#endif /* WM_DRAWABLE_H */
