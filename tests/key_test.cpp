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
    std::printf("%s\n", bad == 0 ? "ALL PASSED" : "FAILED");
    return bad;
}
