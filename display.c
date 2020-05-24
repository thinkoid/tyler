/* -*- mode: c; -*- */

#include <display.h>
#include <malloc-wrapper.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>

struct display
{
        Display *dpy;
        GC gc;
};

static struct display *dpy /* = 0 */;

void make_display(const char *s)
{
        if (0 == dpy) {
                dpy = malloc_(sizeof *dpy);

                if (0 == (dpy->dpy = XOpenDisplay(s))) {
                        fprintf(stderr, "XOpenDisplay(%s) failed\n",
                                s ? s : ":0");
                        exit(1);
                }

                dpy->gc = XCreateGC(DPY, ROOT, 0, 0);
                XSetLineAttributes(DPY, dpy->gc, 1, LineSolid, CapButt, JoinMiter);
        }
}

void free_display(void)
{
        if (dpy) {
                ASSERT(dpy->dpy);
                ASSERT(dpy->gc);

                XFreeGC(DPY, dpy->gc);
                XCloseDisplay(dpy->dpy);

                free(dpy);
                dpy = 0;
        }
}

void release_display(void)
{
        if (dpy) {
                ASSERT(dpy->dpy);
                ASSERT(dpy->gc);

                XFreeGC(DPY, dpy->gc);
                close(ConnectionNumber(dpy->dpy));

                free(dpy);
                dpy = 0;
        }
}

Display *display(void)
{
        ASSERT(dpy && dpy->dpy);
        return dpy->dpy;
}

GC gc(void)
{
        ASSERT(dpy && dpy->gc);
        return dpy->gc;
}

int display_width(void)
{
        ASSERT(dpy && dpy->dpy);
        return DisplayWidth(DPY, SCRN);
}

int display_height(void)
{
        ASSERT(dpy && dpy->dpy);
        return DisplayHeight(DPY, SCRN);
}
