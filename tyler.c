/* -*- mode: c; -*- */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>

#include <xkbcommon/xkbcommon.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

/*
 * Unused as yet; proves the wayland-scanner rigging end to end.
 */
#include <xdg-shell-protocol.h>

#define LISTEN(src, listener, handler)             \
        do {                                       \
                (listener)->notify = (handler);    \
                wl_signal_add((src), (listener));  \
        } while (0)

/*
 * A client's mutable half, double-buffered as in classic: fullscreen
 * (later) flips current_state instead of remembering geometry ad hoc.
 * min/max size hints stay in wlr_xdg_toplevel's own state — fixed and
 * transient are derived, not stored.
 */
struct state {
        struct wlr_box r;               /* border box, layout coordinates */
        unsigned tags;

        unsigned floating   : 1;
        unsigned fullscreen : 1;
        unsigned urgent     : 1;
};

struct client {
        struct wl_list link;            /* in clients, global */
        struct wl_list focus_link;      /* in fstack, global MRU */

        struct screen *screen;          /* NULL is legal: orphan */

        struct state state[2];
        int current_state;

        struct wlr_xdg_toplevel *toplevel;
        struct wlr_scene_tree *scene;
        struct wlr_scene_tree *scene_surface;
        struct wlr_scene_rect *border[4];

        struct wl_listener commit;
        struct wl_listener map;
        struct wl_listener unmap;
        struct wl_listener destroy;
};

struct screen {
        struct wl_list link;            /* in screens */

        struct wlr_output *output;      /* invariant: never NULL */
        struct wlr_scene_output *scene_output;

        struct wlr_box area;            /* layout coordinates, full */
        struct wlr_box warea;           /* minus the bar strip */

        unsigned tags;
        float master_ratio;
        int showbar, bh;

        struct wl_listener frame;
        struct wl_listener request_state;
        struct wl_listener destroy;
};

/*
 * Per-output state that survives the output: written back on death,
 * read on (re)appearance. Keyed by output name; process-lifetime scope
 * is deliberate — hibernation keeps the process alive, and that is the
 * churn case.
 */
struct screen_memory {
        char name[64];
        unsigned tags;
        float master_ratio;
        int showbar;
};

struct popup {
        struct wlr_xdg_popup *popup;

        struct wl_listener commit;
        struct wl_listener destroy;
};

/*
 * One group per source of keyboards: the main one collects every
 * physical device; each virtual keyboard (the vkbd oracle, on-screen
 * boards) gets a group of its own with the same keymap and handlers.
 */
struct keyboard {
        struct wlr_keyboard_group *group;

        /* a held binding repeats compositor-side; any key re-arms */
        const xkb_keysym_t *repeat_syms;
        int repeat_nsyms;
        uint32_t repeat_mods;
        struct wl_event_source *repeat_timer;

        struct wl_listener key;
        struct wl_listener modifiers;
        struct wl_listener destroy;     /* virtual keyboards only */
};

struct key {
        uint32_t mod;
        xkb_keysym_t keysym;
        void (*func)(unsigned);
        unsigned arg;
};

static struct wl_display *display;
static struct wl_event_loop *event_loop;

static struct wlr_session *session;
static struct wlr_backend *backend;
static struct wlr_renderer *renderer;
static struct wlr_allocator *allocator;

static struct wlr_scene *scene;
static struct wlr_scene_output_layout *scene_layout;
static struct wlr_output_layout *output_layout;

static struct wlr_xdg_shell *xdg_shell;

static struct wlr_seat *seat;
static struct keyboard *kb_main;
static struct xkb_keymap *keymap;
static struct wlr_virtual_keyboard_manager_v1 *vkbd_mgr;

static struct wl_list screens;
static struct wl_list clients;
static struct wl_list fstack;

static struct screen *current_screen;

static struct screen_memory screen_memories[8];

static struct wl_listener new_output_listener;
static struct wl_listener layout_change_listener;
static struct wl_listener new_toplevel_listener;
static struct wl_listener new_popup_listener;
static struct wl_listener new_input_listener;
static struct wl_listener new_vkbd_listener;

/* the key table in config.h points at these */
static void zoom(unsigned);
static void spawn_terminal(unsigned);
static void toggle_bar(unsigned);
static void focus_next(unsigned);
static void focus_prev(unsigned);
static void zap(unsigned);
static void focus_prev_screen(unsigned);
static void focus_next_screen(unsigned);
static void move_prev_screen(unsigned);
static void move_next_screen(unsigned);
static void tile_current(unsigned);
static void view_tag(unsigned);
static void change_tag(unsigned);
static void quit(unsigned);

