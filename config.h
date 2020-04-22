/* -*- mode: c; -*- */

#ifndef WM_CONFIG_H
#define WM_CONFIG_H

#include <defs.h>

#include <X11/Xlib.h>
#include <X11/cursorfont.h>

const char **config_colors();
size_t config_colors_size();

const int *config_cursors();
size_t config_cursors_size();

const char **config_termcmd();

int config_showbar();
int config_border_width();
int config_bar_height();

int config_master_size();
float config_master_ratio();

int config_snap();

#endif /* WM_CONFIG_H */
