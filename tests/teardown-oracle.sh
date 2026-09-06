#!/bin/sh
#
# The teardown oracle: a clean quit with N outputs up must exit 0.
#
# Regression gate for the fini() ordering bug. The font was destroyed
# before the backend, so wlr_backend_destroy's per-output destroy signal
# reached output_destroy_handler -> arrange(survivor) -> drawbar, which
# rasterized glyphs against an already-freed fcft.
#
# ONE OUTPUT DOES NOT CATCH IT. The last output leaves a null survivor
# and arrange() returns early, so nothing draws and the quit is clean
# either way. The gate needs two, which is exactly why the bug shipped
# and ran for a day on single-display machines without a symptom.
#
# No display, no GPU, no keyboard: the headless backend takes an output
# count, and SIGTERM reaches fini() through terminate_handler by the
# same path the quit binding uses.
set -eu

tyler=$1
outputs=$2

runtime=$(mktemp -d)
trap 'rm -rf "$runtime"' EXIT

XDG_RUNTIME_DIR="$runtime" \
WLR_BACKENDS=headless \
WLR_RENDERER=pixman \
WLR_HEADLESS_OUTPUTS="$outputs" \
        "$tyler" > "$runtime/log" 2>&1 &
pid=$!

# Wait for the socket rather than sleeping a guessed interval: a fixed
# sleep either wastes time or races the compositor on a loaded machine.
waited=0
while ! grep -aq 'running on wayland-' "$runtime/log" 2>/dev/null; do
        kill -0 "$pid" 2>/dev/null || {
                echo "oracle: tyler died before it came up" >&2
                sed 's/^/  /' "$runtime/log" >&2
                exit 1
        }
        waited=$((waited + 1))
        [ "$waited" -gt 200 ] && {
                echo "oracle: tyler never came up within 20s" >&2
                sed 's/^/  /' "$runtime/log" >&2
                kill -KILL "$pid" 2>/dev/null || true
                exit 1
        }
        sleep 0.1
done

# Every output must be enrolled before the quit, or the gate passes for
# the wrong reason -- a race that leaves one screen would look clean.
enrolled=$(grep -ac 'screen HEADLESS-' "$runtime/log" || true)
if [ "$enrolled" -ne "$outputs" ]; then
        echo "oracle: enrolled $enrolled screens, wanted $outputs" >&2
        sed 's/^/  /' "$runtime/log" >&2
        kill -KILL "$pid" 2>/dev/null || true
        exit 1
fi

kill -TERM "$pid"

rc=0
wait "$pid" || rc=$?

if [ "$rc" -ne 0 ]; then
        echo "oracle: quit with $outputs output(s) exited $rc, wanted 0" >&2
        [ "$rc" -eq 139 ] && echo "oracle: 139 is SIGSEGV -- the fini() ordering regressed" >&2
        sed 's/^/  /' "$runtime/log" >&2
        exit 1
fi
