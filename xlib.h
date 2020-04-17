/* -*- mode: c++ -*- */

#ifndef WM_XLIB_H
#define WM_XLIB_H

#include <X11/Xutil.h>

XSizeHints *size_hints(Window win, XSizeHints *hints);
void fill_size_hints_defaults(XSizeHints *hints);

XWMHints *wm_hints(Window win);

#endif /* WM_XLIB_H */
