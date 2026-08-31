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