#include "config.h"

static void die(const char *s)
{
        fprintf(stderr, "tyler: %s\n", s);
        exit(1);
}

static struct state *state_of(struct client *c)
{
        return &c->state[c->current_state];
}

static int visible_on(struct client *c, struct screen *s)
{
        return s && c->screen == s && (state_of(c)->tags & s->tags);
}

static int tiled_on(struct client *c, struct screen *s)
{
        return visible_on(c, s) &&
                !state_of(c)->floating && !state_of(c)->fullscreen;
}

/*
 * The focused client is never stored, only computed — a dangling
 * "current" pointer is unrepresentable.
 */
static struct client *current_client(void)
{
        struct client *c;

        wl_list_for_each(c, &fstack, focus_link)
                if (visible_on(c, current_screen))
                        return c;

        return 0;
}

static int client_is_fixed(struct client *c)
{
        struct wlr_xdg_toplevel_state *s = &c->toplevel->current;

        return 0 < s->min_width && 0 < s->min_height &&
                s->min_width == s->max_width &&
                s->min_height == s->max_height;
}

static void set_border_color(struct client *c, const float color[4])
{
        size_t i;

        /* an unmapping client's rects are already gone (focus sees it
         * as the outgoing surface) */
        if (0 == c->border[0])
                return;

        for (i = 0; i < 4; ++i)
                wlr_scene_rect_set_color(c->border[i], color);
}

/**********************************************************************/
/* Screens                                                            */

static struct screen_memory *screen_memory_find(const char *name)
{
        size_t i;

        for (i = 0; i < sizeof screen_memories / sizeof *screen_memories; ++i)
                if (0 == strcmp(screen_memories[i].name, name))
                        return &screen_memories[i];

        return 0;
}

static void screen_memory_save(struct screen *s)
{
        struct screen_memory *m = screen_memory_find(s->output->name);
        size_t i;

        if (0 == m) {
                for (i = 0;
                     i < sizeof screen_memories / sizeof *screen_memories; ++i)
                        if (0 == screen_memories[i].name[0]) {
                                m = &screen_memories[i];
                                break;
                        }
        }

        /* table full: this output degrades to defaults, never crashes */
        if (0 == m)
                return;

        snprintf(m->name, sizeof m->name, "%s", s->output->name);
        m->tags = s->tags;
        m->master_ratio = s->master_ratio;
        m->showbar = s->showbar;
}

static void resize(struct client *, struct wlr_box);

/*
 * Classic's tile: one master column on the left, ratio-split, the rest
 * stacked to the right; every cell inset by margin. A lone client gets
 * the whole work area.
 */
static void tile(struct screen *s)
{
        struct client *c;
        int n = 0, i = 0, left, dist;
        int x, y, w, h;
        int sx, sy, sw, sh, sn;

        wl_list_for_each(c, &clients, link)
                if (tiled_on(c, s))
                        ++n;

        if (0 == n)
                return;

        x = s->warea.x;
        y = s->warea.y;
        w = s->warea.width;
        h = s->warea.height;

        left = 1 == n ? w : (int)(w * s->master_ratio);

        sx = x + left;
        sy = y;
        sw = w - left;
        sh = h;
        sn = n - 1;

        wl_list_for_each(c, &clients, link) {
                if (!tiled_on(c, s))
                        continue;

                if (0 == i++) {
                        resize(c, (struct wlr_box){
                                        x + margin, y + margin,
                                        left - 2 * margin, h - 2 * margin });
                        continue;
                }

                dist = sh / sn--;
                resize(c, (struct wlr_box){
                                sx + margin, sy + margin,
                                sw - 2 * margin, dist - 2 * margin });

                sy += dist;
                sh -= dist;
        }
}

static void arrange(struct screen *s)
{
        struct client *c;

        if (0 == s)
                return;

        wl_list_for_each(c, &clients, link)
                if (c->screen == s)
                        wlr_scene_node_set_enabled(&c->scene->node,
                                                   visible_on(c, s));

        tile(s);
}

static void screen_update_area(struct screen *s)
{
        wlr_output_layout_get_box(output_layout, s->output, &s->area);

        s->warea = s->area;

        if (s->showbar) {
                s->warea.y += s->bh;
                s->warea.height -= s->bh;
        }
}

/*
 * Fires whenever the layout shifts — outputs added, removed, or
 * repositioned. The one place screen geometry is recomputed wholesale.
 */
