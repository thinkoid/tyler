/* -*- mode: c; -*- */

#include <atom.h>
#include <display.h>
#include <malloc-wrapper.h>

#include <stdlib.h>
#include <string.h>

#include <X11/Xatom.h>

static Atom g_atoms[SIZEOF_ATOMS];

void make_atoms(void)
{
        static const char *names[] = {
                /* clang-format off */
                "WM_PROTOCOLS",
                "WM_DELETE_WINDOW",
                "WM_STATE",
                "WM_TAKE_FOCUS",
                "_NET_SUPPORTED",
                "_NET_ACTIVE_WINDOW",
                "_NET_CLIENT_LIST",
                "_NET_WM_STATE",
                "_NET_WM_NAME",
                "_NET_WM_STATE_FULLSCREEN"
                /* clang-format on */
        };

        int i;
        ASSERT(SIZEOF_ATOMS == SIZEOF(names));

        for (i = 0; i < SIZEOF_ATOMS; ++i)
                g_atoms[i] = XInternAtom(DPY, names[i], 0);

        XChangeProperty(DPY, ROOT, NET_SUPPORTED, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)(g_atoms + ATOM_NET_SUPPORTED),
                        SIZEOF_ATOMS - ATOM_NET_SUPPORTED);
}

Atom atom(enum atom_sym sym)
{
        ASSERT(0 <= sym && sym <= SIZEOF_ATOMS);
        return g_atoms[sym];
}

Atom atomic_property(Window win, Atom prop)
{
        int i;
        Atom a, result = None;
        unsigned long l;
        unsigned char *p;

        if (XGetWindowProperty(DPY, win, prop, 0L, sizeof(Atom), 0, XA_ATOM, &a,
                               &i, &l, &l, &p) &&
            p) {
                result = *(Atom *)p;
                XFree(p);
        }

        return result;
}

const char *atom_name(Atom prop, char *buf, size_t len)
{
        char *s, *pbuf = buf;
        size_t n;

        if (0 == (s = XGetAtomName(DPY, prop))) {
                return 0;
        }

        n = strlen(s);
        if (n > len - 1) {
                pbuf = malloc_(n + 1);
        }

        strcpy(pbuf, s);
        XFree(s);

        return pbuf;
}
