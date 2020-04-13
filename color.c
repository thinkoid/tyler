/* -*- mode: c; -*- */

#include <color.h>
#include <display.h>

#include <X11/Xft/Xft.h>

static XftColor g_colors[6];

void make_colors(const char **pp, size_t n)
{
        size_t i;
        XftColor *p = g_colors;

        ASSERT(pp);
        ASSERT(n == SIZEOF(g_colors));

        for (i = 0; i < SIZEOF(g_colors); ++i) {
                if (!XftColorAllocName(DPY, VISUAL, COLORMAP, pp[i], p + i)) {
                        fprintf(stderr, "color %s allocation failed", pp[i]);
                        exit(1);
                }
        }
}

void free_colors()
{
        size_t i;
        XftColor *p = g_colors;

        for (i = 0; i < SIZEOF(g_colors); ++i) {
                XftColorFree(DPY, VISUAL, COLORMAP, p + i);
        }
}

color_t color(enum color_type type)
{
        ASSERT(0 <= type && type < SIZEOF(g_colors));
        return g_colors[type].pixel;
}