static void layout_change_handler(struct wl_listener *unused, void *arg)
{
        struct screen *s;

        (void)unused;
        (void)arg;

        wl_list_for_each(s, &screens, link) {
                screen_update_area(s);
                arrange(s);
        }
}

/* neighbors in layout order, wrapping; NULL when s is alone */
static struct screen *screen_after(struct screen *s)
{
        struct wl_list *l = s->link.next;
        struct screen *n;

        if (l == &screens)
                l = screens.next;

        n = wl_container_of(l, n, link);

        return n == s ? 0 : n;
}

static struct screen *screen_before(struct screen *s)
{
        struct wl_list *l = s->link.prev;
        struct screen *n;

        if (l == &screens)
                l = screens.prev;

        n = wl_container_of(l, n, link);

        return n == s ? 0 : n;
}

static void frame_handler(struct wl_listener *listener, void *arg)
{
        struct screen *s = wl_container_of(listener, s, frame);
        struct timespec now;

        (void)arg;

        wlr_scene_output_commit(s->scene_output, 0);

        clock_gettime(CLOCK_MONOTONIC, &now);
        wlr_scene_output_send_frame_done(s->scene_output, &now);
}

static void request_state_handler(struct wl_listener *listener, void *arg)
{
        struct wlr_output_event_request_state *event = arg;

        (void)listener;

        wlr_output_commit_state(event->output, event->state);
}

static void focus(struct client *);

static void output_destroy_handler(struct wl_listener *listener, void *arg)
{
        struct screen *s = wl_container_of(listener, s, destroy);
        struct screen *survivor = 0;
        struct client *c;

        (void)arg;

        screen_memory_save(s);

        wl_list_remove(&s->frame.link);
        wl_list_remove(&s->request_state.link);
        wl_list_remove(&s->destroy.link);
        wl_list_remove(&s->link);

        /*
         * The layout output and the scene-output are addons on the
         * wlr_output; they tear themselves down with it. Only our
         * pointers need dropping.
         */
        s->output->data = 0;

        if (!wl_list_empty(&screens))
                survivor = wl_container_of(screens.next, survivor, link);

        /* clients keep their tags; with no survivor they orphan */
        wl_list_for_each(c, &clients, link)
                if (c->screen == s)
                        c->screen = survivor;

        if (current_screen == s)
                current_screen = survivor;

        free(s);

        arrange(survivor);
        focus(current_client());
}

static void new_output_handler(struct wl_listener *unused, void *arg)
{
        struct wlr_output *out = arg;
        struct wlr_output_layout_output *l_output;
        struct wlr_output_state state;
        struct screen_memory *m;
        struct screen *s;
        struct client *c;

        (void)unused;

        wlr_output_init_render(out, allocator, renderer);

        s = calloc(1, sizeof *s);
        if (0 == s)
                die("calloc failed");

        s->output = out;
        out->data = s;

        /* seed from the memory table — this output was here before —
         * or from config */
        m = screen_memory_find(out->name);

        s->tags = m ? m->tags : 1;
        s->master_ratio = m ? m->master_ratio : master_ratio;
        s->showbar = m ? m->showbar : showbar;
        s->bh = bar_height;

        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, 1);

        if (wlr_output_preferred_mode(out))
                wlr_output_state_set_mode(&state,
                                          wlr_output_preferred_mode(out));

        wlr_output_commit_state(out, &state);
        wlr_output_state_finish(&state);

        LISTEN(&out->events.frame, &s->frame, frame_handler);
        LISTEN(&out->events.request_state, &s->request_state,
               request_state_handler);
        LISTEN(&out->events.destroy, &s->destroy, output_destroy_handler);

        wl_list_insert(&screens, &s->link);

        s->scene_output = wlr_scene_output_create(scene, out);
        l_output = wlr_output_layout_add_auto(output_layout, out);
        wlr_scene_output_layout_add_output(scene_layout, l_output,
                                           s->scene_output);

        if (0 == current_screen)
                current_screen = s;

        /* re-adopt clients orphaned by an earlier output death */
        wl_list_for_each(c, &clients, link)
                if (0 == c->screen)
                        c->screen = s;

        wlr_log(WLR_INFO, "screen %s: %dx%d",
                out->name, out->width, out->height);
}

/**********************************************************************/
/* Clients                                                            */

/*
 * focus(NULL) is legal and meaningful: no focusable client, seat focus
 * cleared, current_screen keeps its meaning.
 */
