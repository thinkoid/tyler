/* -*- mode: c; -*- */

#include <defs.h>
#include <display.h>

int main ()
{
        Display* dpy;

        dpy = DPY;
        ASSERT (0 == dpy);

        dpy = make_display(0);
        ASSERT (dpy);

        atexit(&free_display);

        return 0;
}
