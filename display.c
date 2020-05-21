/* -*- mode: c; -*- */

#include <display.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>

Display *g_display = 0;

void make_display(const char *s)
{
        if (0 == g_display) {
                if (0 == (g_display = XOpenDisplay(s))) {
                        fprintf(stderr, "failed to open display %s\n", s);
                        exit(1);
                }
        }
}

void free_display(void)
{
        if (g_display) {
                XCloseDisplay(g_display);
                g_display = 0;
        }
}

void release_display(void)
{
        if (g_display) {
                close(ConnectionNumber(g_display));
                g_display = 0;
        }
}

Display *display(void)
{
        return g_display;
}

int display_width(void)
{
        return DisplayWidth(DPY, SCRN);
}

int display_height(void)
{
        return DisplayHeight(DPY, SCRN);
}
