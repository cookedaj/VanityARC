#pragma once
//Offsets that are not in the public dumps and can be set WITHOUT rebuilding: ~/.config/kwin-helper/offsets.txt, one
//"name value" per line (decimal or 0x hex). They are found with the probes (KWINHELPER_PROBE=shield / inventory, see
//the README) and survive game restarts. -1 means "not known": that feature simply stays off.
#ifndef KWINHELPER_OFFSETS_H
#define KWINHELPER_OFFSETS_H

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace live {

//The shield: in the HealthComponent (component -1) or in another component of the pawn (pawn offset of its pointer)
inline int64_t shieldComponent = -1;
inline int64_t shieldValue = -1;      //offset of the current shield inside that component; -1 = no shield bar
inline int64_t shieldMax = -1;        //offset of the maximum, if the game keeps one (else the highest value seen is used)
inline int64_t shieldIsFloat = 0;     //1: the numbers are 4-byte floats instead of 8-byte doubles

//One list of items inside the inventory component (for the inventory popup): a TArray at `off` whose records are
//`stride` bytes long, with the item data asset pointer at `asset` inside each record
struct InvArray { int64_t off = -1, stride = 8, asset = 0; std::string category = "item"; int64_t comp = -1; }; //comp: pawn offset of the component holding the list; -1 = the inventory component
inline std::vector<InvArray> invArrays;

inline std::string path(const std::string& dir = "") {
    if (!dir.empty()) return dir + "/offsets.txt";
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/kwin-helper/offsets.txt";
}

inline void reset() {
    shieldComponent = shieldValue = shieldMax = -1;
    shieldIsFloat = 0;
    invArrays.clear();
}

inline void load(const std::string& dir = "") {
    std::ifstream f(path(dir));
    std::string line;
    while (std::getline(f, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream in(line);
        std::string name;
        if (!(in >> name)) continue;
        auto num = [&](int64_t& out) {
            std::string s;
            if (!(in >> s)) return false;
            char* end = nullptr;
            long long v = std::strtoll(s.c_str(), &end, 0);
            if (end == s.c_str()) return false;
            out = v;
            return true;
        };
        int64_t v;
        if (name == "shield_component" && num(v)) shieldComponent = v;
        else if (name == "shield_value" && num(v)) shieldValue = v;
        else if (name == "shield_max" && num(v)) shieldMax = v;
        else if (name == "shield_is_float" && num(v)) shieldIsFloat = v != 0;
        else if (name == "inv_array" || name == "inv_array_in") { //inv_array <offset> <stride> <asset offset in the record> [category]
            InvArray a;                                             //inv_array_in <component> <offset> <stride> <asset offset> [category]: a list in another component of the pawn
            const bool other = name == "inv_array_in";
            if ((!other || (num(a.comp) && a.comp >= 0x300 && a.comp < 0x1400)) &&
                num(a.off) && num(a.stride) && num(a.asset) && a.off >= 0 && a.stride >= 8 && a.stride <= 0x200 && a.asset >= 0 && a.asset < a.stride) {
                std::string c;
                if (in >> c) a.category = c;
                if (invArrays.size() < 16) invArrays.push_back(a);
            }
        }
    }
}

} // namespace live

#endif //KWINHELPER_OFFSETS_H
