#pragma once
//Amplification modules: "Amplification Module MK. I" to "MK. V". MK I to IV is the level; MK V means the gun is amplified (AMP).
#ifndef KWINHELPER_AMP_H
#define KWINHELPER_AMP_H

#include <cctype>
#include <string>

namespace amp {

//1..5 for an amplification module (MK I..V, or MK 1..5), 0 for anything else. Ignores case, spaces and punctuation.
inline int moduleLevel(const std::string& name) {
    std::string s;
    for (unsigned char c : name) if (std::isalnum(c)) s.push_back((char)std::tolower(c));
    if (s.find("amplificationmodule") == std::string::npos) return 0;
    size_t mk = s.rfind("mk");
    if (mk == std::string::npos) return 0;
    const std::string tok = s.substr(mk + 2);
    if (tok == "i" || tok == "1") return 1;
    if (tok == "ii" || tok == "2") return 2;
    if (tok == "iii" || tok == "3") return 3;
    if (tok == "iv" || tok == "4") return 4;
    if (tok == "v" || tok == "5") return 5;
    return 0;
}

//The level as text: "MK I".."MK IV", "AMP" at MK V, "" for no module
inline const char* levelText(int level) {
    if (level <= 0) return "";
    if (level >= 5) return "AMP";
    return level == 1 ? "MK I" : level == 2 ? "MK II" : level == 3 ? "MK III" : "MK IV";
}

//The text shown for a gun with a module of this level: "AMP Il Toro" at MK V, "Il Toro  MK III" below that
inline std::string gunLabel(const std::string& gun, int level) {
    if (level <= 0) return gun;
    if (level >= 5) return "AMP " + gun;
    return gun + "  " + levelText(level);
}

} // namespace amp

#endif //KWINHELPER_AMP_H
