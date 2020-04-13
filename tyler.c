/* -*- mode: c; -*- */

#include <defs.h>
#include <config.h>
#include <color.h>
#include <cursor.h>
#include <display.h>
#include <error.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>

static int running = 1;

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

static void handle_event(XEvent *arg)
{
        UNUSED(arg);
}

/**********************************************************************/

static void init_display()
{
        make_display(0);
        ASSERT(DPY);
        atexit(free_display);
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
        init_colors();
        init_cursors();

        init_error_handling();
}

static void run()
{
        XEvent ev;

        XSync(DPY, 0);

        for (; running && !XNextEvent(DPY, &ev);)
                handle_event(&ev);

        XSync(DPY, 1);
}

int main()
{
        return init(), run(), 0;
}
