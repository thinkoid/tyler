/* -*- mode: c; -*- */

#include <window.h>
#include <atom.h>
#include <color.h>
#include <config.h>
#include <error.h>
#include <xlib.h>

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

long wm_state(Window win)
{
        Atom real;
        int format;

        unsigned char *prop = 0;
        unsigned long nitems, remaining;

        long result = -1L;

        if (XGetWindowProperty(DPY, win, WM_STATE, 0L, 2L, 0, WM_STATE, &real,
                               &format, &nitems, &remaining,
                               (unsigned char **)&prop)) {
                if (nitems)
                        result = *prop;

                XFree(prop);
        }

        return result;
}

void set_wm_state(Window win, long state)
{
        long arr[] = { 0, None };

        arr[0] = state;
        XChangeProperty(DPY, win, WM_STATE, WM_STATE, 32, PropModeReplace,
                        (unsigned char *)arr, 2);
}

int is_iconic(Window win)
{
        return IconicState == wm_state(win);
}

int is_viewable(Window win)
{
        XWindowAttributes attr;
        return XGetWindowAttributes(DPY, win, &attr) &&
               IsViewable == attr.map_state;
}

Window focused_window()
{
        Atom unused_type;
        int unused_format;

        unsigned long nitems, unused_bytes_after;
        unsigned char *prop;

        int status =
                XGetWindowProperty(DPY, ROOT, NET_ACTIVE_WINDOW, 0, (~0L), 0,
                                   AnyPropertyType, &unused_type, &unused_format,
                                   &nitems, &unused_bytes_after, &prop);

        return Success == status && 0 < nitems ? *(Window *)prop : 0;
}

int has_focus(Window win)
{
        Window focused = focused_window();
        return focused && focused == win;
}

void send_focus(Window win)
{
        send(win, WM_TAKE_FOCUS);
}

void reset_focus()
{
        XSetInputFocus(DPY, ROOT, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(DPY, ROOT, NET_ACTIVE_WINDOW);
}

void set_focus(Window win)
{
        XSetInputFocus(DPY, win, RevertToPointerRoot, CurrentTime);
        XChangeProperty(DPY, ROOT, NET_ACTIVE_WINDOW, XA_WINDOW, 32,
                        PropModeReplace, (unsigned char *)&win, 1);
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

int has_override_redirect(Window win)
{
        XWindowAttributes attr;
        return XGetWindowAttributes(DPY, win, &attr) && attr.override_redirect;
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

Window *all_windows()
{
        Window root, parent, *pbuf = 0;
        unsigned len;

        if (!XQueryTree(DPY, ROOT, &root, &parent, &pbuf, &len))
                pbuf = 0;

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
