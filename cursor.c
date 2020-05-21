/* -*- mode: c; -*- */

#include <cursor.h>
#include <display.h>

#include <X11/Xlib.h>

static cursor_t g_cursors[3] = { 0 };

cursor_t cursor(enum cursor_type type)
{
        ASSERT(0 <= type && type < SIZEOF(g_cursors));
        return g_cursors[type];
}

void make_cursors(const int *p, size_t n)
{
        size_t i;

        ASSERT(p);
        ASSERT(n == SIZEOF(g_cursors));

        for (i = 0; i < SIZEOF(g_cursors); ++i) {
                g_cursors[i] = XCreateFontCursor(DPY, p[i]);
        }
}

void free_cursors(void)
{
        size_t i;

        for (i = 0; i < SIZEOF(g_cursors); ++i) {
                XFreeCursor(DPY, g_cursors[i]);
        }
}
