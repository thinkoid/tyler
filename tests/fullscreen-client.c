/* -*- mode: c; -*- */

/*
 * Fullscreen modality and monocle oracles, by OpenAI LeChuck.
 * Two headless outputs, real xdg toplevels and virtual input. Verdicts
 * use wl_keyboard.enter/leave and xdg configure state, not activation
 * colors or compositor internals. No part of this client is installed.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

struct window {
        struct wl_surface *surface;
        struct xdg_surface *xdg;
        struct xdg_toplevel *top;
        const char *name;
        int width, height, full;
};

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct zwp_pointer_constraints_v1 *constraints;
static struct xdg_wm_base *wm;
static struct zwp_virtual_keyboard_manager_v1 *km;
static struct zwlr_virtual_pointer_manager_v1 *pm;
static struct zwp_virtual_keyboard_v1 *vk;
static struct zwlr_virtual_pointer_v1 *vp;
static struct xkb_state *keys;
static struct wl_surface *focused, *pointed;
static unsigned enters, ticks, motions;
static int locked;

static void require(int ok, const char *message)
{
        if (!ok) {
                fprintf(stderr, "fullscreen oracle: %s\n", message);
                exit(1);
        }
}

/* Round trips drain events; delays allow scheduled configures to run. */
static void settle(void)
{
        int i;
        for (i = 0; i != 15; ++i) {
                require(wl_display_roundtrip(display) >= 0, "connection lost");
                usleep(10000);
        }
}

static void expect(struct window *w, const char *message)
{
        settle();
        require(focused == (w ? w->surface : 0), message);
        printf("ok: %s\n", message);
        fflush(stdout);
}

static void buffer_release(void *data, struct wl_buffer *buffer)
{
        (void)data;
        wl_buffer_destroy(buffer);
}
static const struct wl_buffer_listener buffer_listener = { buffer_release };

static void configure(void *data, struct xdg_surface *xdg, uint32_t serial)
{
        struct window *w = data;
        int size = w->width * w->height * 4;
        int fd = memfd_create("fullscreen-oracle", MFD_CLOEXEC);
        struct wl_shm_pool *pool;
        struct wl_buffer *buffer;
        uint32_t *pixels;
        int i;

        require(fd >= 0 && ftruncate(fd, size) == 0, "shm allocation");
        pixels = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        require(pixels != MAP_FAILED, "shm mapping");
        for (i = 0; i < size / 4; ++i)
                pixels[i] = 0xff304050 + (unsigned char)w->name[0] * 0x010101;
        pool = wl_shm_create_pool(shm, fd, size);
        buffer = wl_shm_pool_create_buffer(pool, 0, w->width, w->height,
                                           w->width * 4, WL_SHM_FORMAT_XRGB8888);
        wl_buffer_add_listener(buffer, &buffer_listener, 0);
        wl_shm_pool_destroy(pool);
        munmap(pixels, size);
        close(fd);
        xdg_surface_ack_configure(xdg, serial);
        wl_surface_attach(w->surface, buffer, 0, 0);
        wl_surface_damage_buffer(w->surface, 0, 0, w->width, w->height);
        wl_surface_commit(w->surface);
}
static const struct xdg_surface_listener surface_listener = { configure };

static void top_configure(void *data, struct xdg_toplevel *top,
                           int32_t width, int32_t height, struct wl_array *states)
{
        struct window *w = data;
        uint32_t *state;
        (void)top;
        if (width > 0 && height > 0) {
                w->width = width;
                w->height = height;
        }
        w->full = 0;
        wl_array_for_each(state, states)
                if (*state == XDG_TOPLEVEL_STATE_FULLSCREEN)
                        w->full = 1;
}
static void top_close(void *data, struct xdg_toplevel *top)
{
        (void)data; (void)top;
        require(0, "unexpected close");
}
static const struct xdg_toplevel_listener top_listener = {
        .configure = top_configure, .close = top_close
};

static void open_window(struct window *w, const char *name, struct window *parent)
{
        *w = (struct window){ .name = name, .width = 300, .height = 200 };
        w->surface = wl_compositor_create_surface(compositor);
        w->xdg = xdg_wm_base_get_xdg_surface(wm, w->surface);
        xdg_surface_add_listener(w->xdg, &surface_listener, w);
        w->top = xdg_surface_get_toplevel(w->xdg);
        xdg_toplevel_add_listener(w->top, &top_listener, w);
        xdg_toplevel_set_title(w->top, name);
        if (parent)
                xdg_toplevel_set_parent(w->top, parent->top);
        wl_surface_commit(w->surface);
        settle();
}

