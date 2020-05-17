/* -*- mode: c; -*- */

#include <draw.h>
#include <font.h>
#include <window.h>

typedef struct draw_surface {
        Drawable drw;
        GC gc;
        XftDraw *xftdrw;
} draw_surface_t;

draw_surface_t *make_draw_surface(int width, int height)
{
        draw_surface_t *p = malloc(sizeof *p);
        memset(p, 0, sizeof *p);

        p->drw = XCreatePixmap(
                DPY, ROOT, width, height, DefaultDepth(DPY, SCRN));

        if (0 == p->drw) {
                fprintf(stderr, "XCreatePixmap(%d, %d) failed", width, height);
                goto bottom;
        }

        p->gc = XCreateGC(DPY, ROOT, 0, 0);
        XSetLineAttributes(DPY, p->gc, 1, LineSolid, CapButt, JoinMiter);

        p->xftdrw = XftDrawCreate(DPY, p->drw, VISUAL, COLORMAP);

        if (0 == p->xftdrw) {
                fprintf(stderr, "XftDrawCreate failed");
                goto top;
        }

        return p;

top:
        XFreeGC(DPY, p->gc);
        XFreePixmap(DPY, p->drw);

bottom:
        free(p);

        return 0;
}

void free_draw_surface(draw_surface_t *surf)
{
        XftDrawDestroy(surf->xftdrw);
        XFreeGC(DPY, surf->gc);
        XFreePixmap(DPY, surf->drw);

        free(surf);
}

void fill(draw_surface_t *surf, const rect_t *r, unsigned long bg)
{
        XSetForeground(DPY, surf->gc, bg);
        XFillRectangle(DPY, surf->drw, surf->gc, r->x, r->y, r->w, r->h);
}

void draw_text(draw_surface_t *surf, const char *s, int x, XftColor *fg)
{
        int a, d, h, y;
        rect_t g = { 0 };

        if (0 == s || 0 == s[0])
                return;

        geometry_of(surf->drw, &g);
        ASSERT (g.x <= x && x < g.x + g.w);

        a = FNT->ascent;
        d = FNT->descent;
        h = a + d;

        y = g.y + (g.h / 2) - (h / 2) + a;

        XftDrawStringUtf8(surf->xftdrw, fg, FNT, x, y,
                          (XftChar8*)s, strlen(s));
}

void draw_rect(draw_surface_t *surf, const rect_t *r, XftColor *fg, int fill) {
        rect_t g = { 0 };
        geometry_of(surf->drw, &g);

        XSetForeground(DPY, surf->gc, fg->pixel);

        if (fill)
                XFillRectangle(DPY, surf->drw, surf->gc, r->x, r->y, r->w, r->h);
        else
                XDrawRectangle(DPY, surf->drw, surf->gc, r->x, r->y, r->w, r->h);
}
