/* -*- mode: c; -*- */

#include <defs.h>
#include <atom.h>
#include <color.h>
#include <config.h>
#include <cursor.h>
#include <display.h>
#include <draw.h>
#include <error.h>
#include <font.h>
#include <window.h>
#include <xlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#if defined(XINERAMA)
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */

/* clang-format off */
#define BUTTONMASK (ButtonPressMask | ButtonReleaseMask)
#define POINTERMASK (PointerMotionMask | BUTTONMASK)

#define MODKEY Mod1Mask

#define LOCKMASK (g_numlockmask | LockMask)
#define ALLMODMASK (Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask)

#define CLEANMASK(x) ((x) & ~LOCKMASK & (ShiftMask | ControlMask | ALLMODMASK))

#define GEOM(c) (&c->state[c->current_state].g)

#define  WIDTH(c) (GEOM(c)->r.w + 2 * GEOM(c)->bw)
#define HEIGHT(c) (GEOM(c)->r.h + 2 * GEOM(c)->bw)

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define ROOTMASK (0                             \
        | SubstructureRedirectMask              \
        | SubstructureNotifyMask                \
        | StructureNotifyMask                   \
        | ButtonPressMask                       \
        | PointerMotionMask                     \
        | PropertyChangeMask)

#define CLIENTMASK (0                           \
        | EnterWindowMask                       \
        | FocusChangeMask                       \
        | PropertyChangeMask)
/* clang-format on */

/**********************************************************************/

static int clampi(int x, int lower, int upper)
{
    return MIN(upper, MAX(x, lower));
}

/**********************************************************************/

#if defined(XINERAMA)
typedef XineramaScreenInfo xi_t;
#endif /* XINERAMA */

typedef int (*qsort_cmp_t)(const void *, const void *);

static int g_running = 1;
static unsigned g_numlockmask /* = 0 */;

typedef struct state {
        geom_t g;
        unsigned fixed : 1, floating : 1, fullscreen : 1, transient : 1, urgent : 1,
                noinput : 1;
        unsigned tags;
} state_t;

typedef struct aspect {
        float min, max;
} aspect_t;

typedef struct size_hints {
        aspect_t aspect;
        ext_t base, inc, max, min;
} size_hints_t;

typedef struct screen screen_t;
typedef struct client client_t;

typedef struct client {
        Window win;

        size_hints_t size_hints;

        state_t state[2];
        int current_state;

        /* List link to next client, next client in focus stack */
        client_t *next, *focus_next;
        screen_t *screen;
} client_t;

struct screen {
        rect_t r;

        Window bar;
        int showbar, bh;

        unsigned tags;

        int master_size;
        float master_ratio;

        /* Client list, stack list, current client shortcut */
        client_t *head, *focus_head, *current;

        /* Next screen in screen list */
        screen_t *next;
};

static draw_surface_t *drw /* = 0 */;
#define DRW drw

static screen_t *screen_head /* = 0 */, *current_screen /* = 0 */;

/**********************************************************************/

static state_t *state_of(client_t *c)
{
        return &c->state[c->current_state];
}

#define IS_TRAIT_DEF(x)                                 \
        static int WM_CAT(is_, x)(client_t *c) {        \
                return state_of(c)->x;                  \
        }

IS_TRAIT_DEF(floating)
IS_TRAIT_DEF(fullscreen)
IS_TRAIT_DEF(transient)
IS_TRAIT_DEF(urgent)

#undef IS_TRAIT_DEF

static int is_fft(client_t *c)
{
        state_t *state = state_of(c);
        return !!(0
                  | state->floating
                  | state->fullscreen
                  | state->transient);
}

static int is_tile(client_t *c)
{
        return !is_fft(c);
}

static int is_tileable(client_t *c)
{
        return !is_fullscreen(c) && !is_transient(c);
}

static int is_resizeable(client_t *c)
{
        return !is_fullscreen(c) && !is_transient(c);
}

static int is_visible(client_t *c)
{
        return !!(state_of(c)->tags & c->screen->tags);
}

static int is_visible_tile(client_t *c)
{
        return is_visible(c) && is_tile(c);
}

static void get_tiles_geometries(screen_t *s, rect_t *rs, size_t n)
{
        int x, y, w, h, left, dist;

        rect_t r = s->r;

        if (s->showbar) {
                r.y  = s->bh;
                r.h -= s->bh;
        }

        switch (n) {
        case 1:
                memcpy(rs, &r, sizeof *rs);
        case 0:
                break;

        default:
                x = r.x;
                y = r.y;
                w = r.w;
                h = r.h;

                left = (int)(w * s->master_ratio);

                rs[0].x = x;
                rs[0].y = y;
                rs[0].w = left;
                rs[0].h = h;

                ++rs;

                x += left;
                w -= left;

                --n;
                ASSERT(0 < n && n < (size_t)h);

                for (dist = 0; 0 < h; y += dist, h -= dist, ++rs) {
                        dist = (double)h / n--;
                        rs[0].x = x;
                        rs[0].y = y;
                        rs[0].w = w;
                        rs[0].h = dist;
                }
                break;
        }
}

static size_t count_visible_clients(screen_t *s)
{
        client_t *c;
        size_t n;

        for (n = 0, c = s->head; c; c = c->next)
                if (is_visible(c))
                        ++n;

        return n;
}

static size_t count_visible_tiles(screen_t *s)
{
        client_t *c;
        size_t n;

        for (n = 0, c = s->head; c; c = c->next)
                if (is_visible_tile(c)) ++n;

        return n;
}

static void move_resize_client(client_t *c, rect_t *r)
{
        geom_t *g = &state_of(c)->g;

        if (r && r != &g->r)
                g->r = *r;

        XMoveResizeWindow(DPY, c->win,
                          g->r.x,
                          g->r.y,
                          g->r.w - 2 * g->bw,
                          g->r.h - 2 * g->bw);
}

static int text_width(const char *text, XftFont* font) {
        XGlyphInfo x;
        XftTextExtentsUtf8(DPY, font, (XftChar8*)text, strlen(text), &x);
        return x.xOff;
}

static int drawtag(screen_t *s, const char *tag,
                   int x, int selected, int filled,
                   XftColor *fg, XftColor *bg)
{
        const int w = text_width(tag, FNT);

        const int a = FNT->ascent;
        const int d = FNT->descent;

        const int h = a + d;

        /*
         *     +---------- client window indicator
         *     |      +--- tag
         *     |      |
         *     v      v
         *  +-----+-------+
         *  |  □  |  /|   |
         *  |     |   |   |
         *  +-----+-------+
         *  | <-> | <---> |
         *    h/2     w
         */
        rect_t r = { 0 };

        r.x = x;
        r.y = s->r.y;
        r.w = h + w;
        r.h = s->bh;

        fill(DRW, &r, bg);

        if (selected) {
                rect_t q = { 0, 0, 5, 5 };

                q.x = r.x + 3;
                q.y = r.y + 3;

                draw_rect(DRW, &q, fg, filled);
        }

        draw_text(DRW, tag, x += h/2, fg);

        return x + w + h /2;
}

