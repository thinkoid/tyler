/* -*- mode: c++ -*- */

#ifndef WM_XLIB_H
#define WM_XLIB_H

#include <X11/Xutil.h>

XSizeHints *normal_hints(Window win);
XWMHints *wm_hints(Window win);

#endif /* WM_XLIB_H */
