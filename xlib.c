/* -*- mode: c; -*- */

#include <display.h>
#include <malloc-wrapper.h>
#include <xlib.h>

#include <stdlib.h>
#include <string.h>

void fill_size_hints_defaults(XSizeHints *hints)
{
        /*
        * ICCCM 4.1.2.3:
        * The min_width and min_height elements specify the minimum size that
        * the window can be for the client to be useful. The max_width and
        * max_height elements specify the maximum size. The base_width and
        * base_height elements in conjunction with width_inc and height_inc
        * define an arithmetic progression of preferred window widths and
        * heights for nonnegative integers i and j:
        *
        * width  = base_width  + (i * width_inc)
        * height = base_height + (j * height_inc)
        *
        * Window managers are encouraged to use i and j instead of width and
        * height in reporting window sizes to users. If a base size is not
        * provided, the minimum size is to be used in its place and vice
        * versa.
        */

        switch (hints->flags & (PBaseSize | PMinSize)) {
        case 0:
                /* TODO: what now? */
                break;

        case PMinSize:
                hints->base_width = hints->min_width;
                hints->base_height = hints->min_height;
                break;

        case PBaseSize:
                hints->min_width = hints->base_width;
                hints->min_height = hints->base_height;
                break;

        default:
                break;
        }
}

XSizeHints *get_size_hints(Window win, XSizeHints *hints)
{
        long ignore;
        XSizeHints *p = hints;

        if (0 == p)
                p = malloc_(sizeof *p);

        memset(p, 0, sizeof *p);

        if (!XGetWMNormalHints(DPY, win, p, &ignore))
                p->flags = PSize;

        return p;
}

XWMHints *wm_hints(Window win)
{
        return XGetWMHints(DPY, win);
}