static int drawtags(screen_t *s)
{
        int x = s->r.x;

        size_t i = 0;
        char tag[2] = { 0 };

        client_t *c = current_screen->current;

        const int cct = c ? state_of(c)->tags : 0;
        unsigned occ = 0, urg = 0;

        for (c = s->head; c; c = c->next) {
                state_t *state = state_of(c);

                occ |= state->tags;

                if (state->urgent)
                        urg |= state->tags;
        }

        for (i = 0; i < 9; ++i) {
                int tagged = s->tags & (1U << i);

                XftColor *fg = tagged ? XFT_SELECT_FG : XFT_NORMAL_FG;
                XftColor *bg = tagged ? XFT_SELECT_BG : XFT_NORMAL_BG;

                if (urg & (1U << i)) {
                        XftColor *tmp = fg; fg = bg; bg = tmp;
                }

                tag[0] = i + 1 + '0';

                x = drawtag(s, tag, x,
                            occ & (1U << i),  /* selected */
                            cct & (1U << i),  /* filled rectangle */
                            fg, bg);
        }

        return drawtag(s, "[]=", x, 0, 0, XFT_NORMAL_FG, XFT_NORMAL_BG);
}

static char *title_of(Window win, char *buf, size_t len)
{
        char *pbuf = 0;

        if (0 == (pbuf = text_property(win, NET_WM_NAME, buf, len)))
                pbuf = text_property(win, XA_WM_NAME, buf, len);

        return pbuf;
}

static int drawstatus(screen_t *s, int left)
{
        int w;
        rect_t r;

        char buf[512] = { 0 }, *pbuf = buf;
        size_t n = sizeof buf;

        if (0 == (pbuf = title_of(ROOT, buf, n)))
                strcpy((pbuf = buf), "tyler");

        w = text_width(pbuf, FNT);

        r.x = s->r.x + s->r.w - w;
        r.y = 0;
        r.w = w;
        r.h = s->bh;

        if (r.x < left) {
                r.w -= left - r.x; r.x = left;

                n = strlen(pbuf);
                for (; n && r.w < text_width(pbuf, FNT); pbuf[--n] = 0)
                        ;

                if (0 == n)
                        return left;
        }

        fill(DRW, &r, XFT_NORMAL_BG);
        draw_text(DRW, pbuf, r.x, XFT_NORMAL_FG);

        if (pbuf != buf)
                free(pbuf);

        return r.x;
}

static void drawtitle(screen_t *s, int left, int right)
{
        client_t *c;

        if (0 == (c = s->current))
                return;

        rect_t r;

        char buf[512], *pbuf = buf;
        size_t n = sizeof buf;

        if (0 == (pbuf = title_of(c->win, buf, n)))
                strcpy((pbuf = buf), "(null)");

        r.x = left;
        r.y = 0;
        r.w = right - left;
        r.h = s->bh;

        fill(DRW, &r, s == current_screen ? XFT_SELECT_BG : XFT_NORMAL_BG);

        n = strlen(pbuf);
        for (; n && r.w < text_width(pbuf, FNT); pbuf[--n] = 0)
                ;

        if (n)
                draw_text(DRW, pbuf, left, XFT_NORMAL_FG);
}

static void drawbar(screen_t *s)
{
        int left;

        rect_t r;

        r.x = s->r.x;
        r.y = 0;
        r.w = s->r.w;
        r.h = s->bh;

        fill(DRW, &r, XFT_NORMAL_BG);

        left = drawtags(s);
        drawtitle(s, left, drawstatus(s, left));

        copy(DRW, s->bar, s->r.x, 0, s->r.w, s->bh, 0, 0);
}

static void drawbars()
{
        screen_t *s = screen_head;
        for (drawbar(s); s; drawbar(s), s = s->next)
                ;
}

static void tile(screen_t *s)
{
        rect_t rs[64], *prs = rs, *r;

        client_t *c;
        size_t n;

        drawbar(s);

        for (n = 0, c = s->head; c; c = c->next)
                if (is_visible_tile(c))
                        ++n;

        if (n > SIZEOF(rs)) {
                prs = malloc(n * sizeof *prs);
        }

        get_tiles_geometries(s, prs, n);
        r = prs;

        for (c = s->head; c; c = c->next)
                if (is_visible_tile(c))
                        move_resize_client(c, r++);

        if (prs != rs)
                free(prs);
}

/**********************************************************************/

#if defined(XINERAMA)

static int xi_greater_x_then_y(const xi_t **a, const xi_t **b)
{
        xi_t const *lhs = *a, *rhs = *b;
        return lhs->x_org > rhs->x_org ||
               (lhs->x_org == rhs->x_org && lhs->y_org > rhs->y_org);
}

static int xi_greater_screen_number(const xi_t **a, const xi_t **b)
{
        xi_t const *lhs = *a, *rhs = *b;
        return lhs->screen_number > rhs->screen_number;
}

static int xi_eq(const xi_t *lhs, const xi_t *rhs)
{
        return lhs->x_org == rhs->x_org && lhs->y_org == rhs->y_org;
}

static void unique_xinerama_geometries(xi_t ***pptr, xi_t **pend)
{
        xi_t **p = *pptr;

        for (; p != pend; ++p)
                if (*pptr != p && !xi_eq(**pptr, *p))
                        if (++*pptr != p)
                                **pptr = *p;

        ++*pptr;
}

static rect_t *get_xinerama_screen_geometries(rect_t *rs, size_t *len)
{
        qsort_cmp_t by_x_then_y = (qsort_cmp_t)xi_greater_x_then_y;
        qsort_cmp_t by_screen_number = (qsort_cmp_t)xi_greater_screen_number;

        rect_t *prs = rs;
        int i, n;

        xi_t *xis, *pxis[64], **ppxis = pxis, **p = ppxis;

        if ((xis = XineramaQueryScreens(DPY, &n))) {
                if ((size_t)n > SIZEOF(pxis)) {
                        ppxis = malloc(n * sizeof *ppxis);
                }

                for (i = 0; i < n; ++i)
                        ppxis[i] = xis + i;

                /*
                 * Sort the geometries by x, then y if x is same:
                 */
                qsort(ppxis, n, sizeof *ppxis, by_x_then_y);

                /*
                 * Eliminate duplicate geometries:
                 */
                unique_xinerama_geometries(&p, ppxis + n);
                n = p - ppxis;

                /*
                 * Sort back by the screen number:
                 */
                qsort(ppxis, n, sizeof *ppxis, by_screen_number);

                if ((size_t)n > *len) {
                        prs = malloc(n * sizeof *prs);
                }

                *len = n;

                for (i = 0; i < n; ++i) {
                        prs[i].x = ppxis[i]->x_org;
                        prs[i].y = ppxis[i]->y_org;
                        prs[i].w = ppxis[i]->width;
                        prs[i].h = ppxis[i]->height;
                }

                XFree(xis);

                if (ppxis != pxis)
                        free(ppxis);

                return prs;
        }

        return 0;
}

