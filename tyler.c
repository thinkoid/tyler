/* -*- mode: c; -*- */

#include <linux/input-event-codes.h>

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <fcft/fcft.h>
#include <pixman.h>

#include <wayland-server-core.h>

#include <xkbcommon/xkbcommon.h>

#include <wlr/interfaces/wlr_buffer.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_system_bell_v1.h>
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

#define CLEANMASK(mask) ((mask) & ~(WLR_MODIFIER_CAPS | WLR_MODIFIER_MOD2))

/* classic's color slots, same names, same order */
enum {
        COLOR_NORMAL_BORDER,
        COLOR_NORMAL_BG,
        COLOR_NORMAL_FG,
        COLOR_SELECT_BORDER,
        COLOR_SELECT_BG,
        COLOR_SELECT_FG,
};

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
        struct wl_listener request_fullscreen;
        struct wl_listener set_title;
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

        struct wlr_scene_buffer *bar;

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

struct button {
        uint32_t mod;
        uint32_t button;                /* BTN_LEFT and friends */
        void (*func)(struct client *);
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

/*
 * Stacking, bottom to top: tiles and floats, the bars, fullscreen
 * clients over everything. Creation order is the stacking order.
 */
static struct wlr_scene_tree *layer_tile;
static struct wlr_scene_tree *layer_bar;
static struct wlr_scene_tree *layer_fs;

static struct fcft_font *font;
static char status[256];
static struct wl_event_source *status_source;

/* the menu draws in menu_screen's bar strip while active */
static int menu_active;
static struct screen *menu_screen;

static struct wlr_xdg_shell *xdg_shell;

static struct wlr_seat *seat;
static struct keyboard *kb_main;
static struct xkb_keymap *keymap;
static struct wlr_virtual_keyboard_manager_v1 *vkbd_mgr;

static struct wlr_cursor *cursor;
static struct wlr_xcursor_manager *cursor_mgr;
static struct wlr_virtual_pointer_manager_v1 *vptr_mgr;

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
static struct wl_listener new_vptr_listener;
static struct wl_listener cursor_motion_listener;
static struct wl_listener cursor_motion_absolute_listener;
static struct wl_listener cursor_button_listener;
static struct wl_listener cursor_axis_listener;
static struct wl_listener cursor_frame_listener;
static struct wl_listener request_cursor_listener;
static struct wl_listener request_activate_listener;
static struct wl_listener bell_ring_listener;

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
static void menu_open(unsigned);
static void screenshot(unsigned);
static void quit(unsigned);

/* the buttons table points at these */
static void mouse_move(struct client *);
static void mouse_resize(struct client *);

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

/*
 * Scene hit test: the surface under the point (for the seat) and the
 * owning client (for focus). Client scene trees carry their owner in
 * node data, so a hit on a border rect still names the client.
 */
static struct client *client_at(double x, double y,
                                struct wlr_surface **psurface,
                                double *sx, double *sy)
{
        struct wlr_scene_node *node;
        struct wlr_scene_tree *tree;

        *psurface = 0;

        node = wlr_scene_node_at(&scene->tree.node, x, y, sx, sy);
        if (0 == node)
                return 0;

        if (WLR_SCENE_NODE_BUFFER == node->type) {
                struct wlr_scene_surface *s =
                        wlr_scene_surface_try_from_buffer(
                                wlr_scene_buffer_from_node(node));

                if (s)
                        *psurface = s->surface;
        }

        for (tree = node->parent; tree && 0 == tree->node.data;
             tree = tree->node.parent)
                ;

        return tree ? tree->node.data : 0;
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
/* Bar                                                                */

/*
 * The bar is a plain pixel buffer the compositor draws itself — no
 * layer-shell, no client. Each redraw allocates a fresh buffer and
 * hands it to the scene; the scene holds the only lock.
 */
struct bar_buffer {
        struct wlr_buffer base;
        uint32_t *data;
};

static void bar_buffer_destroy(struct wlr_buffer *buffer)
{
        struct bar_buffer *buf = wl_container_of(buffer, buf, base);

        free(buf->data);
        free(buf);
}

static bool bar_buffer_begin_data_ptr_access(struct wlr_buffer *buffer,
                                             uint32_t flags, void **data,
                                             uint32_t *format,
                                             size_t *stride)
{
        struct bar_buffer *buf = wl_container_of(buffer, buf, base);

        (void)flags;

        *data = buf->data;
        *format = DRM_FORMAT_ARGB8888;
        *stride = 4 * buffer->width;

        return true;
}

static void bar_buffer_end_data_ptr_access(struct wlr_buffer *buffer)
{
        (void)buffer;
}

static const struct wlr_buffer_impl bar_buffer_impl = {
        .destroy = bar_buffer_destroy,
        .begin_data_ptr_access = bar_buffer_begin_data_ptr_access,
        .end_data_ptr_access = bar_buffer_end_data_ptr_access,
};

static struct bar_buffer *bar_buffer_create(int w, int h)
{
        struct bar_buffer *buf = calloc(1, sizeof *buf);

        if (0 == buf)
                die("calloc failed");

        buf->data = calloc((size_t)w * h, 4);
        if (0 == buf->data)
                die("calloc failed");

        wlr_buffer_init(&buf->base, &bar_buffer_impl, w, h);

        return buf;
}

static pixman_color_t pixman_color(const float rgba[4])
{
        return (pixman_color_t){
                .red = (uint16_t)(rgba[0] * 0xffff),
                .green = (uint16_t)(rgba[1] * 0xffff),
                .blue = (uint16_t)(rgba[2] * 0xffff),
                .alpha = (uint16_t)(rgba[3] * 0xffff),
        };
}

static void fill_rect(pixman_image_t *dst, int x, int y, int w, int h,
                      const float rgba[4])
{
        pixman_color_t c = pixman_color(rgba);

        pixman_image_fill_rectangles(PIXMAN_OP_SRC, dst, &c, 1,
                                     &(pixman_rectangle16_t){
                                             x, y, w, h });
}

static uint32_t utf8_next(const char **s)
{
        const unsigned char *p = (const unsigned char *)*s;
        uint32_t cp;
        int len;

        if (p[0] < 0x80) {
                cp = p[0];
                len = 1;
        } else if (0xc0 == (p[0] & 0xe0)) {
                cp = p[0] & 0x1f;
                len = 2;
        } else if (0xe0 == (p[0] & 0xf0)) {
                cp = p[0] & 0x0f;
                len = 3;
        } else if (0xf0 == (p[0] & 0xf8)) {
                cp = p[0] & 0x07;
                len = 4;
        } else {
                *s += 1;
                return 0xfffd;
        }

        for (int i = 1; i < len; ++i) {
                if (0x80 != (p[i] & 0xc0)) {
                        *s += 1;
                        return 0xfffd;
                }

                cp = cp << 6 | (p[i] & 0x3f);
        }

        *s += len;
        return cp;
}

/* the decoder's mirror; out must hold 4 bytes */
static int utf8_encode(uint32_t cp, char *out)
{
        if (cp < 0x80) {
                out[0] = (char)cp;
                return 1;
        }

        if (cp < 0x800) {
                out[0] = (char)(0xc0 | cp >> 6);
                out[1] = (char)(0x80 | (cp & 0x3f));
                return 2;
        }

        if (cp < 0x10000) {
                out[0] = (char)(0xe0 | cp >> 12);
                out[1] = (char)(0x80 | (cp >> 6 & 0x3f));
                out[2] = (char)(0x80 | (cp & 0x3f));
                return 3;
        }

        out[0] = (char)(0xf0 | cp >> 18);
        out[1] = (char)(0x80 | (cp >> 12 & 0x3f));
        out[2] = (char)(0x80 | (cp >> 6 & 0x3f));
        out[3] = (char)(0x80 | (cp & 0x3f));
        return 4;
}

/*
 * Glyphs composite through their alpha mask in the fg color; color
 * glyphs (emoji, nerd icons) blend as-is.
 */
static int draw_text(pixman_image_t *dst, const char *utf8, int x,
                     const float fg[4], int max_x, int bh)
{
        pixman_color_t c = pixman_color(fg);
        pixman_image_t *src = pixman_image_create_solid_fill(&c);

        int baseline = (bh + font->ascent - font->descent) / 2;

        while (*utf8) {
                const struct fcft_glyph *g;
                uint32_t cp = utf8_next(&utf8);

                g = fcft_rasterize_char_utf32(font, cp, FCFT_SUBPIXEL_NONE);
                if (0 == g)
                        continue;

                if (max_x < x + g->advance.x)
                        break;

                if (g->is_color_glyph)
                        pixman_image_composite32(
                                PIXMAN_OP_OVER, g->pix, 0, dst, 0, 0, 0, 0,
                                x + g->x, baseline - g->y,
                                g->width, g->height);
                else
                        pixman_image_composite32(
                                PIXMAN_OP_OVER, src, g->pix, dst, 0, 0, 0, 0,
                                x + g->x, baseline - g->y,
                                g->width, g->height);

                x += g->advance.x;
        }

        pixman_image_unref(src);

        return x;
}

static int text_width(const char *utf8)
{
        int w = 0;

        while (*utf8) {
                const struct fcft_glyph *g = fcft_rasterize_char_utf32(
                        font, utf8_next(&utf8), FCFT_SUBPIXEL_NONE);

                if (g)
                        w += g->advance.x;
        }

        return w;
}

/* the drawn buffer's road into the scene, dump hook included */
static void bar_commit(struct screen *s, struct bar_buffer *buf,
                       pixman_image_t *img)
{
        pixman_image_unref(img);

        /* the oracle's eye: TYLER_BAR_DUMP=<dir> writes each redraw */
        if (getenv("TYLER_BAR_DUMP")) {
                char path[256];
                FILE *f;

                snprintf(path, sizeof path, "%s/bar-%s.ppm",
                         getenv("TYLER_BAR_DUMP"), s->output->name);

                f = fopen(path, "w");
                if (f) {
                        int i, n = s->area.width * s->bh;

                        fprintf(f, "P6\n%d %d\n255\n", s->area.width, s->bh);

                        for (i = 0; i < n; ++i) {
                                uint32_t p = buf->data[i];
                                unsigned char rgb[3] = { p >> 16, p >> 8,
                                                         p };

                                fwrite(rgb, 1, 3, f);
                        }

                        fclose(f);
                }
        }

        wlr_scene_buffer_set_buffer(s->bar, &buf->base);
        wlr_buffer_drop(&buf->base);

        wlr_scene_node_set_position(&s->bar->node, s->area.x, s->area.y);
}

static void draw_menu(pixman_image_t *, struct screen *);

/* the screen's top visible client — its title owns the bar */
static struct client *focustop(struct screen *s)
{
        struct client *c;

        wl_list_for_each(c, &fstack, focus_link)
                if (visible_on(c, s))
                        return c;

        return 0;
}

static void drawbar(struct screen *s)
{
        struct bar_buffer *buf;
        pixman_image_t *img;
        struct client *c;

        unsigned occ = 0, urg = 0;
        int i, x = 0, w = s->area.width, sel;
        char tag[2] = { 0 };

        /* the menu borrows the strip even on a hidden bar */
        int menu_here = menu_active && s == menu_screen;

        if (0 == s->bar)
                return;

        wlr_scene_node_set_enabled(&s->bar->node, s->showbar || menu_here);
        if (!s->showbar && !menu_here)
                return;

        buf = bar_buffer_create(w, s->bh);
        img = pixman_image_create_bits_no_clear(PIXMAN_a8r8g8b8, w, s->bh,
                                                buf->data, 4 * w);

        if (menu_here) {
                draw_menu(img, s);
                bar_commit(s, buf, img);
                return;
        }

        fill_rect(img, 0, 0, w, s->bh, colors[COLOR_NORMAL_BG]);

        wl_list_for_each(c, &clients, link) {
                if (c->screen != s)
                        continue;

                occ |= state_of(c)->tags;

                if (state_of(c)->urgent)
                        urg |= state_of(c)->tags;
        }

        /* tags: viewed = select bg, occupied = underline, urgent = swap */
        for (i = 0; i < 9; ++i) {
                const float *fg, *bg;
                int pad = s->bh / 4, tw, cw;

                tag[0] = '1' + i;
                tw = text_width(tag);
                cw = tw + 2 * pad;

                sel = s->tags & 1U << i;

                fg = sel ? colors[COLOR_SELECT_FG] : colors[COLOR_NORMAL_FG];
                bg = sel ? colors[COLOR_SELECT_BG] : colors[COLOR_NORMAL_BG];

                if (urg & 1U << i) {
                        const float *tmp = fg;
                        fg = bg;
                        bg = tmp;
                }

                fill_rect(img, x, 0, cw, s->bh, bg);
                draw_text(img, tag, x + pad, fg, x + cw, s->bh);

                if (occ & 1U << i)
                        fill_rect(img, x + pad,
                                  s->bh - 2 - (0 < font->underline.thickness
                                                       ? font->underline
                                                                 .thickness
                                                       : 1),
                                  tw,
                                  0 < font->underline.thickness
                                          ? font->underline.thickness
                                          : 1,
                                  fg);

                x += cw;
        }

        /* status, right-aligned, on the current screen only */
        if (s == current_screen && status[0]) {
                int sw = text_width(status) + s->bh / 4;

                draw_text(img, status, w - sw, colors[COLOR_NORMAL_FG], w,
                          s->bh);
                w -= sw + s->bh / 4;
        }

        /* the title field takes the rest; select colors when current */
        c = focustop(s);
        sel = s == current_screen;

        fill_rect(img, x, 0, w - x,
                  s->bh, colors[sel ? COLOR_SELECT_BG : COLOR_NORMAL_BG]);

        if (c && c->toplevel->title)
                draw_text(img, c->toplevel->title, x + s->bh / 4,
                          colors[sel ? COLOR_SELECT_FG : COLOR_NORMAL_FG],
                          w, s->bh);

        bar_commit(s, buf, img);
}

static void drawbars(void)
{
        struct screen *s;

        wl_list_for_each(s, &screens, link)
                drawbar(s);
}

/**********************************************************************/
/* Menu                                                               */

/*
 * The integrated dmenu: no client, no grabs — the compositor owns the
 * keyboard, so menu_active simply reroutes key events here and the
 * drawing rides the bar path. The $PATH run-launcher, as designed.
 */

static void spawn(const char *const *);

static char menu_input[256];

static char **menu_items;               /* the sorted $PATH scan */
static size_t menu_nitems;

static char **menu_matches;             /* pointers into menu_items */
static size_t menu_nmatches;
static size_t menu_sel, menu_first;

static int cmpstrp(const void *a, const void *b)
{
        return strcmp(*(char *const *)a, *(char *const *)b);
}

static void menu_scan_path(void)
{
        static size_t cap;

        char *path, *dir, *save = 0;
        size_t i, n = 0;

        for (i = 0; i < menu_nitems; ++i)
                free(menu_items[i]);
        menu_nitems = 0;

        path = getenv("PATH");
        if (0 == path)
                return;

        path = strdup(path);

        for (dir = strtok_r(path, ":", &save); dir;
             dir = strtok_r(0, ":", &save)) {
                DIR *d = opendir(dir);
                struct dirent *e;

                if (0 == d)
                        continue;

                while ((e = readdir(d))) {
                        struct stat st;

                        if (0 == strcmp(e->d_name, ".") ||
                            0 == strcmp(e->d_name, ".."))
                                continue;

                        if (fstatat(dirfd(d), e->d_name, &st, 0) < 0 ||
                            !S_ISREG(st.st_mode) ||
                            faccessat(dirfd(d), e->d_name, X_OK, 0) < 0)
                                continue;

                        if (menu_nitems == cap) {
                                cap = cap ? 2 * cap : 1024;
                                menu_items = realloc(
                                        menu_items,
                                        cap * sizeof *menu_items);

                                if (0 == menu_items)
                                        die("realloc failed");
                        }

                        menu_items[menu_nitems++] = strdup(e->d_name);
                }

                closedir(d);
        }

        free(path);

        qsort(menu_items, menu_nitems, sizeof *menu_items, cmpstrp);

        /* $PATH dirs shadow each other; adjacent duplicates collapse */
        for (i = 0; i < menu_nitems; ++i)
                if (n && 0 == strcmp(menu_items[n - 1], menu_items[i]))
                        free(menu_items[i]);
                else
                        menu_items[n++] = menu_items[i];

        menu_nitems = n;

        /* matches can never outnumber items */
        menu_matches = realloc(menu_matches,
                               menu_nitems * sizeof *menu_matches);
        if (menu_nitems && 0 == menu_matches)
                die("realloc failed");
}

/* dmenu's ranking, abridged: prefix matches, then substring matches */
static void menu_filter(void)
{
        size_t i, len = strlen(menu_input);

        menu_nmatches = 0;

        for (i = 0; i < menu_nitems; ++i)
                if (0 == strncmp(menu_items[i], menu_input, len))
                        menu_matches[menu_nmatches++] = menu_items[i];

        for (i = 0; i < menu_nitems; ++i)
                if (strncmp(menu_items[i], menu_input, len) &&
                    strstr(menu_items[i], menu_input))
                        menu_matches[menu_nmatches++] = menu_items[i];

        menu_sel = 0;
        menu_first = 0;
}

static void draw_menu(pixman_image_t *img, struct screen *s)
{
        int pad = s->bh / 4;
        int w = s->area.width, inw = w / 4;
        size_t i;
        int x;

        fill_rect(img, 0, 0, w, s->bh, colors[COLOR_NORMAL_BG]);

        /* the input field, caret at the end of the text */
        x = draw_text(img, menu_input, pad, colors[COLOR_SELECT_FG],
                      inw - pad, s->bh);
        fill_rect(img, x + 1, 2, 1, s->bh - 4, colors[COLOR_SELECT_FG]);

        /* page so the selection stays visible */
        if (menu_sel < menu_first)
                menu_first = menu_sel;
        else if (menu_nmatches) {
                int avail = w - inw;

                for (i = menu_first; i <= menu_sel; ++i)
                        if ((avail -= text_width(menu_matches[i]) +
                                      2 * pad) < 0) {
                                menu_first = menu_sel;
                                break;
                        }
        }

        x = inw;

        for (i = menu_first; i < menu_nmatches && x < w; ++i) {
                int tw = text_width(menu_matches[i]) + 2 * pad;
                int sel = i == menu_sel;

                fill_rect(img, x, 0, tw < w - x ? tw : w - x, s->bh,
                          colors[sel ? COLOR_SELECT_BG : COLOR_NORMAL_BG]);
                draw_text(img, menu_matches[i], x + pad,
                          colors[sel ? COLOR_SELECT_FG : COLOR_NORMAL_FG],
                          x + tw < w ? x + tw - pad : w, s->bh);

                x += tw;
        }
}

static void menu_open(unsigned unused)
{
        (void)unused;

        if (menu_active || 0 == current_screen)
                return;

        menu_scan_path();

        menu_input[0] = 0;
        menu_filter();

        menu_active = 1;
        menu_screen = current_screen;

        drawbar(menu_screen);
}

static void menu_close(void)
{
        struct screen *s = menu_screen;

        menu_active = 0;
        menu_screen = 0;

        drawbar(s);
}

static void menu_exec(int verbatim)
{
        const char *cmd = !verbatim && menu_nmatches
                                  ? menu_matches[menu_sel]
                                  : menu_input;
        const char *const args[] = { "sh", "-c", cmd, 0 };

        if (cmd[0])
                spawn(args);
}

/* every press is the menu's while it is up — the return says so */
static int menu_key(uint32_t mods, xkb_keysym_t sym)
{
        uint32_t cp;
        size_t n;

        switch (sym) {
        case XKB_KEY_Escape:
                menu_close();
                return 1;

        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
                /* shifted runs the input verbatim, as in dmenu */
                menu_exec(mods & WLR_MODIFIER_SHIFT);
                menu_close();
                return 1;

        case XKB_KEY_Left:
                if (0 < menu_sel)
                        --menu_sel;
                break;

        case XKB_KEY_Right:
                if (menu_sel + 1 < menu_nmatches)
                        ++menu_sel;
                break;

        case XKB_KEY_Tab:
                if (menu_nmatches) {
                        snprintf(menu_input, sizeof menu_input, "%s",
                                 menu_matches[menu_sel]);
                        menu_filter();
                }
                break;

        case XKB_KEY_BackSpace:
                n = strlen(menu_input);

                while (0 < n &&
                       0x80 == (0xc0 & (unsigned char)menu_input[--n]))
                        ;

                menu_input[n] = 0;
                menu_filter();
                break;

        default:
                cp = xkb_keysym_to_utf32(sym);

                if (cp < 0x20 || 0x7f == cp)
                        return 1;

                n = strlen(menu_input);

                if (n + 4 < sizeof menu_input) {
                        n += utf8_encode(cp, menu_input + n);
                        menu_input[n] = 0;
                        menu_filter();
                }
                break;
        }

        drawbar(menu_screen);
        return 1;
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
        drawbar(s);
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

        /* the menu's screen dying takes the menu with it */
        if (menu_screen == s) {
                menu_active = 0;
                menu_screen = 0;
        }

        wl_list_remove(&s->frame.link);
        wl_list_remove(&s->request_state.link);
        wl_list_remove(&s->destroy.link);
        wl_list_remove(&s->link);

        wlr_scene_node_destroy(&s->bar->node);

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
        s->bh = font->height + 2;
        s->bar = wlr_scene_buffer_create(layer_bar, 0);

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
                set_border_color(c, colors[COLOR_SELECT_BORDER]);
                wlr_scene_node_raise_to_top(&c->scene->node);
        }

        if (old) {
                struct wlr_xdg_toplevel *t =
                        wlr_xdg_toplevel_try_from_wlr_surface(old);

                if (t) {
                        struct client *o = t->base->data;

                        if (o)
                                set_border_color(o, colors[COLOR_NORMAL_BORDER]);

                        wlr_xdg_toplevel_set_activated(t, 0);
                }
        }

        if (0 == c) {
                wlr_seat_keyboard_notify_clear_focus(seat);
                drawbars();
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

        drawbars();
}

static void resize(struct client *c, struct wlr_box r)
{
        /* fullscreen is edge to edge: the border disappears with it */
        const int bw = state_of(c)->fullscreen ? 0 : border_width;

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

/*
 * Classic's double buffer earns its keep: entering fullscreen flips
 * current_state to a scratch copy; leaving flips back to the saved
 * pre-fullscreen state — geometry, flags, and tags all restored in one
 * move.
 */
static void set_fullscreen(struct client *c, int on)
{
        struct state *from = state_of(c), *to;

        if (on == (int)from->fullscreen)
                return;

        c->current_state ^= 1;
        to = state_of(c);

        if (on) {
                *to = *from;
                to->fullscreen = 1;

                /* over everything, the bar included */
                wlr_scene_node_reparent(&c->scene->node, layer_fs);

                if (c->screen)
                        resize(c, c->screen->area);
        } else {
                /* `to` is the saved normal state, untouched */
                wlr_scene_node_reparent(&c->scene->node, layer_tile);
                resize(c, to->r);
        }

        wlr_xdg_toplevel_set_fullscreen(c->toplevel, on);
        arrange(c->screen);
}

static void request_fullscreen_handler(struct wl_listener *listener,
                                       void *arg)
{
        struct client *c =
                wl_container_of(listener, c, request_fullscreen);

        (void)arg;

        /* an unmapped client's wish is honored at map */
        if (c->scene)
                set_fullscreen(c, c->toplevel->requested.fullscreen);
}

static void set_title_handler(struct wl_listener *listener, void *arg)
{
        struct client *c = wl_container_of(listener, c, set_title);

        (void)arg;

        if (c->screen)
                drawbar(c->screen);
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

        c->scene = wlr_scene_tree_create(layer_tile);
        c->scene->node.data = c;        /* client_at walks up to this */
        c->scene_surface =
                wlr_scene_xdg_surface_create(c->scene, c->toplevel->base);

        /* popups look their parent's scene tree up here */
        c->toplevel->base->surface->data = c->scene_surface;

        for (i = 0; i < 4; ++i)
                c->border[i] = wlr_scene_rect_create(c->scene, 0, 0,
                                                     colors[COLOR_NORMAL_BORDER]);

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

        if (c->toplevel->requested.fullscreen)
                set_fullscreen(c, 1);
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
        wl_list_remove(&c->request_fullscreen.link);
        wl_list_remove(&c->set_title.link);

        free(c);
}

/*
 * Criterion 1, five lines as promised (twice): an activation request
 * or a bell ring for a client that isn't focused marks it urgent; the
 * bar swaps its tag's colors. focus() clears the flag when the user
 * gets there.
 */
static void set_urgent(struct wlr_surface *surface)
{
        struct wlr_xdg_toplevel *t =
                surface ? wlr_xdg_toplevel_try_from_wlr_surface(surface)
                        : 0;
        struct client *c = t ? t->base->data : 0;

        if (0 == c || c == current_client())
                return;

        state_of(c)->urgent = 1;

        if (c->screen)
                drawbar(c->screen);
}

static void request_activate_handler(struct wl_listener *unused, void *arg)
{
        struct wlr_xdg_activation_v1_request_activate_event *event = arg;

        (void)unused;

        set_urgent(event->surface);
}

static void bell_ring_handler(struct wl_listener *unused, void *arg)
{
        struct wlr_xdg_system_bell_v1_ring_event *event = arg;

        (void)unused;

        set_urgent(event->surface);
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
        LISTEN(&toplevel->events.request_fullscreen, &c->request_fullscreen,
               request_fullscreen_handler);
        LISTEN(&toplevel->events.set_title, &c->set_title,
               set_title_handler);
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
/* Screenshots                                                        */

/*
 * Internal scene->PNG, as designed: no wlr-screencopy, no external
 * grabber. The PNG is hand-rolled with stored deflate blocks — bigger
 * files, zero dependencies.
 */

static uint32_t crc32_of(uint32_t crc, const unsigned char *p, size_t n)
{
        static uint32_t table[256];

        if (0 == table[1]) {
                uint32_t i, j, c;

                for (i = 0; i < 256; ++i) {
                        for (c = i, j = 0; j < 8; ++j)
                                c = c & 1 ? 0xedb88320 ^ c >> 1 : c >> 1;

                        table[i] = c;
                }
        }

        while (n--)
                crc = table[(crc ^ *p++) & 0xff] ^ crc >> 8;

        return crc;
}

static void png_chunk(FILE *f, const char *tag, const unsigned char *data,
                      size_t n)
{
        unsigned char b[4] = { n >> 24, n >> 16, n >> 8, n };
        uint32_t crc;

        fwrite(b, 1, 4, f);
        fwrite(tag, 1, 4, f);

        if (n)
                fwrite(data, 1, n, f);

        crc = crc32_of(0xffffffff, (const unsigned char *)tag, 4);

        if (n)
                crc = crc32_of(crc, data, n);

        crc ^= 0xffffffff;

        b[0] = crc >> 24;
        b[1] = crc >> 16;
        b[2] = crc >> 8;
        b[3] = crc;

        fwrite(b, 1, 4, f);
}

static int png_write(const char *path, const uint32_t *px, int w, int h)
{
        /* one filter byte per row, RGB triples after */
        size_t raw = (size_t)h * (1 + 3 * (size_t)w);
        size_t zn = 2 + 5 * ((raw + 65534) / 65535) + raw + 4;
        size_t i, n, left;

        unsigned char *filt, *z, *p;
        unsigned char ihdr[13] = { (unsigned)w >> 24, (unsigned)w >> 16,
                                   (unsigned)w >> 8,  (unsigned)w,
                                   (unsigned)h >> 24, (unsigned)h >> 16,
                                   (unsigned)h >> 8,  (unsigned)h,
                                   8, 2, 0, 0, 0 };
        uint32_t a = 1, b = 0;
        int x, y;
        FILE *f;

        filt = malloc(raw);
        z = malloc(zn);
        if (0 == filt || 0 == z)
                die("malloc failed");

        p = filt;

        for (y = 0; y < h; ++y) {
                *p++ = 0;

                for (x = 0; x < w; ++x) {
                        uint32_t c = px[(size_t)y * w + x];

                        *p++ = c >> 16;
                        *p++ = c >> 8;
                        *p++ = c;
                }
        }

        /* zlib: header, stored blocks, adler32 of the filtered bytes */
        for (i = 0; i < raw; ++i) {
                a = (a + filt[i]) % 65521;
                b = (b + a) % 65521;
        }

        p = z;
        *p++ = 0x78;
        *p++ = 0x01;

        for (left = raw; 0 < left; left -= n) {
                n = 65535 < left ? 65535 : left;

                *p++ = n == left;
                *p++ = n;
                *p++ = n >> 8;
                *p++ = ~n;
                *p++ = ~n >> 8;

                memcpy(p, filt + (raw - left), n);
                p += n;
        }

        *p++ = b >> 8;
        *p++ = b;
        *p++ = a >> 8;
        *p++ = a;

        f = fopen(path, "w");
        if (f) {
                fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
                png_chunk(f, "IHDR", ihdr, sizeof ihdr);
                png_chunk(f, "IDAT", z, p - z);
                png_chunk(f, "IEND", 0, 0);
                fclose(f);
        }

        free(filt);
        free(z);

        return 0 != f;
}

static void screenshot(unsigned unused)
{
        struct screen *s = current_screen;
        struct wlr_output_state state;
        struct wlr_texture *tex;
        uint32_t *px;

        char stamp[32], path[512];
        const char *dir;
        time_t now;

        (void)unused;

        if (0 == s)
                return;

        wlr_output_state_init(&state);

        if (!wlr_scene_output_build_state(s->scene_output, &state, 0) ||
            0 == state.buffer) {
                wlr_output_state_finish(&state);
                return;
        }

        tex = wlr_texture_from_buffer(renderer, state.buffer);
        if (0 == tex) {
                wlr_output_state_finish(&state);
                return;
        }

        px = malloc(4 * (size_t)tex->width * tex->height);
        if (0 == px)
                die("malloc failed");

        if (!wlr_texture_read_pixels(
                    tex, &(struct wlr_texture_read_pixels_options){
                                 .data = px,
                                 .format = DRM_FORMAT_ARGB8888,
                                 .stride = 4 * tex->width })) {
                wlr_log(WLR_ERROR, "screenshot: pixel readback failed");
                goto out;
        }

        dir = getenv("TYLER_SHOT_DIR");
        if (0 == dir)
                dir = getenv("HOME");
        if (0 == dir)
                dir = ".";

        now = time(0);
        strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
        snprintf(path, sizeof path, "%s/tyler-%s-%s.png", dir, stamp,
                 s->output->name);

        if (png_write(path, px, tex->width, tex->height))
                wlr_log(WLR_INFO, "screenshot: %s", path);
        else
                wlr_log(WLR_ERROR, "screenshot: writing %s failed", path);

out:
        free(px);
        wlr_texture_destroy(tex);
        wlr_output_state_finish(&state);
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

static void set_fullscreen(struct client *, int);

static void tile_current(unsigned unused)
{
        struct client *c = current_client();
        struct state *state;

        (void)unused;

        if (0 == c)
                return;

        /* leave fullscreen through the mechanism — the client must
         * hear about it, and the buffer flip restores the saved state */
        if (state_of(c)->fullscreen)
                set_fullscreen(c, 0);

        state = state_of(c);

        if (state->floating) {
                state->floating = 0;
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
/* Pointer                                                            */

enum { GRAB_NONE, GRAB_MOVE, GRAB_RESIZE };

static struct client *grab_client;
static int grab_mode;
static double grab_dx, grab_dy;         /* cursor offset into the client */
static struct wlr_box grab_box;         /* geometry at grab start */

static void grab_start(struct client *c, int mode)
{
        struct state *state = state_of(c);

        if (state->fullscreen)
                return;

        /* dragging a tile tears it out of the tiling, dwm-style */
        if (!state->floating) {
                state->floating = 1;
                arrange(c->screen);
        }

        focus(c);
        wlr_scene_node_raise_to_top(&c->scene->node);

        grab_client = c;
        grab_mode = mode;
        grab_box = state->r;
        grab_dx = cursor->x - grab_box.x;
        grab_dy = cursor->y - grab_box.y;
}

static void mouse_move(struct client *c)
{
        grab_start(c, GRAB_MOVE);
}

static void mouse_resize(struct client *c)
{
        grab_start(c, GRAB_RESIZE);
}

/*
 * Sloppy focus, motion-driven: focus moves only when the pointer
 * does. A window appearing under a stationary cursor steals nothing —
 * classic's EnterNotify ghosts do not port.
 */
static void process_motion(uint32_t time)
{
        if (GRAB_MOVE == grab_mode) {
                struct wlr_box r = state_of(grab_client)->r;

                r.x = (int)(cursor->x - grab_dx);
                r.y = (int)(cursor->y - grab_dy);

                resize(grab_client, r);
                return;
        }

        if (GRAB_RESIZE == grab_mode) {
                struct wlr_box r = grab_box;
                int w = (int)(cursor->x - grab_box.x);
                int h = (int)(cursor->y - grab_box.y);

                r.width = 32 < w ? w : 32;
                r.height = 32 < h ? h : 32;

                resize(grab_client, r);
                return;
        }

        double sx = 0, sy = 0;
        struct wlr_surface *surface;
        struct wlr_output *out;
        struct client *c;
        struct screen *s;

        c = client_at(cursor->x, cursor->y, &surface, &sx, &sy);

        /*
         * Criterion 2: crossing into another output moves screen
         * focus unconditionally — clients there or not.
         */
        out = wlr_output_layout_output_at(output_layout,
                                          cursor->x, cursor->y);
        s = out ? out->data : 0;

        if (s && s != current_screen) {
                current_screen = s;

                if (0 == c)
                        focus(current_client());
        }

        /*
         * Unconditional: focus() itself no-ops when the surface already
         * holds seat focus. Guarding on the COMPUTED current here once
         * skipped the seat entirely (computed said "already current"
         * while the keyboard sat on another screen's client).
         */
        if (c)
                focus(c);

        if (surface) {
                wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
                wlr_seat_pointer_notify_motion(seat, time, sx, sy);
        } else {
                wlr_seat_pointer_clear_focus(seat);
                wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
        }
}

static void cursor_motion_handler(struct wl_listener *unused, void *arg)
{
        struct wlr_pointer_motion_event *event = arg;

        (void)unused;

        wlr_cursor_move(cursor, &event->pointer->base,
                        event->delta_x, event->delta_y);
        process_motion(event->time_msec);
}

static void cursor_motion_absolute_handler(struct wl_listener *unused,
                                           void *arg)
{
        struct wlr_pointer_motion_absolute_event *event = arg;

        (void)unused;

        wlr_cursor_warp_absolute(cursor, &event->pointer->base,
                                 event->x, event->y);
        process_motion(event->time_msec);
}

static void cursor_button_handler(struct wl_listener *unused, void *arg)
{
        struct wlr_pointer_button_event *event = arg;

        (void)unused;

        if (WL_POINTER_BUTTON_STATE_RELEASED == event->state) {
                if (GRAB_NONE != grab_mode) {
                        grab_mode = GRAB_NONE;
                        grab_client = 0;
                        return;
                }
        } else {
                struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);
                uint32_t mods = kb ? wlr_keyboard_get_modifiers(kb) : 0;

                struct wlr_surface *surface;
                double sx, sy;
                struct client *c = client_at(cursor->x, cursor->y,
                                             &surface, &sx, &sy);
                const struct button *b;

                if (c)
                        for (b = buttons;
                             b < buttons + sizeof buttons / sizeof *buttons;
                             ++b)
                                if (b->button == event->button &&
                                    CLEANMASK(b->mod) == CLEANMASK(mods)) {
                                        b->func(c);
                                        return;
                                }
        }

        wlr_seat_pointer_notify_button(seat, event->time_msec,
                                       event->button, event->state);
}

static void cursor_axis_handler(struct wl_listener *unused, void *arg)
{
        struct wlr_pointer_axis_event *event = arg;

        (void)unused;

        wlr_seat_pointer_notify_axis(seat, event->time_msec,
                                     event->orientation, event->delta,
                                     event->delta_discrete, event->source,
                                     event->relative_direction);
}

static void cursor_frame_handler(struct wl_listener *unused, void *arg)
{
        (void)unused;
        (void)arg;

        wlr_seat_pointer_notify_frame(seat);
}

static void request_cursor_handler(struct wl_listener *unused, void *arg)
{
        struct wlr_seat_pointer_request_set_cursor_event *event = arg;

        (void)unused;

        /* only the pointer-focused client may set the image */
        if (event->seat_client == seat->pointer_state.focused_client)
                wlr_cursor_set_surface(cursor, event->surface,
                                       event->hotspot_x, event->hotspot_y);
}

static void new_vptr_handler(struct wl_listener *unused, void *arg)
{
        struct wlr_virtual_pointer_v1_new_pointer_event *event = arg;

        (void)unused;

        wlr_cursor_attach_input_device(cursor,
                                       &event->new_pointer->pointer.base);
}

static void cursor_init(void)
{
        cursor = wlr_cursor_create();
        wlr_cursor_attach_output_layout(cursor, output_layout);

        cursor_mgr = wlr_xcursor_manager_create(0, 24);

        LISTEN(&cursor->events.motion, &cursor_motion_listener,
               cursor_motion_handler);
        LISTEN(&cursor->events.motion_absolute,
               &cursor_motion_absolute_listener,
               cursor_motion_absolute_handler);
        LISTEN(&cursor->events.button, &cursor_button_listener,
               cursor_button_handler);
        LISTEN(&cursor->events.axis, &cursor_axis_listener,
               cursor_axis_handler);
        LISTEN(&cursor->events.frame, &cursor_frame_listener,
               cursor_frame_handler);

        LISTEN(&seat->events.request_set_cursor, &request_cursor_listener,
               request_cursor_handler);

        vptr_mgr = wlr_virtual_pointer_manager_v1_create(display);
        LISTEN(&vptr_mgr->events.new_virtual_pointer, &new_vptr_listener,
               new_vptr_handler);
}

/**********************************************************************/
/* Input                                                              */

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

/* an active menu owns every key; the repeat machinery serves both */
static int key_dispatch(uint32_t mods, xkb_keysym_t sym)
{
        return menu_active ? menu_key(mods, sym) : keybinding(mods, sym);
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
                        handled |= key_dispatch(mods, syms[i]);

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

        /* releases of keys the menu swallowed stay swallowed */
        if (menu_active)
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
                key_dispatch(kb->repeat_mods, kb->repeat_syms[i]);

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
        } else if (WLR_INPUT_DEVICE_POINTER == device->type) {
                wlr_cursor_attach_input_device(cursor, device);
        }
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
         * The group keyboard exists and the compositor draws the
         * cursor whether or not hardware ever shows up, so both
         * capabilities are unconditional — headless included.
         */
        wlr_seat_set_capabilities(seat, WL_SEAT_CAPABILITY_KEYBOARD |
                                                WL_SEAT_CAPABILITY_POINTER);
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

/*
 * The status feeder: the compositor's child, one line of stdout per
 * bar update — the xprop/root-WM_NAME transport is dead on Wayland,
 * the feeder script lives on.
 */
static int status_handler(int fd, uint32_t mask, void *unused)
{
        char buf[256];
        char *nl;
        ssize_t n;

        (void)unused;

        if (mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP)) {
                wl_event_source_remove(status_source);
                status_source = 0;
                close(fd);

                return 0;
        }

        n = read(fd, buf, sizeof buf - 1);
        if (n <= 0)
                return 0;

        buf[n] = 0;

        /* keep only the newest complete line */
        nl = strrchr(buf, '\n');
        if (nl) {
                *nl = 0;
                nl = strrchr(buf, '\n');
        }

        snprintf(status, sizeof status, "%s", nl ? nl + 1 : buf);
        drawbars();

        return 0;
}

static void status_spawn(void)
{
        int fds[2];

        if (0 == statuscmd[0] || pipe(fds) < 0)
                return;

        if (0 == fork()) {
                dup2(fds[1], STDOUT_FILENO);
                close(fds[0]);
                close(fds[1]);
                setsid();

                execvp(statuscmd[0], (char *const *)statuscmd);
                exit(1);
        }

        close(fds[1]);

        status_source = wl_event_loop_add_fd(event_loop, fds[0],
                                             WL_EVENT_READABLE,
                                             status_handler, 0);
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

        layer_tile = wlr_scene_tree_create(&scene->tree);
        layer_bar = wlr_scene_tree_create(&scene->tree);
        layer_fs = wlr_scene_tree_create(&scene->tree);

        if (!fcft_init(FCFT_LOG_COLORIZE_NEVER, 0, FCFT_LOG_CLASS_ERROR))
                die("fcft_init failed");

        font = fcft_from_name(1, &fontname, 0);
        if (0 == font)
                die("fcft_from_name failed");

        status_spawn();

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

        LISTEN(&wlr_xdg_activation_v1_create(display)->events
                        .request_activate,
               &request_activate_listener, request_activate_handler);
        LISTEN(&wlr_xdg_system_bell_v1_create(display, 1)->events.ring,
               &bell_ring_listener, bell_ring_handler);

        seat = wlr_seat_create(display, "seat0");

        LISTEN(&backend->events.new_input, &new_input_listener,
               new_input_handler);

        vkbd_mgr = wlr_virtual_keyboard_manager_v1_create(display);
        LISTEN(&vkbd_mgr->events.new_virtual_keyboard, &new_vkbd_listener,
               new_vkbd_handler);

        keyboard_init();
        cursor_init();
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
        wl_list_remove(&new_vptr_listener.link);
        wl_list_remove(&cursor_motion_listener.link);
        wl_list_remove(&cursor_motion_absolute_listener.link);
        wl_list_remove(&cursor_button_listener.link);
        wl_list_remove(&cursor_axis_listener.link);
        wl_list_remove(&cursor_frame_listener.link);
        wl_list_remove(&request_cursor_listener.link);
        wl_list_remove(&request_activate_listener.link);
        wl_list_remove(&bell_ring_listener.link);

        keyboard_destroy(kb_main);
        xkb_keymap_unref(keymap);

        if (status_source)
                wl_event_source_remove(status_source);

        fcft_destroy(font);
        fcft_fini();

        wlr_backend_destroy(backend);
        wlr_cursor_destroy(cursor);
        wlr_xcursor_manager_destroy(cursor_mgr);
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
