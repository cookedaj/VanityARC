#include <cstdio>
#include <set>
#include <string>
#include "utils/keytext.h"
#include "utils/hotkey.h"
int main() {
    int bad = 0, typed = 0;
    std::set<char> seen;
    for (int code = -5; code < 800; code++)
        for (int shift = 0; shift < 2; shift++) {
            char c = KeyToChar(code, shift);
            if (c == 0) continue;
            typed++;
            if (c < 32 || c > 126) bad++;
            if (!shift) seen.insert(c);
        }
    std::printf("%d typed characters, %d outside printable ASCII, %zu distinct unshifted characters\n", typed, bad, seen.size());
    //the number pad types digits, and Caps Lock changes letters only
    int wrong = 0;
    if (KeyToChar(KEY_KP7, false) != '7' || KeyToChar(KEY_KP0, false) != '0' || KeyToChar(KEY_KPDOT, false) != '.' ||
        KeyToChar(KEY_KPMINUS, true) != '-' || KeyToChar(KEY_KPSLASH, false) != '/' || KeyToChar(KEY_KPASTERISK, false) != '*') wrong++;
    if (KeyToChar(KEY_A, false, true) != 'A' || KeyToChar(KEY_A, true, true) != 'a' ||
        KeyToChar(KEY_1, false, true) != '1' || KeyToChar(KEY_LEFTBRACE, false, true) != '[') wrong++;
    std::printf("%d wrong number pad / Caps Lock results\n", wrong);
    //Insert toggles the menu with Shift held (sprinting); its release and key repeat do nothing.
    //A pipe stands in for the keyboard device (it cannot report held keys, so this checks the event path).
    int menu = 0, p[2];
    if (pipe2(p, O_NONBLOCK) != 0) menu++;
    else {
        auto feed = [&](int code, int value) {
            input_event e{}; e.type = EV_KEY; e.code = (unsigned short)code; e.value = value;
            if (write(p[1], &e, sizeof e) != (ssize_t)sizeof e) menu++;
        };
        feed(KEY_LEFTSHIFT, 1); feed(MENU_KEY, 1);
        if (!HandleKeyEvents(p[0], false) || !g_MenuToggle.exchange(false)) menu++;
        feed(MENU_KEY, 2); feed(MENU_KEY, 0); feed(KEY_LEFTSHIFT, 0);
        HandleKeyEvents(p[0], false);
        if (g_MenuToggle.exchange(false)) menu++;
        close(p[0]); close(p[1]);
    }
    std::printf("%d wrong Insert menu results\n", menu);
    //the on-screen "Insert does not work" card: only with no keyboard open, and with the right reason
    auto why = [](int kb, int denied, bool had) { const char* w = HotkeyProblem(kb, denied, had).why; return std::string(w ? w : ""); };
    if (why(-1, 3, false) != "" || why(2, 3, false) != "" || why(1, 0, true) != "" ||
        why(0, 3, false) != "no permission to read the keyboard" || why(0, 0, false) != "no keyboard found" ||
        why(0, 3, true) != "keyboard lost" || !HotkeyProblem(0, 0, false).fix) wrong++;
    std::printf("%d wrong results after the hotkey problem check\n", wrong);
    std::printf("%s\n", bad == 0 && wrong == 0 && menu == 0 ? "ALL PASSED" : "FAILED");
    return bad + wrong + menu;
}