static void focus(struct client *c)
{
        struct wlr_surface *old = seat->keyboard_state.focused_surface;
        struct wlr_keyboard *kb;

        if (c && c->toplevel->base->surface == old)
                return;

        if (c) {
                wl_list_remove(&c->focus_link);
                wl_list_insert(&fstack, &c->focus_link);

                /* unconditional: kills classic's abutting-boundary bug */
                current_screen = c->screen;

                state_of(c)->urgent = 0;
                set_border_color(c, color_border_select);
                wlr_scene_node_raise_to_top(&c->scene->node);
        }

        if (old) {
                struct wlr_xdg_toplevel *t =
                        wlr_xdg_toplevel_try_from_wlr_surface(old);

                if (t) {
                        struct client *o = t->base->data;

                        if (o)
                                set_border_color(o, color_border_normal);

                        wlr_xdg_toplevel_set_activated(t, 0);
                }
        }

        if (0 == c) {
                wlr_seat_keyboard_notify_clear_focus(seat);
                return;
        }

        wlr_xdg_toplevel_set_activated(c->toplevel, 1);

        kb = wlr_seat_get_keyboard(seat);
        if (kb)
                wlr_seat_keyboard_notify_enter(seat,
                                               c->toplevel->base->surface,
                                               kb->keycodes,
                                               kb->num_keycodes,
                                               &kb->modifiers);
        else
                wlr_seat_keyboard_notify_enter(seat,
                                               c->toplevel->base->surface,
                                               0, 0, 0);
}

static void resize(struct client *c, struct wlr_box r)
{
        const int bw = border_width;

        state_of(c)->r = r;

        wlr_scene_node_set_position(&c->scene->node, r.x, r.y);
        wlr_scene_node_set_position(&c->scene_surface->node, bw, bw);

        wlr_scene_rect_set_size(c->border[0], r.width, bw);
        wlr_scene_rect_set_size(c->border[1], r.width, bw);
        wlr_scene_rect_set_size(c->border[2], bw, r.height - 2 * bw);
        wlr_scene_rect_set_size(c->border[3], bw, r.height - 2 * bw);

        wlr_scene_node_set_position(&c->border[1]->node, 0, r.height - bw);
        wlr_scene_node_set_position(&c->border[2]->node, 0, bw);
        wlr_scene_node_set_position(&c->border[3]->node, r.width - bw, bw);

        wlr_xdg_toplevel_set_size(c->toplevel,
                                  r.width - 2 * bw, r.height - 2 * bw);
}

static void commit_handler(struct wl_listener *listener, void *arg)
{
        struct client *c = wl_container_of(listener, c, commit);

        (void)arg;

        /*
         * The initial commit must be answered with a configure before
         * the client can map; 0x0 lets it pick a size, tiling overrides
         * at map anyway.
         */
        if (c->toplevel->base->initial_commit)
                wlr_xdg_toplevel_set_size(c->toplevel, 0, 0);
}

static void map_handler(struct wl_listener *listener, void *arg)
{
        struct client *c = wl_container_of(listener, c, map);
        struct state *state = state_of(c);
        size_t i;

        (void)arg;

        c->scene = wlr_scene_tree_create(&scene->tree);
        c->scene_surface =
                wlr_scene_xdg_surface_create(c->scene, c->toplevel->base);

        /* popups look their parent's scene tree up here */
        c->toplevel->base->surface->data = c->scene_surface;

        for (i = 0; i < 4; ++i)
                c->border[i] = wlr_scene_rect_create(c->scene, 0, 0,
                                                     color_border_normal);

        wl_list_insert(&clients, &c->link);
        wl_list_insert(&fstack, &c->focus_link);

        c->screen = current_screen;     /* no outputs: a legal orphan */

        if (c->toplevel->parent) {
                struct client *p = c->toplevel->parent->base->data;

                /* transient: borrow the parent's tags, float */
                state->tags = p ? state_of(p)->tags : 1;
                state->floating = 1;
        } else {
                state->tags = c->screen ? c->screen->tags : 1;
                state->floating = client_is_fixed(c);
        }

        /* floating clients keep their own size, centered */
        if (state->floating && c->screen) {
                struct wlr_box g = c->toplevel->base->geometry;
                struct wlr_box r = {
                        .width = g.width + 2 * border_width,
                        .height = g.height + 2 * border_width
                };

                r.x = c->screen->warea.x +
                        (c->screen->warea.width - r.width) / 2;
                r.y = c->screen->warea.y +
                        (c->screen->warea.height - r.height) / 2;

                resize(c, r);
        }

        arrange(c->screen);
        focus(c);
}

