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

void free_display()
{
        if (g_display) {
                XCloseDisplay(g_display);
                g_display = 0;
        }
}

void release_display()
{
        if (g_display) {
                close(ConnectionNumber(g_display));
                g_display = 0;
        }
}

Display *display()
{
        return g_display;
}

int display_width()
{
        return DisplayWidth(DPY, SCRN);
}

int display_height()
{
        return DisplayHeight(DPY, SCRN);
}
