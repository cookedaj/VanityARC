#!/usr/bin/env bash
# Read-only check of what the game's sandbox can and cannot see of kwin-helper.
# Run it with the game open (best: in a raid), and again after a game or Steam update.
#   bash check-isolation.sh
# Exit status is the number of failed checks. It changes nothing and needs no sudo.
set -u
FAILS=0
ok()   { printf '  [ OK ] %s\n' "$1"; }
bad()  { printf '  [FAIL] %s\n' "$1"; FAILS=$((FAILS + 1)); }
note() { printf '  [ -- ] %s\n' "$1"; }

GAME="$(pgrep -x GameThread | head -1)"
if [ -z "$GAME" ]; then echo "The game is not running: start it first."; exit 1; fi
HOSTNS="$(readlink /proc/self/ns/pid)"
GAMENS="$(readlink "/proc/$GAME/ns/pid" 2>/dev/null)"

echo "1. Steam sandbox"
if flatpak info com.valvesoftware.Steam >/dev/null 2>&1; then
    ok "Steam is the Flatpak build"
    PERMS="$(flatpak info --show-permissions com.valvesoftware.Steam 2>/dev/null)"
    if printf '%s\n' "$PERMS" | grep -E '^shared=' | grep -qE '(^|[=;])pid(;|$)'; then bad "the sandbox shares the host PID namespace"; else ok "the sandbox has its own PID namespace (no pid share)"; fi
    if printf '%s\n' "$PERMS" | grep -E '^filesystems=' | grep -qE '(^|[=;])(home|host|host-os|host-etc)(:[a-z]+)?(;|$)'; then bad "the sandbox can read your home or the whole host filesystem"; else ok "the sandbox cannot read your home or the host filesystem"; fi
else
    bad "Steam is not the Flatpak build: the game can see your processes"
fi

echo "2. PID namespaces"
if [ -n "$GAMENS" ] && [ "$GAMENS" != "$HOSTNS" ]; then ok "the game is in its own PID namespace ($GAMENS), not the host's ($HOSTNS)"; else bad "the game is in the host PID namespace"; fi

echo "3. Anti-cheat processes"
FOUNDAC=0
for p in $(pgrep -f -i 'denuvo|easyanticheat|eac_|battleye|beservice' 2>/dev/null); do
    [ "$p" = "$$" ] && continue
    c="$(cat "/proc/$p/comm" 2>/dev/null)"; ns="$(readlink "/proc/$p/ns/pid" 2>/dev/null)"
    case "$c" in bash|sh|pgrep|grep|check-isolation*) continue;; esac
    FOUNDAC=1
    if [ "$ns" = "$HOSTNS" ]; then bad "$c (pid $p) runs on the HOST"; else ok "$c (pid $p) runs inside the game's sandbox ($ns)"; fi
done
[ "$FOUNDAC" = 0 ] && note "no anti-cheat process found right now (it may start only in a raid)"

echo "4. Tracing"
tp="$(grep -E '^TracerPid' "/proc/$GAME/status" | awk '{print $2}')"
if [ "$tp" = "0" ]; then ok "nothing is ptrace-attached to the game"; else bad "the game is being traced by pid $tp"; fi

echo "5. Wayland: what the sandbox's socket offers"
SOCK="$(awk '{ if (match($4, /\/\.flatpak\/wl\/wayland-[A-Za-z0-9]+/)) { print substr($4, RSTART, RLENGTH); exit } }' "/proc/$GAME/mountinfo" 2>/dev/null)"
if [ -z "$SOCK" ]; then
    note "could not find the sandbox's Wayland socket"
elif ! command -v wayland-info >/dev/null 2>&1; then
    note "wayland-info is not installed (package wayland-utils): skipped"
else
    GLOBALS="$(WAYLAND_DISPLAY="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}$SOCK" wayland-info 2>/dev/null | sed -n "s/^interface: '\([^']*\)'.*/\1/p")"
    if [ -z "$GLOBALS" ]; then note "the sandbox socket did not answer: skipped"
    elif printf '%s\n' "$GLOBALS" | grep -qE 'foreign_toplevel|plasma_window_management|screencast|screencopy|image_copy_capture|image_capture_source'; then
        bad "the sandbox can list windows or capture the screen: $(printf '%s\n' "$GLOBALS" | grep -E 'foreign_toplevel|plasma_window_management|screencast|screencopy|image_copy_capture|image_capture_source' | tr '\n' ' ')"
    else ok "no window-listing or screen-capture protocol is offered to the sandbox"; fi
fi

echo "6. kwin-helper itself"
KP="$(pgrep -x kwin-helper | head -1)"
if [ -z "$KP" ]; then
    note "kwin-helper is not running: start it and run this again for the live checks"
else
    [ "$(awk '/^NSpid:/ {print NF-1}' "/proc/$KP/status" 2>/dev/null)" = 1 ] && ok "it runs in the host PID namespace" || bad "it runs inside a sandbox namespace"
    seen=0
    for d in $(ls "/proc/$GAME/root/proc" 2>/dev/null | grep -E '^[0-9]+$'); do
        [ "$(cat "/proc/$GAME/root/proc/$d/comm" 2>/dev/null)" = "kwin-helper" ] && seen=1
    done
    [ "$seen" = 0 ] && ok "the game's own /proc does not list it" || bad "the game's own /proc lists it"
    if [ ! -r "/proc/$KP/environ" ]; then
        note "its ptrace permission hides its sockets and environment from you: not checked"
    else
        if ! command -v ss >/dev/null 2>&1; then note "ss is not installed (package iproute2): socket check skipped"
        elif ss -xp 2>/dev/null | grep "pid=$KP," | awk '{print $5}' | grep -q '^@'; then bad "it holds a named abstract socket (visible through the shared network namespace)"; else ok "it holds no named abstract socket"; fi
        tr '\0' '\n' < "/proc/$KP/environ" 2>/dev/null | grep -qiE 'vanity|raiders|pioneer|steam|proton' && note "its environment names the project folder (readable only from the host, not from the sandbox)" || ok "its environment is clean"
    fi
fi

echo
if [ "$FAILS" = 0 ]; then echo "All checks passed."; else echo "$FAILS check(s) failed."; fi
exit "$FAILS"
