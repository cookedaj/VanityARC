#include <cstdio>
#include <set>
#include <string>
#include "utils/keytext.h"
#include "utils/hotkey.h"
#include "utils/cue.h"
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
        //F9 (say status): a press sets it, and nothing else; its repeat and release do nothing
        feed(KEY_F9, 1);
        HandleKeyEvents(p[0], false);
        if (!g_KeySay.exchange(false) || g_KeyClear || g_KeyQuit || g_KeyStream || g_KeyMark) menu++;
        feed(KEY_F9, 2); feed(KEY_F9, 0);
        HandleKeyEvents(p[0], false);
        if (g_KeySay.exchange(false)) menu++;
        close(p[0]); close(p[1]);
    }
    std::printf("%d wrong Insert menu / F9 results\n", menu);
    //spd-say's command line: enemy warnings with priority important, Speech speed only when it is not 0
    auto line = [](const std::string& text, bool urgent) { std::string s; for (const std::string& a : cue::Args(text, urgent)) s += "[" + a + "]"; return s; };
    if (line("Low health", false) != "[spd-say][--][Low health]" || line("Enemy close", true) != "[spd-say][-P][important][--][Enemy close]") wrong++;
    config::speechRate = 20;
    if (line("-r 5", true) != "[spd-say][-P][important][-r][20][--][-r 5]") wrong++;
    config::speechRate = -49.6;
    if (line("x", false) != "[spd-say][-r][-50][--][x]") wrong++;
    config::speechRate = 0;
    cue::noPriority = true; //an spd-say that failed with -P: enemy warnings without it from then on
    if (line("Enemy close", true) != "[spd-say][--][Enemy close]") wrong++;
    cue::noPriority = false;
    std::printf("%d wrong results after the spd-say command line check\n", wrong);
    //the on-screen "Insert does not work" card: only with no keyboard open, and with the right reason
    auto why = [](int kb, int denied, bool had) { const char* w = HotkeyProblem(kb, denied, had).why; return std::string(w ? w : ""); };
    if (why(-1, 3, false) != "" || why(2, 3, false) != "" || why(1, 0, true) != "" ||
        why(0, 3, false) != "no permission to read the keyboard" || why(0, 0, false) != "no keyboard found" ||
        why(0, 3, true) != "keyboard lost" || !HotkeyProblem(0, 0, false).fix) wrong++;
    std::printf("%d wrong results after the hotkey problem check\n", wrong);
    std::printf("%s\n", bad == 0 && wrong == 0 && menu == 0 ? "ALL PASSED" : "FAILED");
    return bad + wrong + menu;
}
