/* -*- mode: c; -*- */

#ifndef WM_WINDOW_H
#define WM_WINDOW_H

#include <defs.h>
#include <atom.h>
#include <display.h>
#include <xlib.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

int is_transient(Window win);
int is_urgent(Window win);
int is_noinput(Window win);
int is_fullscreen(Window win);

long wm_state(Window);

int is_iconic(Window win);
int is_viewable(Window win);

Window focused_window();

int has_focus(Window win);
void send_focus(Window win);

void reset_focus();
void set_focus(Window win);

void reset_urgent(Window);
void set_urgent(Window);

int has_protocol(Window win, Atom proto);
int has_override_redirect(Window win);

void reset_fullscreen_property(Window win);
void set_fullscreen_property(Window win);

int send(Window, Atom);

void zap_window(Window win);

Window *all_windows();

#endif /* WM_WINDOW_H */
