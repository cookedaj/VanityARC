#pragma once
//Recoil reference: the spray pattern of a gun as a list of shots, shown in the Recoil tab. READ ONLY: nothing here
//moves the mouse or touches the game. The numbers come from ~/.config/vanityarc/recoil.txt, one gun per line:
//
//    Ferro: 0,0  0.4,1.2  -0.3,2.6  0.8,4.1        (x to the right, y upward, shot by shot, in any unit you like)
//
//Nothing is built in: the game does not publish these numbers, so they have to be measured or collected.
#ifndef VANITYARC_RECOIL_H
#define VANITYARC_RECOIL_H

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace recoil {

struct Pattern {
    std::string gun;
    std::vector<std::pair<float, float>> shots;
};

//One line of the file; false when it is a comment, empty or not understood
inline bool parseLine(const std::string& raw, Pattern& out) {
    std::string line = raw;
    const size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    const size_t colon = line.find(':');
    if (colon == std::string::npos) return false;
    std::string gun = line.substr(0, colon);
    while (!gun.empty() && (gun.back() == ' ' || gun.back() == '\t')) gun.pop_back();
    size_t a = 0;
    while (a < gun.size() && (gun[a] == ' ' || gun[a] == '\t')) a++;
    gun = gun.substr(a);
    if (gun.empty() || gun.size() > 40) return false;

    Pattern p;
    p.gun = gun;
    std::istringstream in(line.substr(colon + 1));
    std::string tok;
    while (in >> tok && p.shots.size() < 200) {
        const size_t comma = tok.find(',');
        if (comma == std::string::npos) return false;
        char *e1 = nullptr, *e2 = nullptr;
        const std::string xs = tok.substr(0, comma), ys = tok.substr(comma + 1);
        const double x = std::strtod(xs.c_str(), &e1), y = std::strtod(ys.c_str(), &e2);
        if (xs.empty() || ys.empty() || *e1 || *e2 || !std::isfinite(x) || !std::isfinite(y) || std::fabs(x) > 1e6 || std::fabs(y) > 1e6) return false;
        p.shots.push_back({(float)x, (float)y});
    }
    if (p.shots.empty()) return false;
    out = std::move(p);
    return true;
}

inline std::map<std::string, Pattern>& table() {
    static std::map<std::string, Pattern> t;
    return t;
}

inline std::string path(const std::string& dir = "") {
    if (!dir.empty()) return dir + "/recoil.txt";
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/vanityarc/recoil.txt";
}

inline void load(const std::string& dir = "") {
    table().clear();
    std::ifstream f(path(dir));
    std::string line;
    while (std::getline(f, line)) {
        Pattern p;
        if (parseLine(line, p)) table()[p.gun] = p;
    }
}

inline const Pattern* find(const std::string& gun) {
    auto it = table().find(gun);
    return it == table().end() ? nullptr : &it->second;
}

} // namespace recoil

#endif //VANITYARC_RECOIL_H