#endif /* XINERAMA */

static rect_t *get_all_screens_geometries(rect_t *buf, size_t *buflen)
{
#if defined(XINERAMA)
        if (XineramaIsActive(DPY)) {
                return get_xinerama_screen_geometries(buf, buflen);
        } else
#endif /* XINERAMA */
        {
                buf->x = 0;
                buf->y = 0;

                buf->w = display_width();
                buf->h = display_height();

                buflen[0] = 1;

                return buf;
        }
}

static Window make_bar(int x, int y, int w, int h)
{
        Window win;
        XClassHint hints = { "tyler", "tyler" };

        XSetWindowAttributes wa = { 0 };

        wa.override_redirect = True;
        wa.background_pixmap = ParentRelative;
        wa.event_mask = ExposureMask;

        win = XCreateWindow(
                DPY, ROOT, x, y, w, h, 0, DefaultDepth(DPY, SCRN),
                CopyFromParent, DefaultVisual(DPY, SCRN),
                CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);

        XDefineCursor(DPY, win, NORMAL_CURSOR);
        XMapRaised(DPY, win);
        XSetClassHint(DPY, win, &hints);

        return win;
}

static screen_t *make_screen(const rect_t *r)
{
        screen_t *s = malloc(sizeof *s);
        memset(s, 0, sizeof *s);

        s->r.x = r->x;
        s->r.y = r->y;
        s->r.w = r->w;
        s->r.h = r->h;

        s->tags = 1;

        s->showbar = config_showbar();
        s->bh = FNT->height;

        s->bar = make_bar(r->x, r->y, r->w, s->bh);

        s->master_size = config_master_size();
        s->master_ratio = config_master_ratio();

        s->head = s->focus_head = s->current = 0;
        s->next = 0;

        return s;
}

/**********************************************************************/

static client_t *client_for(Window win)
{
        client_t *c;
        screen_t *s;

        for (s = screen_head; s; s = s->next)
                for (c = s->head; c; c = c->next)
                        if (win == c->win)
                                return c;

        return 0;
}

static int get_root_pointer_coordinates(int *x, int *y)
{
        int i;
        unsigned u;
        Window w;

        return XQueryPointer(DPY, ROOT, &w, &w, x, y, &i, &i, &u);
}

static int test_point_in(rect_t *r, int x, int y)
{
        return r->x <= x && x < r->x + r->w && r->y <= y && y < r->y + r->h;
}

static screen_t *screen_at(int x, int y)
{
        screen_t *s;

        if (test_point_in (&current_screen->r, x, y))
                return current_screen;

        for (s = screen_head; s && !test_point_in(&s->r, x, y); s = s->next)
                ;

        return s;
}

static screen_t *screen_for(Window win)
{
        int x, y;
        client_t *c;

        if (ROOT == win && get_root_pointer_coordinates(&x, &y))
                return screen_at(x, y);

        if ((c = client_for(win)))
                return c->screen;

        return current_screen;
}

static int is_managed(Window win)
{
        return 0 != client_for(win);
}

static client_t *transient_client_for(Window win)
{
        Window other;
        return (other = transient_for_property(win)) ? client_for(other) : 0;
}

static void send_configure_notify(client_t *c)
{
        state_t *state;

        XConfigureEvent x;

        x.type = ConfigureNotify;
        x.display = DPY;

        x.event = c->win;
        x.window = c->win;

        state = state_of(c);

        x.x = state->g.r.x;
        x.y = state->g.r.y;
        x.width = state->g.r.w;
        x.height = state->g.r.h;

        x.border_width = state->g.bw;

        x.above = None;
        x.override_redirect = False;

        XSendEvent(DPY, c->win, False, StructureNotifyMask, (XEvent *)&x);
}

static void update_client_size_hints(client_t *c)
{
        size_hints_t *h = &c->size_hints;

        XSizeHints x = { 0 };
        get_size_hints(c->win, &x);

        fill_size_hints_defaults(&x);

        if (x.flags & PAspect) {
                h->aspect.min = x.min_aspect.x
                        ? (float)x.min_aspect.y / x.min_aspect.x : 0;
                h->aspect.max = x.max_aspect.y
                        ? (float)x.max_aspect.x / x.max_aspect.y : 0;
        }

        h->base.w = x.base_width;
        h->base.h = x.base_height;

        h->min.w = x.min_width;
        h->min.h = x.min_height;

        if (x.flags & PResizeInc) {
                h->inc.w = x.width_inc;
                h->inc.h = x.height_inc;
        }

        if (x.flags & PMaxSize) {
                h->max.w = x.max_width;
                h->max.h = x.max_height;
        }

        state_of(c)->fixed = h->max.w && h->max.h && h->max.w == h->min.w &&
                h->max.h == h->min.h;
}

static void update_client_wm_hints(client_t *c)
{
        state_t *state;
        XWMHints *hints;

        if ((hints = wm_hints(c->win))) {
                state = state_of(c);

                state->urgent = !!(hints->flags & XUrgencyHint);
                state->noinput = ((hints->flags & InputHint) && !hints->input);

                XFree(hints);
        }
}

static client_t *make_client(Window win, XWindowAttributes *attr)
{
        client_t *transient;

        state_t *state;
        geom_t *geom;

        client_t *c = malloc(sizeof(client_t));
        memset(c, 0, sizeof *c);

        c->win = win;

        state = state_of(c);
        geom = &state->g;

        geom->r.x = attr->x;
        geom->r.y = attr->y;
        geom->r.w = attr->width;
        geom->r.h = attr->height;

        geom->bw = config_border_width();

        /*
         * Set window border width, color, and send client geometry information:
         */
        set_default_window_border(win);
        send_configure_notify(c);

        /*
         * Transient property and the borrowing of tags from its parent should
         * take place only once, upon initialization:
         */
        if ((transient = transient_client_for(win))) {
                c->screen = transient->screen;
                state->tags = transient->state[transient->current_state].tags;
                state->transient = 1;
        } else {
                c->screen = current_screen;
                state->tags = current_screen->tags;
        }

        update_client_size_hints(c);
        update_client_wm_hints(c);

        /* TODO : resize if fullscreen */
        state->fullscreen = has_fullscreen_property(c->win);
        state->floating = state->transient || state->fixed;

        return c;
}

static void grab_keys(Window win);
static void grab_buttons(Window win, int focus);

static client_t *first_visible_client(client_t *p, client_t *pend)
{
        for (; p && p != pend && !is_visible(p); p = p->next)
                ;
        return p;
}

static client_t *first_visible_tile(client_t *p, client_t *pend)
{
        for (; p && p != pend && !is_visible_tile(p); p = p->next)
                ;
        return p;
}

static client_t *last_visible_client(client_t *p, client_t *pend)
{
        client_t *c = 0;

        for (; p && p != pend; p = p->next)
                if (is_visible(p))
                        c = p;

        return c;
}

