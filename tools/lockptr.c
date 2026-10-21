/* -*- mode: c; -*- */

/*
 * lockptr — the pointer-lock oracle's client. Maps a toplevel, and on
 * the pointer's first entry locks it there (zwp_pointer_constraints_v1)
 * and asks for relative motion (zwp_relative_pointer_v1). Every event of
 * interest goes to stdout, one word a line, so a script can count:
 *
 *      mapped enter leave motion locked unlocked relative
 *
 * SIGTERM, or the deadline, releases the lock with a cursor hint at the
 * surface's centre and exits 0; the script owns the verdict.
 *
 *      lockptr [DEADLINE_MS]           (default 10000)
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include <pointer-constraints-unstable-v1-client-protocol.h>
#include <relative-pointer-unstable-v1-client-protocol.h>
#include <xdg-shell-client-protocol.h>

/* the window takes whatever size the compositor configures */
static int width = 256, height = 256, resize;

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct xdg_wm_base *wm_base;
static struct zwp_pointer_constraints_v1 *constraints;
static struct zwp_relative_pointer_manager_v1 *relative_mgr;

static struct wl_surface *surface;
static struct wl_pointer *pointer;
static struct zwp_locked_pointer_v1 *lock;

static int configured;
static volatile sig_atomic_t stop;

static void die(const char *s)
{
        fprintf(stderr, "lockptr: %s\n", s);
        exit(1);
}

static void say(const char *s)
{
        puts(s);
        fflush(stdout);
}

static struct wl_buffer *make_buffer(void);

/* a fresh buffer at the current size, the old one released by the server */
static void draw(void)
{
        wl_surface_attach(surface, make_buffer(), 0, 0);
        wl_surface_damage_buffer(surface, 0, 0, width, height);
        wl_surface_commit(surface);
        resize = 0;
}

static void on_term(int unused)
{
        (void)unused;
        stop = 1;
}

/* xdg-shell */

