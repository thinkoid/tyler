/* -*- mode: c; -*- */

#include <cursor.h>
#include <display.h>

#include <X11/Xlib.h>

static cursor_t g_cursors[SIZEOF_CURSORS] = { 0 };

cursor_t cursor(enum cursor_type type)
{
        ASSERT(0 <= type && type < SIZEOF_CURSORS);
        return g_cursors[type];
}

void make_cursors(const int *p, size_t n)
{
        int i;

        ASSERT(p);
        ASSERT(n == SIZEOF_CURSORS);

        for (i = 0; i < SIZEOF_CURSORS; ++i) {
                g_cursors[i] = XCreateFontCursor(DPY, p[i]);
        }
}

void free_cursors(void)
{
        int i;
        for (i = 0; i < SIZEOF_CURSORS; ++i)
                XFreeCursor(DPY, g_cursors[i]);
}