static client_t *last_focused_client(screen_t *s)
{
        client_t *c = 0;
        for (c = s->focus_head; c && !is_visible(c); c = c->focus_next) ;

        if (c && (c = c->focus_next))
                for (; c && !is_visible(c); c = c->focus_next) ;

        return c;
}

static void unmap(int visible)
{
        client_t *c;

        for (c = current_screen->head; c; c = c->next)
                if (visible == (is_visible(c)))
                        XUnmapWindow(DPY, c->win);
}

static void unmap_visible() { return unmap(1); }
static void unmap_invisible() { return unmap(0); }

static void map_visible()
{
        client_t *c;

        for (c = current_screen->head; c; c = c->next)
                if (is_visible(c))
                        XMapWindow(DPY, c->win);
}

static void push_front(client_t *c)
{
        c->next = c->screen->head;
        c->screen->head = c;
}

static void stack_push_front(client_t *c)
{
        if (0 == c)
                return;

        c->focus_next = c->screen->focus_head;
        c->screen->focus_head = c;

        if (is_visible(c))
                c->screen->current = c;
}

static void push_back(client_t *c)
{
        client_t **pptr;

        if (0 == c)
                return;

        for (pptr = &c->screen->head; *pptr; pptr = &(*pptr)->next)
                ;

        *pptr = c;
}

static void stack_push_back(client_t *c)
{
        client_t **pptr;

        if (0 == c)
                return;

        for (pptr = &c->screen->focus_head; *pptr; pptr = &(*pptr)->focus_next)
                ;

        *pptr = c;
}

static void pop(client_t *c)
{
        client_t **pptr;

        if (0 == c)
                return;

        for (pptr = &c->screen->head; *pptr && *pptr != c; pptr = &(*pptr)->next)
                ;

        ASSERT(*pptr);
        *pptr = c->next;

        c->next = 0;
}

static client_t *stack_top(screen_t *s)
{
        client_t *c;

        if (0 == s)
                return 0;

        for (c = s->focus_head; c && !is_visible(c); c = c->focus_next)
                ;

        return c;
}

static void stack_pop(client_t *c)
{
        client_t **pptr;

        if (0 == c)
                return;

        pptr = &c->screen->focus_head;
        for (; *pptr && *pptr != c; pptr = &(*pptr)->focus_next)
                ;

        ASSERT(*pptr);
        *pptr = c->focus_next;

        if (c == c->screen->current)
                c->screen->current = stack_top(c->screen);

        c->focus_next = 0;
}

static void stack(screen_t *s)
{
        XEvent ignore;
        client_t *c, *cur;

        Window ws[64], *pws = ws;
        size_t n = 1, i = 1, j = 1, k = 1;

        for (c = s->head; c; c = c->next) {
                if (is_visible(c)) {
                        ++n;

                        if (is_fft(c)) {
                                ++k;

                                if (!state_of(c)->fullscreen)
                                        ++j;
                        }
                }
        }

        if (1 == n)
                return;

        if (n > SIZEOF(ws)) {
                pws = malloc(n * sizeof *pws);
        }

        pws[0] = s->bar;

        if ((cur = s->current)) {
                if (is_fft(cur)) {
                        if (cur->state[cur->current_state].fullscreen) {
                                pws[j++] = cur->win;
                        } else {
                                pws[i++] = cur->win;
                        }
                } else {
                        pws[j++] = cur->win;
                        ++k;
                }
        }

        for (c = s->head; c; c = c->next) {
                if (c == cur || !is_visible(c))
                        continue;

                if (is_fft(c)) {
                        if (state_of(c)->fullscreen) {
                                pws[j++] = c->win;
                        } else {
                                pws[i++] = c->win;
                        }
                } else {
                        pws[j++] = c->win;
                }
        }

        if (n)
                XRestackWindows(DPY, pws, n);

        /*
         * An EnterNotify event is also generated when a window is mapped over
         * the current position of the pointer, and a LeaveNotify is generated
         * when a window containing the pointer is unmapped.
         *
         * [..] All EnterNotify and LeaveNotify events caused by a hierarchy
         * change are generated after any hierarchy event (UnmapNotify,
         * MapNotify, ConfigureNotify, GravityNotify, CirculateNotify) caused by
         * that change.
         *
         * To prevent a restack-enter-focus loop we consume all EnterNotify
         * events here:
         */
        XSync(DPY, 0);
        while (XCheckMaskEvent(DPY, EnterWindowMask, &ignore));

        if (pws != ws)
                free(pws);
}

static void set_client_focus(client_t *c)
{
        ASSERT(c);

        if (!state_of(c)->noinput)
                set_focus_property(c->win);

        send_focus(c->win);
}

static void unfocus(client_t *c)
{
        if (0 == c)
                return;

        set_default_window_border(c->win);
        grab_buttons(c->win, 0);
}

static void focus(client_t *c)
{
        screen_t *s, *other;

        if (0 == c) {
                unfocus(current_screen->current);
                reset_focus_property();
                return;
        }

        s = c->screen;

        if (s != current_screen) {
                unfocus(current_screen->current);

                other = current_screen;
                current_screen = s;

                drawbar(other);
        }

        if (c != current_screen->current) {
                unfocus(current_screen->current);

                stack_pop(c);
                stack_push_front(c);
        }

        set_select_window_border(c->win);
        set_client_focus(c);

        grab_buttons(c->win, 1);

        if (is_tile(c))
                tile(current_screen);

        stack(current_screen);

        drawbar(current_screen);
}

static void update_client_list()
{
        screen_t *s;
        client_t *c;

        long buf[512], *pbuf = buf;
        size_t i = 0, n = sizeof buf / sizeof *buf;

        for (s = screen_head; s; s = s->next) {
                for (c = s->head; c; c = c->next) {
                        if (i >= n) {
                                long *p = pbuf;

                                pbuf = malloc(2 * n * sizeof *pbuf);
                                memcpy(pbuf, p, n * sizeof *pbuf);

                                n *= 2;

                                if (p != buf)
                                        free(p);
                        }

                        pbuf[i++] = c->win;
                }
        }

        if (i)
                XChangeProperty(DPY, ROOT, NET_CLIENT_LIST, XA_WINDOW, 32,
                                PropModeReplace, (unsigned char *)pbuf, i);

        if (pbuf != buf)
                free(pbuf);
}

static void unmanage(client_t *c)
{
        pop(c);
        stack_pop(c);

        update_client_list();

        free(c);
}

static client_t *manage(Window win, XWindowAttributes *attr)
{
        screen_t *s;

        client_t *c = make_client(win, attr);
        ASSERT(c);

        s = c->screen;
        ASSERT(s);

        push_front(c);
        stack_push_back(c);

        update_client_list();

        XSelectInput(DPY, c->win, CLIENTMASK);
        XMapWindow(DPY, c->win);

        focus(c);

        return c;
}

/**********************************************************************/

