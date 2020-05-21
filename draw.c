/* -*- mode: c; -*- */

#include <draw.h>
#include <font.h>
#include <window.h>

struct draw_surface {
        Drawable drw;
        GC gc;
        XftDraw *xftdrw;
};

struct draw_surface *make_draw_surface(int width, int height)
{
        struct draw_surface *p = malloc(sizeof *p);
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

void free_draw_surface(struct draw_surface *surf)
{
        XftDrawDestroy(surf->xftdrw);
        XFreeGC(DPY, surf->gc);
        XFreePixmap(DPY, surf->drw);

        free(surf);
}

void fill(struct draw_surface *surf, const struct rect *r, XftColor *bg)
{
        XSetForeground(DPY, surf->gc, bg->pixel);
        XFillRectangle(DPY, surf->drw, surf->gc, r->x, r->y, r->w, r->h);
}

void draw_text(struct draw_surface *surf, const char *s, int x, XftColor *fg)
{
        int a, d, h, y;
        struct rect g = { 0 };

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

void draw_rect(struct draw_surface *surf, const struct rect *r, XftColor *fg, int fill) {
        struct rect g = { 0 };
        geometry_of(surf->drw, &g);

        XSetForeground(DPY, surf->gc, fg->pixel);

        if (fill)
                XFillRectangle(DPY, surf->drw, surf->gc, r->x, r->y, r->w, r->h);
        else
                XDrawRectangle(DPY, surf->drw, surf->gc, r->x, r->y, r->w, r->h);
}

void copy(struct draw_surface *surf,
          Drawable drw, int x, int y, int w, int h,
          int xto, int yto)
{
        XCopyArea(DPY, surf->drw, drw, surf->gc, x, y, w, h, xto, yto);
        XSync(DPY, 0);
}

