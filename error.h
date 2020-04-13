/* -*- mode: c; -*- */

#ifndef WM_ERROR_H
#define WM_ERROR_H

#include <defs.h>

#include <X11/Xlib.h>

typedef int (*error_handler_type)(Display *, XErrorEvent *);

void init_error_handling();

void pause_error_handling();
void resume_error_handling();

#endif /* WM_ERROR_H */
