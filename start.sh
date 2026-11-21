#!/usr/bin/env bash
# Starts kwin-helper. Works from any folder.
#   normal run:   bash start.sh
#   other monitor: bash start.sh DP-1     (default: your primary monitor; list them with: kscreen-doctor -o)
#   with the Insert hotkey before you have logged out/in after joining group "input":
#                 sg input -c "bash start.sh"
set -euo pipefail
SELF="$(readlink -f "$0")"
cd "$(dirname "$SELF")"

# Joining the "input" group (sudo usermod -aG input $USER) only applies to NEW logins. Until then, "sg" runs this
# same script with the group, so the Insert hotkey works right away without logging out.
if [ -z "${KWINHELPER_SG:-}" ] && ! id -nG | tr ' ' '\n' | grep -qx input \
   && getent group input | cut -d: -f4 | tr ',' '\n' | grep -qx "$(id -un)"; then
    exec sg input -c "env KWINHELPER_SG=1 bash '$SELF' $*"
fi

BIN=./cmake-build-release/kwin-helper
if [ ! -x "$BIN" ]; then
    echo "Not built yet. Build it first:" >&2
    echo "  cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build-release" >&2
    exit 1
fi

# Reading another process's memory needs the ptrace capability on the binary. It is lost every time the
# program is rebuilt, so it is only set (and sudo only asked for) when it is missing.
if ! getcap "$BIN" 2>/dev/null | grep -q cap_sys_ptrace; then
    echo "[+] First start after a build: giving kwin-helper permission to read the game (sudo password needed once)"
    sudo setcap cap_sys_ptrace=eip "$BIN"
fi

# The overlay shows on the monitor the game is on: the primary one unless you name another
MON="${1:-${KWINHELPER_MONITOR:-}}"
if [ -z "$MON" ] && command -v kscreen-doctor >/dev/null 2>&1; then
    MON="$(kscreen-doctor -o 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | awk '/^Output:/ { name = $3 } /priority/ && $2 == 1 { print name; exit }' || true)"
fi
if [ -n "$MON" ]; then export KWINHELPER_MONITOR="$MON"; echo "[+] Overlay monitor: $MON"; fi

exec "$BIN"   # exec: Ctrl+C and kill reach kwin-helper itself, so it can save and release the keyboard
