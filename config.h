/* -*- mode: c; -*- */

/*
 * tyler configuration, compiled in. Fleet rule: nothing machine-specific
 * lives here — no output names, no per-host paths. One binary, one
 * config, every machine.
 *
 * Colors are RGBA floats for wlr_scene; values carried over from
 * tyler-classic's palette.
 */

/* #444444 / #93a660 / #EEEEEE */
static const float color_border_normal[] = { 0.267f, 0.267f, 0.267f, 1.0f };
static const float color_border_select[] = { 0.576f, 0.651f, 0.376f, 1.0f };
static const float color_border_urgent[] = { 0.933f, 0.933f, 0.933f, 1.0f };

static const int border_width = 1;
static const int margin       = 2;

static const float master_ratio = 0.5f;

/*
 * The bar itself lands with fcft; the work-area strip is reserved now so
 * tiling geometry doesn't shift when it does. Height is a placeholder
 * until it derives from the font.
 */
static const int showbar    = 1;
static const int bar_height = 24;

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
/* clang-format on */
