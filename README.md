Tyler
=====

Tyler is derivative work and all thanks go to suckless, and to dwl!

This is the Wayland reincarnation of tyler, whose X11 parent now lives on as
tyler-classic. The lineage repeats itself one platform up: where tyler-classic
used dwm as a template, debugging reference, and learning tutorial, this tyler
uses dwl the same way — a compact, hackable compositor built on wlroots, with
the window management written from scratch rather than forked.

The porthole defoggers are factory-installed.

What it is
----------

A single-file tiling compositor, ~3,200 lines of C against wlroots 0.19,
rendered through wlr_scene, configured by recompiling `config.h`. One binary,
one config, every machine: nothing machine-specific lives in the config —
output placement is decided by policy (internal panel leftmost, externals to
the right), never by output name.

The design stance is that everything the desktop needs lives *in* the
compositor. There is no layer-shell and there are no helper clients:

- **The bar** is compositor-drawn pixels — fcft text, per-screen tags
  (viewed = background, occupied = underline, urgent = swap), window title,
  and a status field fed by a child process on a pipe. The status speaks
  kwm's color escapes (`^#RRGGBBAA` sets, `^#!` restores).
- **The launcher** (Mod+p) is an integrated dmenu: the compositor owns the
  keyboard, so there are no grabs and no focus games — a flag reroutes key
  events into the filter loop and the drawing rides the bar path.
- **Screenshots** (PrtSc) render the scene into a buffer and write a PNG with
  a hand-rolled writer — no screencopy protocol, no external grabber, zero
  added dependencies.

The rest of the roster: master/stack tiling with per-screen tags, a global
MRU focus stack with motion-driven sloppy focus, mouse move/resize (Alt+drag)
that tears tiles out into floats, fullscreen, urgency from both
xdg-activation and the xdg system bell, clipboard and primary selection,
server-side decorations, VT switching, keyboard repeat and libinput knobs
(tap, natural scroll, adaptive accel) applied per-device the moment hardware
appears, XF86 volume/brightness keys, and screen memories — per-output state
keyed by name that survives the output dying and returning, so a
hibernate/resume cycle with a lid closed does not shred the layout.

Deliberately absent: Xwayland (the X11 sibling is the fallback for
stragglers), layer-shell (nothing external draws on this desktop), and
drag-and-drop (middle-click paste covers it).

Building
--------

    meson setup build
    ninja -C build

Dependencies: wlroots 0.19, wayland-server, xkbcommon, fcft, pixman, libdrm,
libinput. The build also produces `vkbd` and `vptr`, protocol-level input
injectors used by the test harness; they are never installed.

Running
-------

Type `tyler` at a VT login shell (libseat/seatd session). Configuration is
`config.h`, one compilation away; the key table is dwm's shape with Alt as
the modifier. The status feeder is `tools/tyler-status -o` — swap in anything
that prints a line per update.

Testing
-------

The compositor is verified headless: `WLR_BACKENDS=headless
WLR_RENDERER=pixman`, clients driven by `vkbd` and `vptr`, geometry read back
from `WAYLAND_DEBUG` traces, bar pixels dumped via `TYLER_BAR_DUMP`, and
screenshots via the internal PNG path. Most features landed with an oracle
run proving them before they ever touched real hardware.

Pictures
--------

Coming soon.

Provenance
----------

Designed and built 21 August – 1 September 2026, from empty repository to
daily driver. The overwhelming majority of the code was written by Claude
(Anthropic), with design direction, taste, and live-hardware verdicts from a
human who mostly rode along; the commit trailers carry the flags of the crew.

License: WTFPL.
