/* -*- mode: c; -*- */

#ifndef WM_DISPLAY_H
#define WM_DISPLAY_H

#include <defs.h>
#include <geometry.h>

#include <X11/Xlib.h>

void make_display(const char *s);

void free_display();
void release_display();

Display *display();

#define DPY (display())

#define SCRN (DefaultScreen(DPY))
#define ROOT (RootWindow(DPY, SCRN))

#define VISUAL (DefaultVisual(DPY, SCRN))
#define COLORMAP (DefaultColormap(DPY, SCRN))

int display_width();
int display_height();

#endif /* WM_DISPLAY_H */
