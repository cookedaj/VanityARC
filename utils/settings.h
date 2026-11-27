#pragma once
//Remembers the Insert-menu settings between runs in ~/.config/kwin-helper/settings.txt
//(one "name value" per line). Anything missing keeps its value from config.h. Every value has a safe range: a
//hand-edited or damaged file can never put the overlay in an unusable state. "Reset" in the menu restores the
//values config.h started with.
#ifndef KWINHELPER_SETTINGS_H
#define KWINHELPER_SETTINGS_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#include "../config.h"
#include "identity.h"   // ::configDir(): one source for ~/.config/kwin-helper

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
        B("showLootGuns", &config::showLootGuns); B("showLootAmmo", &config::showLootAmmo); B("showLootMeds", &config::showLootMeds);
        B("showLootShields", &config::showLootShields); B("showLootGrenades", &config::showLootGrenades);
        B("showLootGadgets", &config::showLootGadgets); B("showLootMods", &config::showLootMods); B("showLootAugments", &config::showLootAugments);
        B("showLootBlueprints", &config::showLootBlueprints); B("showLootKeys", &config::showLootKeys);
        B("showLootMaterials", &config::showLootMaterials); B("showLootValuables", &config::showLootValuables); B("showLootOther", &config::showLootOther);
        B("showContainerMedical", &config::showContainerMedical); B("showContainerWeapons", &config::showContainerWeapons);
        B("showContainerTech", &config::showContainerTech); B("showContainerIndustrial", &config::showContainerIndustrial);
        B("showContainerHome", &config::showContainerHome); B("showContainerLockers", &config::showContainerLockers);
        B("showContainerTrash", &config::showContainerTrash); B("showContainerArc", &config::showContainerArc);
        B("showContainerNature", &config::showContainerNature); B("showContainerCaches", &config::showContainerCaches);
        B("showContainerOther", &config::showContainerOther);
        B("colorLootKinds", &config::colorLootKinds); B("highlightValuable", &config::highlightValuable);
        D("valuableFrom", &config::valuableFrom, 1000, 100000);
        B("showPlayerNames", &config::showPlayerNames); B("showPlayerGuns", &config::showPlayerGuns);
        B("showArcNames", &config::showArcNames); B("showContainerNames", &config::showContainerNames);
        B("showItemNames", &config::showItemNames); B("labelCards", &config::labelCards);
        B("showSkeleton", &config::showSkeleton); B("dimHidden", &config::dimHidden); D("hiddenAlpha", &config::hiddenAlpha, 40, 255); B("showBars", &config::showBars); D("barStyle", &config::barStyle, 0, 2); B("barNumbers", &config::barNumbers); B("showFacing", &config::showFacing); D("skeletonMaxDist", &config::skeletonMaxDist, 20, 300);
        D("headMarker", &config::headMarker, 0, 1); D("skeletonColor", &config::skeletonColor, 0, 2);
        B("showOffscreen", &config::showOffscreen); B("showArcOffscreen", &config::showArcOffscreen);
        D("arcArrowDist", &config::arcArrowDist, 10, 150); D("lowHealthFrom", &config::lowHealthFrom, 10, 90);
        B("showTeammates", &config::showTeammates);
        B("closeWarning", &config::closeWarning); D("warnDist", &config::warnDist, 5, 100);
        B("hideUnknownTeams", &config::hideUnknownTeams); B("monochrome", &config::monochrome);
        B("enableRadar", &config::enableRadar); B("showSelfHealth", &config::showSelfHealth);
        D("maxPlayerDist", &config::maxPlayerDist, 50, 800); D("maxArcDist", &config::maxArcDist, 20, 500);
        D("maxLootDist", &config::maxLootDist, 5, 150); D("nameMaxLootDist", &config::nameMaxLootDist, 3, 150);
        D("boxStyle", &config::boxStyle, 0, 3); D("boxColor", &config::boxColor, 0, 5);
        B("showBoxes", &config::showBoxes); //showGroundItems / showSkeletons are other names for showPickups / showSkeleton above: no lines of their own
        D("arcColor", &config::arcColor, 0, 5); D("containerColor", &config::containerColor, 0, 5); D("itemColor", &config::itemColor, 0, 5);
        F("markerScale", &config::markerScale, 1, 3); //never under 1: markers are never smaller than they were
        F("boxThickness", &config::boxThickness, 1, 10);
        F("overlayOpacity", &config::overlayOpacity, 0.2, 1); F("menuOpacity", &config::menuOpacity, 0.5, 1);
        F("menuTextSize", &config::menuTextSize, 15, 30);
        D("trackerHz", &config::trackerHz, 30, 240);
        D("listMode", &config::listMode, 0, 2); D("listRows", &config::listRows, 3, 16);
        B("showItemValues", &config::showItemValues);
        B("showDebugInfo", &config::showDebugInfo); B("hotkeysEnabled", &config::hotkeysEnabled);
        B("soundCues", &config::soundCues); D("cueRange", &config::cueRange, 10, 150); D("speechRate", &config::speechRate, -50, 50);
        B("menuKeyboard", &config::menuKeyboard);
        //streamHide is not saved: every launch starts visible, so a stray F8 can never look like a broken overlay
        B("fovIsHorizontal", &config::fovIsHorizontal); B("useCameraRoll", &config::useCameraRoll);
        F("fovScale", &config::fovScale, 0.5, 2);
        F("centerOffsetX", &config::centerOffsetX, -300, 300); F("centerOffsetY", &config::centerOffsetY, -300, 300);
        B("gameViewAuto", &config::gameViewAuto); D("gameRenderW", &config::gameRenderW, 320, 8192);
        D("gameRenderH", &config::gameRenderH, 240, 8192); B("gameStretch", &config::gameStretch);
        F("labelSize", &config::labelSize, 10, 40); B("fullSizeText", &config::fullSizeText);
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

