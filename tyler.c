/* -*- mode: c; -*- */

#include <defs.h>
#include <display.h>

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

static void init()
{
        setup_sigchld();
        init_display();
}

static void run()
{
        XEvent ev;

        XSync(DPY, 0);

        for (; running && !XNextEvent(DPY, &ev);) {
                handle_event(&ev);
        }

        XSync(DPY, 1);
}

int main()
{
        return init(), run(), 0;
}