static void make_screens()
{
        screen_t **pptr = &screen_head;

        rect_t rs[16], *prs = rs;
        size_t i, n = SIZEOF(rs);

        prs = get_all_screens_geometries(rs, &n);
        ASSERT(prs);

        for (i = 0; i < n; ++i) {
                *pptr = make_screen(prs + i);
                pptr = &(*pptr)->next;
        }

        ASSERT(screen_head);
        current_screen = screen_head;

        if (prs != rs)
                free(prs);

        ASSERT(0 == DRW);
        DRW = make_draw_surface(display_width(), FNT->height);
}

static void free_screens()
{
        screen_t *s = screen_head, *snext;
        client_t *c, *cnext;

        for (; s; snext = s->next, free(s), s = snext) {
                if (!send(s->bar, WM_DELETE_WINDOW))
                        zap_window(s->bar);

                c = s->head;
                for (; c; cnext = c->next, free(c), c = cnext)
                        if (!send(c->win, WM_DELETE_WINDOW))
                                zap_window(c->win);
        }
}

static int update_screen(screen_t *s, rect_t *r)
{
        if (memcmp(r, &s->r, sizeof *r)) {
                memcpy(&s->r, r, sizeof *r);

                XMoveResizeWindow(DPY, s->bar, s->r.x, s->r.y, s->r.w, s->bh);

                tile(s);
                stack(s);

                if (s == current_screen)
                        focus(s->current);

                return 1;
        }

        return 0;
}

static void remap_quiet()
{
        pause_propagate(ROOT, SubstructureNotifyMask);

        map_visible();
        unmap_invisible();

        resume_propagate(ROOT, ROOTMASK);
}

static int update_screens()
{
        int dirty = 0;

        screen_t **pps = &screen_head, *ptmp, *ps = *pps, *pscur = 0;
        client_t *c;

        rect_t rs[16], *prs = rs;
        size_t i, n = SIZEOF(rs);

        prs = get_all_screens_geometries(rs, &n);

        ASSERT(prs);
        ASSERT(n);

        for (i = 0; i < n && *pps; ++i, pps = &(*pps)->next) {
                if (current_screen == (ps = *pps))
                        /*
                         * Save the current screen if found:
                         */
                        pscur = ps;

                dirty = update_screen(ps, prs + i);
        }

        for (; i < n; ++i, pps = &(*pps)->next) {
                /*
                 * If there are more geometries than screens, create one screen
                 * for each remaining ones:
                 */
                *pps = make_screen(prs + i);
        }

        if (*pps) {
                dirty = 1;

                if (0 == pscur)
                        /*
                         * If current screen has not been found it means it
                         * is one of the lost screens. Transfer all clients to
                         * the last valid screen:
                         */
                        pscur = ps;

                ps = *pps;
                *pps = 0;

                for (; ps; ptmp = ps->next, free(ps), ps = ptmp) {
                        unfocus(ps->current);

                        for (c = ps->head; c; c = c->next)
                                c->screen = pscur;

                        push_back(ps->head);
                        stack_push_back(ps->focus_head);
                }
        }

        current_screen = pscur;
        ASSERT(current_screen);

        remap_quiet();

        if (prs != rs)
                free(prs);

        return dirty;
}

/**********************************************************************/

static unsigned numlockmask()
{
        int i, j, n;
        unsigned mask = 0;

        XModifierKeymap *modmap;
        KeyCode *keycode, numlock;

        modmap = XGetModifierMapping(DPY);
        n = modmap->max_keypermod;

        keycode = modmap->modifiermap;
        numlock = XKeysymToKeycode(DPY, XK_Num_Lock);

        for (i = 0; i < 8; ++i)
                for (j = 0; j < n; ++j, ++keycode)
                        if (keycode[0] == numlock)
                                mask = (1U << i);

        return XFreeModifiermap(modmap), mask;
}

static void update_numlockmask()
{
        g_numlockmask = numlockmask();
}

/**********************************************************************/

static void sigchld_handler(int ignore)
{
        UNUSED(ignore);

        if (SIG_ERR == signal(SIGCHLD, sigchld_handler)) {
                fprintf(stderr, "wm : failed to install SIGCHLD handler\n");
                exit(1);
        }

        while (0 < waitpid(-1, 0, WNOHANG))
                ;
}

static void setup_sigchld()
{
        sigchld_handler(0);
}

/**********************************************************************/

static int grab_pointer(cursor_t cursor)
{
        return GrabSuccess == XGrabPointer(
                DPY, ROOT, False, POINTERMASK, GrabModeAsync, GrabModeAsync,
                None, cursor, CurrentTime);
}

static void ungrab_pointer()
{
        XUngrabPointer(DPY, CurrentTime);
}

static void snap(rect_t *r, int *x, int *y, int w, int h, int snap)
{
        if (abs(r->x - *x) < snap)
                *x = r->x;
        else if (abs((r->x + r->w) - (*x + w)) < snap)
                *x = r->x + r->w - w;

        if (abs(r->y - *y) < snap)
                *y = r->y;
        else if (abs((r->y + r->h) - (*y + h)) < snap)
                *y = r->y + r->h - h;
}

/**********************************************************************/

static int zoom()
{
        client_t *c, *cur;

        if ((cur = current_screen->current) && is_tile(cur)) {
                c = first_visible_tile(current_screen->head, 0);
                ASSERT(c);

                if (c == cur) {
                        c = first_visible_tile(c->next, 0);
                        if (c && c != cur) {
                                pop(c);
                                push_front(c);

                                focus(c);
                        }
                } else {
                        pop(cur);
                        push_front(cur);

                        tile(current_screen);
                        stack(current_screen);
                }
        }

        return 0;
}

static int spawn(char **args)
{
        if (0 == fork()) {
                release_display();

                setsid();
                execvp(args[0], args);

                fprintf(stderr, "tyler: execvp %s", args[0]);
                perror(" failed");

                exit(0);
        }

        return 0;
}

static int spawn_terminal()
{
        return spawn((char **)config_termcmd());
}

static int current_screen_ordinal()
{
        int i = 0;
        screen_t *s = screen_head;

        for (; s && s != current_screen; s = s->next, ++i)
                ;

        return i;
}

static int spawn_program()
{
        char ordinal[16] = "0";

        static const char *cmd[] = {
                "dmenu_run", "-m", 0, "-fn", 0, 0
        };

        sprintf(ordinal, "%d", current_screen_ordinal());
        cmd[2] = ordinal;

        cmd[4] = config_fontname();

        return spawn((char **)cmd);
}

static int toggle_bar()
{
        return 0;
}

static int move_focus_left()
{
        screen_t *s;
        client_t *c, *cur;

        ASSERT(current_screen);
        s = current_screen;

        if ((cur = s->current)) {
                if (0 == (c = last_visible_client(s->head, cur)))
                        c = last_visible_client(cur->next, 0);

                focus(c);
        }

        return 0;
}

static int move_focus_right()
{
        screen_t *s;
        client_t *c, *cur;

        ASSERT(current_screen);
        s = current_screen;

        if ((cur = s->current)) {
                if (0 == (c = first_visible_client(cur->next, 0)))
                        c = first_visible_client(s->head, cur);

                focus(c);
        }

        return 0;
}