static void close_window(struct window *w)
{
        xdg_toplevel_destroy(w->top);
        xdg_surface_destroy(w->xdg);
        wl_surface_destroy(w->surface);
        settle();
}

static void ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
        (void)data;
        xdg_wm_base_pong(base, serial);
}
static const struct xdg_wm_base_listener wm_listener = { ping };

static void keymap(void *data, struct wl_keyboard *kb, uint32_t format,
                    int32_t fd, uint32_t size)
{
        (void)data; (void)kb; (void)format; (void)size;
        close(fd);
}
static void enter(void *data, struct wl_keyboard *kb, uint32_t serial,
                   struct wl_surface *surface, struct wl_array *pressed)
{
        (void)data; (void)kb; (void)serial; (void)pressed;
        focused = surface;
        ++enters;
}
static void leave(void *data, struct wl_keyboard *kb, uint32_t serial,
                   struct wl_surface *surface)
{
        (void)data; (void)kb; (void)serial; (void)surface;
        focused = 0;
}
static void key(void *data, struct wl_keyboard *kb, uint32_t serial,
                 uint32_t time, uint32_t code, uint32_t state)
{
        (void)data; (void)kb; (void)serial; (void)time; (void)code; (void)state;
}
static void modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
                       uint32_t depressed, uint32_t latched, uint32_t locked,
                       uint32_t group)
{
        (void)data; (void)kb; (void)serial; (void)depressed;
        (void)latched; (void)locked; (void)group;
}
static const struct wl_keyboard_listener keyboard_listener = {
        .keymap = keymap, .enter = enter, .leave = leave,
        .key = key, .modifiers = modifiers
};

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
                           struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
        (void)data; (void)p; (void)serial; (void)x; (void)y;
        pointed = surface;
}
static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
                           struct wl_surface *surface)
{
        (void)data; (void)p; (void)serial; (void)surface;
        pointed = 0;
}
static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
                            wl_fixed_t x, wl_fixed_t y)
{
        (void)data; (void)p; (void)time; (void)x; (void)y;
        ++motions;
}
static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
                            uint32_t time, uint32_t button, uint32_t state)
{
        (void)data; (void)p; (void)serial; (void)time; (void)button; (void)state;
}
static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time,
                          uint32_t axis, wl_fixed_t value)
{
        (void)data; (void)p; (void)time; (void)axis; (void)value;
}
static const struct wl_pointer_listener pointer_listener = {
        .enter = pointer_enter, .leave = pointer_leave,
        .motion = pointer_motion, .button = pointer_button, .axis = pointer_axis
};

static void on_locked(void *data, struct zwp_locked_pointer_v1 *lock)
{
        (void)data; (void)lock;
        locked = 1;
}
static void on_unlocked(void *data, struct zwp_locked_pointer_v1 *lock)
{
        (void)data; (void)lock;
        locked = 0;
}
static const struct zwp_locked_pointer_v1_listener lock_listener = {
        on_locked, on_unlocked
};

static void global(void *data, struct wl_registry *registry, uint32_t name,
                    const char *iface, uint32_t version)
{
        (void)data; (void)version;
#define BIND(var, interface, ver) \
        if (strcmp(iface, interface.name) == 0) \
                var = wl_registry_bind(registry, name, &interface, ver)
        BIND(compositor, wl_compositor_interface, 4);
        else BIND(shm, wl_shm_interface, 1);
        else BIND(seat, wl_seat_interface, 1);
        else BIND(wm, xdg_wm_base_interface, 1);
        else BIND(km, zwp_virtual_keyboard_manager_v1_interface, 1);
        else BIND(pm, zwlr_virtual_pointer_manager_v1_interface, 1);
        else BIND(constraints, zwp_pointer_constraints_v1_interface, 1);
#undef BIND
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
        (void)data; (void)registry; (void)name;
}
static const struct wl_registry_listener registry_listener = { global, global_remove };

