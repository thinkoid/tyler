#!/bin/sh
#
# The pointer-lock oracle: a client that locks the pointer must be told
# so, must then read every movement as relative motion, and must see no
# absolute motion and no leave while the lock holds; the compositor must
# still quit clean with the lock alive.
#
# Headless, one output. lockptr maps a toplevel (tiled, so it covers the
# screen) and locks on the pointer's first entry; vptr supplies the
# entry and the movements in layout-percentage coordinates.
#
#       lock-oracle.sh TYLER LOCKPTR VPTR
set -eu

tyler=$1
lockptr=$2
vptr=$3

runtime=$(mktemp -d)
trap 'rm -rf "$runtime"' EXIT

XDG_RUNTIME_DIR="$runtime" \
WLR_BACKENDS=headless \
WLR_RENDERER_ALLOW_SOFTWARE=1 \
WLR_HEADLESS_OUTPUTS=1 \
        "$tyler" > "$runtime/log" 2>&1 &
pid=$!

fail() {
        echo "oracle: $1" >&2
        echo "--- tyler" >&2
        sed 's/^/  /' "$runtime/log" >&2
        echo "--- lockptr" >&2
        sed 's/^/  /' "$runtime/client" >&2 2>/dev/null || true
        kill -KILL "$pid" 2>/dev/null || true
        kill -KILL "${cpid:-0}" 2>/dev/null || true
        exit 1
}

# wait for the socket (see teardown-oracle.sh for why not a fixed sleep)
waited=0
while ! grep -aq 'running on wayland-' "$runtime/log" 2>/dev/null; do
        kill -0 "$pid" 2>/dev/null || fail "tyler died before it came up"
        waited=$((waited + 1))
        [ "$waited" -gt 200 ] && fail "tyler never came up within 20s"
        sleep 0.1
done
socket=$(sed -n 's/.*running on \(wayland-[0-9]*\).*/\1/p' "$runtime/log" | head -1)

export XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket"

"$lockptr" 20000 > "$runtime/client" 2>&1 &
cpid=$!

await() {
        waited=0
        while ! grep -q "^$1\$" "$runtime/client" 2>/dev/null; do
                kill -0 "$cpid" 2>/dev/null || fail "lockptr died waiting for $1"
                waited=$((waited + 1))
                [ "$waited" -gt 100 ] && fail "no '$1' from lockptr within 10s"
                sleep 0.1
        done
}

await mapped

# the pointer enters at the centre: the client locks, tyler must answer
"$vptr" 100 100 move 50 50
await locked

# the lock holds: these must arrive as relative motion and nothing else
"$vptr" 100 100 move 10 10 move 90 90 move 50 50
sleep 0.3

after=$(sed -n '/^locked$/,$p' "$runtime/client" | sed '1d')
relative=$(printf '%s\n' "$after" | grep -c '^relative ' || true)
motion=$(printf '%s\n' "$after" | grep -c '^motion$' || true)
leave=$(printf '%s\n' "$after" | grep -c '^leave$' || true)
locks=$(grep -c '^locked$' "$runtime/client" || true)

[ "$locks" -eq 1 ] || fail "locked $locks times, wanted 1"
[ "$relative" -ge 3 ] || fail "$relative relative motions after the lock, wanted 3"
[ "$motion" -eq 0 ] || fail "$motion absolute motions leaked through the lock"
[ "$leave" -eq 0 ] || fail "the pointer left the locked surface $leave time(s)"

# quit with the lock alive: the constraint's destroy path runs at teardown
kill -TERM "$pid"
rc=0
wait "$pid" || rc=$?
[ "$rc" -eq 0 ] || fail "quit with a live lock exited $rc, wanted 0"

kill -TERM "$cpid" 2>/dev/null || true
wait "$cpid" 2>/dev/null || true
