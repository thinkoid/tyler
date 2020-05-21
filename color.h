/* -*- mode: c; -*- */

#ifndef WM_COLOR_H
#define WM_COLOR_H

#include <defs.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

enum color_type {
        COLOR_NORMAL_BORDER,
        COLOR_NORMAL_BACKGROUND,
        COLOR_NORMAL_FOREGROUND,
        COLOR_SELECT_BORDER,
        COLOR_SELECT_BACKGROUND,
        COLOR_SELECT_FOREGROUND,
};

unsigned long color(enum color_type type);
XftColor *xftcolor(enum color_type type);

/* clang-format off */
#define NORMAL_BORDER  (color(COLOR_NORMAL_BORDER))
#define NORMAL_BG      (color(COLOR_NORMAL_BACKGROUND))
#define NORMAL_FG      (color(COLOR_NORMAL_FOREGROUND))
#define SELECT_BORDER  (color(COLOR_SELECT_BORDER))
#define SELECT_BG      (color(COLOR_SELECT_BACKGROUND))
#define SELECT_FG      (color(COLOR_SELECT_FOREGROUND))

#define XFT_NORMAL_BORDER  (xftcolor(COLOR_NORMAL_BORDER))
#define XFT_NORMAL_BG      (xftcolor(COLOR_NORMAL_BACKGROUND))
#define XFT_NORMAL_FG      (xftcolor(COLOR_NORMAL_FOREGROUND))
#define XFT_SELECT_BORDER  (xftcolor(COLOR_SELECT_BORDER))
#define XFT_SELECT_BG      (xftcolor(COLOR_SELECT_BACKGROUND))
#define XFT_SELECT_FG      (xftcolor(COLOR_SELECT_FOREGROUND))
/* clang-format on */

void make_colors(const char **pp, size_t n);
void free_colors(void);

#endif /* WM_COLOR_H */
