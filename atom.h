/* -*- mode: c++ -*- */

#ifndef WM_ATOM_H
#define WM_ATOM_H

#include <defs.h>

#include <X11/Xlib.h>

enum atom_sym {
        ATOM_WM_PROTOCOLS,
        ATOM_WM_DELETE_WINDOW,
        ATOM_WM_STATE,
        ATOM_WM_TAKE_FOCUS,
        ATOM_NET_ACTIVE_WINDOW,
        ATOM_NET_SUPPORTED,
        ATOM_NET_WM_STATE,
        ATOM_NET_WM_STATE_FULLSCREEN
};

void make_atoms();

Atom atom(enum atom_sym sym);

Atom* atoms();
size_t atoms_size();

#define WM_PROTOCOLS (atom(ATOM_WM_PROTOCOLS))
#define WM_DELETE_WINDOW (atom(ATOM_WM_DELETE_WINDOW))
#define WM_STATE (atom(ATOM_WM_STATE))
#define WM_TAKE_FOCUS (atom(ATOM_WM_TAKE_FOCUS))
#define NET_ACTIVE_WINDOW (atom(ATOM_NET_ACTIVE_WINDOW))
#define NET_SUPPORTED (atom(ATOM_NET_SUPPORTED))
#define NET_WM_STATE (atom(ATOM_NET_WM_STATE))
#define NET_WM_STATE_FULLSCREEN (atom(ATOM_NET_WM_STATE_FULLSCREEN))

Atom atomic_property(Window win, Atom prop);
const char *atom_name(Atom prop, char *buf, size_t len);

#endif /* WM_ATOM_H */
