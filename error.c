/* -*- mode: c; -*- */

#include <error.h>
#include <display.h>

#include <stdio.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/Xproto.h>

static error_handler_type other_error_handler = 0, error_handler = 0;

static int null_error_handler(Display *dpy, XErrorEvent *ee)
{
        UNUSED(dpy);
        UNUSED(ee);
        return 0;
}

static int aborting_error_handler(Display *dpy, XErrorEvent *ee)
{
        UNUSED(dpy);
        UNUSED(ee);
        fprintf(stderr, "fatal : another window manager is running\n");
        exit(1);
}

static int default_error_handler(Display *dpy, XErrorEvent *ee)
{
        UNUSED(dpy);

#define REQ(x) ((x)->request_code)
#define ERR(x) ((x)->error_code)

        /* clang-format off */
        if ( ERR(ee) == BadWindow ||
            (REQ(ee) == X_SetInputFocus     && ERR(ee) == BadMatch)    ||
            (REQ(ee) == X_PolyText8         && ERR(ee) == BadDrawable) ||
            (REQ(ee) == X_PolyFillRectangle && ERR(ee) == BadDrawable) ||
            (REQ(ee) == X_PolySegment       && ERR(ee) == BadDrawable) ||
            (REQ(ee) == X_ConfigureWindow   && ERR(ee) == BadMatch)    ||
            (REQ(ee) == X_GrabButton        && ERR(ee) == BadAccess)   ||
            (REQ(ee) == X_GrabKey           && ERR(ee) == BadAccess)   ||
            (REQ(ee) == X_CopyArea          && ERR(ee) == BadDrawable))
                return 0;
        /* clang-format on */

        fprintf(stderr, "window manager fatal error : request = %d, error = %d\n",
                REQ(ee), REQ(ee));

#undef REQ
#undef ERR

        return 1;
}

static int error_handler_dispatch(Display *dpy, XErrorEvent *ee)
{
        UNUSED(dpy);
        UNUSED(ee);

        ASSERT(other_error_handler);
        ASSERT(error_handler);

        if ((error_handler && 0 == error_handler(dpy, ee)) || other_error_handler)
                return other_error_handler(dpy, ee);

        return 0;
}

void init_error_handling()
{
        ASSERT(0 == error_handler);
        ASSERT(0 == other_error_handler);

        /*
         * Hook the top error-handler and save the Xlib "other" error handling
         * routine:
         */
        other_error_handler = XSetErrorHandler(aborting_error_handler);

        /*
         * ... SubstructureRedirectMask can only be set once for the root
         * window, by a WM:
         */

        XSelectInput(DPY, ROOT, SubstructureRedirectMask);
        XSync(DPY, 0);

        error_handler = default_error_handler;
        XSetErrorHandler(error_handler_dispatch);

        XSync(DPY, 0);
}

void pause_error_handling()
{
        XSetErrorHandler(null_error_handler);
}

void resume_error_handling()
{
        XSetErrorHandler(error_handler_dispatch);
}
