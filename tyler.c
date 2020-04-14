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

static int spawn_terminal()
{
        return 0;
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

static int key_press_handler(XEvent *arg)
{
        XKeyEvent *ev = &arg->xkey;

        KeySym keysym = XKeycodeToKeysym(DPY, (KeyCode)ev->keycode, 0);
        unsigned mod = CLEANMASK(ev->state);

        update_numlockmask();

        keycmd_t *p = g_keycmds, *pend = g_keycmds + SIZEOF(g_keycmds);
        for (; p != pend; ++p)
                if (keysym == p->keysym && mod == CLEANMASK(p->mod) && p->fun)
                        return p->fun();

        return 0;
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

static void do_grab_keys(Window win, unsigned mod, KeyCode keycode)
{
#define GRAB(x)                                                                \
        XGrabKey(DPY, keycode, mod | x, win, 1, GrabModeAsync, GrabModeAsync)

        GRAB(0);
        GRAB(LockMask);
        GRAB(g_numlockmask);
        GRAB(LockMask | g_numlockmask);

#undef GRAB
}

static void ungrab_keys(Window win)
{
        XUngrabKey(DPY, AnyKey, AnyModifier, win);
}

static void grab_keys(Window win)
{
        KeyCode keycode;
        keycmd_t *p = g_keycmds, *pend = g_keycmds + SIZEOF(g_keycmds);

        update_numlockmask();

        ungrab_keys(win);
        for (; p != pend; ++p)
                if ((keycode = XKeysymToKeycode(DPY, p->keysym)))
                        do_grab_keys(win, p->mod, keycode);
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
