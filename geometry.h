/* -*- mode: c; -*- */

#ifndef WM_GEOMETRY_H
#define WM_GEOMETRY_H

#include <defs.h>

typedef struct ext {
        int w, h;
} ext_t;

typedef struct rect {
        int x, y, w, h;
} rect_t;

typedef struct geom {
        rect_t r;
        int bw;
} geom_t;

#endif /* WM_GEOMETRY_H */
