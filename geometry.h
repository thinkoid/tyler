/* -*- mode: c; -*- */

#ifndef WM_GEOMETRY_H
#define WM_GEOMETRY_H

#include <defs.h>

struct ext {
        int w, h;
};

struct rect {
        int x, y, w, h;
};

struct geom {
        struct rect r;
        int bw;
};

#endif /* WM_GEOMETRY_H */
