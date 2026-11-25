#pragma once
//Offsets that are not in the public dumps. The built-in values below were found with the probes on game build
//0x6AF5C6FC; ~/.config/kwin-helper/offsets.txt overrides them WITHOUT rebuilding, one "name value" per line (decimal or
//0x hex). After a game update, find new ones with a probe build (cmake -DKWINHELPER_PROBES=ON, see the README).
//shield_value -1 turns the shield bar off. A file list replaces the built-in held list.
#ifndef KWINHELPER_OFFSETS_H
#define KWINHELPER_OFFSETS_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace live {

//The shield: in the HealthComponent (component -1) or in another component of the pawn (pawn offset of its pointer).
//Probed with a Light Shield: current 40 -> 0 on the first hit, maximum 40 until death (both doubles).
constexpr int64_t SHIELD_VALUE = 0x1D0, SHIELD_MAX = 0x1D8;
inline int64_t shieldComponent = -1;
inline int64_t shieldValue = SHIELD_VALUE; //offset of the current shield inside that component; -1 = no shield bar
inline int64_t shieldMax = SHIELD_MAX;     //offset of the maximum, if the game keeps one (else the highest value seen is used)
inline int64_t shieldIsFloat = 0;     //1: the numbers are 4-byte floats instead of 8-byte doubles

//One list of items inside the inventory component (for the inventory popup): a TArray at `off` whose records are
//`stride` bytes long, with the item data asset pointer at `asset` inside each record. qty: offset of the stack size (int)
//in a record; -1 = not known, so each record counts as one slot
struct InvArray { int64_t off = -1, stride = 8, asset = 0; std::string category = "item"; int64_t comp = -1; int64_t qty = -1; }; //comp: pawn offset of the component holding the list; -1 = the inventory component
//Built in: what a player has in their hands (inventory component +0x530, 0x20-byte records, item at +0x10). Their
//backpack is not here: the probe found no list the game sends for other players.
inline std::vector<InvArray> builtInArrays() { InvArray held; held.off = 0x530; held.stride = 0x20; held.asset = 0x10; held.category = "held"; return {held}; }
inline std::vector<InvArray> invArrays = builtInArrays();

inline std::string path(const std::string& dir = "") {
    if (!dir.empty()) return dir + "/offsets.txt";
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/kwin-helper/offsets.txt";
}

inline void reset() {
    shieldComponent = -1;
    shieldValue = SHIELD_VALUE;
    shieldMax = SHIELD_MAX;
    shieldIsFloat = 0;
    invArrays = builtInArrays();
}

inline void load(const std::string& dir = "") {
    std::ifstream f(path(dir));
    std::string line;
    //The file's own lists replace the built-in ones (not added to them: that would count items twice). A shield set in
    //the file without its maximum gets none, rather than the built-in offset in a different place.
    bool fileLists = false, fileShield = false, fileMax = false;
    while (std::getline(f, line)) {
        if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3); //UTF-8 byte-order mark (some Windows editors add one)
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream in(line);
        std::string name;
        auto num = [&](int64_t& out) {
            std::string s;
            if (!(in >> s)) return false;
            const size_t sign = s[0] == '-' || s[0] == '+';
            const bool hex = s.compare(sign, 2, "0x") == 0 || s.compare(sign, 2, "0X") == 0;
            char* end = nullptr;
            long long v = std::strtoll(s.c_str(), &end, hex ? 16 : 10); //never octal: "0710" is 710
            if (end == s.c_str() || *end != '\0') { //the whole token must be the number ("0x718zz", "12abc" are refused)
                std::printf("[!] offsets.txt: ignored \"%s\" (\"%s\" is not a decimal or 0x hex number)\n", line.c_str(), s.c_str());
                return false;
            }
            out = v;
            return true;
        };
        //every word of the line: a probe's ready line ("if this is the shield:  shield_component -1  shield_value 0x710 ...",
        //"offsets.txt:  inv_array ...") can be pasted as it is; words that are not keys are skipped
        while (in >> name) {
            int64_t v;
            if (name == "shield_component" && num(v)) { shieldComponent = v; fileShield = true; }
            else if (name == "shield_value" && num(v)) { shieldValue = v; fileShield = true; }
            else if (name == "shield_max" && num(v)) { shieldMax = v; fileMax = true; }
            else if (name == "shield_is_float" && num(v)) shieldIsFloat = v != 0;
            else if (name == "inv_array" || name == "inv_array_in") { //inv_array <offset> <stride> <asset offset in the record> [stack size offset] [category]
                InvArray a;                                             //inv_array_in <component> <offset> <stride> <asset offset> [stack size offset] [category]: a list in another component of the pawn
                const bool other = name == "inv_array_in";
                if ((!other || (num(a.comp) && a.comp >= 0x300 && a.comp < 0x1400)) &&
                    num(a.off) && num(a.stride) && num(a.asset) && a.off >= 0 && a.stride >= 8 && a.stride <= 0x200 && a.asset >= 0 && a.asset < a.stride) {
                    std::string c;
                    if (in >> c) {
                        char* end = nullptr;
                        const bool hx = c.compare(0, 2, "0x") == 0 || c.compare(0, 2, "0X") == 0;
                        const long long q = std::strtoll(c.c_str(), &end, hx ? 16 : 10); //never octal, like num()
                        if (end != c.c_str() && *end == '\0') { //a number before the category: where the stack size sits in a record
                            if (q >= 0 && q <= a.stride - 4) a.qty = q;
                            if (!(in >> c)) c.clear();
                        }
                        if (!c.empty()) a.category = c;
                    }
                    if (!fileLists) invArrays.clear();
                    fileLists = true;
                    auto key = [](int64_t comp) { return comp < 0 ? int64_t(0xCB0) : comp; }; //-1 is the inventory component at pawn+0xCB0
                    bool dup = false;
                    for (const InvArray& b : invArrays) if (key(b.comp) == key(a.comp) && b.off == a.off) dup = true; //the same list twice would count every item twice
                    if (!dup && invArrays.size() < 16) invArrays.push_back(a);
                }
                break; //the rest of the line belongs to the list (its category)
            }
        }
    }
    //a probe's ready line for the built-in place has no maximum: keep the built-in one then
    if (fileShield && !fileMax && !(shieldComponent == -1 && shieldValue == SHIELD_VALUE)) shieldMax = -1;
}

} // namespace live

#endif //KWINHELPER_OFFSETS_H