static void unmap_handler(struct wl_listener *listener, void *arg)
{
        struct client *c = wl_container_of(listener, c, unmap);
        struct screen *s = c->screen;

        (void)arg;

        wl_list_remove(&c->link);
        wl_list_remove(&c->focus_link);

        c->toplevel->base->surface->data = 0;

        wlr_scene_node_destroy(&c->scene->node);
        c->scene = 0;
        c->scene_surface = 0;
        memset(c->border, 0, sizeof c->border);

        arrange(s);
        focus(current_client());
}

static void toplevel_destroy_handler(struct wl_listener *listener, void *arg)
{
        struct client *c = wl_container_of(listener, c, destroy);

        (void)arg;

        wl_list_remove(&c->commit.link);
        wl_list_remove(&c->map.link);
        wl_list_remove(&c->unmap.link);
        wl_list_remove(&c->destroy.link);

        free(c);
}

static void new_toplevel_handler(struct wl_listener *unused, void *arg)
{
        struct wlr_xdg_toplevel *toplevel = arg;
        struct client *c;

        (void)unused;

        c = calloc(1, sizeof *c);
        if (0 == c)
                die("calloc failed");

        c->toplevel = toplevel;
        toplevel->base->data = c;

        LISTEN(&toplevel->base->surface->events.commit, &c->commit,
               commit_handler);
        LISTEN(&toplevel->base->surface->events.map, &c->map, map_handler);
        LISTEN(&toplevel->base->surface->events.unmap, &c->unmap,
               unmap_handler);
        LISTEN(&toplevel->events.destroy, &c->destroy,
               toplevel_destroy_handler);
}

static void popup_commit_handler(struct wl_listener *listener, void *arg)
{
        struct popup *p = wl_container_of(listener, p, commit);

        (void)arg;

        if (p->popup->base->initial_commit)
                wlr_xdg_surface_schedule_configure(p->popup->base);
}

static void popup_destroy_handler(struct wl_listener *listener, void *arg)
{
        struct popup *p = wl_container_of(listener, p, destroy);

        (void)arg;

        wl_list_remove(&p->commit.link);
        wl_list_remove(&p->destroy.link);

        free(p);
}

static void new_popup_handler(struct wl_listener *unused, void *arg)
{
        struct wlr_xdg_popup *popup = arg;
        struct wlr_scene_tree *parent;
        struct popup *p;

        (void)unused;

        /* a popup for an unmapped parent has nowhere to render */
        parent = popup->parent ? popup->parent->data : 0;
        if (0 == parent) {
                wlr_xdg_popup_destroy(popup);
                return;
        }

        p = calloc(1, sizeof *p);
        if (0 == p)
                die("calloc failed");

        p->popup = popup;

        /* nested popups hook into this one the same way */
        popup->base->surface->data =
                wlr_scene_xdg_surface_create(parent, popup->base);

        LISTEN(&popup->base->surface->events.commit, &p->commit,
               popup_commit_handler);
        LISTEN(&popup->events.destroy, &p->destroy, popup_destroy_handler);
}

/**********************************************************************/
/* Actions                                                            */

static void spawn(const char *const *args)
{
        if (0 == fork()) {
                setsid();
                execvp(args[0], (char *const *)args);

                fprintf(stderr, "tyler: execvp %s failed\n", args[0]);
                exit(1);
        }
}

static void spawn_terminal(unsigned unused)
{
        (void)unused;

        spawn(termcmd);
}

static void quit(unsigned unused)
{
        (void)unused;

        wl_display_terminate(display);
}

static void zap(unsigned unused)
{
        struct client *c = current_client();

        (void)unused;

        if (c)
                wlr_xdg_toplevel_send_close(c->toplevel);
}

static void toggle_bar(unsigned unused)
{
        struct screen *s = current_screen;

        (void)unused;

        if (0 == s)
                return;

        s->showbar = !s->showbar;

        screen_update_area(s);
        arrange(s);
}

static void zoom(unsigned unused)
{
        struct client *cur = current_client(), *c = 0, *it;
        struct wl_list *l;

        (void)unused;

        if (0 == cur || !tiled_on(cur, current_screen))
                return;

        /* the first tiled client in list order is the master */
        wl_list_for_each(it, &clients, link)
                if (tiled_on(it, current_screen)) {
                        c = it;
                        break;
                }

        if (c != cur) {
                c = cur;
        } else {
                /* the master zooms the next tile up instead */
                c = 0;

                for (l = cur->link.next; l != &clients; l = l->next) {
                        it = wl_container_of(l, it, link);

                        if (tiled_on(it, current_screen)) {
                                c = it;
                                break;
                        }
                }

                if (0 == c)
                        return;
        }

        wl_list_remove(&c->link);
        wl_list_insert(&clients, &c->link);

        arrange(current_screen);
        focus(c);
}