static void send_key(uint32_t code, int down)
{
        xkb_state_update_key(keys, code + 8, down ? XKB_KEY_DOWN : XKB_KEY_UP);
        zwp_virtual_keyboard_v1_modifiers(vk,
                xkb_state_serialize_mods(keys, XKB_STATE_MODS_DEPRESSED), 0, 0, 0);
        zwp_virtual_keyboard_v1_key(vk, ++ticks, code, down);
}
static void chord(uint32_t code)
{
        send_key(KEY_LEFTALT, 1);
        send_key(code, 1);
        send_key(code, 0);
        send_key(KEY_LEFTALT, 0);
        settle();
}
static void move(uint32_t x, uint32_t y)
{
        zwlr_virtual_pointer_v1_motion_absolute(vp, ++ticks, x, y, 2560, 720);
        zwlr_virtual_pointer_v1_frame(vp);
        settle();
}
static void blocked(struct window *w, uint32_t code, const char *message)
{
        unsigned before = enters;
        chord(code);
        expect(w, message);
        require(enters == before, "covered window briefly received keyboard focus");
}

/* Check layout behavior through client sizes and the pointer's top surface. */
static void expect_size(struct window *w, int width, int height,
                         const char *message)
{
        settle();
        require(w->width == width && w->height == height, message);
        printf("ok: %s\n", message);
        fflush(stdout);
}

static void test_monocle(void)
{
        struct window a, b, floating, other_a, other_b, tag_a, tag_b;
        int width, height, aw, ah, bw, bh, ow, oh;

        move(100, 100);
        chord(KEY_T);
        chord(KEY_F);
        chord(KEY_M);
        chord(KEY_M);
        open_window(&a, "A", 0);
        width = a.width;
        height = a.height;
        open_window(&b, "B", 0);
        aw = a.width; ah = a.height;
        bw = b.width; bh = b.height;
        require(aw < width && bw < width, "two windows start in master/stack");

        chord(KEY_M);
        expect(&b, "monocle keeps the selected tile focused");
        expect_size(&a, width, height, "monocle expands A to the single-tile area");
        expect_size(&b, width, height, "monocle expands B to the same area");
        require(!a.full && !b.full, "monocle does not set fullscreen state");
        move(640, 360);
        require(pointed == b.surface, "focused B is visible on top");
        chord(KEY_J);
        expect(&a, "Alt+j cycles freely in monocle");
        require(pointed == a.surface, "cycling raises A under a stationary pointer");
        chord(KEY_TAB);
        expect(&b, "Alt+Tab switches monocle tiles");
        require(pointed == b.surface, "Alt+Tab raises B");

        chord(KEY_M);
        expect_size(&a, aw, ah, "second Alt+m restores A's tiled geometry");
        expect_size(&b, bw, bh, "second Alt+m restores B's tiled geometry");
        chord(KEY_T);
        expect_size(&a, width, height, "Alt+t removes only B from tiling");
        expect_size(&b, bw, bh, "floating B retains its geometry");
        chord(KEY_F);
        expect(&b, "Alt+f enters fullscreen on floating B");
        require(b.full && b.width == 1280 && b.height == 720,
                "fullscreen covers the entire output");
        chord(KEY_T);
        require(b.full, "Alt+t does not change fullscreen state");
        chord(KEY_F);
        require(!b.full, "second Alt+f exits fullscreen");
        expect_size(&b, bw, bh, "fullscreen exit restores floating geometry");
        expect_size(&a, width, height, "fullscreen exit keeps B out of tiling");
        chord(KEY_T);
        expect_size(&a, aw, ah, "second Alt+t restores A's tile");
        expect_size(&b, bw, bh, "second Alt+t returns B to tiling");

        chord(KEY_M);
        open_window(&floating, "floating", &b);
        expect_size(&floating, 300, 200, "monocle leaves a floating window's size alone");
        chord(KEY_M);
        expect_size(&floating, 300, 200, "leaving monocle preserves floating geometry");
        chord(KEY_M);
        chord(KEY_T);
        expect_size(&floating, width, height, "Alt+t joins a float to monocle");
        chord(KEY_T);
        chord(KEY_M);
        expect_size(&a, aw, ah, "Alt+t removes the floated window from the layout");
        expect_size(&floating, width, height, "floating window stays unchanged on layout switch");
        close_window(&floating);
        expect(&b, "closing float restores B");

        chord(KEY_M);
        chord(KEY_F);
        require(b.full, "fullscreen can be entered from monocle");
        blocked(&b, KEY_J, "fullscreen modality still blocks monocle cycling");
        chord(KEY_F);
        expect_size(&b, width, height, "fullscreen exit returns to monocle");
        require(!b.full, "monocle after fullscreen is not fullscreen");
        chord(KEY_F);
        chord(KEY_M);
        require(b.full && b.width == 1280 && b.height == 720,
                "layout toggle does not disturb fullscreen");
        chord(KEY_F);
        expect_size(&b, bw, bh, "fullscreen exit uses the newly selected tiled layout");

        chord(KEY_M);
        chord(KEY_DOT);
        open_window(&other_a, "other A", 0);
        open_window(&other_b, "other B", 0);
        ow = other_a.width; oh = other_a.height;
        require(ow < width && other_b.width < width,
                "monocle is local to its monitor");
        expect_size(&a, width, height, "other monitor activity preserves monocle");
        chord(KEY_M);
        expect_size(&other_a, width, height, "second monitor can independently enter monocle");
        chord(KEY_COMMA);
        expect(&b, "return to primary monitor restores focused monocle tile");
        chord(KEY_M);
        expect_size(&a, aw, ah, "primary monitor can independently leave monocle");
        expect_size(&other_a, width, height, "second monitor remains in monocle");

        chord(KEY_2);
        open_window(&tag_a, "tag A", 0);
        open_window(&tag_b, "tag B", 0);
        require(tag_a.width < width && tag_b.width < width,
                "new tag uses current monitor layout");
        chord(KEY_M);
        expect_size(&tag_a, width, height, "monocle applies to the viewed tag");
        chord(KEY_1);
        expect_size(&a, width, height, "monitor layout follows tag switching");
        expect_size(&b, width, height, "returning tag arranges every tile");
        move(640, 360);
        require(pointed == b.surface, "returning tag shows its focused tile");
        chord(KEY_2);
        close_window(&tag_b);
        expect(&tag_a, "closing top monocle window reveals the previous tile");
        close_window(&tag_a);
        chord(KEY_1);
        close_window(&b);
        expect(&a, "last monocle tile remains usable");
        close_window(&a);
        chord(KEY_M);
        open_window(&a, "new A", 0);
        open_window(&b, "new B", 0);
        require(a.width < width && b.width < width,
                "monocle can be toggled off on an empty tag");
        close_window(&b);
        close_window(&a);
        chord(KEY_DOT);
        chord(KEY_M);
        expect_size(&other_a, ow, oh, "second monitor restores its original layout");
        close_window(&other_b);
        close_window(&other_a);
        puts("monocle oracle: PASS");
}