static int increment_master()
{
        return 0;
}

static int decrement_master()
{
        return 0;
}

static int shrink_master()
{
        return 0;
}

static int grow_master()
{
        return 0;
}

static int zap()
{
        client_t *cur;

        ASSERT(current_screen);
        cur = current_screen->current;

        if (cur && !send(cur->win, WM_DELETE_WINDOW))
                zap_window(cur->win);

        return 0;
}

static screen_t *next_screen(screen_t *arg)
{
        screen_t *s = arg->next;

        if (0 == s && arg == (s = screen_head))
                s = 0;

        return s;
}

static screen_t *last_screen(screen_t *p, screen_t *pend)
{
        screen_t *s = 0;

        for (; p && p != pend; p = p->next)
                s = p;

        return s;
}

static screen_t *prev_screen(screen_t *arg)
{
        screen_t *s;

        if (0 == (s = last_screen(screen_head, arg)))
                s = last_screen(arg, 0);

        return s;
}

static int focus_prev_screen()
{
        screen_t *s;

        if (0 == (s = prev_screen(current_screen)))
                return 0;

        return focus((current_screen = s)->current), 0;
}

static int focus_next_screen()
{
        screen_t *s;

        if (0 == (s = next_screen(current_screen)))
                return 0;

        return focus((current_screen = s)->current), 0;
}

static int move_other_screen(screen_t *s, client_t *c)
{
        int x, y;
        state_t *state = state_of(c);

        x = state->g.r.x - c->screen->r.x;
        y = state->g.r.y - c->screen->r.y;

        unfocus(c);

        pop(c);
        stack_pop(c);

        x += s->r.x;
        y += s->r.y;

        XMoveWindow(DPY, c->win, x, y);

        focus(current_screen->current);

        c->screen = s;
        state->tags = current_screen->tags;

        push_front(c);
        stack_push_front(c);

        if (is_tile(c))
                tile(s);

        stack(s);

        return 0;
}

static int move_prev_screen()
{
        screen_t *s;
        client_t *c;

        if (0 == (c = current_screen->current) ||
            0 == (s = prev_screen(current_screen)))
                return 0;

        return move_other_screen(s, c);
}

static int move_next_screen()
{
        screen_t *s;
        client_t *c;

        if (0 == (c = current_screen->current) ||
            0 == (s = next_screen(current_screen)))
                return 0;

        return move_other_screen(s, c);
}

static int quit()
{
        return g_running = 0;
}

static int handle_event(XEvent *arg);

static int do_move_client(client_t *c)
{
        int x, y, xorig, yorig;

        XEvent ev;
        Time t = 0;

        state_t *state;
        rect_t *r;

        /* TODO: Warp pointer to window center */
        if (!get_root_pointer_coordinates(&xorig, &yorig))
                return 0;

        state = state_of(c);
        r = &state->g.r;

        x = r->x;
        y = r->y;

        do {
                XMaskEvent(DPY, POINTERMASK | ExposureMask |
                           SubstructureRedirectMask, &ev);

                switch (ev.type) {
                case ConfigureRequest:
                case Expose:
                case MapRequest:
                        handle_event(&ev);
                        break;

                case MotionNotify:
                        if ((ev.xmotion.time - t) <= (1000 / 60))
                                continue;

                        t = ev.xmotion.time;

                        x = r->x + (ev.xmotion.x - xorig);
                        y = r->y + (ev.xmotion.y - yorig);

                        if (!state->floating && (x != r->x || y != r->y)) {
                                state->floating = 1;

                                tile(c->screen);
                                stack(c->screen);
                        }

                        XMoveWindow(DPY, c->win, x, y);
                        break;
                }
        } while (ev.type != ButtonRelease);

        r->x = x;
        r->y = y;

        return 0;
}

static int move_client()
{
        client_t *c;

        if (0 == (c = current_screen->current) || is_fullscreen(c))
                return 0;

        if (grab_pointer(MOVE_CURSOR)) {
                do_move_client(c);
                ungrab_pointer();
        }

        return 0;
}

static int do_resize_client(client_t *c)
{
        int x, y, minw, minh;

        XEvent ev;
        Time t = 0;

        state_t *state;
        rect_t *r;

        state = state_of(c);
        r = &state->g.r;

        minw = c->size_hints.min.w;
        minh = c->size_hints.min.h;

        XWarpPointer(DPY, None, c->win, 0, 0, 0, 0, r->w - 1, r->h - 1);

        do {
                XMaskEvent(DPY, POINTERMASK | ExposureMask |
                           SubstructureRedirectMask, &ev);

                switch (ev.type) {
                case ConfigureRequest:
                case Expose:
                case MapRequest:
                        handle_event(&ev);
                        break;

                case MotionNotify:
                        if ((ev.xmotion.time - t) <= (1000 / 60))
                                continue;

                        t = ev.xmotion.time;

                        x = ev.xmotion.x;
                        y = ev.xmotion.y;

                        if (!state->floating &&
                            (x != r->x + r->w || y != r->y + r->h)) {
                                state->floating = 1;

                                tile(c->screen);
                                stack(c->screen);
                        }

                        if (x - r->x < minw) x = r->x + minw;
                        if (y - r->y < minh) y = r->y + minh;

                        if (x != ev.xmotion.x || y != ev.xmotion.y)
                                XWarpPointer(DPY, None, c->win, 0, 0, 0, 0,
                                             x - r->x - 1, y - r->y - 1);

                        r->w = x - r->x;
                        r->h = y - r->y;

                        XResizeWindow(DPY, c->win, r->w, r->h);
                        break;
                default:
                        break;
                }
        } while (ev.type != ButtonRelease);

        return 0;
}

static int resize_client()
{
        client_t *c;

        if (0 == (c = current_screen->current) || !is_resizeable(c))
                return 0;

        if (grab_pointer(RESIZE_CURSOR)) {
                do_resize_client(c);
                ungrab_pointer();
        }

        return 0;
}

static int do_tag()
{
        remap_quiet();

        focus(stack_top(current_screen));
        drawbar(current_screen);

        return 0;
}

static int tag(unsigned n)
{
        if (n == current_screen->tags)
                return 0;

        current_screen->tags = n;

        return do_tag();
}

#define TAG_DEF(x) static int WM_CAT(tag_, x)() { return tag(1U << (x - 1)); }
TAG_DEF(1)
TAG_DEF(2)
TAG_DEF(3)
TAG_DEF(4)
TAG_DEF(5)
TAG_DEF(6)
TAG_DEF(7)
TAG_DEF(8)
TAG_DEF(9)
#undef TAG_DEF

static int change_tag(unsigned n)
{
        client_t *c;

        if (0 == (c = current_screen->current) || n == current_screen->tags)
                return 0;

        state_of(c)->tags = n;
        do_tag();

        return 0;
}

#define TAG_DEF(x) \
        static int WM_CAT(change_tag_, x)() { return change_tag(1U << (x - 1)); }
