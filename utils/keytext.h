#pragma once
//Turns Linux key codes into the characters they type (US keyboard layout) so names can be typed into the menu.
#ifndef VANITYARC_KEYTEXT_H
#define VANITYARC_KEYTEXT_H

#include <linux/input-event-codes.h>

//The character a key types, or 0 if it types none (arrows, function keys, ...)
inline char KeyToChar(int code, bool shift) {
    struct Row { int first; const char* plain; const char* shifted; };
    static const Row rows[] = {
        {KEY_1, "1234567890-=", "!@#$%^&*()_+"},
        {KEY_Q, "qwertyuiop[]", "QWERTYUIOP{}"},
        {KEY_A, "asdfghjkl;'`", "ASDFGHJKL:\"~"},
        {KEY_Z, "zxcvbnm,./", "ZXCVBNM<>?"},
    };
    if (code == KEY_SPACE) return ' ';
    if (code == KEY_BACKSLASH) return shift ? '|' : '\\';
    for (const Row& r : rows) {
        int n = 0;
        while (r.plain[n]) n++;
        if (code >= r.first && code < r.first + n) return (shift ? r.shifted : r.plain)[code - r.first];
    }
    return 0;
}

#endif //VANITYARC_KEYTEXT_H
