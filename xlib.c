/* -*- mode: c++ -*- */

#include <xlib.h>
#include <display.h>

#include <stdlib.h>
#include <string.h>

XSizeHints *normal_hints(Window win)
{
        long ignore;

        XSizeHints *hints = malloc(sizeof(XSizeHints));
        memset(hints, 0, sizeof *hints);

        if (!XGetWMNormalHints(DPY, win, hints, &ignore))
                hints->flags = PSize;

        return hints;
}

XWMHints *wm_hints(Window win)
{
        return XGetWMHints(DPY, win);
}
