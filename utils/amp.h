#pragma once
//Gun levels. The game writes a level as "<Gun> <Roman numeral>" ("Stitcher IV") and an amplified gun as
//"Amplified <Gun>". The overlay shows "Stitcher 4" (levels 1 to 4) and "AMP Il Toro" (amplified, or level V).
//Amplification modules ("Amplification Module MK. I" to "MK. V") are items, not levels: they keep their game name.
#ifndef KWINHELPER_AMP_H
#define KWINHELPER_AMP_H

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace amp {

//Level of a numeral: "I".."V" (any case) or "1".."5" ("01" too); 0 for anything else
inline int numeral(const std::string& w) {
    std::string s;
    for (unsigned char c : w) s.push_back((char)std::tolower(c));
    if (s == "i" || s == "1" || s == "01") return 1;
    if (s == "ii" || s == "2" || s == "02") return 2;
    if (s == "iii" || s == "3" || s == "03") return 3;
    if (s == "iv" || s == "4" || s == "04") return 4;
    if (s == "v" || s == "5" || s == "05") return 5;
    return 0;
}

struct GunLevel {
    std::string base; //the name without its level part, so the name tables still find the gun
    int level = 0;    //1..4 = level I..IV, 5 = amplified (or level V), 0 = the name does not say
};

//A gun's level from its raw asset name or a shown name:
//  "Stitcher IV", "Stitcher 4", "Stitcher Lvl 4", "Il Toro Tier IV"   a level after a space (the game's own format)
//  "..._T0" .. "..._T3"                                                 level I..IV (icons and redirect assets)
//  "..._Level_4", "..._Tier4", "..._Lvl_IV", "..._01_IV"                a level / tier token ("_T4", "_Level_6": no level)
//  "Amplified Il Toro", "DA_Item_<Gun>_Ascended", "DA_WeaponVisuals_<Gun>_Ascended_Default_A"     amplified
//  "DA_Item_<Gun>_01_<Upgrade>", "BP_WeaponActor_<Gun>_01_<Upgrade>_C"                         amplified (upgraded)
//The "_01" after the gun is its variant number, never a level.
inline GunLevel gunLevel(const std::string& raw) {
    struct Tok { char sep; std::string text, low; }; //sep: the '_' or ' ' before the token ('\0' for the first)
    std::vector<Tok> t;
    for (char c : raw) {
        const bool sep = c == '_' || c == ' ';
        if (sep || t.empty()) t.push_back({sep ? c : '\0', "", ""});
        if (!sep) { t.back().text.push_back(c); t.back().low.push_back((char)std::tolower((unsigned char)c)); }
    }
    GunLevel g;
    std::vector<bool> drop(t.size(), false);
    size_t cut = t.size(); //from here on: the upgrade / look of an amplified gun, not part of its name
    bool numSeen = false;
    auto found = [&](int level, size_t i) { g.level = std::max(g.level, level); drop[i] = true; };
    for (size_t i = 0; i < t.size(); i++) {
        const std::string& s = t[i].low;
        if (s == "amplified" || s == "ascended") { found(5, i); if (s == "ascended") cut = std::min(cut, i); continue; }
        if (t[i].sep == '_' && s.size() == 2 && s[0] == 't' && s[1] >= '0' && s[1] <= '3') { found(s[1] - '0' + 1, i); continue; }
        if ((s == "lvl" || s == "level" || s == "tier") && i + 1 < t.size() && numeral(t[i + 1].low)) {
            found(numeral(t[i + 1].low), i);
            drop[++i] = true;
            continue;
        }
        for (const std::string w : {"lvl", "level", "tier"}) //"Level4", "TierIV" in one token; not "LowTier"
            if (s.size() > w.size() && s.compare(0, w.size(), w) == 0 && numeral(s.substr(w.size()))) found(numeral(s.substr(w.size())), i);
        if (drop[i]) continue;
        if (t[i].sep == ' ' && i + 1 == t.size() && numeral(s)) { found(numeral(s), i); continue; } //"Stitcher IV"
        if (t[i].sep == '_' && !s.empty() && s.find_first_not_of("0123456789") == std::string::npos) { numSeen = true; continue; }
        if (numSeen && t[i].sep == '_') {
            if (s.size() >= 2 && numeral(s)) { found(numeral(s), i); continue; } //"_01_IV" ("_I" / "_V" stay variant letters)
            //a level word this does not read ("_T4", "_Level_6", "Tier7") is not an upgrade
            const bool tDigits = s.size() >= 2 && s[0] == 't' && s.find_first_not_of("0123456789", 1) == std::string::npos;
            if (tDigits || s.starts_with("lvl") || s.starts_with("level") || s.starts_with("tier")) continue;
        }
        //A word after the gun's number is an upgrade ("MoreReload", "Carbine", "ChargedBurst"). "A", "C" and "02" are
        //variants, and "Default" is the plain look of a stowed gun.
        if (numSeen && t[i].sep == '_' && s.size() >= 2 && s != "default") { g.level = 5; cut = std::min(cut, i); }
    }
    for (size_t i = 0; i < cut; i++) {
        if (drop[i]) continue;
        if (!g.base.empty() && t[i].sep) g.base.push_back(t[i].sep);
        g.base += t[i].text;
    }
    if (g.base.empty()) g.base = raw;
    return g;
}

//The text shown for a gun: "Stitcher 4" for level 1 to 4, "AMP Il Toro" when amplified (5), the plain name otherwise
inline std::string gunLabel(const std::string& gun, int level) {
    if (level <= 0) return gun;
    if (level >= 5) return "AMP " + gun;
    return gun + " " + std::to_string(level);
}

} // namespace amp

#endif //KWINHELPER_AMP_H