/*
 * The walk rings over the client list in tile order, skipping the
 * sentinel when it wraps; with nothing focused it starts anywhere.
 */
static void focus_next(unsigned unused)
{
        struct client *cur = current_client(), *c = 0, *it;
        struct wl_list *l, *start = cur ? &cur->link : &clients;

        (void)unused;

        for (l = start->next; l != start; l = l->next) {
                if (l == &clients)
                        continue;

                it = wl_container_of(l, it, link);

                if (visible_on(it, current_screen)) {
                        c = it;
                        break;
                }
        }

        if (c && c != cur)
                focus(c);
}

static void focus_prev(unsigned unused)
{
        struct client *cur = current_client(), *c = 0, *it;
        struct wl_list *l, *start = cur ? &cur->link : &clients;

        (void)unused;

        for (l = start->prev; l != start; l = l->prev) {
                if (l == &clients)
                        continue;

                it = wl_container_of(l, it, link);

                if (visible_on(it, current_screen)) {
                        c = it;
                        break;
                }
        }

        if (c && c != cur)
                focus(c);
}

static void view_tag(unsigned n)
{
        unsigned mask = 1U << (n - 1);

        if (0 == current_screen || mask == current_screen->tags)
                return;

        current_screen->tags = mask;

        arrange(current_screen);
        focus(current_client());
}

static void change_tag(unsigned n)
{
        unsigned mask = 1U << (n - 1);
        struct client *c = current_client();

        if (0 == c || mask == state_of(c)->tags)
                return;

        state_of(c)->tags = mask;

        arrange(current_screen);
        focus(current_client());
}

static void tile_current(unsigned unused)
{
        struct client *c = current_client();
        struct state *state;

        (void)unused;

        if (0 == c)
                return;

        state = state_of(c);

        if (state->floating || state->fullscreen) {
                state->floating = 0;
                state->fullscreen = 0;

                arrange(current_screen);
        }
}

static void focus_other_screen(struct screen *s)
{
        if (0 == s)
                return;

        current_screen = s;
        focus(current_client());
}

static void focus_next_screen(unsigned unused)
{
        (void)unused;

        if (current_screen)
                focus_other_screen(screen_after(current_screen));
}

static void focus_prev_screen(unsigned unused)
{
        (void)unused;

        if (current_screen)
                focus_other_screen(screen_before(current_screen));
}

/*
 * As in classic: the client keeps its tags, focus stays on this
 * screen, and the mover sits atop the target's focus stack — it
 * becomes current the moment you look over there.
 */
static void move_other_screen(struct screen *s)
{
        struct client *c = current_client();
        struct screen *old = current_screen;

        if (0 == c || 0 == s)
                return;

        c->screen = s;

        arrange(old);
        arrange(s);
        focus(current_client());
}

static void move_next_screen(unsigned unused)
{
        (void)unused;

        if (current_screen)
                move_other_screen(screen_after(current_screen));
}

static void move_prev_screen(unsigned unused)
{
        (void)unused;

        if (current_screen)
                move_other_screen(screen_before(current_screen));
}

/**********************************************************************/
/* Input                                                              */

#define CLEANMASK(mask) ((mask) & ~(WLR_MODIFIER_CAPS | WLR_MODIFIER_MOD2))

static int keybinding(uint32_t mods, xkb_keysym_t sym)
{
        const struct key *k;

        for (k = keys; k < keys + sizeof keys / sizeof *keys; ++k)
                if (CLEANMASK(k->mod) == CLEANMASK(mods) &&
                    xkb_keysym_to_lower(k->keysym) ==
                            xkb_keysym_to_lower(sym)) {
                        k->func(k->arg);
                        return 1;
                }

        return 0;
}

