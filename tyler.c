/* -*- mode: c; -*- */

#include <defs.h>
#include <atom.h>
#include <config.h>
#include <color.h>
#include <cursor.h>
#include <display.h>
#include <error.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#define BUTTONMASK (ButtonPressMask | ButtonReleaseMask)

#define MODKEY Mod4Mask

#define LOCKMASK (g_numlockmask | LockMask)
#define ALLMODMASK (Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask)

#define CLEANMASK(x) ((x) & ~LOCKMASK & (ShiftMask | ControlMask | ALLMODMASK))

/* clang-format off */
#define ROOTMASK (0                             \
        | SubstructureRedirectMask              \
        | SubstructureNotifyMask                \
        | ButtonPressMask                       \
        | PropertyChangeMask)
/* clang-format on */

static int g_running = 1;
static unsigned g_numlockmask = 0;

typedef struct state {
        geom_t g;
        unsigned fixed : 1, floating : 1, transient : 1, urgent : 1,
                noinput : 1, fullscreen : 1;
        unsigned tags;
} state_t;

typedef struct aspect {
        float min, max;
} aspect_t;

typedef struct screen screen_t;
typedef struct client client_t;

typedef struct client {
        Window win;

        aspect_t aspect;
        ext_t base, inc, max, min;

        state_t state[2];
        int current_state;

        /* List link to next client, next client in stack */
        client_t *next, *focus_next;
        screen_t *screen;
} client_t;

struct screen {
        rect_t r;

        int master_size;
        float master_ratio;

        /* Client list, stack list, current client shortcut */
        client_t *client_head, *focus_head, *client_cur;

        /* Next screen in screen list */
        screen_t *next;
};

static screen_t *screen_head = 0;

/**********************************************************************/

static client_t *make_client(Window win, const screen_t *screen)
{
        UNUSED(win);
        UNUSED(screen);
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

static void grab_keys(Window win);
static void grab_buttons(Window win, int focus);

/**********************************************************************/

static void sigchld_handler(int ignore)
{
        UNUSED(ignore);
        while (0 < waitpid(-1, 0, WNOHANG))
                ;
}

static void setup_sigchld()
{
        if (SIG_ERR == signal(SIGCHLD, sigchld_handler)) {
                fprintf(stderr, "wm : failed to install SIGCHLD handler\n");
                exit(1);
        }
}

/**********************************************************************/

static int zoom()
{
        return 0;
}

static int spawn(char **args)
{
        if (0 == fork()) {
                release_display();

                setsid();
                execvp(args[0], args);

                fprintf(stderr, "dwm: execvp %s", args[0]);
                perror(" failed");

                exit(0);
        }

        return 0;
}

static int spawn_terminal()
{
        return spawn((char **)termcmd);
}

static int toggle_bar()
{
        return 0;
}

static int move_focus_left()
{
        return 0;
}

static int move_focus_right()
{
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
        return 0;
}

static int focus_next_monitor()
{
        return 0;
}

static int focus_prev_monitor()
{
        return 0;
}

static int move_next_monitor()
{
        return 0;
}

static int move_prev_monitor()
{
        return 0;
}

static int quit()
{
        return g_running = 0;
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
        { MODKEY,               XK_b,       toggle_bar         },
        { MODKEY | ShiftMask,   XK_Left,    move_focus_left    },
        { MODKEY | ShiftMask,   XK_Right,   move_focus_right   },
        { MODKEY,               XK_i,       increment_master   },
        { MODKEY,               XK_d,       decrement_master   },
        { MODKEY,               XK_h,       shrink_master      },
        { MODKEY,               XK_l,       grow_master        },
        { MODKEY | ShiftMask,   XK_c,       zap                },
        { MODKEY,               XK_comma,   focus_next_monitor },
        { MODKEY,               XK_period,  focus_prev_monitor },
        { MODKEY | ShiftMask,   XK_comma,   move_next_monitor  },
        { MODKEY | ShiftMask,   XK_period,  move_prev_monitor  },
        { MODKEY | ShiftMask,   XK_q,       quit               }
        /* clang-format on */
};

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

static int button_press_handler(XEvent *arg)
{
        UNUSED(arg);
        return 0;
}

static int enter_notify_handler(XEvent *arg)
{
        UNUSED(arg);
        return 0;
}

static int focus_in_handler(XEvent *arg)
{
        UNUSED(arg);
        return 0;
}

static int destroy_notify_handler(XEvent *arg)
{
        UNUSED(arg);
        return 0;
}

static int unmap_notify_handler(XEvent *arg)
{
        UNUSED(arg);
        return 0;
}

static int map_request_handler(XEvent *arg)
{
        UNUSED(arg);
        return 0;
}

static int configure_request_handler(XEvent *arg)
{
        UNUSED(arg);
        return 0;
}

static int property_notify_handler(XEvent *arg)
{
        UNUSED(arg);
        return 0;
}

static int client_message_handler(XEvent *arg)
{
        UNUSED(arg);
        return 0;
}

static int handle_event(XEvent *arg)
{
        static int (*fun[LASTEvent])(XEvent *) = {
                /* clang-format off */
                0, 0,
                key_press_handler,
                0,
                button_press_handler,
                0, 0,
                enter_notify_handler,
                0,
                focus_in_handler,
                0, 0, 0, 0, 0, 0, 0,
                destroy_notify_handler,
                unmap_notify_handler,
                0,
                map_request_handler,
                0, 0,
                configure_request_handler,
                0, 0, 0, 0,
                property_notify_handler,
                0, 0, 0, 0,
                client_message_handler,
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

        XUngrabKey(DPY, AnyKey, AnyModifier, win);

#define GRABKEYS(x)                                                            \
        XGrabKey(DPY, keycode, g_keycmds[i].mod | x, win, 1, GrabModeAsync,    \
                 GrabModeAsync)

        for (i = 0; i < SIZEOF(g_keycmds); ++i)
                if ((keycode = XKeysymToKeycode(DPY, g_keycmds[i].keysym))) {
                        GRABKEYS(0);
                        GRABKEYS(LockMask);
                        GRABKEYS(g_numlockmask);
                        GRABKEYS(g_numlockmask | LockMask);
                }
#undef GRABKEYS
}

/**********************************************************************/

static void setup_root_supported_atoms()
{
        Atom *p = atoms();
        size_t n = atoms_size();

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
        make_colors(colors, SIZEOF(colors));
        atexit(free_colors);
}

static void init_cursors()
{
        make_cursors(cursors, SIZEOF(cursors));
        atexit(free_cursors);
}

static void init()
{
        setup_sigchld();

        init_display();
        init_atoms();
        init_colors();
        init_cursors();
        init_error_handling();

        setup_root();
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