//Only these settings back to their defaults (for example the calibration ones). which: the config variables.
inline void reset(std::initializer_list<const void*> which) {
    const std::vector<double>& d = defaults();
    for (size_t i = 0; i < table().size(); i++)
        if (std::find(which.begin(), which.end(), table()[i].p) != which.end()) setValue(table()[i], d[i]);
}

//Brings every value back inside its range (a menu slider cannot leave it, but a bug or a text edit could)
inline void clampAll() {
    for (const Entry& e : table()) setValue(e, value(e));
}

inline std::string path(const std::string& dir = "") {
    if (!dir.empty()) return dir + "/settings.txt";
    return ::configDir() + "/settings.txt";
}

//Returns how many lines were ignored (each is printed with its line number and the expected format)
inline int load(const std::string& dir = "") {
    defaults();
    std::ifstream f(path(dir));
    std::string line;
    int lineNo = 0, ignored = 0;
    while (std::getline(f, line)) {
        lineNo++;
        if (!line.empty() && line.back() == '\r') line.pop_back(); //file edited on another system
        if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3); //UTF-8 byte-order mark (some Windows editors add one)
        std::istringstream in(line);
        std::string name;
        double v;
        if (!(in >> name) || name[0] == '#') continue; //blank line or comment
        if (name == "streamHide") continue; //no longer saved, but files from older versions still have it: not an error
        bool known = false;
        if ((in >> v) && std::isfinite(v))
            for (const Entry& e : table())
                if (name == e.name) { setValue(e, v); known = true; } //out of range: pulled to the nearest allowed value
        if (!known && ++ignored <= 5) //a few are enough to find them; the toast gives the total
            std::printf("[!] settings.txt line %d ignored: \"%s\" (expected: a setting name, a space, a number, e.g. \"showArc 1\")\n",
                        lineNo, line.c_str());
    }
    return ignored;
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
