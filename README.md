Tyler
=====

Tyler is derivative work and all thanks go to suckless!

Its direct parent  is dwm which I  used as a template,  debugging reference, and
learning tutorial.  Dwm has inspired  a lot of  emulation around the  subject of
tiling  window  managers  by  the   virtue  of  its  small,  simple,  accessible
codebase. I, personally, have learned a lot about Xlib by studying the dwm code.

Partly, this rewrite lead to minor operational changes. E.g., windows are hidden
by un-mapping  them and  not by  moving them  outside the  viewable area  of the
screen, like in dwm.  This in turn changes the way the  events are consumed, and
so on.

Initially, tyler did not have a status bar. However, that sucks when it comes to
marshal data  between the  WM and its  status bar. So,  it re-acquired  a status
bar. Just  like dwm, structured  in three fields.  It's hard to  improve optimal
designs.

v0.11
-----

Bug fixes in moving clients between screens.

v0.9
----

Fixes for focus jumping and stuttering caused by overcomplicated and aggressive
tiling.

v0.8
----

Fixes related to multi-monitor usage.

v0.7
----

Stable enough, trimmed and readable. Used daily on all my desktops.

v0.6
----

Improved stability. Fixed bugs in drawing and updating of status. Added client
list property management to help X tools in talking to clients.

v0.5
----

Stable in daily use. Internal status bar for simplicity reasons.

v0.4
----

Stable in daily use. Basic functionality is present, creation, resizing, and
moving of windows, warping from a desktop to another, tagging, etc. There is no
status bar.
