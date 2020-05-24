/* -*- mode: c; -*- */

#include <memory.h>

void *malloc_(size_t n)
{
        void *p = malloc(n);

        if (0 == p) {
                fprintf("malloc failed : %lu bytes\n", n);
                exit(1);
        }

        return p;
}
