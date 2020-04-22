/* -*- mode: c; -*- */

#include <defs.h>
#include <config.h>

/* clang-format off */
static int g_showbar      =  1;
static int g_border_width =  1;
static int g_bar_height   = 18;

static int   g_master_size  = 1;
static float g_master_ratio = .5f;

static const char *g_colors[] = {
        "#444444", "#222222", "#BBBBBB", "#93a660", "#334600", "#EEEEEE"
};

static const int g_cursors[] = {
        XC_top_left_arrow, XC_sizing, XC_fleur
};

static const char *g_termcmd[] = {
        "st", "-f", "Fira Code Retina:size=10", 0
};

static int g_snap = 5;
/* clang-format on */

const char **config_colors()
{
        return g_colors;
}

size_t config_colors_size()
{
        return SIZEOF(g_colors);
}

const int *config_cursors()
{
        return g_cursors;
}

size_t config_cursors_size()
{
        return SIZEOF(g_cursors);
}

const char **config_termcmd()
{
        return g_termcmd;
}

int config_showbar()
{
        return g_showbar;
}

int config_border_width()
{
        return g_border_width;
}

int config_bar_height()
{
        return g_bar_height;
}

int config_master_size()
{
        return g_master_size;
}

float config_master_ratio()
{
        return g_master_ratio;
}

int config_snap()
{
        return g_snap;
}
