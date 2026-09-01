/* -*- mode: c; -*- */

/*
 * vkbd — key injector for the tyler oracle. Speaks
 * virtual-keyboard-unstable-v1: each argument is a chord like
 * "alt+shift+j" — modifiers pressed, key tapped, modifiers released
 * in reverse, then a settle delay so the compositor can act.
 *
 * For choreography with vptr: "+alt" presses without releasing,
 * "-alt" releases, "hold N" sleeps N ms in between — so a drag can
 * run under a held modifier.
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

#include <virtual-keyboard-unstable-v1-client-protocol.h>

static struct wl_seat *seat;
static struct zwp_virtual_keyboard_manager_v1 *manager;

/*
 * The virtual-keyboard contract: key events carry no modifier
 * semantics — the client owns the xkb state and reports it through
 * the modifiers request. So we track our own.
 */
static struct xkb_state *state;

/* clang-format off */
static const struct {
        const char *name;
        uint32_t code;
} keytab[] = {
        { "alt",    KEY_LEFTALT   },
        { "shift",  KEY_LEFTSHIFT },
        { "ctrl",   KEY_LEFTCTRL  },
        { "super",  KEY_LEFTMETA  },

        { "return",    KEY_ENTER     },
        { "escape",    KEY_ESC       },
        { "backspace", KEY_BACKSPACE },
        { "tab",       KEY_TAB       },
        { "left",      KEY_LEFT      },
        { "right",     KEY_RIGHT     },
        { "comma",     KEY_COMMA     },
        { "period",    KEY_DOT       },
        { "minus",     KEY_MINUS     },

        { "1", KEY_1 }, { "2", KEY_2 }, { "3", KEY_3 },
        { "4", KEY_4 }, { "5", KEY_5 }, { "6", KEY_6 },
        { "7", KEY_7 }, { "8", KEY_8 }, { "9", KEY_9 },
        { "0", KEY_0 },

        { "a", KEY_A }, { "b", KEY_B }, { "c", KEY_C },
        { "d", KEY_D }, { "e", KEY_E }, { "f", KEY_F },
        { "g", KEY_G }, { "h", KEY_H }, { "i", KEY_I },
        { "j", KEY_J }, { "k", KEY_K }, { "l", KEY_L },
        { "m", KEY_M }, { "n", KEY_N }, { "o", KEY_O },
        { "p", KEY_P }, { "q", KEY_Q }, { "r", KEY_R },
        { "s", KEY_S }, { "t", KEY_T }, { "u", KEY_U },
        { "v", KEY_V }, { "w", KEY_W }, { "x", KEY_X },
        { "y", KEY_Y }, { "z", KEY_Z },
};
/* clang-format on */

static void die(const char *s)
{
        fprintf(stderr, "vkbd: %s\n", s);
        exit(1);
}

static uint32_t code_of(const char *name)
{
        size_t i;

        for (i = 0; i < sizeof keytab / sizeof *keytab; ++i)
                if (0 == strcmp(keytab[i].name, name))
                        return keytab[i].code;

        fprintf(stderr, "vkbd: unknown key '%s'\n", name);
        exit(1);
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
                         zwp_virtual_keyboard_manager_v1_interface.name))
                manager = wl_registry_bind(
                        registry, name,
                        &zwp_virtual_keyboard_manager_v1_interface, 1);
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

static int keymap_fd(size_t *size)
{
        struct xkb_context *context;
        struct xkb_keymap *map;
        char *s;
        int fd;

        context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        map = xkb_keymap_new_from_names(context, 0,
                                        XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (0 == map)
                die("xkb_keymap_new_from_names failed");

        state = xkb_state_new(map);

        s = xkb_keymap_get_as_string(map, XKB_KEYMAP_FORMAT_TEXT_V1);
        *size = strlen(s) + 1;

        fd = memfd_create("vkbd-keymap", 0);
        if (fd < 0 || write(fd, s, *size) != (ssize_t)*size)
                die("keymap fd failed");

        free(s);
        xkb_keymap_unref(map);
        xkb_context_unref(context);

        return fd;
}

static void send_key(struct zwp_virtual_keyboard_v1 *vk, uint32_t *t,
                     uint32_t code, int pressed)
{
        xkb_state_update_key(state, code + 8,
                             pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

        zwp_virtual_keyboard_v1_modifiers(
                vk,
                xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED),
                xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED),
                xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED),
                xkb_state_serialize_layout(state,
                                           XKB_STATE_LAYOUT_EFFECTIVE));

        zwp_virtual_keyboard_v1_key(vk, *t += 10, code,
                                    pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                                            : WL_KEYBOARD_KEY_STATE_RELEASED);
}

int main(int argc, char **argv)
{
        struct wl_display *display;
        struct wl_registry *registry;
        struct zwp_virtual_keyboard_v1 *vk;

        size_t size;
        uint32_t t = 1;
        int i, fd;

        display = wl_display_connect(0);
        if (0 == display)
                die("wl_display_connect failed");

        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &registry_listener, 0);
        wl_display_roundtrip(display);

        if (0 == seat || 0 == manager)
                die("no wl_seat or zwp_virtual_keyboard_manager_v1");

        vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
                manager, seat);

        fd = keymap_fd(&size);
        zwp_virtual_keyboard_v1_keymap(vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                       fd, size);
        wl_display_roundtrip(display);

        for (i = 1; i < argc; ++i) {
                char chord[64];
                uint32_t codes[8];
                int j, n = 0;
                char *tok;

                if (0 == strcmp(argv[i], "hold") && i + 1 < argc) {
                        usleep(1000UL * strtoul(argv[++i], 0, 10));
                        continue;
                }

                if ('+' == argv[i][0] || '-' == argv[i][0]) {
                        send_key(vk, &t, code_of(argv[i] + 1),
                                 '+' == argv[i][0]);

                        wl_display_roundtrip(display);
                        usleep(100 * 1000);
                        continue;
                }

                snprintf(chord, sizeof chord, "%s", argv[i]);

                for (tok = strtok(chord, "+"); tok && n < 8;
                     tok = strtok(0, "+"))
                        codes[n++] = code_of(tok);

                for (j = 0; j < n; ++j)
                        send_key(vk, &t, codes[j], 1);

                for (j = n - 1; 0 <= j; --j)
                        send_key(vk, &t, codes[j], 0);

                wl_display_roundtrip(display);
                usleep(100 * 1000);
        }

        zwp_virtual_keyboard_v1_destroy(vk);
        wl_display_roundtrip(display);
        wl_display_disconnect(display);

        return 0;
}