TAG_DEF(1)
TAG_DEF(2)
TAG_DEF(3)
TAG_DEF(4)
TAG_DEF(5)
TAG_DEF(6)
TAG_DEF(7)
TAG_DEF(8)
TAG_DEF(9)
#undef TAG_DEF

static int tile_current()
{
        client_t *cur;

        if (0 == (cur = current_screen->current))
                return 0;

        if (is_tileable(cur)) {
                state_t *state = state_of(cur);
                state->floating = state->fullscreen = 0;

                focus(cur);
        }

        return 0;
}

/**********************************************************************/

typedef struct keycmd {
        unsigned mod;
        KeySym keysym;
        int (*fun)();
} keycmd_t;

static keycmd_t g_keycmds[] = {
        /* clang-format off */
        { MODKEY,               XK_Return,  zoom               },
        { MODKEY | ShiftMask,   XK_Return,  spawn_terminal     },
        { MODKEY,               XK_p,       spawn_program      },
        { MODKEY,               XK_b,       toggle_bar         },
        { MODKEY | ShiftMask,   XK_Left,    move_focus_left    },
        { MODKEY | ShiftMask,   XK_Right,   move_focus_right   },
        { MODKEY,               XK_i,       increment_master   },
        { MODKEY,               XK_d,       decrement_master   },
        { MODKEY,               XK_h,       shrink_master      },
        { MODKEY,               XK_l,       grow_master        },
        { MODKEY | ShiftMask,   XK_c,       zap                },
        { MODKEY,               XK_comma,   focus_prev_screen  },
        { MODKEY,               XK_period,  focus_next_screen  },
        { MODKEY | ShiftMask,   XK_comma,   move_prev_screen   },
        { MODKEY | ShiftMask,   XK_period,  move_next_screen   },
        { MODKEY | ShiftMask,   XK_q,       quit               },
        { MODKEY,               XK_t,       tile_current       },

#define TAG_DEF(x)                                                      \
        { MODKEY,             WM_CAT(XK_, x), WM_CAT(tag_, x) },        \
        { MODKEY | ShiftMask, WM_CAT(XK_, x), WM_CAT(change_tag_, x) }

        TAG_DEF(1), TAG_DEF(2), TAG_DEF(3), TAG_DEF(4), TAG_DEF(5),
        TAG_DEF(6), TAG_DEF(7), TAG_DEF(8), TAG_DEF(9)

#undef TAG_DEF

        /* clang-format on */
};

typedef struct ptrcmd {
        unsigned mod, button;
        int (*fun)();
} ptrcmd_t;

static ptrcmd_t g_ptrcmds[] = { { MODKEY, Button1, move_client },
                                { MODKEY, Button3, resize_client } };

/**********************************************************************/

static void exit_fullscreen(client_t *c)
{
        state_t *state;

        ASSERT(1 == c->current_state);
        c->current_state = 0;

        state = state_of(c);

        state->fullscreen = 0;
        reset_fullscreen_property(c->win);

        move_resize_client(c, 0);

        tile(c->screen);
        stack(c->screen);
}

static void enter_fullscreen(client_t *c)
{
        state_t *state = state_of(c);

        ASSERT(0 == c->current_state);
        c->state[(c->current_state = 1)] = *state;

        state = state_of(c);

        state->g.r = c->screen->r;
        state->g.bw = 0;

        state->fullscreen = state->floating = 1;
        set_fullscreen_property(c->win);

        move_resize_client(c, 0);

        tile(c->screen);
        stack(c->screen);
}

static void mark_urgent(client_t *c)
{
        state_of(c)->urgent = 1;
}

/**********************************************************************/

static int do_key_press_handler(KeySym keysym, unsigned mod)
{
        keycmd_t *p = g_keycmds, *pend = g_keycmds + SIZEOF(g_keycmds);

        update_numlockmask();

        for (; p != pend; ++p)
                if (keysym == p->keysym && mod == CLEANMASK(p->mod) && p->fun)
                        return p->fun();

        return 0;
}

static int key_press_handler(XEvent *arg)
{
        XKeyEvent *ev = &arg->xkey;

        KeySym keysym = XKeycodeToKeysym(DPY, ev->keycode, 0);
        unsigned mod = CLEANMASK(ev->state);

        return do_key_press_handler(keysym, mod);
}

static int do_button_press_handler(unsigned button, unsigned mod)
{
        ptrcmd_t *p = g_ptrcmds, *pend = g_ptrcmds + SIZEOF(g_ptrcmds);

        update_numlockmask();

        for (; p != pend; ++p)
                if (button == p->button && mod == CLEANMASK(p->mod) && p->fun)
                        return p->fun();

        return 0;
}

static int button_press_handler(XEvent *arg)
{
        XButtonEvent *ev = &arg->xbutton;
        unsigned mod = CLEANMASK(ev->state);
        return do_button_press_handler(ev->button, CLEANMASK(mod));
}

static int motion_notify_handler(XEvent *arg)
{
        screen_t *s;

        XMotionEvent *ev = &arg->xmotion;
        int x = ev->x_root, y = ev->y_root;

        if (ROOT == ev->window) {
                s = screen_at(x, y);
                ASSERT(s);

                if (s && s != current_screen)
                        focus((current_screen = s)->current);
        }

        return 0;
}

static int enter_notify_handler(XEvent *arg)
{
        XCrossingEvent *ev = &arg->xcrossing;

        if (ev->mode != NotifyNormal || ev->detail == NotifyInferior)
                return 0;

        return focus(client_for(ev->window)), 0;
}

static int focusin_handler(XEvent *arg)
{
        client_t *cur = current_screen->current;

        if (cur && cur->win != arg->xfocus.window)
                focus(cur);

        return 0;
}

static int expose_handler(XEvent *arg)
{
        XExposeEvent *ev = &arg->xexpose;

        if (0 == ev->count)
                drawbar(screen_for(ev->window));

        return 0;
}

static int destroy_notify_handler(XEvent *arg)
{
        client_t *c;

        if ((c = client_for(arg->xdestroywindow.window)))
                unmanage(c);

        focus(current_screen->current);

        return 0;
}

static int unmap_notify_handler(XEvent *arg)
{
        client_t *c;

        if ((c = client_for(arg->xunmap.window)))
                unmanage(c);

        focus(current_screen->current);

        return 0;
}

static int map_request_handler(XEvent *arg)
{
        XMapRequestEvent *ev = &arg->xmaprequest;
        XWindowAttributes attr;

        if (!XGetWindowAttributes(DPY, ev->window, &attr) ||
            attr.override_redirect)
                return 0;

        if (!is_managed(ev->window))
                manage(ev->window, &attr);

        return 0;
}

static int configure_request_handler(XEvent *arg)
{
        client_t *c;
        XConfigureRequestEvent *ev = &arg->xconfigurerequest;

        XWindowChanges wc = {
                ev->x, ev->y,  ev->width, ev->height, ev->border_width,
                ev->above, ev->detail
        };

        if (XConfigureWindow(DPY, ev->window, ev->value_mask, &wc))
                XSync(DPY, 0);

        if ((c = client_for(ev->window)) && is_visible_tile(c))
                tile(c->screen);

        return 0;
}

