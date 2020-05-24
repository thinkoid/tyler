/* -*- mode: c; -*- */

#include <color.h>
#include <display.h>

static XftColor g_colors[SIZEOF_COLORS];

void make_colors(const char **pp, size_t n)
{
        int i;
        XftColor *p = g_colors;

        ASSERT(pp);
        ASSERT(n == SIZEOF_COLORS);

        for (i = 0; i < SIZEOF_COLORS; ++i) {
                if (!XftColorAllocName(DPY, VISUAL, COLORMAP, pp[i], p + i)) {
                        fprintf(stderr, "color %s allocation failed", pp[i]);
                        exit(1);
                }
        }
}

void free_colors(void)
{
        int i;
        XftColor *p = g_colors;

        for (i = 0; i < SIZEOF_COLORS; ++i) {
                XftColorFree(DPY, VISUAL, COLORMAP, p + i);
        }
}

unsigned long color(enum color_type type)
{
        ASSERT(0 <= type && type < SIZEOF_COLORS);
        return g_colors[type].pixel;
}

XftColor *xftcolor(enum color_type type)
{
        ASSERT(0 <= type && type < SIZEOF_COLORS);
        return g_colors + type;
}
