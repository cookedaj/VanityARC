#!/usr/bin/env bash
# Only needed for the FALLBACK overlay window (KWINHELPER_BACKEND=glfw, or a compositor without layer-shell).
# The normal overlay is a layer-shell window: it shows above fullscreen games by itself and needs no KDE rule.
# One-time setup for the kwin-helper overlay on KDE Plasma (Wayland). Safe to re-run.
# Edits the KDE window rule "KWin Helper Fix" (creates it if missing) so the overlay:
#   - stays above the game, with no title bar (the title bar shifts everything down ~25 px)
#   - never takes keyboard focus from the game, and is pinned to the top-left of ONE monitor, at that monitor's size
# Usage: bash fix-overlay-rule.sh [MONITOR]   e.g. DP-1. Default = your primary monitor.
# Pick the monitor the GAME is on (list them with: kscreen-doctor -o).
# The game itself must be in WINDOWED mode: KDE draws an active fullscreen window above
# everything, including "keep above" windows.
set -euo pipefail

WANT="${1:-}"
OUT="$(kscreen-doctor -o 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' || true)"
# Top-left corner ("x,y") and size ("WxH") of the chosen monitor; the primary monitor has priority 1
GEO="$(printf '%s\n' "$OUT" | awk -v want="$WANT" '
    /^Output:/  { name = $3; prio = "" }
    /priority/  { prio = $2 }
    /Geometry:/ { split($2, a, ","); if ((want != "" && name == want) || (want == "" && prio == 1)) { print a[1] "," a[2] " " $3; exit } }')"
POS="${GEO%% *}"
WH="${GEO#* }"
if [ -z "$POS" ] || [ -z "$WH" ]; then
    echo "Could not find monitor '${WANT:-primary}'. Your monitors:" >&2
    printf '%s\n' "$OUT" | grep '^Output:' >&2 || true
    exit 1
fi

F="${KWINRULES_FILE:-kwinrulesrc}"
TITLE="KWin Helper"          # must match OVERLAY_NAME in config.h
DESC="KWin Helper Fix"

rd() { kreadconfig6 --file "$F" "$@"; }
wr() { kwriteconfig6 --file "$F" "$@"; }

# Reuse the rule if it already exists
GROUP=""
for g in $(rd --group General --key rules | tr ',' ' '); do
    if [ "$(rd --group "$g" --key Description)" = "$DESC" ]; then GROUP="$g"; break; fi
done
if [ -z "$GROUP" ]; then
    GROUP="$(cat /proc/sys/kernel/random/uuid)"
    rules="$(rd --group General --key rules)"
    count="$(rd --group General --key count --default 0)"
    wr --group General --key rules "${rules:+$rules,}$GROUP"
    wr --group General --key count "$((count + 1))"
    wr --group "$GROUP" --key Description "$DESC"
fi

# Match on the window title (GLFW gives Wayland windows no usable window class)
wr --group "$GROUP" --key wmclassmatch 0
wr --group "$GROUP" --key title "$TITLE"
wr --group "$GROUP" --key titlematch 1

# "rule 2" = Force
force() { wr --group "$GROUP" --key "$1" "$2"; wr --group "$GROUP" --key "${1}rule" 2; }
force above true
force noborder true
force acceptfocus false
force position "$POS"
force size "${WH/x/,}"

qdbus6 org.kde.KWin /KWin reconfigure >/dev/null 2>&1 || true
echo "Done. Overlay pinned to monitor '${WANT:-primary}' at $POS, size $WH. Restart kwin-helper (bash start.sh) to apply it."