static void kb_key_handler(struct wl_listener *listener, void *arg)
{
        struct keyboard *kb = wl_container_of(listener, kb, key);
        struct wlr_keyboard_key_event *event = arg;

        /* libinput keycode -> xkb */
        uint32_t keycode = event->keycode + 8;
        uint32_t mods = wlr_keyboard_get_modifiers(&kb->group->keyboard);

        const xkb_keysym_t *syms;
        int i, handled = 0;
        int nsyms = xkb_state_key_get_syms(kb->group->keyboard.xkb_state,
                                           keycode, &syms);

        if (WL_KEYBOARD_KEY_STATE_PRESSED == event->state)
                for (i = 0; i < nsyms; ++i)
                        handled |= keybinding(mods, syms[i]);

        /*
         * Held bindings repeat compositor-side — X gave classic this
         * for free. Any further key event re-arms or disarms.
         */
        if (handled && 0 < kb->group->keyboard.repeat_info.delay) {
                kb->repeat_mods = mods;
                kb->repeat_syms = syms;
                kb->repeat_nsyms = nsyms;

                wl_event_source_timer_update(
                        kb->repeat_timer,
                        kb->group->keyboard.repeat_info.delay);
        } else {
                kb->repeat_nsyms = 0;
                wl_event_source_timer_update(kb->repeat_timer, 0);
        }

        if (handled)
                return;

        wlr_seat_set_keyboard(seat, &kb->group->keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec,
                                     event->keycode, event->state);
}

static void kb_modifiers_handler(struct wl_listener *listener, void *arg)
{
        struct keyboard *kb = wl_container_of(listener, kb, modifiers);

        (void)arg;

        wlr_seat_set_keyboard(seat, &kb->group->keyboard);
        wlr_seat_keyboard_notify_modifiers(seat,
                                           &kb->group->keyboard.modifiers);
}

static int kb_repeat_handler(void *arg)
{
        struct keyboard *kb = arg;
        int i, rate = kb->group->keyboard.repeat_info.rate;

        if (0 == kb->repeat_nsyms || 0 >= rate)
                return 0;

        wl_event_source_timer_update(kb->repeat_timer, 1000 / rate);

        for (i = 0; i < kb->repeat_nsyms; ++i)
                keybinding(kb->repeat_mods, kb->repeat_syms[i]);

        return 0;
}

static struct keyboard *keyboard_create(void)
{
        struct keyboard *kb = calloc(1, sizeof *kb);

        if (0 == kb)
                die("calloc failed");

        kb->group = wlr_keyboard_group_create();

        wlr_keyboard_set_keymap(&kb->group->keyboard, keymap);
        wlr_keyboard_set_repeat_info(&kb->group->keyboard,
                                     repeat_rate, repeat_delay);

        LISTEN(&kb->group->keyboard.events.key, &kb->key, kb_key_handler);
        LISTEN(&kb->group->keyboard.events.modifiers, &kb->modifiers,
               kb_modifiers_handler);

        wl_list_init(&kb->destroy.link);

        kb->repeat_timer =
                wl_event_loop_add_timer(event_loop, kb_repeat_handler, kb);

        return kb;
}

static void keyboard_destroy(struct keyboard *kb)
{
        wl_list_remove(&kb->key.link);
        wl_list_remove(&kb->modifiers.link);
        wl_list_remove(&kb->destroy.link);

        wl_event_source_remove(kb->repeat_timer);
        wlr_keyboard_group_destroy(kb->group);

        free(kb);
}

static void vkbd_destroy_handler(struct wl_listener *listener, void *arg)
{
        struct keyboard *kb = wl_container_of(listener, kb, destroy);

        (void)arg;

        keyboard_destroy(kb);
}

static void new_vkbd_handler(struct wl_listener *unused, void *arg)
{
        struct wlr_virtual_keyboard_v1 *v = arg;
        struct keyboard *kb = keyboard_create();

        (void)unused;

        LISTEN(&v->keyboard.base.events.destroy, &kb->destroy,
               vkbd_destroy_handler);

        /* our keymap, not the client's — bindings must stay coherent */
        wlr_keyboard_set_keymap(&v->keyboard, keymap);
        wlr_keyboard_group_add_keyboard(kb->group, &v->keyboard);
}

static void new_input_handler(struct wl_listener *unused, void *arg)
{
        struct wlr_input_device *device = arg;

        (void)unused;

        /*
         * Every keyboard joins the group and inherits keymap and repeat
         * settings the moment it appears — the whole xset-reverts bug
         * class from X11 dissolves here.
         */
        if (WLR_INPUT_DEVICE_KEYBOARD == device->type) {
                struct wlr_keyboard *kb =
                        wlr_keyboard_from_input_device(device);

                wlr_keyboard_set_keymap(kb, keymap);
                wlr_keyboard_group_add_keyboard(kb_main->group, kb);
        }

        wlr_seat_set_capabilities(seat, WL_SEAT_CAPABILITY_KEYBOARD);
}

