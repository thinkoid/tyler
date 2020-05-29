/* -*- mode: c; -*- */

#include <atom.h>
#include <color.h>
#include <config.h>
#include <error.h>
#include <malloc-wrapper.h>
#include <window.h>
#include <xlib.h>

#include <stdlib.h>

Window transient_for_property(Window win)
{
        Window other;

        if (!XGetTransientForHint(DPY, win, &other)) {
                other = 0;
        }

        return other;
}

int has_fullscreen_property(Window win)
{
        return NET_WM_STATE_FULLSCREEN == atomic_property(win, NET_WM_STATE);
}

/**********************************************************************/

void send_focus(Window win)
{
        send(win, WM_TAKE_FOCUS);
}

int has_protocol(Window win, Atom proto)
{
        int i, n, ret = 0;
        Atom *ptr = 0;

        if (XGetWMProtocols(DPY, win, &ptr, &n)) {
                for (i = 0; i < n && proto != ptr[i]; ++i)
                        ;
                ret = i < n;
                XFree(ptr);
        }

        return ret;
}

void reset_focus_property(void)
{
        XSetInputFocus(DPY, ROOT, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(DPY, ROOT, NET_ACTIVE_WINDOW);
}

void set_focus_property(Window win)
{
        XSetInputFocus(DPY, win, RevertToPointerRoot, CurrentTime);
        XChangeProperty(DPY, ROOT, NET_ACTIVE_WINDOW, XA_WINDOW, 32,
                        PropModeReplace, (unsigned char *)&win, 1);

        XSync(DPY, 0);
}

void reset_urgent(Window win)
{
        XWMHints *p;

        if ((p = XGetWMHints(DPY, win))) {
                if (p->flags & XUrgencyHint) {
                        p->flags &= ~XUrgencyHint;
                        XSetWMHints(DPY, win, p);
                }

                XFree(p);
        }
}

void set_urgent(Window win)
{
        XWMHints *p;

        if ((p = XGetWMHints(DPY, win))) {
                if (0 == (p->flags & XUrgencyHint)) {
                        p->flags |= XUrgencyHint;
                        XSetWMHints(DPY, win, p);
                }

                XFree(p);
        }
}

void reset_fullscreen_property(Window win)
{
        XChangeProperty(DPY, win, NET_WM_STATE, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)0, 0);
}

void set_fullscreen_property(Window win)
{
        Atom prop = NET_WM_STATE_FULLSCREEN;
        XChangeProperty(DPY, win, NET_WM_STATE, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)&prop, 1);
}

static void set_default_window_border_width(Window win)
{
        XWindowChanges wc;
        wc.border_width = config_border_width();
        XConfigureWindow(DPY, win, CWBorderWidth, &wc);
}

static void set_default_window_border_color(Window win)
{
        XSetWindowBorder(DPY, win, NORMAL_BORDER);
}

void set_default_window_border(Window win)
{
        set_default_window_border_width(win);
        set_default_window_border_color(win);
}

static void set_select_window_border_color(Window win)
{
        XSetWindowBorder(DPY, win, SELECT_BORDER);
}

void set_select_window_border(Window win)
{
        set_default_window_border_width(win);
        set_select_window_border_color(win);
}

static int do_send(Window win, Atom proto)
{
        XEvent ev = { 0 };

        ev.type = ClientMessage;
        ev.xclient.window = win;

        ev.xclient.message_type = WM_PROTOCOLS;
        ev.xclient.format = 32;

        ev.xclient.data.l[0] = proto;
        ev.xclient.data.l[1] = CurrentTime;

        return XSendEvent(DPY, win, 0, NoEventMask, &ev);
}

int send(Window win, Atom proto)
{
        return has_protocol(win, proto) && do_send(win, proto);
}

void zap_window(Window win)
{
        XGrabServer(DPY);
        pause_error_handling();

        XSetCloseDownMode(DPY, DestroyAll);
        XKillClient(DPY, win);

        XSync(DPY, 0);

        resume_error_handling();
        XUngrabServer(DPY);
}

Window *all_windows(Window *pbuf, size_t *plen)
{
        Window root, parent, *p = 0;
        unsigned n = 0;

        if (!XQueryTree(DPY, ROOT, &root, &parent, &p, &n))
                return 0;

        if (n > *plen)
                pbuf = malloc_((*plen = n) * sizeof *p);

        memcpy(pbuf, p, n * sizeof *p);
        XFree(p);

        return pbuf;
}

void pause_propagate(Window win, long mask)
{
        XSetWindowAttributes attr = { 0 };
        attr.do_not_propagate_mask = mask;
        XChangeWindowAttributes(DPY, win, CWEventMask, &attr);
}

void resume_propagate(Window win, long mask)
{
        XSetWindowAttributes attr = { 0 };
        attr.event_mask = mask;
        XChangeWindowAttributes(DPY, win, CWEventMask, &attr);
}

static char *copy_text_property(const char *src, char *buf, size_t len)
{
        char *pbuf = 0;

        if (src && src[0]) {
                size_t n = strlen(src);

                if (0 == (pbuf = buf) || len < n + 1)
                        pbuf = malloc_(n + 1);

                strcpy(pbuf, src);
        }

        return pbuf;
}

char *text_property(Window win, Atom atom, char *buf, size_t len)
{
        char *pbuf = 0;

        XTextProperty prop = { 0 };

        if (XGetTextProperty(DPY, win, &prop, atom) && prop.nitems) {
                if (prop.encoding == XA_STRING) {
                        pbuf = copy_text_property((char *)prop.value, buf, len);
                }
                else {
                        char** list = 0;
                        int n;

                        if (0 <= Xutf8TextPropertyToTextList(DPY, &prop, &list, &n) &&
                            0 < n && list [0]) {
                                pbuf = copy_text_property(list[0], buf, len);
                                XFreeStringList(list);
                        }
                }

                XFree(prop.value);
        }

        return pbuf;
}