static void wm_base_ping(void *unused, struct xdg_wm_base *base,
                         uint32_t serial)
{
        (void)unused;
        xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = { wm_base_ping };

static void xdg_surface_configure(void *unused, struct xdg_surface *xs,
                                  uint32_t serial)
{
        (void)unused;
        xdg_surface_ack_configure(xs, serial);
        configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
        xdg_surface_configure
};

static void toplevel_configure(void *unused, struct xdg_toplevel *t,
                               int32_t w, int32_t h, struct wl_array *states)
{
        (void)unused;
        (void)t;
        (void)states;

        if (0 < w && 0 < h && (w != width || h != height)) {
                width = w;
                height = h;
                resize = 1;
        }
}

static void toplevel_close(void *unused, struct xdg_toplevel *t)
{
        (void)unused;
        (void)t;
        stop = 1;
}

static void toplevel_bounds(void *unused, struct xdg_toplevel *t, int32_t w,
                            int32_t h)
{
        (void)unused;
        (void)t;
        (void)w;
        (void)h;
}

static void toplevel_caps(void *unused, struct xdg_toplevel *t,
                          struct wl_array *caps)
{
        (void)unused;
        (void)t;
        (void)caps;
}

static const struct xdg_toplevel_listener toplevel_listener = {
        toplevel_configure, toplevel_close, toplevel_bounds, toplevel_caps
};

/* the lock */

static void locked(void *unused, struct zwp_locked_pointer_v1 *l)
{
        (void)unused;
        (void)l;
        say("locked");
}

static void unlocked(void *unused, struct zwp_locked_pointer_v1 *l)
{
        (void)unused;
        (void)l;
        say("unlocked");
}

static const struct zwp_locked_pointer_v1_listener lock_listener = {
        locked, unlocked
};

static void relative_motion(void *unused, struct zwp_relative_pointer_v1 *rp,
                            uint32_t hi, uint32_t lo, wl_fixed_t dx,
                            wl_fixed_t dy, wl_fixed_t udx, wl_fixed_t udy)
{
        char line[64];

        (void)unused;
        (void)rp;
        (void)hi;
        (void)lo;
        (void)udx;
        (void)udy;

        snprintf(line, sizeof line, "relative %d %d", wl_fixed_to_int(dx),
                 wl_fixed_to_int(dy));
        say(line);
}

static const struct zwp_relative_pointer_v1_listener relative_listener = {
        relative_motion
};

/* the pointer */

static void pointer_enter(void *unused, struct wl_pointer *p, uint32_t serial,
                          struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
        (void)unused;
        (void)p;
        (void)serial;
        (void)s;
        (void)x;
        (void)y;

        say("enter");

        if (0 == lock) {
                lock = zwp_pointer_constraints_v1_lock_pointer(
                        constraints, surface, pointer, 0,
                        ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
                zwp_locked_pointer_v1_add_listener(lock, &lock_listener, 0);
                wl_surface_commit(surface);
        }
}

static void pointer_leave(void *unused, struct wl_pointer *p, uint32_t serial,
                          struct wl_surface *s)
{
        (void)unused;
        (void)p;
        (void)serial;
        (void)s;
        say("leave");
}

static void pointer_motion(void *unused, struct wl_pointer *p, uint32_t time,
                           wl_fixed_t x, wl_fixed_t y)
{
        (void)unused;
        (void)p;
        (void)time;
        (void)x;
        (void)y;
        say("motion");
}

static void pointer_button(void *unused, struct wl_pointer *p,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state)
{
        (void)unused;
        (void)p;
        (void)serial;
        (void)time;
        (void)button;
        (void)state;
}

static void pointer_axis(void *unused, struct wl_pointer *p, uint32_t time,
                         uint32_t axis, wl_fixed_t value)
{
        (void)unused;
        (void)p;
        (void)time;
        (void)axis;
        (void)value;
}

static void pointer_frame(void *unused, struct wl_pointer *p)
{
        (void)unused;
        (void)p;
}

static void pointer_axis_source(void *unused, struct wl_pointer *p,
                                uint32_t source)
{
        (void)unused;
        (void)p;
        (void)source;
}

static void pointer_axis_stop(void *unused, struct wl_pointer *p,
                              uint32_t time, uint32_t axis)
{
        (void)unused;
        (void)p;
        (void)time;
        (void)axis;
}

static void pointer_axis_discrete(void *unused, struct wl_pointer *p,
                                  uint32_t axis, int32_t discrete)
{
        (void)unused;
        (void)p;
        (void)axis;
        (void)discrete;
}

static void pointer_axis_value120(void *unused, struct wl_pointer *p,
                                  uint32_t axis, int32_t value120)
{
        (void)unused;
        (void)p;
        (void)axis;
        (void)value120;
}

static void pointer_axis_relative_direction(void *unused,
                                            struct wl_pointer *p,
                                            uint32_t axis, uint32_t dir)
{
        (void)unused;
        (void)p;
        (void)axis;
        (void)dir;
}

/* designated: wl_pointer grows an event now and then (warp, 1.26) */
static const struct wl_pointer_listener pointer_listener = {
        .enter = pointer_enter,
        .leave = pointer_leave,
        .motion = pointer_motion,
        .button = pointer_button,
        .axis = pointer_axis,
        .frame = pointer_frame,
        .axis_source = pointer_axis_source,
        .axis_stop = pointer_axis_stop,
        .axis_discrete = pointer_axis_discrete,
        .axis_value120 = pointer_axis_value120,
        .axis_relative_direction = pointer_axis_relative_direction,
};

static void seat_capabilities(void *unused, struct wl_seat *s, uint32_t caps)
{
        (void)unused;

        if ((caps & WL_SEAT_CAPABILITY_POINTER) && 0 == pointer) {
                pointer = wl_seat_get_pointer(s);
                wl_pointer_add_listener(pointer, &pointer_listener, 0);
                say("pointer");
        }
}

static void seat_name(void *unused, struct wl_seat *s, const char *name)
{
        (void)unused;
        (void)s;
        (void)name;
}

static const struct wl_seat_listener seat_listener = {
        seat_capabilities, seat_name
};

/* registry */

static void registry_global(void *unused, struct wl_registry *registry,
                            uint32_t name, const char *iface,
                            uint32_t version)
{
        (void)unused;
        (void)version;

        if (0 == strcmp(iface, wl_compositor_interface.name))
                compositor = wl_registry_bind(registry, name,
                                              &wl_compositor_interface, 4);
        else if (0 == strcmp(iface, wl_shm_interface.name))
                shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        else if (0 == strcmp(iface, wl_seat_interface.name)) {
                seat = wl_registry_bind(registry, name, &wl_seat_interface,
                                        5);
                wl_seat_add_listener(seat, &seat_listener, 0);
        } else if (0 == strcmp(iface, xdg_wm_base_interface.name)) {
                wm_base = wl_registry_bind(registry, name,
                                           &xdg_wm_base_interface, 1);
                xdg_wm_base_add_listener(wm_base, &wm_base_listener, 0);
        }
        else if (0 == strcmp(iface,
                             zwp_pointer_constraints_v1_interface.name))
                constraints = wl_registry_bind(
                        registry, name, &zwp_pointer_constraints_v1_interface,
                        1);
        else if (0 == strcmp(
                         iface,
                         zwp_relative_pointer_manager_v1_interface.name))
                relative_mgr = wl_registry_bind(
                        registry, name,
                        &zwp_relative_pointer_manager_v1_interface, 1);
}

static void registry_global_remove(void *unused,
                                   struct wl_registry *registry,
                                   uint32_t name)
{
        (void)unused;
        (void)registry;
        (void)name;
}

static const struct wl_registry_listener registry_listener = {
        registry_global, registry_global_remove
};

static struct wl_buffer *make_buffer(void)
{
        const int stride = width * 4, size = stride * height;
        struct wl_shm_pool *pool;
        struct wl_buffer *buffer;
        uint32_t *px;
        int fd, i;

        fd = memfd_create("lockptr", MFD_CLOEXEC);
        if (fd < 0 || ftruncate(fd, size) < 0)
                die("memfd");

        px = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (MAP_FAILED == px)
                die("mmap");
        for (i = 0; i < width * height; ++i)
                px[i] = 0xff336699;
        munmap(px, size);

        pool = wl_shm_create_pool(shm, fd, size);
        buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                                           WL_SHM_FORMAT_XRGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);

        return buffer;
}

int main(int argc, char **argv)
{
        struct wl_display *display;
        struct wl_registry *registry;
        struct xdg_surface *xs;
        struct xdg_toplevel *toplevel;
        long deadline = argc > 1 ? strtol(argv[1], 0, 10) : 10000;
        int fd, elapsed = 0;

        signal(SIGTERM, on_term);
        signal(SIGINT, on_term);

        display = wl_display_connect(0);
        if (0 == display)
                die("wl_display_connect failed");

        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &registry_listener, 0);
        wl_display_roundtrip(display);  /* the globals */
        wl_display_roundtrip(display);  /* the seat's capabilities */

        if (0 == compositor || 0 == shm || 0 == seat || 0 == wm_base)
                die("no compositor, shm, seat or xdg_wm_base");
        if (0 == constraints || 0 == relative_mgr)
                die("no zwp_pointer_constraints_v1 or "
                    "zwp_relative_pointer_manager_v1");
        if (0 == pointer)
                die("the seat offered no pointer");

        zwp_relative_pointer_v1_add_listener(
                zwp_relative_pointer_manager_v1_get_relative_pointer(
                        relative_mgr, pointer),
                &relative_listener, 0);

        surface = wl_compositor_create_surface(compositor);
        xs = xdg_wm_base_get_xdg_surface(wm_base, surface);
        xdg_surface_add_listener(xs, &xdg_surface_listener, 0);
        toplevel = xdg_surface_get_toplevel(xs);
        xdg_toplevel_add_listener(toplevel, &toplevel_listener, 0);
        xdg_toplevel_set_title(toplevel, "lockptr");
        wl_surface_commit(surface);

        while (!configured)
                if (wl_display_dispatch(display) < 0)
                        die("dispatch");

        draw();
        wl_display_roundtrip(display);
        if (resize)                     /* the tile's size came back */
                draw();
        wl_display_roundtrip(display);
        say("mapped");

        /* dispatch with a 50 ms heartbeat so SIGTERM and the deadline bite */
        fd = wl_display_get_fd(display);
        while (!stop && elapsed < deadline) {
                struct pollfd pfd = { fd, POLLIN, 0 };

                while (wl_display_prepare_read(display) != 0)
                        wl_display_dispatch_pending(display);
                wl_display_flush(display);

                if (poll(&pfd, 1, 50) > 0)
                        wl_display_read_events(display);
                else
                        wl_display_cancel_read(display);
                wl_display_dispatch_pending(display);
                if (resize)
                        draw();

                elapsed += 50;
        }

        if (lock) {
                zwp_locked_pointer_v1_set_cursor_position_hint(
                        lock, wl_fixed_from_int(width / 2),
                        wl_fixed_from_int(height / 2));
                wl_surface_commit(surface);
                zwp_locked_pointer_v1_destroy(lock);
        }
        xdg_toplevel_destroy(toplevel);
        xdg_surface_destroy(xs);
        wl_surface_destroy(surface);
        wl_display_roundtrip(display);
        wl_display_disconnect(display);

        say("done");
        return 0;
}