static void keyboard_init(void)
{
        struct xkb_context *context;

        context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (0 == context)
                die("xkb_context_new failed");

        /* NULL rules: layout comes from the XKB_DEFAULT_* environment */
        keymap = xkb_keymap_new_from_names(context, 0,
                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (0 == keymap)
                die("xkb_keymap_new_from_names failed");

        xkb_context_unref(context);

        kb_main = keyboard_create();

        wlr_seat_set_keyboard(seat, &kb_main->group->keyboard);

        /*
         * The group keyboard exists whether or not hardware ever shows
         * up, so the capability is unconditional — headless included.
         */
        wlr_seat_set_capabilities(seat, WL_SEAT_CAPABILITY_KEYBOARD);
}

/**********************************************************************/

static int terminate_handler(int signo, void *unused)
{
        (void)unused;

        wlr_log(WLR_INFO, "caught signal %d, terminating", signo);
        wl_display_terminate(display);

        return 0;
}

static int reap_handler(int signo, void *unused)
{
        (void)signo;
        (void)unused;

        while (0 < waitpid(-1, 0, WNOHANG))
                ;

        return 0;
}

static void init(void)
{
        display = wl_display_create();
        if (0 == display)
                die("wl_display_create failed");

        event_loop = wl_display_get_event_loop(display);

        wl_event_loop_add_signal(event_loop, SIGINT, terminate_handler, 0);
        wl_event_loop_add_signal(event_loop, SIGTERM, terminate_handler, 0);
        wl_event_loop_add_signal(event_loop, SIGCHLD, reap_handler, 0);

        backend = wlr_backend_autocreate(event_loop, &session);
        if (0 == backend)
                die("wlr_backend_autocreate failed");

        renderer = wlr_renderer_autocreate(backend);
        if (0 == renderer)
                die("wlr_renderer_autocreate failed");

        /* without this, no buffer-bearing client can attach */
        wlr_renderer_init_wl_shm(renderer, display);

        allocator = wlr_allocator_autocreate(backend, renderer);
        if (0 == allocator)
                die("wlr_allocator_autocreate failed");

        wlr_compositor_create(display, 6, renderer);
        wlr_subcompositor_create(display);
        wlr_data_device_manager_create(display);

        scene = wlr_scene_create();

        output_layout = wlr_output_layout_create(display);
        scene_layout = wlr_scene_attach_output_layout(scene, output_layout);

        wl_list_init(&screens);
        wl_list_init(&clients);
        wl_list_init(&fstack);

        LISTEN(&output_layout->events.change, &layout_change_listener,
               layout_change_handler);
        LISTEN(&backend->events.new_output, &new_output_listener,
               new_output_handler);

        xdg_shell = wlr_xdg_shell_create(display, 6);

        LISTEN(&xdg_shell->events.new_toplevel, &new_toplevel_listener,
               new_toplevel_handler);
        LISTEN(&xdg_shell->events.new_popup, &new_popup_listener,
               new_popup_handler);

        seat = wlr_seat_create(display, "seat0");

        LISTEN(&backend->events.new_input, &new_input_listener,
               new_input_handler);

        vkbd_mgr = wlr_virtual_keyboard_manager_v1_create(display);
        LISTEN(&vkbd_mgr->events.new_virtual_keyboard, &new_vkbd_listener,
               new_vkbd_handler);

        keyboard_init();
}

static void run(void)
{
        const char *socket = wl_display_add_socket_auto(display);

        if (0 == socket)
                die("wl_display_add_socket_auto failed");

        if (!wlr_backend_start(backend))
                die("wlr_backend_start failed");

        setenv("WAYLAND_DISPLAY", socket, 1);
        wlr_log(WLR_INFO, "running on %s", socket);

        wl_display_run(display);
}

static void fini(void)
{
        wl_display_destroy_clients(display);

        /*
         * wlroots asserts every listener is gone before the backend
         * finishes; unhook ours first.
         */
        wl_list_remove(&new_output_listener.link);
        wl_list_remove(&layout_change_listener.link);
        wl_list_remove(&new_toplevel_listener.link);
        wl_list_remove(&new_popup_listener.link);
        wl_list_remove(&new_input_listener.link);
        wl_list_remove(&new_vkbd_listener.link);

        keyboard_destroy(kb_main);
        xkb_keymap_unref(keymap);

        wlr_backend_destroy(backend);
        wl_display_destroy(display);

        /* the scene is not owned by the display; last out */
        wlr_scene_node_destroy(&scene->tree.node);
}

int main(void)
{
        wlr_log_init(WLR_INFO, 0);

        init();
        run();
        fini();

        return 0;
}
