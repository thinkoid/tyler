/* -*- mode: c; -*- */

#include <malloc-wrapper.h>

#include <stdio.h>
#include <stdlib.h>

void *malloc_(size_t n)
{
        void *p = malloc(n);

        if (0 == p) {
                fprintf(stderr, "malloc failed : %lu bytes\n", n);
                exit(1);
        }

        return p;
}