int main(int argc, char **argv)
{
        struct window a, b, c, dialog, other, second;
        struct wl_registry *registry;
        struct wl_keyboard *keyboard;
        struct wl_pointer *pointer;
        struct zwp_locked_pointer_v1 *lock;
        unsigned before;
        struct xkb_context *context;
        struct xkb_keymap *map;
        char *text;
        int fd;
        size_t size;

        require(argc == 1 || (argc == 2 && strcmp(argv[1], "--monocle") == 0),
                "usage: fullscreen-client [--monocle]");
        display = wl_display_connect(0);
        require(display != 0, "connect");
        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &registry_listener, 0);
        settle();
        require(compositor && shm && seat && wm && km && pm && constraints,
                "required globals");
        xdg_wm_base_add_listener(wm, &wm_listener, 0);
        vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(km, seat);
        vp = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(pm, seat);
        context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        map = xkb_keymap_new_from_names(context, 0, XKB_KEYMAP_COMPILE_NO_FLAGS);
        require(map != 0, "keymap");
        keys = xkb_state_new(map);
        text = xkb_keymap_get_as_string(map, XKB_KEYMAP_FORMAT_TEXT_V1);
        size = strlen(text) + 1;
        fd = memfd_create("fullscreen-keys", MFD_CLOEXEC);
        require(fd >= 0 && write(fd, text, size) == (ssize_t)size, "keymap fd");
        zwp_virtual_keyboard_v1_keymap(vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
        close(fd);
        free(text);
        xkb_keymap_unref(map);
        xkb_context_unref(context);
        settle();
        keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(keyboard, &keyboard_listener, 0);
        pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(pointer, &pointer_listener, 0);

        if (argc == 2) {
                test_monocle();
                wl_display_disconnect(display);
                return 0;
        }

        move(100, 100);
        open_window(&a, "A", 0);
        open_window(&b, "B", 0);
        expect(&b, "ordinary mapping focuses B");
        move(100, 100);
        require(pointed == b.surface, "pointer initially on B");
        xdg_toplevel_set_fullscreen(a.top, 0);
        expect(&a, "inactive A entering fullscreen takes focus");
        require(a.full, "A received fullscreen configure");
        require(pointed == a.surface, "stationary pointer follows fullscreen entry");
        blocked(&a, KEY_TAB, "Alt+Tab cannot focus covered B");
        blocked(&a, KEY_J, "Alt+j cannot focus covered B");
        blocked(&a, KEY_K, "Alt+k cannot focus covered B");
        lock = zwp_pointer_constraints_v1_lock_pointer(constraints, a.surface,
                pointer, 0, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        zwp_locked_pointer_v1_add_listener(lock, &lock_listener, 0);
        wl_surface_commit(a.surface);
        settle();
        require(locked, "fullscreen pointer lock activates");
        before = motions;
        blocked(&a, KEY_TAB, "Alt+Tab preserves fullscreen pointer lock");
        require(locked && motions == before,
                "blocked switch must not unlock or send absolute motion");
        zwp_locked_pointer_v1_destroy(lock);
        settle();
        open_window(&c, "C", 0);
        expect(&a, "new unrelated C cannot steal fullscreen focus");
        move(640, 360);
        expect(&a, "pointer stays on fullscreen A");
        require(pointed == a.surface, "A is above covered clients");

        open_window(&dialog, "dialog", &a);
        expect(&dialog, "fullscreen dialog takes focus");
        move(641, 361);
        require(pointed == dialog.surface, "dialog is drawn above fullscreen A");
        blocked(&dialog, KEY_TAB, "Alt+Tab cannot bury fullscreen dialog");
        move(20, 100);
        expect(&dialog, "pointer over parent cannot steal dialog focus");
        require(pointed == 0, "modal dialog blocks pointer input to parent");
        close_window(&dialog);
        expect(&a, "closing dialog returns focus to fullscreen A");

        chord(KEY_DOT);
        open_window(&other, "other monitor", 0);
        expect(&other, "other monitor remains usable");
        chord(KEY_COMMA);
        expect(&a, "returning monitor selects fullscreen A");
        chord(KEY_2);
        open_window(&second, "second tag", 0);
        expect(&second, "other tag remains usable");
        chord(KEY_1);
        expect(&a, "returning tag selects fullscreen A");

        chord(KEY_F);
        expect(&a, "Alt+f exits fullscreen with A focused");
        require(!a.full, "Alt+f sent non-fullscreen configure");
        chord(KEY_J);
        require(focused != a.surface, "cycling resumes after fullscreen exit");
        printf("ok: cycling resumes after fullscreen exit\n");

        open_window(&dialog, "existing dialog", &a);
        xdg_toplevel_set_fullscreen(a.top, 0);
        expect(&dialog, "existing dialog stays above entering fullscreen parent");
        close_window(&dialog);
        expect(&a, "parent resumes after existing dialog closes");
        xdg_toplevel_set_fullscreen(b.top, 0);
        expect(&b, "new top fullscreen B takes focus");
        blocked(&b, KEY_TAB, "lower fullscreen A cannot take focus");
        close_window(&b);
        expect(&a, "closing top fullscreen restores lower fullscreen");
        open_window(&dialog, "orphan dialog", &a);
        expect(&dialog, "dialog before parent unmap takes focus");
        xdg_toplevel_set_parent(dialog.top, 0);
        expect(&a, "detaching dialog restores fullscreen focus");
        xdg_toplevel_set_parent(dialog.top, a.top);
        expect(&dialog, "reattaching mapped dialog promotes it");
        close_window(&a);
        expect(&dialog, "parent unmap leaves its dialog usable");
        close_window(&dialog);
        expect(&c, "closing last fullscreen restores normal focus");
        close_window(&c);
        chord(KEY_2);
        close_window(&second);
        chord(KEY_DOT);
        close_window(&other);
        wl_display_disconnect(display);
        puts("fullscreen oracle: PASS");
        return 0;
}
