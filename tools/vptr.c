/* -*- mode: c; -*- */

/*
 * vptr — pointer injector for the tyler oracle. Speaks
 * wlr-virtual-pointer-unstable-v1:
 *
 *      vptr XEXT YEXT [move X Y] [click left|right|middle] ...
 *
 * Absolute motion lands at (X/XEXT, Y/YEXT) of the whole output
 * layout, so pass the layout size as the extents and think in layout
 * coordinates. A settle delay follows every command.
 */

#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>

#include <wlr-virtual-pointer-unstable-v1-client-protocol.h>

static struct wl_seat *seat;
static struct zwlr_virtual_pointer_manager_v1 *manager;

static void die(const char *s)
{
        fprintf(stderr, "vptr: %s\n", s);
        exit(1);
}

static uint32_t button_of(const char *s)
{
        if (0 == strcmp(s, "right"))
                return BTN_RIGHT;
        if (0 == strcmp(s, "middle"))
                return BTN_MIDDLE;

        return BTN_LEFT;
}

static void registry_global(void *unused, struct wl_registry *registry,
                            uint32_t name, const char *iface,
                            uint32_t version)
{
        (void)unused;
        (void)version;

        if (0 == strcmp(iface, wl_seat_interface.name))
                seat = wl_registry_bind(registry, name,
                                        &wl_seat_interface, 1);
        else if (0 == strcmp(
                         iface,
                         zwlr_virtual_pointer_manager_v1_interface.name))
                manager = wl_registry_bind(
                        registry, name,
                        &zwlr_virtual_pointer_manager_v1_interface, 1);
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

int main(int argc, char **argv)
{
        struct wl_display *display;
        struct wl_registry *registry;
        struct zwlr_virtual_pointer_v1 *vp;

        uint32_t xext, yext, t = 1;
        int i;

        if (argc < 3)
                die("usage: vptr XEXT YEXT [move X Y] [click left|right] ...");

        xext = strtoul(argv[1], 0, 10);
        yext = strtoul(argv[2], 0, 10);

        display = wl_display_connect(0);
        if (0 == display)
                die("wl_display_connect failed");

        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &registry_listener, 0);
        wl_display_roundtrip(display);

        if (0 == seat || 0 == manager)
                die("no wl_seat or zwlr_virtual_pointer_manager_v1");

        vp = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(
                manager, seat);

        for (i = 3; i < argc; ++i) {
                if (0 == strcmp(argv[i], "move") && i + 2 < argc) {
                        uint32_t x = strtoul(argv[i + 1], 0, 10);
                        uint32_t y = strtoul(argv[i + 2], 0, 10);

                        i += 2;

                        zwlr_virtual_pointer_v1_motion_absolute(
                                vp, t += 10, x, y, xext, yext);
                        zwlr_virtual_pointer_v1_frame(vp);
                } else if (0 == strcmp(argv[i], "click") && i + 1 < argc) {
                        uint32_t b = button_of(argv[i + 1]);

                        i += 1;

                        zwlr_virtual_pointer_v1_button(
                                vp, t += 10, b,
                                WL_POINTER_BUTTON_STATE_PRESSED);
                        zwlr_virtual_pointer_v1_frame(vp);
                        zwlr_virtual_pointer_v1_button(
                                vp, t += 10, b,
                                WL_POINTER_BUTTON_STATE_RELEASED);
                        zwlr_virtual_pointer_v1_frame(vp);
                } else if ((0 == strcmp(argv[i], "press") ||
                            0 == strcmp(argv[i], "release")) &&
                           i + 1 < argc) {
                        uint32_t state =
                                0 == strcmp(argv[i], "press")
                                        ? WL_POINTER_BUTTON_STATE_PRESSED
                                        : WL_POINTER_BUTTON_STATE_RELEASED;
                        uint32_t b = button_of(argv[i + 1]);

                        i += 1;

                        zwlr_virtual_pointer_v1_button(vp, t += 10, b,
                                                       state);
                        zwlr_virtual_pointer_v1_frame(vp);
                } else {
                        die("bad command");
                }

                wl_display_roundtrip(display);
                usleep(100 * 1000);
        }

        zwlr_virtual_pointer_v1_destroy(vp);
        wl_display_roundtrip(display);
        wl_display_disconnect(display);

        return 0;
}
