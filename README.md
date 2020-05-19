Tyler
=====

Tyler is derivative work.

Its direct parent is dwm which I used as a template, debugging reference, and
learning tutorial. I stripped out all that I did not find essential to my
workflow in dwm. Although all code has been re-written (c89) it is still
conceptually identical to dwm.

One important difference from dwm is the hiding mechanism for windows that do
not match the current screen tag. While dwm moves them outside screen
boundaries, tyler unmaps them. The same mechanism is used in monsterwm
although that WM does not use tags.

Like monsterwm, tyler does not provide a bar. Instead it dumps its status to
stdout to be consumed by an external script with a user's choice of status
bar. Monsterwm project has a good deal of examples of such scripts.

v0.5
----

Stable in daily use. Internal status bar for simplicity reasons.

v0.4
----

Stable in daily use. Basic functionality is present, creation, resizing, and
moving of windows, warping from a desktop to another, tagging, etc. There is no
status bar.
