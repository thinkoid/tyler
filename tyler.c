/* -*- mode: c; -*- */

#include <defs.h>
#include <display.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>

/**********************************************************************/

xstatic void sigchld_handler(int ignore) {
        UNUSED(ignore);
        while (0 < waitpid (-1, 0, WNOHANG)) ;
}

static void setup_sigchld() {
    if (SIG_ERR == signal(SIGCHLD, sigchld_handler)) {
            fprintf(stderr, "wm : failed to install SIGCHLD handler\n");
            exit(1);
    }
}

/**********************************************************************/

int main ()
{
        Display* dpy;

        dpy = DPY;
        ASSERT (0 == dpy);

        dpy = make_display(0);
        ASSERT (dpy);

        atexit(&free_display);

        setup_sigchld();

        return 0;
}
