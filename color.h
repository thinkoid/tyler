/* -*- mode: c; -*- */

#ifndef WM_COLOR_H
#define WM_COLOR_H

#include <defs.h>
#include <geometry.h>

#include <X11/Xlib.h>

typedef unsigned long color_t;

enum color_type {
        COLOR_NORMAL_BORDER,
        COLOR_NORMAL_BACKGROUND,
        COLOR_NORMAL_FOREGROUND,
        COLOR_SELECT_BORDER,
        COLOR_SELECT_BACKGROUND,
        COLOR_SELECT_FOREGROUND,
};

color_t color(enum color_type type);

#define NORMAL_BORDER (color(COLOR_NORMAL_BORDER))
#define NORMAL_BG (color(COLOR_NORMAL_BACKGROUND))
#define NORMAL_FG (color(COLOR_NORMAL_FOREGROUND))
#define SELECT_BORDER (color(COLOR_SELECT_BORDER))
#define SELECT_BG (color(COLOR_SELECT_BACKGROUND))
#define SELECT_FG (color(COLOR_SELECT_FOREGROUND))

void make_colors(const char **pp, size_t n);
void free_colors();

#endif /* WM_COLOR_H */
