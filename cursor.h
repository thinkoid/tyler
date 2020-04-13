/* -*- mode: c; -*- */

#ifndef WM_CURSOR_H
#define WM_CURSOR_H

#include <defs.h>

#include <X11/Xlib.h>

typedef Cursor cursor_t;

enum cursor_type { CURSOR_NORMAL, CURSOR_RESIZE, CURSOR_MOVE };

cursor_t cursor(enum cursor_type type);

void make_cursors(const int *p, size_t n);
void free_cursors();

#define NORMAL_CURSOR (cursor(CURSOR_NORMAL))
#define RESIZE_CURSOR (cursor(CURSOR_RESIZE))
#define MOVE_CURSOR (cursor(CURSOR_MOVE))

#endif /* WM_CURSOR_H */
