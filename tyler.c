/* -*- mode: c; -*- */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <wayland-server-core.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/util/log.h>

/*
 * Unused as yet; proves the wayland-scanner rigging end to end.
 */
#include <xdg-shell-protocol.h>

static struct wl_display *display;
static struct wl_event_loop *event_loop;

static struct wlr_session *session;
static struct wlr_backend *backend;
static struct wlr_renderer *renderer;
static struct wlr_allocator *allocator;

static struct wlr_output_layout *output_layout;

static struct wl_listener new_output_listener;

static void die(const char *s)
{
        fprintf(stderr, "tyler: %s\n", s);
        exit(1);
}

static void new_output_handler(struct wl_listener *unused, void *arg)
{
        struct wlr_output *out = arg;
        struct wlr_output_state state;

        (void)unused;

        wlr_output_init_render(out, allocator, renderer);

        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, 1);

        if (wlr_output_preferred_mode(out))
                wlr_output_state_set_mode(&state,
                                          wlr_output_preferred_mode(out));

        wlr_output_commit_state(out, &state);
        wlr_output_state_finish(&state);

        wlr_output_layout_add_auto(output_layout, out);

        wlr_log(WLR_INFO, "output %s: %dx%d",
                out->name, out->width, out->height);
}

static int terminate_handler(int signo, void *unused)
{
        (void)unused;

        wlr_log(WLR_INFO, "caught signal %d, terminating", signo);
        wl_display_terminate(display);

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

        backend = wlr_backend_autocreate(event_loop, &session);
        if (0 == backend)
                die("wlr_backend_autocreate failed");

        renderer = wlr_renderer_autocreate(backend);
        if (0 == renderer)
                die("wlr_renderer_autocreate failed");

        allocator = wlr_allocator_autocreate(backend, renderer);
        if (0 == allocator)
                die("wlr_allocator_autocreate failed");

        wlr_compositor_create(display, 6, renderer);
        wlr_subcompositor_create(display);
        wlr_data_device_manager_create(display);

        output_layout = wlr_output_layout_create(display);

        new_output_listener.notify = new_output_handler;
        wl_signal_add(&backend->events.new_output, &new_output_listener);
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

        wlr_backend_destroy(backend);
        wl_display_destroy(display);
}

int main(void)
{
        wlr_log_init(WLR_INFO, 0);

        init();
        run();
        fini();

        return 0;
}
