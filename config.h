/* -*- mode: c; -*- */

/*
 * tyler configuration, compiled in. Fleet rule: nothing machine-specific
 * lives here — no output names, no per-host paths. One binary, one
 * config, every machine.
 *
 * Colors are RGBA floats for wlr_scene; values carried over from
 * tyler-classic's palette.
 */

/* classic's palette: #444444 #222222 #BBBBBB #93a660 #4f5b3f #EEEEEE */
/* clang-format off */
static const float colors[][4] = {
        [COLOR_NORMAL_BORDER] = { 0.267f, 0.267f, 0.267f, 1.0f },
        [COLOR_NORMAL_BG]     = { 0.133f, 0.133f, 0.133f, 1.0f },
        [COLOR_NORMAL_FG]     = { 0.733f, 0.733f, 0.733f, 1.0f },
        [COLOR_SELECT_BORDER] = { 0.576f, 0.651f, 0.376f, 1.0f },
        [COLOR_SELECT_BG]     = { 0.310f, 0.357f, 0.247f, 1.0f },
        [COLOR_SELECT_FG]     = { 0.933f, 0.933f, 0.933f, 1.0f },
};
/* clang-format on */

static const int border_width = 1;
static const int margin       = 2;

static const float master_ratio = 0.5f;

static const int showbar = 1;

/*
 * One string feeds the bar (and later the menu). The nerd-patched
 * Iosevka, as in classic; the bar height derives from it.
 */
static const char *fontname = "IosevkaTerm Nerd Font:style=Light:size=24";

/*
 * The status feeder: spawned by the compositor, one line on stdout per
 * bar update. Replace with tyler-status when it grows a stdout mode.
 */
static const char *const statuscmd[] = {
        "sh", "-c", "while date '+%a %d %b %H:%M'; do sleep 60; done", 0
};

/*
 * Keyboard autorepeat, applied to every keyboard the moment it appears
 * (new_input) — no xset, nothing to revert.
 */
static const int repeat_rate  = 60;  /* per second */
static const int repeat_delay = 250; /* ms */

static const char *const termcmd[] = { "foot", 0 };

#define MODKEY   WLR_MODIFIER_ALT
#define MODSHIFT (MODKEY | WLR_MODIFIER_SHIFT)

#define TAGKEYS(key, shifted, n)                 \
        { MODKEY,   key,     view_tag,   n },    \
        { MODSHIFT, shifted, change_tag, n }

/* clang-format off */
static const struct key keys[] = {
        { MODKEY,   XKB_KEY_Return,  zoom,              0 },
        { MODSHIFT, XKB_KEY_Return,  spawn_terminal,    0 },
        { MODKEY,   XKB_KEY_p,       menu_open,         0 },
        { MODKEY,   XKB_KEY_b,       toggle_bar,        0 },
        { MODKEY,   XKB_KEY_j,       focus_next,        0 },
        { MODKEY,   XKB_KEY_k,       focus_prev,        0 },
        { MODSHIFT, XKB_KEY_Right,   focus_next,        0 },  /* classic */
        { MODSHIFT, XKB_KEY_Left,    focus_prev,        0 },  /* classic */
        { MODSHIFT, XKB_KEY_C,       zap,               0 },
        { MODKEY,   XKB_KEY_comma,   focus_prev_screen, 0 },
        { MODKEY,   XKB_KEY_period,  focus_next_screen, 0 },
        { MODSHIFT, XKB_KEY_less,    move_prev_screen,  0 },
        { MODSHIFT, XKB_KEY_greater, move_next_screen,  0 },
        { 0,        XKB_KEY_Print,   screenshot,        0 },
        { MODKEY,   XKB_KEY_t,       tile_current,      0 },
        { MODSHIFT, XKB_KEY_Q,       quit,              0 },

        TAGKEYS(XKB_KEY_1, XKB_KEY_exclam,      1),
        TAGKEYS(XKB_KEY_2, XKB_KEY_at,          2),
        TAGKEYS(XKB_KEY_3, XKB_KEY_numbersign,  3),
        TAGKEYS(XKB_KEY_4, XKB_KEY_dollar,      4),
        TAGKEYS(XKB_KEY_5, XKB_KEY_percent,     5),
        TAGKEYS(XKB_KEY_6, XKB_KEY_asciicircum, 6),
        TAGKEYS(XKB_KEY_7, XKB_KEY_ampersand,   7),
        TAGKEYS(XKB_KEY_8, XKB_KEY_asterisk,    8),
        TAGKEYS(XKB_KEY_9, XKB_KEY_parenleft,   9),
};

static const struct button buttons[] = {
        { MODKEY, BTN_LEFT,  mouse_move   },
        { MODKEY, BTN_RIGHT, mouse_resize },
};
/* clang-format on */
