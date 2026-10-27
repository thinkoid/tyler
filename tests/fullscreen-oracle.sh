#!/bin/sh
#
# Check fullscreen modality with two isolated headless outputs.
# Usage: fullscreen-oracle.sh TYLER FULLSCREEN_CLIENT
# Example: ./tests/fullscreen-oracle.sh ./build/tyler ./build/fullscreen-client
# OpenAI LeChuck

###### Global variables

tyler=$1
client=$2
runtime=
pid=

###### Functions

cleanup() {
        # Stop only the compositor this oracle launched and remove its files.
        if [ -n "$pid" ]; then
                kill -TERM "$pid" 2>/dev/null
                wait "$pid" 2>/dev/null
        fi
        [ -z "$runtime" ] || rm -rf "$runtime"
}

run_oracle() {
        # Run the client after both outputs are enrolled; report any failure.
        waited=0
        while [ "$(grep -ac 'screen HEADLESS-' "$runtime/log")" -ne 2 ]; do
                kill -0 "$pid" 2>/dev/null || return 1
                waited=$((waited + 1))
                [ "$waited" -lt 200 ] || return 1
                sleep 0.1
        done

        socket=$(sed -n 's/.*running on \(wayland-[0-9]*\).*/\1/p' "$runtime/log")
        [ -n "$socket" ] || return 1
        echo "### fullscreen client:"
        XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" "$client" || return 1
        kill -TERM "$pid" || return 1
        wait "$pid"
        result=$?
        pid=
        return "$result"
}

###### Main body

runtime=$(mktemp -d) || exit 1
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

echo "### headless compositor:"
XDG_RUNTIME_DIR="$runtime" WLR_BACKENDS=headless \
WLR_RENDERER_ALLOW_SOFTWARE=1 WLR_HEADLESS_OUTPUTS=2 \
        "$tyler" > "$runtime/log" 2>&1 &
pid=$!

if ! run_oracle; then
        echo "### compositor log:" >&2
        cat "$runtime/log" >&2
        exit 1
fi
