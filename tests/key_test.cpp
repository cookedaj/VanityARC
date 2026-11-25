#include <cstdio>
#include <set>
#include "utils/keytext.h"
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
    std::printf("%s\n", bad == 0 && wrong == 0 ? "ALL PASSED" : "FAILED");
    return bad + wrong;
}
