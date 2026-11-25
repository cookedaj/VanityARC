#pragma once
//Remembers the Insert-menu settings between runs in ~/.config/kwin-helper/settings.txt
//(one "name value" per line). Anything missing keeps its value from config.h. Every value has a safe range: a
//hand-edited or damaged file can never put the overlay in an unusable state. "Reset" in the menu restores the
//values config.h started with.
#ifndef KWINHELPER_SETTINGS_H
#define KWINHELPER_SETTINGS_H

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../config.h"

namespace settings {

struct Entry { const char* name; char type; void* p; double lo, hi; }; // type: b = bool, d = double, f = float

inline std::vector<Entry>& table() {
    static std::vector<Entry> t = [] {
        std::vector<Entry> v;
        auto B = [&](const char* n, bool* p) { v.push_back({n, 'b', p, 0, 1}); };
        auto D = [&](const char* n, double* p, double lo, double hi) { v.push_back({n, 'd', p, lo, hi}); };
        auto F = [&](const char* n, float* p, double lo, double hi) { v.push_back({n, 'f', p, lo, hi}); };
        B("espEnabled", &config::espEnabled);
        B("showPlayers", &config::showPlayers); B("showArc", &config::showArc); B("showPickups", &config::showPickups);
        B("showContainers", &config::showContainers); B("showDeadPlayers", &config::showDeadPlayers);
        B("showDeadArc", &config::showDeadArc); B("showTurrets", &config::showTurrets);
        B("showPlayerNames", &config::showPlayerNames); B("showPlayerGuns", &config::showPlayerGuns);
        B("showArcNames", &config::showArcNames); B("showContainerNames", &config::showContainerNames);
        B("showItemNames", &config::showItemNames); B("labelCards", &config::labelCards);
        B("showSkeleton", &config::showSkeleton); B("dimHidden", &config::dimHidden); D("hiddenAlpha", &config::hiddenAlpha, 40, 255); B("showBars", &config::showBars); D("skeletonMaxDist", &config::skeletonMaxDist, 20, 300);
        B("showOffscreen", &config::showOffscreen); B("showTeammates", &config::showTeammates);
        B("hideUnknownTeams", &config::hideUnknownTeams); B("monochrome", &config::monochrome);
        B("enableRadar", &config::enableRadar);
        D("maxPlayerDist", &config::maxPlayerDist, 50, 800); D("maxArcDist", &config::maxArcDist, 20, 500);
        D("maxLootDist", &config::maxLootDist, 5, 150); D("nameMaxLootDist", &config::nameMaxLootDist, 3, 150);
        D("boxStyle", &config::boxStyle, 0, 2); D("boxColor", &config::boxColor, 0, 5);
        F("boxThickness", &config::boxThickness, 1, 4);
        F("overlayOpacity", &config::overlayOpacity, 0.2, 1); F("menuOpacity", &config::menuOpacity, 0.5, 1);
        D("trackerHz", &config::trackerHz, 30, 240);
        D("listMode", &config::listMode, 0, 2); D("listRows", &config::listRows, 3, 16);
        B("showDebugInfo", &config::showDebugInfo); B("hotkeysEnabled", &config::hotkeysEnabled);
        //streamHide is not saved: every launch starts visible, so a stray F8 can never look like a broken overlay
        B("fovIsHorizontal", &config::fovIsHorizontal); B("useCameraRoll", &config::useCameraRoll);
        F("fovScale", &config::fovScale, 0.5, 2);
        F("centerOffsetX", &config::centerOffsetX, -300, 300); F("centerOffsetY", &config::centerOffsetY, -300, 300);
        B("gameViewAuto", &config::gameViewAuto); D("gameRenderW", &config::gameRenderW, 320, 8192);
        D("gameRenderH", &config::gameRenderH, 240, 8192); B("gameStretch", &config::gameStretch);
        F("labelSize", &config::labelSize, 10, 28);
        F("radarCenterX", &config::radarCenterX, 0, 8192); F("radarCenterY", &config::radarCenterY, 0, 8192);
        F("radarRadius", &config::radarRadius, 50, 300); F("scale", &config::scale, 10, 2000);
        return v;
    }();
    return t;
}

inline double value(const Entry& e) {
    return e.type == 'b' ? (double)*(bool*)e.p : e.type == 'd' ? *(double*)e.p : (double)*(float*)e.p;
}

//Sets a value inside its safe range. NaN and infinity are ignored.
inline void setValue(const Entry& e, double v) {
    if (!std::isfinite(v)) return;
    v = std::min(e.hi, std::max(e.lo, v));
    if (e.type == 'b') *(bool*)e.p = v != 0;
    else if (e.type == 'd') *(double*)e.p = v;
    else *(float*)e.p = (float)v;
}

//What config.h starts with. The first call (settings::load() does it) must come before anything changes a value.
inline std::vector<double>& defaults() {
    static std::vector<double> d = [] {
        std::vector<double> r;
        for (const Entry& e : table()) r.push_back(value(e));
        return r;
    }();
    return d;
}

inline void resetAll() {
    const std::vector<double>& d = defaults();
    for (size_t i = 0; i < table().size(); i++) setValue(table()[i], d[i]);
}

//Brings every value back inside its range (a menu slider cannot leave it, but a bug or a text edit could)
inline void clampAll() {
    for (const Entry& e : table()) setValue(e, value(e));
}

inline std::string path(const std::string& dir = "") {
    if (!dir.empty()) return dir + "/settings.txt";
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/kwin-helper/settings.txt";
}

inline void load(const std::string& dir = "") {
    defaults();
    std::ifstream f(path(dir));
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::string name;
        double v;
        if (!(in >> name >> v)) continue;
        for (const Entry& e : table())
            if (name == e.name) setValue(e, v); //out of range: pulled to the nearest allowed value; NaN: ignored
    }
}

inline void save(const std::string& dir = "") {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path(dir)).parent_path(), ec);
    const std::string target = path(dir), tmp = target + ".tmp"; //write aside, then rename: never a half-written file
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        f.precision(10);
        for (const Entry& e : table()) f << e.name << ' ' << value(e) << '\n';
        f.flush();
        if (!f) { std::filesystem::remove(tmp, ec); return; }
    }
    std::filesystem::rename(tmp, target, ec);
}

} // namespace settings

#endif //KWINHELPER_SETTINGS_H
