#pragma once
//Turns Linux key codes into the characters they type (US keyboard layout) so names can be typed into the menu.
#ifndef KWINHELPER_KEYTEXT_H
#define KWINHELPER_KEYTEXT_H

#include <linux/input-event-codes.h>

//The character a key types, or 0 if it types none (arrows, function keys, ...). caps: Caps Lock is on (letters only).
inline char KeyToChar(int code, bool shift, bool caps = false) {
    struct Row { int first; const char* plain; const char* shifted; };
    static const Row rows[] = {
        {KEY_1, "1234567890-=", "!@#$%^&*()_+"},
        {KEY_Q, "qwertyuiop[]", "QWERTYUIOP{}"},
        {KEY_A, "asdfghjkl;'`", "ASDFGHJKL:\"~"},
        {KEY_Z, "zxcvbnm,./", "ZXCVBNM<>?"},
        {KEY_KP7, "789-456+1230.", "789-456+1230."}, //the number pad: KP7 to KPDOT are numbered in a row
    };
    if (code == KEY_SPACE) return ' ';
    if (code == KEY_BACKSLASH) return shift ? '|' : '\\';
    if (code == KEY_KPSLASH) return '/';
    if (code == KEY_KPASTERISK) return '*';
    for (const Row& r : rows) {
        int n = 0;
        while (r.plain[n]) n++;
        if (code >= r.first && code < r.first + n) {
            char c = (shift ? r.shifted : r.plain)[code - r.first];
            if (caps && c >= 'a' && c <= 'z') return char(c - 'a' + 'A');
            if (caps && c >= 'A' && c <= 'Z') return char(c - 'A' + 'a');
            return c;
        }
    }
    return 0;
}

#endif //KWINHELPER_KEYTEXT_H
