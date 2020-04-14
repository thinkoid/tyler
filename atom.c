/* -*- mode: c++ -*- */

#include <atom.h>
#include <display.h>

#include <stdlib.h>
#include <string.h>

#include <X11/Xatom.h>

static Atom g_atoms[8];

void make_atoms()
{
        size_t i;

        static const char *names[] = {
                /* clang-format off */
                "WM_PROTOCOLS",
                "WM_DELETE_WINDOW",
                "WM_STATE",
                "WM_TAKE_FOCUS",
                "_NET_ACTIVE_WINDOW",
                "_NET_SUPPORTED",
                "_NET_WM_STATE",
                "_NET_WM_STATE_FULLSCREEN"
                /* clang-format on */
        };

        ASSERT(SIZEOF(g_atoms) == SIZEOF(names));

        for (i = 0; i < SIZEOF(g_atoms); ++i)
                g_atoms[i] = XInternAtom(DPY, names[i], 0);
}

Atom atom(enum atom_sym sym)
{
        ASSERT(0 <= sym && sym <= SIZEOF(g_atoms));
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
                pbuf = malloc(n + 1);
        }

        strcpy(pbuf, s);
        XFree(s);

        return pbuf;
}
