/* -*- mode: c; -*- */

#ifndef WM_DEFS_H
#define WM_DEFS_H

#include <assert.h>

#define WM_ASSERT assert
#define ASSERT WM_ASSERT

#define WM_UNUSED(x) ((void)x)
#define UNUSED WM_UNUSED

#define IGNORE UNUSED

#define WM_DO_CAT(x, y) x ## y
#define WM_CAT(x, y)  WM_DO_CAT(x, y)

#define WM_CAT3(x, y, z)  WM_CAT (WM_CAT(x, y), z)

#define WM_DO_STR(x) #x
#define WM_STR(x)  WM_DO_STR(x)

#endif /* WM_DEFS_H */
