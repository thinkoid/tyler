/* -*- mode: c; -*- */

#include <display.h>

#include <unistd.h>
#include <X11/Xlib.h>

Display *g_display = 0;

Display *make_display(const char *s)
{
        return g_display = XOpenDisplay(s);
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
        };
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

void display_geometry(ext_t *ext)
{
        ext->w = display_width();
        ext->h = display_height();
}
