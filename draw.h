/* -*- mode: c; -*- */

#ifndef WM_DRAWABLE_H
#define WM_DRAWABLE_H

#include <defs.h>
#include <display.h>
#include <geometry.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

typedef struct draw_surface draw_surface_t;

draw_surface_t *make_draw_surface(int width, int height);
draw_surface_t *draw_surface();

void free_draw_surface(draw_surface_t *surf);

void fill(draw_surface_t * surf, const rect_t *r, XftColor *bg);

void draw_text(draw_surface_t *surf, const char *s, int x, XftColor *fg);
void draw_rect(draw_surface_t *surf, const rect_t *r, XftColor *fg, int fill);

void copy(draw_surface_t *surf,
          Drawable drw, int x, int y, int w, int h,
          int xto, int yto);

#endif /* WM_DRAWABLE_H */
