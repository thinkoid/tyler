/* -*- mode: c; -*- */

#ifndef WM_WINDOW_H
#define WM_WINDOW_H

#include <defs.h>
#include <atom.h>
#include <display.h>
#include <xlib.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

Window transient_for_property(Window win);

int has_protocol(Window win, Atom proto);

void send_focus(Window win);

void reset_focus_property(void);
void set_focus_property(Window win);

void reset_urgent(Window);
void set_urgent(Window);

int has_fullscreen_property(Window win);

void reset_fullscreen_property(Window win);
void set_fullscreen_property(Window win);

void set_default_window_border(Window win);
void set_select_window_border(Window win);

void select_window(Window win);
void unselect_window(Window win);

int send(Window, Atom);

void zap_window(Window win);

Window *all_windows(Window *pbuf, size_t *plen);

void pause_propagate(Window win, long mask);
void resume_propagate(Window win, long mask);

char *text_property(Window win, Atom atom, char *buf, size_t len);

#endif /* WM_WINDOW_H */