static int configure_notify_handler(XEvent *arg)
{
        XConfigureEvent *ev = &arg->xconfigure;

        if (ROOT == ev->window) {
                free_draw_surface(DRW);
                DRW = make_draw_surface(ev->width, FNT->height);

                if (update_screens()) {
                        focus(current_screen->current);
                        drawbars();
                }
        }

        return 0;
}

static int property_notify_handler(XEvent *arg)
{
        client_t *c;
        XPropertyEvent *ev = &arg->xproperty;

        if (PropertyDelete == ev->state)
                return 0;

        if (ROOT == ev->window && XA_WM_NAME == ev->atom)
                drawbars();
        else if ((c = client_for(ev->window))) {
                switch(ev->atom) {
                case XA_WM_TRANSIENT_FOR:
                        if (!is_floating(c) && transient_client_for(c->win)) {
                                state_of(c)->floating = 1;

                                tile(c->screen);
                                stack(c->screen);
                        }
                        break;

                case XA_WM_NORMAL_HINTS:
                        update_client_size_hints(c);
                        break;

                case XA_WM_HINTS:
                        update_client_wm_hints(c);
                        break;
                default:
                        break;
                }

                if (XA_WM_NAME == ev->atom || NET_WM_NAME == ev->atom)
                        drawbar(current_screen);
        }

        return 0;
}

static int client_message_handler(XEvent *arg)
{
        XClientMessageEvent *ev = &arg->xclient;

        client_t *c = client_for(ev->window);

        if (0 == c)
                return 0;

        if (NET_WM_STATE == ev->message_type) {
                if (NET_WM_STATE_FULLSCREEN == (Atom)ev->data.l[1] ||
                    NET_WM_STATE_FULLSCREEN == (Atom)ev->data.l[2]) {
                        if ( 0 == ev->data.l[0] ||
                            (2 == ev->data.l[0] && is_fullscreen(c)))
                                exit_fullscreen(c);
                        else
                                enter_fullscreen(c);
                }
        } else if (NET_ACTIVE_WINDOW == ev->message_type) {
                if (c != current_screen->current && !is_urgent(c))
                        mark_urgent(c);
        }

        return 0;
}

static int handle_event(XEvent *arg)
{
        static int (*fun[LASTEvent])(XEvent *) = {
                /* clang-format off */
                0, 0,
                key_press_handler, /* 2 */
                0,
                button_press_handler, /* 4 */
                0,
                motion_notify_handler, /* 6 */
                enter_notify_handler, /* 7 */
                0,
                focusin_handler, /* 9 */
                0, 0,
                expose_handler, /* 12 */
                0, 0, 0, 0,
                destroy_notify_handler, /* 17 */
                unmap_notify_handler,  /* 18 */
                0,
                map_request_handler, /* 20 */
                0,
                configure_notify_handler, /* 22 */
                configure_request_handler, /* 23 */
                0, 0, 0, 0,
                property_notify_handler, /* 28 */
                0, 0, 0, 0,
                client_message_handler, /* 33 */
                0
                /* clang-format on */
        };

        if (fun[arg->type])
                return fun[arg->type](arg);

        return 0;
}

/**********************************************************************/

static void grab_keys(Window win)
{
        size_t i;
        KeyCode keycode;

        update_numlockmask();

        /*
         * Un(passive)grab all keys for this client:
         */
        XUngrabKey(DPY, AnyKey, AnyModifier, win);

/* clang-format off */
#define GRABKEYS(x)                                             \
        XGrabKey(DPY, keycode, g_keycmds[i].mod | x, win, 1,    \
                 GrabModeAsync, GrabModeAsync)
        /* clang-format on */

        for (i = 0; i < SIZEOF(g_keycmds); ++i)
                if ((keycode = XKeysymToKeycode(DPY, g_keycmds[i].keysym))) {
                        /*
                         * (Passive) grab all combos in the map:
                         */
                        GRABKEYS(0);
                        GRABKEYS(LockMask);
                        GRABKEYS(g_numlockmask);
                        GRABKEYS(g_numlockmask | LockMask);
                }
#undef GRABKEYS
}

static void grab_buttons(Window win, int focus)
{
        size_t i;

        update_numlockmask();

        /*
         * Un(passive)grab all buttons for this client:
         */
        XUngrabButton(DPY, AnyButton, AnyModifier, win);

        if (!focus)
                /*
                 * Button `passthrough' if client is not in focus:
                 */
                XGrabButton(DPY, AnyButton, AnyModifier, win, 0, BUTTONMASK,
                            GrabModeAsync, GrabModeAsync, None, None);

/* clang-format off */
#define GRABBUTTONS(x)                                  \
        XGrabButton(DPY,                                \
                    g_ptrcmds[i].button,                \
                    g_ptrcmds[i].mod | x,               \
                    win, 0, BUTTONMASK,                 \
                    GrabModeAsync, GrabModeAsync,       \
                    None, None)
/* clang-format on */

        for (i = 0; i < SIZEOF(g_ptrcmds); i++) {
                GRABBUTTONS(0);
                GRABBUTTONS(LockMask);
                GRABBUTTONS(g_numlockmask);
                GRABBUTTONS(g_numlockmask | LockMask);
        }
}

/**********************************************************************/

static void setup_root_supported_atoms()
{
        Atom *p = netatoms();
        size_t n = netatoms_size();

        XChangeProperty(DPY, ROOT, NET_SUPPORTED, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)p, n);
}

static void setup_root_masks_and_cursor()
{
        XSetWindowAttributes x = { 0 };

        x.event_mask = ROOTMASK;
        x.cursor = NORMAL_CURSOR;

        XChangeWindowAttributes(DPY, ROOT, CWEventMask | CWCursor, &x);
}

static void setup_root()
{
        setup_root_masks_and_cursor();
        setup_root_supported_atoms();

        grab_keys(ROOT);
}

/**********************************************************************/

static void init_display()
{
        make_display(0);
        ASSERT(DPY);
        atexit(free_display);
}

static void init_atoms()
{
        make_atoms();
}

static void init_colors()
{
        make_colors(config_colors(), config_colors_size());
        atexit(free_colors);
}

static void init_cursors()
{
        make_cursors(config_cursors(), config_cursors_size());
        atexit(free_cursors);
}

static void init_font()
{
        make_font(config_fontname());
}

static void init_screens()
{
        make_screens();
        atexit(free_screens);
}

static void init()
{
        setup_sigchld();

        init_display();
        init_atoms();
        init_colors();
        init_cursors();
        init_font();

        init_error_handling();
        setup_root();

        init_screens();
}

static void run()
{
        XEvent ev;

        XSync(DPY, 0);

        for (; g_running && !XNextEvent(DPY, &ev);)
                handle_event(&ev);

        XSync(DPY, 1);
}

int main()
{
        return init(), run(), 0;
}
