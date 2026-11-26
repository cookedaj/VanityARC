#ifndef KWINHELPER_ESP_H
#define KWINHELPER_ESP_H
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Declutter.h"
#include "Overlay.h"
#include "W2S.h"
#include "../utils/gameview.h"
#include "../utils/itemvalues.h"
#include "../utils/labels.h"
#include "../utils/localUtil.h"
#include "../utils/marks.h"

inline double getProportionalRadius(double dist, double maxDist) {
    return (1-(dist/maxDist)) * 6 + 4;
}

//The projection settings from config.h / the Calibration part of the menu
inline Projection MakeProjection() {
    Projection p;
    if (g_gameView.renderW > 0 && g_gameView.renderH > 0) { //gamescope / stretched resolution: see gameview.h
        p.width = g_gameView.renderW;
        p.height = g_gameView.renderH;
        p.dx = g_gameView.dx; p.dy = g_gameView.dy; p.dw = g_gameView.dw; p.dh = g_gameView.dh;
    } else {
        p.width = config::SCREEN_W;
        p.height = config::SCREEN_H;
    }
    p.fovHorizontal = config::fovIsHorizontal;
    p.scale = config::fovScale;
    p.offX = config::centerOffsetX;
    p.offY = config::centerOffsetY;
    p.useRoll = config::useCameraRoll;
    return p;
}

//How far down the screen the crosshair is: the middle of the game's picture, moved by Shift Y (as View's cy)
inline float CrosshairY() {
    const Projection p = MakeProjection();
    return (float)(p.dy + (p.dh > 0 ? p.dh : p.height) / 2.0 + p.offY);
}
//...and across: moved by Shift X (as View's cx)
inline float CrosshairX() {
    const Projection p = MakeProjection();
    return (float)(p.dx + (p.dw > 0 ? p.dw : p.width) / 2.0 + p.offX);
}

namespace COLOR {
    inline ImU32 red = IM_COL32(207, 56, 56, 255);
    inline ImU32 deadRed = IM_COL32(210, 120, 120, 255);   //a down player's cross: a light, greyish red, not the enemy red
    inline ImU32 orange = IM_COL32(240, 168, 43, 255);
    inline ImU32 deadOrange = IM_COL32(190, 150, 100, 255); //destroyed ARC: a light, greyish orange, not the live ARC orange
    inline ImU32 green = IM_COL32(34, 189, 39, 255);
    inline ImU32 cyan = IM_COL32(39, 219, 217, 255);
    inline ImU32 white = IM_COL32(255, 255, 255, 255);
    inline ImU32 grey = IM_COL32(170, 170, 170, 255);

    inline ImU32 shield = IM_COL32(80, 170, 255, 255);

    inline bool mono() { return config::monochrome; } //white and grey only

    //The enemy colour chosen in the menu
    inline ImU32 enemy() {
        if (mono()) return white;
        switch ((int)config::boxColor) {
            case 1: return IM_COL32(255, 255, 255, 255);
            case 2: return IM_COL32(245, 215, 60, 255);
            case 3: return IM_COL32(70, 120, 255, 255);
            case 4: return IM_COL32(40, 200, 70, 255);
            case 5: return IM_COL32(170, 90, 255, 255);
            default: return red;
        }
    }
    inline ImU32 mate() { return mono() ? grey : green; }
    //Down players and destroyed ARC: solid, never faint; told apart from the living by their shape and the word on their
    //label (DOWN, DEAD), and in monochrome by a mid grey instead of white
    inline ImU32 deadGrey = IM_COL32(150, 150, 150, 255);
    inline ImU32 arc(bool dead) { return mono() ? (dead ? deadGrey : white) : (dead ? deadOrange : orange); }
    inline ImU32 container() { return mono() ? IM_COL32(150, 150, 150, 170) : green; }
    //Ground items: cyan. With "Colour items by kind" (Loot tab) guns are magenta, ammo white, meds green and shields blue;
    //the rest stay cyan. Monochrome: light grey for all of them.
    inline ImU32 pickup(int kind = -1) {
        if (mono()) return IM_COL32(215, 215, 215, 200);
        if (config::colorLootKinds) switch ((labels::Kind)kind) {
            case labels::Kind::Gun: return IM_COL32(255, 60, 220, 255);
            case labels::Kind::Ammo: return IM_COL32(255, 255, 255, 255);
            case labels::Kind::Meds: return IM_COL32(90, 255, 90, 255);
            case labels::Kind::Shield: return IM_COL32(70, 150, 255, 255);
            default: break;
        }
        return cyan;
    }
    inline ImU32 gold = IM_COL32(255, 215, 0, 255);
    inline ImU32 valuable() { return mono() ? white : gold; } //"Highlight valuable items" (Loot tab)
    inline ImU32 deadPlayer() { return mono() ? deadGrey : deadRed; }
    inline ImU32 shieldBar() { return mono() ? IM_COL32(170, 170, 170, 255) : shield; }

    inline ImU32 withAlpha(ImU32 color, int alpha) {
        return (color & 0x00FFFFFF) | ((ImU32)(alpha & 0xFF) << 24);
    }

    //Red at 0 to green at 1 (white in monochrome)
    inline ImU32 health(float f) {
        f = std::min(1.0f, std::max(0.0f, f));
        if (mono()) return white;
        const int r = (int)(207 + (34 - 207) * f), g = (int)(56 + (189 - 56) * f), b = (int)(56 + (39 - 56) * f);
        return IM_COL32(r, g, b, 255);
    }
}

inline void ESPInit() {
    if (config::makeLootESPTranslucent) {
        COLOR::green = COLOR::withAlpha(COLOR::green, 128);
        COLOR::cyan = COLOR::withAlpha(COLOR::cyan, 128);
    }
    if (config::makeDeadBotESPTranslucent) COLOR::deadOrange = COLOR::withAlpha(COLOR::deadOrange, 128);
    if (config::makeDeadPlayerESPTranslucent) COLOR::deadRed = COLOR::withAlpha(COLOR::deadRed, 128);
}

//How much bigger markers, bars, edge arrows and the radar's lines and dots are drawn: they grow with Text size, and are
//never smaller than at the 15 px default (1)
inline float SizeScale() { return std::max(1.0f, config::labelSize / 15.0f); }

//The size of the smaller text (distances, guns, the labels by markers, the radar's ring ranges, the status card): f times
//Text size, or the full Text size with "Full-size small text" (Settings tab, LOOK)
inline float SmallTextSize(float f) { return config::labelSize * (config::fullSizeText ? 1.0f : f); }

//The Loot tab's switch for each kind of ground item (labels::Kind order) and each group of containers (labels::Group order)
inline bool* const* LootSwitches() {
    static bool* const s[] = {&config::showLootGuns, &config::showLootAmmo, &config::showLootMeds, &config::showLootShields,
                              &config::showLootGrenades, &config::showLootGadgets, &config::showLootMods, &config::showLootAugments,
                              &config::showLootBlueprints, &config::showLootKeys, &config::showLootMaterials, &config::showLootValuables,
                              &config::showLootOther};
    static_assert(sizeof s / sizeof *s == labels::KIND_COUNT, "one switch per kind");
    return s;
}
inline bool* const* ContainerSwitches() {
    static bool* const s[] = {&config::showContainerMedical, &config::showContainerWeapons, &config::showContainerTech,
                              &config::showContainerIndustrial, &config::showContainerHome, &config::showContainerLockers,
                              &config::showContainerTrash, &config::showContainerArc, &config::showContainerNature,
                              &config::showContainerCaches, &config::showContainerOther};
    static_assert(sizeof s / sizeof *s == labels::GROUP_COUNT, "one switch per group");
    return s;
}
//Is this kind (group) switched on? Not read yet (-1) or anything unknown follows Other, the last switch.
inline bool KindShown(bool* const* s, int count, int kind) { return *s[kind >= 0 && kind < count ? kind : count - 1]; }

//A price as a short suffix: "800", "14k", "14.5k", "1.2M"
inline std::string ShortPrice(long v) {
    char b[24];
    if (v < 1000) std::snprintf(b, sizeof b, "%ld", v);
    else if (v < 1000000 && (v % 1000 < 50 || v % 1000 >= 950)) std::snprintf(b, sizeof b, "%.0fk", v / 1000.0);
    else if (v < 1000000) std::snprintf(b, sizeof b, "%.1fk", v / 1000.0);
    else std::snprintf(b, sizeof b, "%.1fM", v / 1000000.0);
    return b;
}

//Your squad (same team id) or players you marked by hand
inline bool IsTeammate(const RenderEntity& e, int localTeam) {
    return (localTeam >= 0 && e.teamID == localTeam) || marks::isMarked(e.account);
}

//--- short messages at the top of the screen ("Teammate marked: ..."). Up to TOAST_COUNT show at once, one under the
//other in the order they came, each for its own time, so a second message never wipes the first and none has to wait
//(a "press again within 2 s" message would be too late). A fourth one makes the oldest go; the same text again only
//starts its time anew.
constexpr size_t TOAST_COUNT = 3;
struct ToastMsg { std::string text; double until; };
inline std::vector<ToastMsg> g_toasts;
inline void Toast(const std::string& text, double seconds = 3.0) {
    const double until = ImGui::GetTime() + seconds;
    for (ToastMsg& m : g_toasts) if (m.text == text) { m.until = until; return; }
    if (g_toasts.size() >= TOAST_COUNT) g_toasts.erase(g_toasts.begin());
    g_toasts.push_back({text, until});
}

//Where a card of text at size, wanted with its middle at y, goes so that all of it (its top padding and light border too)
//is on the screen: y, or just low enough to keep 2 px under the screen's top
inline float OnScreenCardY(float y, const char* text, float size) { return std::max(y, LabelSize(text, size).y / 2 + CARD_PAD_Y + 2); }

//Where the cards at the top of the screen go (main.cpp: GAME UPDATED, NO LIVE GAME DATA, the status card, the card
//about Insert and the F-keys, the overlay window problem): while the menu is closed over every window, as the warnings
//are (a player list with a big Menu text size and many List rows reaches the screen's top and hid them); while it is
//open under the menu (on top they would block menu rows for long). The short messages always go over every window.
inline ImDrawList* TopMessageList(bool menuOpen) { return menuOpen ? nullptr : ImGui::GetForegroundDrawList(); }

//Status card at the top, in the middle: its lines a little smaller than Text size, or in the full Text size with
//Full-size small text. Never off the screen's top. Returns where the next thing can go. dl: where it goes (null: the
//background list).
inline float DrawStatusCard(const std::string& line1, const std::string& line2, float y = 20, ImDrawList* dl = nullptr) {
    const float x = (float)config::SCREEN_W * 0.5f, ls = config::labelSize, s1 = SmallTextSize(0.9f), s2 = SmallTextSize(0.8f);
    y = std::max(y, s1 / 2 + CARD_PAD_Y + 2);
    DrawLabelCentered(x, y, line1.c_str(), s1, COLOR::white, true, dl);
    y += (s1 + s2) / 2 + 0.15f * ls + 10; //Text size + 10 between their middles when they are 0.9 and 0.8 times it
    if (!line2.empty()) { DrawLabelCentered(x, y, line2.c_str(), s2, COLOR::grey, true, dl); y += s2 + 10; }
    return y;
}

//Insert cannot open the menu: the INSERT AND F-KEYS OFF card with why (1.3 x Text size), how to fix it and how to open
//the menu meanwhile, fully solid whatever Overlay opacity says, its top never off the screen. y: the next free line at
//the top. Returns where the next thing can go. dl: where it goes (null: the background list).
inline float DrawHotkeyCard(float y, const char* why, const char* fix, ImDrawList* dl = nullptr) {
    const FullStrength solid;
    const float x = (float)config::SCREEN_W * 0.5f, ls = config::labelSize, big = ls * 1.3f;
    const std::string title = std::string("INSERT AND F-KEYS OFF: ") + why;
    y = OnScreenCardY(y + (big - ls) / 2, title.c_str(), big);
    DrawLabelCentered(x, y, title.c_str(), big, COLOR::white, true, dl);
    y += big / 2 + 8 + ls / 2;
    DrawLabelCentered(x, y, fix, ls, COLOR::white, true, dl);
    y += ls + 8;
    DrawLabelCentered(x, y, "Open the menu meanwhile with:  pkill -USR1 -x kwin-helper", ls, COLOR::white, true, dl);
    return y + ls + 10;
}

//Lets go of the messages whose time is up; true while any is left to show
inline bool ToastShowing() {
    const double now = ImGui::GetTime();
    std::erase_if(g_toasts, [&](const ToastMsg& m) { return now >= m.until; });
    return !g_toasts.empty();
}

//The messages from y down (y: the middle of the first one's line), their cards TOAST_GAP px apart. dl: where to draw
//(null: the background list; main.cpp passes the foreground list while the menu is open). Returns the bottom of the last
//one's card (y when none shows).
constexpr float TOAST_GAP = 6;
inline float DrawToast(float y, ImDrawList* dl = nullptr) {
    if (!ToastShowing()) return y;
    const float ls = config::labelSize;
    float lastHalf = -1; //half the card height of the message above (-1: none yet)
    for (const ToastMsg& m : g_toasts) {
        const float half = LabelSize(m.text.c_str(), ls).y / 2 + CARD_PAD_Y;
        if (lastHalf >= 0) y += lastHalf + TOAST_GAP + half;
        else y = OnScreenCardY(y, m.text.c_str(), ls); //the first one never off the screen's top
        DrawLabelCentered((float)config::SCREEN_W * 0.5f, y, m.text.c_str(), ls, COLOR::white, true, dl);
        lastHalf = half;
    }
    return y + lastHalf;
}

//Where a point is from the camera, flat (the camera's yaw only, so looking up or down changes nothing): f ahead and r to
//the right, in cm (negative: behind, to the left)
inline void FlatOffset(const FminimalViewInfo& cam, const Vector3& pos, double& f, double& r) {
    const double yaw = cam.Rotation.y * 3.14159265358979 / 180.0;
    const double dx = pos.x - cam.Location.x, dy = pos.y - cam.Location.y;
    f = dx * std::cos(yaw) + dy * std::sin(yaw);
    r = dy * std::cos(yaw) - dx * std::sin(yaw);
}

//Which way a point is from the camera, flat: 0 ahead, 1 behind, 2 left, 3 right, each a quarter of the circle; -1 when it
//cannot be told. The card and the voice both use it.
inline int FlatDir(const FminimalViewInfo& cam, const Vector3& pos) {
    double f, r;
    FlatOffset(cam, pos, f, r);
    return !std::isfinite(f + r) ? -1 : f >= std::abs(r) ? 0 : -f >= std::abs(r) ? 1 : r > 0 ? 3 : 2;
}

//1 when a point is more than HEIGHT_WORD_M higher than the camera, -1 when that much lower, else 0 (also when it cannot
//be told). The card's ABOVE / BELOW and the voice's "above" / "below" use it.
constexpr double HEIGHT_WORD_M = 3.0;
inline int HeightDir(const FminimalViewInfo& cam, const Vector3& pos) {
    const double dz = (pos.z - cam.Location.z) / 100.0;
    return !std::isfinite(dz) ? 0 : dz > HEIGHT_WORD_M ? 1 : dz < -HEIGHT_WORD_M ? -1 : 0;
}

//A live enemy player as the warnings count them: not a teammate, and nobody while your own team is unknown with "Hide
//players with unknown team" on (as on screen: no team to compare with)
inline bool IsWarnedEnemy(const RenderEntity& e, int localTeam) {
    return e.type == Object::PLAYER && !e.isDead && !IsTeammate(e, localTeam) && !(config::hideUnknownTeams && localTeam < 0);
}

//How the voice says where someone is: "left, 8 meters", and "above" / "below" more than 3 m up or down
inline std::string SpokenWhere(const FminimalViewInfo& cam, const Vector3& pos, double meters) {
    static const char* const words[] = {"ahead", "behind", "left", "right"};
    const int d = FlatDir(cam, pos), h = HeightDir(cam, pos);
    std::string s = std::string(d < 0 ? "near" : words[d]) + ", " + std::to_string((long)std::lround(meters)) + " meters";
    if (h) s += h > 0 ? ", above" : ", below";
    return s;
}

//--- the spoken warning when an enemy player first comes within config::cueRange ("Enemy behind, 30 meters"), and once
//more when one already said comes within the Warning card range (config::warnDist): "Enemy close, left, 8 meters", or
//with several that close "3 enemies close, nearest left, 8 meters" (the nearest, as the card says it). At most one
//every 2 s, close ones first, then the nearest; one who has to wait is said at the next free moment if he is still that
//close. He is said again only after going more than 5 m past the range (each range on its own), so walking along its
//edge does not repeat it.
struct NearCue {
    std::set<ptr> said;  //enemies already said and still within the range (+ 5 m)
    std::set<ptr> close; //of those, the ones already said as close and still within the Warning card range (+ 5 m)
    double last = -1e9;  //when the last warning was given (seconds)

    //Returns what to say this frame, "" for nothing. now: seconds.
    std::string update(const std::vector<RenderEntity>& ents, const FminimalViewInfo& cam, int localTeam, double now) {
        constexpr double SLACK = 5;
        std::set<ptr> keep, keepClose;
        std::vector<ptr> closeNews;                                     //within the card range and not said as close yet
        const RenderEntity *best = nullptr, *nearest = nullptr;          //the news to say; the nearest within the card range
        double bestD = 1e18, nearestD = 1e18;
        bool bestClose = false;
        int count = 0;                                                   //enemies within the card range
        for (const RenderEntity& e : ents) {
            if (!IsWarnedEnemy(e, localTeam)) continue;
            const double d = e.pos.Dist(cam.Location) / 100.0;
            if (!std::isfinite(d)) continue;
            const bool inCard = d <= config::warnDist;
            if (inCard) {
                count++;
                if (d < nearestD) { nearest = &e; nearestD = d; }
            }
            const bool wasSaid = said.count(e.actor) && d <= config::cueRange + SLACK;
            const bool wasClose = wasSaid && close.count(e.actor) && d <= config::warnDist + SLACK;
            if (wasSaid) keep.insert(e.actor);
            if (wasClose) keepClose.insert(e.actor);
            //news: never beyond the spoken range unless he was said already; close is news until said as close
            if (!(wasSaid || d <= config::cueRange) || (inCard ? wasClose : wasSaid)) continue;
            if (inCard) closeNews.push_back(e.actor);
            if (inCard > bestClose || (inCard == bestClose && d < bestD)) { best = &e; bestD = d; bestClose = inCard; }
        }
        std::string text;
        if (best && now - last >= 2.0) {
            last = now;
            keep.insert(best->actor);
            if (bestClose) { //one sentence for everyone that close now: none of them is said as close again
                for (ptr a : closeNews) { keep.insert(a); keepClose.insert(a); }
                text = count > 1 ? std::to_string(count) + " enemies close, nearest " + SpokenWhere(cam, nearest->pos, nearestD)
                                 : "Enemy close, " + SpokenWhere(cam, best->pos, bestD);
            } else {
                text = "Enemy " + SpokenWhere(cam, best->pos, bestD);
            }
        }
        said.swap(keep);
        close.swap(keepClose);
        return text;
    }
};

//What F9 says: your health and shield, how many enemies are within the Spoken warning range, and where the nearest enemy
//is: "Health 80, shield 40. 2 enemies within 40 meters. Nearest left, 12 meters, above." hp, shield < 0: not read.
inline std::string StatusSentence(double hp, double shield, const std::vector<RenderEntity>& ents, const FminimalViewInfo& cam, int localTeam) {
    std::string s = hp >= 0 ? "Health " + std::to_string(std::lround(hp)) : std::string("Health unknown");
    if (shield >= 0) s += ", shield " + std::to_string(std::lround(shield));
    const RenderEntity* nearest = nullptr;
    double nearestD = 1e18;
    int n = 0;
    for (const RenderEntity& e : ents) {
        if (!IsWarnedEnemy(e, localTeam)) continue;
        const double d = e.pos.Dist(cam.Location) / 100.0;
        if (!std::isfinite(d)) continue;
        n += d <= config::cueRange;
        if (d < nearestD) { nearest = &e; nearestD = d; }
    }
    s += n == 0 ? ". No enemies" : n == 1 ? ". 1 enemy" : ". " + std::to_string(n) + " enemies";
    s += " within " + std::to_string(std::lround(config::cueRange)) + " meters.";
    if (nearest) s += " Nearest " + SpokenWhere(cam, nearest->pos, nearestD) + ".";
    return s;
}

//An item's price in values.txt, found the same way on the ground and in the inventory window: by its internal name (key),
//by the name on screen (shown: your labels.txt name, if you gave it one), then by the game's own name (game), so a rename
//in labels.txt never loses a price listed under the game's name. -1: no price.
inline long ItemPrice(const std::string& key, const std::string& shown, const std::string& game) {
    for (const std::string* n : {&key, &shown, &game})
        if (!n->empty())
            if (const long v = itemvalues::valueOf(*n); v >= 0) return v;
    return -1;
}

//Where the player list at the bottom was drawn (set by DrawPlayerListWindow, one frame old; all zero when it is not shown)
inline LabelRect g_listRect;

//The lowest the close-enemy card and LOW HEALTH may reach: 6 px above the player list (ImGui draws that window over
//them), else the screen's bottom
inline float WarnFloor() {
    const float H = (float)config::SCREEN_H;
    return g_listRect.x1 > g_listRect.x0 ? std::min(H, g_listRect.y0 - 6) : H;
}

//Where LOW HEALTH's red card was drawn (set by DrawSelfHealth, one frame old; all zero when it is not shown). The edge
//arrows and labels keep off it: drawn later and solid red, it hid an enemy's red arrow under it.
inline LabelRect g_lowHealthRect;

//How far down the messages and cards at the top reached last frame, measured as if they had started at the top (main.cpp,
//MessagesTop's end). The close-enemy card keeps LOW HEALTH's place under it rather than send LOW HEALTH up over them.
inline float g_msgEnd = 0;

//Where the messages and cards at the top of the screen start (main.cpp): at top, or 8 px under LOW HEALTH when it
//reaches into where they went last frame and sits nearer the screen's top than the crosshair (a tall player list pushes
//it up there), so its red card, drawn after them, never covers one. end: how far down they reached last frame, measured
//as if they had started at top. under: they went under it last frame; they stay there until LOW HEALTH is a Text size
//clear of them, so the few px the screen's top edge moves them by never make them jump back and forth.
inline float MessagesTop(float top, float end, bool& under) {
    const LabelRect& r = g_lowHealthRect; //one frame old
    under = config::showSelfHealth && r.y1 > r.y0 && r.y0 < end + (under ? config::labelSize : 0) && r.y0 < CrosshairY() - r.y1;
    return under ? std::max(top, r.y1 + 8) : top;
}

//Where the close-enemy card was drawn (set by DrawCloseEnemyWarning, one frame old; all zero when it is not shown). The
//edge arrows keep off it: moved right beside the HP/SH readout it reaches to the screen's right edge, and drawn later
//over every window it hid an enemy's arrow and distance there.
inline LabelRect g_warnRect;

//"Low health from" (ESP tab, YOUR HEALTH) as the menu shows it: a slider drag can stop between whole numbers (35.4 is
//shown as 35), and %.0f rounds a half to even, as lrint does
inline long LowHealthLine() { return std::lrint(config::lowHealthFrom); }

//Is your health low: read, at least 1 (at 0 you are down: the game shows that itself) and under Low health from, both
//as the shown numbers are rounded
inline bool IsLowHealth(double hp) {
    if (!(hp >= 0)) return false; //also NaN
    const long h = std::lround(hp);
    return h >= 1 && h < LowHealthLine();
}

//The room LOW HEALTH takes under the close-enemy card (its 10 px gap and its card), 0 when it does not show
inline float LowHealthRoom(double hp) {
    return config::showSelfHealth && IsLowHealth(hp) ? LabelSize("LOW HEALTH", config::labelSize * 2.4f).y + 12 + 10 : 0;
}

//Where the big HP/SH readout's card is (4 px past the text's sides, 2 px past top and bottom). DrawESP keeps labels and
//edge arrows off it; with no text, the widest readout. Its middle is 300 px above the screen's bottom, or lower when the
//widest readout would come within 20 px of the crosshair there (a short screen with a big Text size: 1920x1080 at 175%
//scaling is 1097x617, where at Text size 40 its nearly black card hid the crosshair and an enemy standing there): then
//its top goes 20 px under the crosshair, but never past the screen's bottom. The widest readout counts, so it does not
//move as the numbers change.
inline LabelRect SelfHealthRect(const char* text = "HP 100   SH 100") {
    const float size = config::labelSize * 1.8f, H = (float)config::SCREEN_H, gap = 20, cy = CrosshairY();
    const ImVec2 ts = LabelSize(text, size), wide = LabelSize("HP 100   SH 100", size);
    const float half = wide.y / 2 + 2;
    float y = H - 300;
    if (44 + wide.x + gap > CrosshairX() && y - half - gap < cy && y + half + gap > cy) y = std::max(y, std::min(cy + gap + half, H - half));
    return {36, y - ts.y / 2 - 2, 44 + ts.x, y + ts.y / 2 + 2};
}

//Where LOW HEALTH's middle goes across the screen when its middle is at height y: the screen's middle, or just right of
//the big HP/SH readout when it would reach over it there (a big Text size on a short, narrow screen: it hid the shield
//number). It moves sideways only, so the crosshair and the close-enemy card stay as free as before. The widest readout
//counts, so it does not move as the numbers change; never past the screen's right edge (a screen too narrow for both
//side by side keeps the overlap smaller, not gone).
inline float LowHealthX(float y) {
    const float W = (float)config::SCREEN_W;
    const ImVec2 ts = LabelSize("LOW HEALTH", config::labelSize * 2.4f);
    const float hx = ts.x / 2 + 14, hy = ts.y / 2 + 6, gap = 8;
    const LabelRect r = SelfHealthRect();
    if (y + hy + gap <= r.y0 || y - hy - gap >= r.y1 || W * 0.5f - hx >= r.x1 + gap) return W * 0.5f;
    return std::max(W * 0.5f, std::min(r.x1 + gap + hx, W - hx));
}

//How far LOW HEALTH's bottom stays above the close-enemy card's top when it has to go above the card: the crosshair
//(18 px or more above that card) stays free between them. A card further down (just under the HP/SH readout on a short,
//narrow screen) counts as if its top were 18 px under the crosshair, so LOW HEALTH never goes over the crosshair.
constexpr float LOW_HEALTH_ABOVE = 40;

//Where DrawLowHealth puts LOW HEALTH's middle down the screen (below / above: as DrawLowHealth's), and its red card with
//its middle at y
inline float LowHealthY(float below, float above) {
    const float half = LabelSize("LOW HEALTH", config::labelSize * 2.4f).y / 2 + 6;
    float y = below > 0 ? below + 10 + half : (float)config::SCREEN_H * 0.62f;
    if (below > 0 && above > 0 && y + half > WarnFloor()) y = std::min(above, CrosshairY() + 18) - LOW_HEALTH_ABOVE - half; //no room under the card
    return std::max(half, std::min(y, WarnFloor() - half));
}
inline LabelRect LowHealthRect(float y) {
    const ImVec2 ts = LabelSize("LOW HEALTH", config::labelSize * 2.4f);
    const float half = ts.y / 2 + 6, x = LowHealthX(y); //off the HP/SH readout
    return {x - ts.x / 2 - 14, y - half, x + ts.x / 2 + 14, y + half};
}

//Your own health and shield in big white text on a dark card, at the bottom left just above the game's health bar.
//hp < 0: not read, so nothing (never a guessed number). shield < 0: not read or no shield offset, so health only.
//Overlay opacity never fades it. dl: where it goes (null: the background list).
inline void DrawSelfReadout(double hp, double shield, ImDrawList* dl = nullptr) {
    if (!(hp >= 0)) return; //also NaN
    const FullStrength solid;
    const int h = (int)std::lround(hp);
    char buf[48];
    if (shield >= 0) std::snprintf(buf, sizeof buf, "HP %d   SH %.0f", h, shield);
    else std::snprintf(buf, sizeof buf, "HP %d", h);
    const LabelRect r = SelfHealthRect(buf);
    DrawLabelCentered((r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2, buf, config::labelSize * 1.8f, COLOR::white, true, dl);
}

//A big red LOW HEALTH card below the crosshair under Low health from. below / above: the bottom and top of the
//close-enemy card (0: none); LOW HEALTH then goes right under it instead of on top of it (the card leaves LowHealthRoom
//for it), or above it when the screen has no room left under it (a short screen, big text and a tall player list), and
//then also above the crosshair (LOW_HEALTH_ABOVE).
//Never under the player list (WarnFloor, also above the card) and never off the screen: when the list reaches almost
//to the screen's top, at the top. Overlay opacity never fades it. dl: where it goes (null: the background list;
//DrawWarnings passes the foreground list while the menu is closed, so it is drawn over the readout and the player list).
inline void DrawLowHealth(double hp, float below = 0, float above = 0, ImDrawList* dl = nullptr) {
    g_lowHealthRect = {};
    if (!IsLowHealth(hp)) return; //also not read or NaN
    const FullStrength solid;
    const float y = LowHealthY(below, above), x = LowHealthX(y); //x: off the HP/SH readout
    const LabelRect r = LowHealthRect(y);
    if (!COLOR::mono()) //white on solid red; monochrome: white on the dark card
        DrawRectFilled(r.x0, r.y0, r.x1, r.y1, COLOR::red, dl);
    DrawLabelCentered(x, y, "LOW HEALTH", config::labelSize * 2.4f, COLOR::white, COLOR::mono(), dl);
    g_lowHealthRect = r;
}

//Both on the background list, LOW HEALTH over the readout
inline void DrawSelfHealth(double hp, double shield, float below = 0, float above = 0) {
    DrawSelfReadout(hp, shield);
    DrawLowHealth(hp, below, above);
}

//--- spoken news about yourself and the game data, each said once: "Low health" when your health drops under Low health
//from (said again only after it was LOW_HEALTH_REARM or more above it), "Shield broken" when your shield goes from
//something to 0, "Game data lost" when live data stops for a while after it was live (lost: main.cpp's, 1.5 s without
//live data while the game runs) and "Game data back" once it has been live again for DATA_BACK_S, so a short stall or a
//moment of data in between says nothing more.
constexpr double LOW_HEALTH_REARM = 5, DATA_BACK_S = 2.0;
struct SelfCue {
    bool low = false;        //"Low health" was said and your health has not come back up since
    double shield = -1;      //your shield last frame (-1: not known, so a 0 now is not news)
    bool hadLive = false;    //live data was seen since the start
    bool lostSaid = false;   //"Game data lost" was said and "Game data back" not yet
    double liveSince = -1;   //when the data became live (seconds, -1: not live)

    //hp, shield: your own (-1: not read). live: the data is current. now: seconds. Returns what to say, "" for nothing.
    std::string update(double hp, double sh, bool live, bool lost, double now) {
        std::string text;
        auto add = [&](const char* s) { text += text.empty() ? s : std::string(". ") + s; };
        if (lost && hadLive && !lostSaid) { lostSaid = true; add("Game data lost"); }
        if (!live) liveSince = -1;
        else if (liveSince < 0) liveSince = now;
        if (live) hadLive = true;
        if (lostSaid && live && now - liveSince >= DATA_BACK_S) { lostSaid = false; add("Game data back"); }
        if (!live) { shield = -1; return text; } //numbers from old data are not news
        if (shield >= 0.5 && sh >= 0 && sh < 0.5) add("Shield broken");
        shield = sh;
        if (!low && IsLowHealth(hp)) { low = true; add("Low health"); }
        else if (low && hp >= 0 && std::lround(hp) >= LowHealthLine() + LOW_HEALTH_REARM) low = false;
        return text;
    }
};

//--- labels: collected while drawing, then the ones that would overlap are moved or dropped (see Declutter.h)
struct PendingLabel {
    LabelReq box;
    std::string text;
    float x = 0, y = 0, size = 15;
    std::vector<ImVec2> centers; //where each alternative is centred (same order as box.alts)
    ImU32 color = IM_COL32(255, 255, 255, 255);
};

inline std::vector<PendingLabel>& PendingLabels() {
    static std::vector<PendingLabel> v;
    return v;
}

//How far a label's card or outline reaches past its text (DrawLabelCentered): a card CARD_PAD_X to the sides and
//CARD_PAD_Y up and down; plain text its outline, but never less than 2 px to the sides and 1 px up and down
inline ImVec2 LabelReach(float size) {
    if (config::labelCards) return ImVec2(CARD_PAD_X, CARD_PAD_Y);
    const float o = OutlineWidth(size);
    return ImVec2(std::max(2.0f, o), std::max(1.0f, o));
}
//How far apart lines of text stacked under each other are (a player's name, distance and guns; a marker label and the
//one under it): pad above and below each line, then a 2 px gap. With cards the cards are 2 px apart; plain lines'
//outlines just touch (pad 0 up to 17 px text)
inline float LinePad(float size) { return config::labelCards ? CARD_PAD_Y : LabelReach(size).y - 1; }
inline float LineSlot(float size) { return size + 2 * LinePad(size) + 2; }

//The area a label covers on screen, its card or outline included, so neighbouring labels never overlap. ts: the text's
//size (its height is the text size)
inline LabelRect LabelAt(float x, float y, const ImVec2& ts) {
    const ImVec2 r = LabelReach(ts.y);
    return {x - ts.x / 2 - r.x, y - ts.y / 2 - r.y, x + ts.x / 2 + r.x, y + ts.y / 2 + r.y};
}

//step: when the place is taken, try one and two steps further (in y), as long as that stays on the screen. The label
//stays inside the screen's width: near the left or right edge it slides in (smoothly, never cut off); one wider than
//the screen is drawn smaller to fit (as DrawLabelCentered does), and placed and kept apart by that width, so all of it
//stays on the screen. color: the text's.
inline void QueueLabel(float x, float y, const std::string& text, float size, int prio, double dist, bool force = false, float step = 0,
                       ImU32 color = IM_COL32(255, 255, 255, 255)) {
    if (text.empty()) return;
    size = FitSize(size, LabelSize(text.c_str(), size).x, (float)config::SCREEN_W - 16); //as DrawLabelCentered draws it
    const ImVec2 ts = LabelSize(text.c_str(), size);
    const float edge = ts.x / 2 + LabelReach(size).x;
    x = std::clamp(x, edge, std::max(edge, (float)config::SCREEN_W - edge));
    PendingLabel p;
    p.x = x; p.y = y; p.size = size; p.text = text; p.color = color;
    const LabelRect r = LabelAt(x, y, ts);
    p.box.x0 = r.x0; p.box.x1 = r.x1; p.box.y0 = r.y0; p.box.y1 = r.y1;
    p.box.prio = prio; p.box.dist = dist; p.box.force = force;
    for (int k = 1; k <= 2 && step != 0; k++) {
        const float cy = y + step * k;
        if (cy - ts.y / 2 < 0 || cy + ts.y / 2 > (float)config::SCREEN_H) break;
        p.centers.push_back(ImVec2(x, cy)); p.box.alts.push_back(LabelAt(x, cy, ts));
    }
    PendingLabels().push_back(std::move(p));
}

//A label for a round marker at (cx, cy): below it if that is free, else above, to the right, to the left, or lower.
//At the bottom edge of the screen it goes above first. It stays 3 px clear of the marker, card included. A place that
//is not fully on the screen is not offered (the label moves to the next one, or is left out, instead of being cut off).
inline void QueueMarkerLabel(float cx, float cy, float radius, const std::string& text, float size, int prio, double dist) {
    if (text.empty()) return;
    size = FitSize(size, LabelSize(text.c_str(), size).x, (float)config::SCREEN_W - 16); //its places by the width it is drawn at
    const ImVec2 ts = LabelSize(text.c_str(), size);
    const float W = (float)config::SCREEN_W, H = (float)config::SCREEN_H;
    const float pad = LinePad(size);
    const float gap = radius + 3 + pad + ts.y / 2, step = LineSlot(size);
    float first = cy + gap, second = cy - gap, out = step;
    if (first + ts.y / 2 + pad > H) { std::swap(first, second); out = -step; }
    QueueLabel(cx, first, text, size, prio, dist);
    PendingLabel& p = PendingLabels().back();
    const float x = p.x; //above and below the marker: slid in from the screen's edge as QueueLabel did
    const ImVec2 alts[4] = {ImVec2(x, second), ImVec2(cx + radius + 8 + ts.x / 2, cy),
                            ImVec2(cx - radius - 8 - ts.x / 2, cy), ImVec2(x, first + out)};
    constexpr float SLACK = 0.5f; //a place slid in to just touch the edge counts as on the screen, whatever the rounding
    for (const ImVec2& c : alts) {
        const LabelRect r = LabelAt(c.x, c.y, ts);
        if (r.x0 < -SLACK || r.y0 < -SLACK || r.x1 > W + SLACK || r.y1 > H + SLACK) continue;
        p.centers.push_back(c); p.box.alts.push_back(r);
    }
}

//Keeps labels away from this area (an arrow, a panel) without drawing anything. body: an enemy's box, which only
//moves loot, ARC and dead-body labels (players' names, distances and guns stay where they are)
inline void QueueReserve(const LabelRect& r, bool body = false) {
    PendingLabel p;
    p.box.x0 = r.x0; p.box.y0 = r.y0; p.box.x1 = r.x1; p.box.y1 = r.y1;
    p.box.prio = -1; p.box.force = true; p.box.body = body;
    PendingLabels().push_back(std::move(p));
}

inline void FlushLabels() {
    auto& v = PendingLabels();
    std::vector<LabelReq> reqs;
    reqs.reserve(v.size());
    for (const PendingLabel& p : v) reqs.push_back(p.box);
    const std::vector<LabelPick> picks = ChoosePlaces(reqs);
    for (auto it = picks.rbegin(); it != picks.rend(); ++it) { //most important last, so it is on top where forced labels overlap
        const LabelPick& pick = *it;
        const PendingLabel& p = v[pick.index];
        if (p.text.empty()) continue; //a reserved area
        const ImVec2 c = pick.alt < 0 ? ImVec2(p.x, p.y) : p.centers[(size_t)pick.alt];
        DrawLabelCentered(c.x, c.y, p.text.c_str(), p.size, p.color, config::labelCards);
    }
    v.clear();
}

//The box's line thickness as drawn: the Line thickness setting, kept to 1..10 px
inline float BoxThickness() { return std::min(10.0f, std::max(1.0f, config::boxThickness)); }

//Is his stick figure drawn? The switch, a pose, and within Skeleton range of the camera.
inline bool SkeletonShown(const View& v, const RenderEntity& ent) {
    return config::showSkeleton && ent.pose && ent.pos.Dist(v.loc) / 100.0 <= config::skeletonMaxDist;
}

//How tall a standing player's box is at his distance, in px. The skeleton's thickness and its simple far figure go by
//this, not by his pose, so they stay the same when he crouches.
inline float StandingBoxPx(const View& v, const Vector3& pos) {
    double x, y, z;
    v.toCamera(pos, x, y, z);
    return z > 1.0 ? (float)(180.0 * v.focal * v.sy / z) : 1e6f;
}
//The simple far figure: it starts to form when a standing box is SIMPLE_FROM_PX tall and is complete at SIMPLE_AT_PX
//(about 40 m on a 1440p screen), its lines SIMPLE_EXTRA px thicker. In between the figure changes smoothly.
constexpr float SIMPLE_FROM_PX = 50.0f, SIMPLE_AT_PX = 40.0f, SIMPLE_EXTRA = 1.0f;
inline float SkeletonSimple(float boxPx) { return std::clamp((SIMPLE_FROM_PX - boxPx) / (SIMPLE_FROM_PX - SIMPLE_AT_PX), 0.0f, 1.0f); }
//The skeleton's line thickness in px (see skeleton::thickness): never thinner than the box's lines
inline float SkeletonWidth(const View& v, const RenderEntity& ent) {
    const float px = StandingBoxPx(v, ent.pos);
    return skeleton::thickness(BoxThickness(), px) + SIMPLE_EXTRA * SkeletonSimple(px);
}

//Where the head marker goes on screen: its middle c and its radii, 9 cm in the world but never under t + 3 px, so a far
//player's head still reads. False when the head is not known or not in front of the camera.
inline bool HeadOnScreen(const View& v, const skeleton::Pose& pose, float t, Vector2& c, float& rx, float& ry) {
    const Vector3 centre = skeleton::headCentre(pose);
    if (!skeleton::jointOk(centre)) return false;
    double x, y, z;
    v.toCamera(centre, x, y, z);
    if (!(z >= View::NEAR) || !v.project(centre, c)) return false;
    rx = std::max((float)(skeleton::HEAD_RADIUS * v.focal * v.sx / z), t + 3.0f);
    ry = std::max((float)(skeleton::HEAD_RADIUS * v.focal * v.sy / z), t + 3.0f);
    return std::isfinite(c.x + c.y + rx + ry);
}
//How far the head marker reaches past its radius: a ring's half thickness plus the 2 px dark edge (a filled one reaches less)
inline float HeadReach(float t) { return t / 2 + 2.0f; }

//Top and bottom of a player's box, as heights in the world. Standing, that is 90 cm each way from his position; when a
//pose is known (crouching, rolling, lying down) the box follows the real figure, head marker included, instead of
//staying standing-sized. Joints that could not be read are left out.
inline void PlayerBoxZ(const RenderEntity& ent, double& top, double& bottom) {
    top = ent.pos.z + 90; bottom = ent.pos.z - 90;
    if (!ent.pose) return;
    double lo = 1e18, hi = -1e18, x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18;
    for (const Vector3& j : ent.pose->j) {
        if (!skeleton::jointOk(j)) continue;
        lo = std::min(lo, j.z); hi = std::max(hi, j.z);
        x0 = std::min(x0, j.x); x1 = std::max(x1, j.x); y0 = std::min(y0, j.y); y1 = std::max(y1, j.y);
    }
    //Not a believable figure (no joints, all in one spot, or far too big): keep the standing box. Lying down counts.
    const double size = std::max(hi - lo, std::hypot(x1 - x0, y1 - y0));
    if (!(size >= 40.0 && size <= 260.0) || hi - lo > 260.0) return;
    const Vector3 head = skeleton::headCentre(*ent.pose);
    if (skeleton::jointOk(head)) hi = std::max(hi, head.z + skeleton::HEAD_RADIUS);
    top = hi + 2.0;
    bottom = lo - 3.0;
}

//A live player's box on screen as DrawESP draws it: its middle cx, its top y0, w wide, h tall. False when it is not on
//the screen.
inline bool PlayerScreenBox(const View& v, const RenderEntity& ent, float& cx, float& y0, float& w, float& h) {
    double boxTop, boxBottom;
    PlayerBoxZ(ent, boxTop, boxBottom);
    Vector3 headPos = ent.pos; headPos.z = boxTop;
    Vector3 feetPos = ent.pos; feetPos.z = boxBottom;
    Vector2 sHead, sFeet;
    if (!v.project(headPos, sHead) || !v.project(feetPos, sFeet)) return false;
    const double midY = (sHead.y + sFeet.y) / 2;
    if (!(sHead.x > -60 && sHead.x < config::SCREEN_W + 60 && midY > -60 && midY < config::SCREEN_H + 60)) return false;
    const float rawH = (float)(sFeet.y - sHead.y);
    if (!std::isfinite(rawH)) return false;
    h = std::max(6.0f, rawH); //a far player still gets a box, name and distance
    //About 90 cm wide whatever the pose (only the height follows it), at the depth of the box's middle; head and
    //feet are both in front, so the middle is too. A stretched picture makes people wider. Never under 3 px, so a
    //far player's box stays visible.
    Vector3 midPos = ent.pos; midPos.z = (boxTop + boxBottom) / 2;
    double mx, my, mz;
    v.toCamera(midPos, mx, my, mz);
    w = std::max(3.0f, (float)(90.0 * v.focal * v.sx / mz));
    cx = (float)sHead.x;
    y0 = (float)sHead.y;
    if (!ent.pose || !std::isfinite(w)) return true;
    //Lying down: wider, out to his joints on screen. Fully so for a figure 90 cm tall or less, not at all from 130 cm,
    //smoothly in between, and never narrower than 90 cm.
    const float reach = SkeletonWidth(v, ent) / 2 + 2.0f; //a joint's dark edge
    double lo = 1e18, hi = -1e18;
    float sx0 = 1e9f, sx1 = -1e9f;
    for (const Vector3& j : ent.pose->j) {
        double jx, jy, jz;
        Vector2 s;
        if (!skeleton::jointOk(j)) continue;
        lo = std::min(lo, j.z); hi = std::max(hi, j.z);
        v.toCamera(j, jx, jy, jz);
        if (jz < View::NEAR || !v.project(j, s)) continue;
        const float x = (float)std::clamp(s.x, v.rx - 60, v.rx + v.rw + 60);
        sx0 = std::min(sx0, x - reach); sx1 = std::max(sx1, x + reach);
    }
    const float low = std::clamp((float)(130.0 - (hi - lo)) / 40.0f, 0.0f, 1.0f);
    if (low > 0 && sx1 > sx0) {
        const float l0 = cx - w / 2, r0 = cx + w / 2;
        const float l = l0 + (std::min(l0, sx0) - l0) * low, r = r0 + (std::max(r0, sx1) - r0) * low;
        cx = (l + r) / 2; w = r - l;
    }
    //The head marker keeps a size on screen it may not have in the world (a far player): the box reaches round it, so
    //it never pokes out into the name above
    Vector2 hc;
    float hrx, hry;
    const float t = SkeletonWidth(v, ent);
    if (SkeletonShown(v, ent) && HeadOnScreen(v, *ent.pose, t, hc, hrx, hry)) {
        const float top = (float)hc.y - hry - HeadReach(t) - 1;
        if (top < y0) { h += y0 - top; y0 = top; }
        const float l = std::min(cx - w / 2, (float)hc.x - hrx - HeadReach(t) - 1), r = std::max(cx + w / 2, (float)hc.x + hrx + HeadReach(t) + 1);
        cx = (l + r) / 2; w = r - l;
    }
    return true;
}

//How wide one bar is beside a box h px tall: 2 to BAR_MAX_W px, times SizeScale() (wider with a bigger Text size). Both
//bars side by side, their 3 px gap and dark edges reach BarsReach() past the box's outline.
constexpr float BAR_MAX_W = 5;
inline float BarWidth(float h) { return std::min(BAR_MAX_W, std::max(2.0f, h * 0.05f)) * SizeScale(); }
inline float BarsReach() { return 2 * BAR_MAX_W * SizeScale() + 6; }

//The bars on the right of a player box, as config::barStyle says: health (red to green) with the shield next to it;
//health only; or one bar with the shield stacked on the health. The side shield bar is gone when the shield is used up
//(or cannot be read). With config::barNumbers, "HP 85" and "SH 40" beside them in the text size, left of the box when the
//screen's right edge is in the way, and never above minTop (the name's place at the screen's top). Returns the right
//edge of the bars; text (if given) gets where the numbers went (cards or outlines included), and is left alone when
//there are none. gap: how far the box's outline reaches past its edge; the bars' dark edge starts there.
inline float DrawPlayerBars(float x0, float y0, float w, float h, const RenderEntity& ent, float gap, LabelRect* text = nullptr,
                            float minTop = 0) {
    const float bw = BarWidth(h);
    auto bar = [&](float bx, float frac, ImU32 col) {
        DrawRectFilled(bx - 1, y0 - 1, bx + bw + 1, y0 + h + 1, IM_COL32(0, 0, 0, 200));
        DrawRectFilled(bx, y0 + h - h * frac, bx + bw, y0 + h, col);
    };
    float bx = x0 + w + gap + 1.0f;
    const float hp = (float)std::min(1.0, std::max(0.0, ent.playerHealth / 100.0)); //never taller than the box
    const int style = (int)config::barStyle;
    const bool hasShield = ent.hasShield && ent.shieldMax > 0;
    if (style == 2 && hasShield) {
        //one bar: full height is full health plus a full shield; health at the bottom, the shield on top of it
        const double full = 100.0 + ent.shieldMax;
        const float hpH = h * (float)(std::clamp(ent.playerHealth, 0.0, 100.0) / full);
        const float shH = h * (float)(std::clamp(ent.shield, 0.0, ent.shieldMax) / full);
        DrawRectFilled(bx - 1, y0 - 1, bx + bw + 1, y0 + h + 1, IM_COL32(0, 0, 0, 200));
        DrawRectFilled(bx, y0 + h - hpH, bx + bw, y0 + h, COLOR::health(hp));
        if (shH > 0) DrawRectFilled(bx, y0 + h - hpH - shH, bx + bw, y0 + h - hpH, COLOR::shieldBar());
    } else {
        bar(bx, hp, COLOR::health(hp));
        if (style == 0 && hasShield && ent.shield >= 1.0) {
            bx += bw + 3.0f;
            bar(bx, (float)std::min(1.0, std::max(0.0, ent.shield / ent.shieldMax)), COLOR::shieldBar());
        }
    }
    const float right = bx + bw + 1;
    if (config::barNumbers) {
        const float ls = config::labelSize; //the text size, never smaller (a seeing aid)
        char hpText[24], shText[24] = "";
        std::snprintf(hpText, sizeof hpText, "HP %.0f", std::clamp(ent.playerHealth, 0.0, 9999.0));
        if (style != 1 && hasShield) std::snprintf(shText, sizeof shText, "SH %.0f", std::clamp(ent.shield, 0.0, 99999.0));
        const float tw = std::max(LabelSize(hpText, ls).x, shText[0] ? LabelSize(shText, ls).x : 0.0f);
        const float W = (float)config::SCREEN_W, H = (float)config::SCREEN_H;
        const ImVec2 reach = LabelReach(ls);
        const float half = (shText[0] ? ls + 1 : ls / 2) + std::max(2.0f, reach.y); //half the height of the lines, cards included
        float nx = right + 4 + tw / 2;
        if (nx + tw / 2 + reach.x > W) nx = x0 - gap - 4 - tw / 2; //no room on the right: left of the box
        nx = std::clamp(nx, tw / 2 + reach.x, std::max(tw / 2 + reach.x, W - tw / 2 - reach.x));
        //At the box's middle, on the screen. At the screen's top the name stays on the screen: the numbers go under it
        //(they slide down smoothly as the box rises, never jump)
        const float mid = std::clamp(y0 + h / 2, minTop + half, std::max(minTop + half, H - half));
        if (shText[0]) {
            DrawLabelCentered(nx, mid - ls / 2 - 1, shText, ls, COLOR::white, config::labelCards);
            DrawLabelCentered(nx, mid + ls / 2 + 1, hpText, ls, COLOR::white, config::labelCards);
        } else {
            DrawLabelCentered(nx, mid, hpText, ls, COLOR::white, config::labelCards);
        }
        if (text) *text = {nx - tw / 2 - reach.x, mid - half, nx + tw / 2 + reach.x, mid + half};
    }
    return right;
}

//Which way something faces, as a direction on screen: up = the same way as refDeg, down = the opposite way, right = to
//its right. Both are the game's yaw in degrees. False when the facing is not known (NaN).
inline bool FacingOnScreen(double yawDeg, double refDeg, float& x, float& y) {
    if (!std::isfinite(yawDeg) || !std::isfinite(refDeg)) return false;
    const double a = (yawDeg - refDeg) * 3.14159265358979 / 180.0;
    x = (float)std::sin(a);
    y = (float)-std::cos(a);
    return true;
}

//A thick line from r0 to r1 px out of (x, y) along (dx, dy), with a dark edge (1 px past the tip too) for any background
inline void DrawFacingLine(float x, float y, float dx, float dy, float r0, float r1, ImU32 color, float t) {
    DrawLine(x + dx * r0, y + dy * r0, x + dx * (r1 + 1), y + dy * (r1 + 1), IM_COL32(0, 0, 0, 200), t + 2.0f);
    DrawLine(x + dx * r0, y + dy * r0, x + dx * r1, y + dy * r1, color, t);
}

//Arrows at the screen edge for players outside your squad who are not on screen (to the side, or behind you). The arrows
//keep other labels away from them; their distance labels only appear where there is room. Nearby live ARC get the same
//arrows, kite-shaped and in the ARC colour. An enemy within the Warning card range gets an arrow CLOSE_ARROW_GROW times
//bigger. Arrows and their distances stay inside the game's picture, also with Shift X / Y set. cap: the most arrows drawn
//(the nearest ones). An arrow whose square touches one in avoid is left out. Returns each drawn arrow's square and its
//distance label's place.
struct EdgeArrow { const RenderEntity* ent; double dist; ImU32 color; bool arc = false; bool close = false; }; //color: enemy, or teammate for a hand mark
constexpr float CLOSE_ARROW_GROW = 1.6f;
inline std::vector<LabelRect> DrawEdgeArrows(const View& v, std::vector<EdgeArrow>& edge, size_t cap = 8,
                                             const std::vector<LabelRect>* avoid = nullptr) {
    std::vector<LabelRect> used;
    if (edge.empty()) return used;
    std::sort(edge.begin(), edge.end(), [](const EdgeArrow& a, const EdgeArrow& b) { return a.dist < b.dist; });
    if (edge.size() > cap) edge.resize(cap);
    const float hx = std::max(20.0f, (float)v.rw / 2 - 46), hy = std::max(20.0f, (float)v.rh / 2 - 46); //edges of the game's picture
    const float k = SizeScale(); //grows with the text size
    char buf[32];
    const LabelRect hpCard = config::showSelfHealth ? SelfHealthRect() : LabelRect{};
    const LabelRect lowCard = config::showSelfHealth ? g_lowHealthRect : LabelRect{};
    const bool lowFirst = lowCard.y0 > g_warnRect.y0; //LOW HEALTH under the close-enemy card: checked before it
    for (const auto& e : edge) {
        const float grow = e.close ? CLOSE_ARROW_GROW : 1.0f;
        const float as = 15.0f * k * grow, half = 14.0f * k * grow; //arrow length, half its reserved square
        const float m = std::max(20.0f, half + 2);                  //clear of the arrow's square at any text size
        double x, y, z;
        v.toCamera(e.ent->pos, x, y, z);
        double ux, uy;
        if (z >= 1.0) { ux = x / z * v.focal * v.sx; uy = -(y / z) * v.focal * v.sy; } //where it would be on screen, relative to the middle
        else { ux = x; uy = -z; }                                          //behind: left/right as it is, down on screen
        double len = std::hypot(ux, uy);
        if (len < 1e-6) { ux = 0; uy = 1; len = 1; }
        ux /= len; uy /= len;
        const float t = std::min(hx / (float)std::max(std::abs(ux), 1e-6), hy / (float)std::max(std::abs(uy), 1e-6));
        //The ring is round the shifted middle (Shift X / Y), the picture is not: its square kept fully inside the picture
        const float x0 = (float)v.rx + half + 2, x1 = std::max(x0, (float)(v.rx + v.rw) - half - 2);
        const float y0 = (float)v.ry + half + 2, y1 = std::max(y0, (float)(v.ry + v.rh) - half - 2);
        float px = std::clamp((float)v.cx + (float)ux * t, x0, x1);
        float py = std::clamp((float)v.cy + (float)uy * t, y0, y1);
        //Not under the player list, the HP readout, LOW HEALTH or the close-enemy card: just above it. When that would
        //leave the picture (it reaches up to the screen's top: a tall player list, or LOW HEALTH put at the top over it),
        //beside it instead, the nearer side first; with no room beside it either (a list as wide as the screen), as high
        //as the picture allows, where the most of the arrow shows. In this order: LOW HEALTH and the card are above the
        //list or at the screen's top, and of those two the lower one goes first. An arrow moved for one can still land
        //under another (the card just under the readout: moved above the card, it lay on the readout), so all of them
        //are checked again until it is under none (a few rounds at most).
        for (int pass = 0; pass < 4; pass++) {
            bool moved = false;
            for (const LabelRect& L : {g_listRect, hpCard, lowFirst ? lowCard : g_warnRect, lowFirst ? g_warnRect : lowCard}) {
                if (!(L.x1 > L.x0 && px > L.x0 - m && px < L.x1 + m && py > L.y0 - m && py < L.y1 + m)) continue;
                moved = true;
                const bool leftNearer = px - L.x0 < L.x1 - px;
                const float sideA = leftNearer ? L.x0 - m : L.x1 + m, sideB = leftNearer ? L.x1 + m : L.x0 - m;
                if (L.y0 - m >= y0) py = L.y0 - m;
                else if (sideA >= x0 && sideA <= x1) px = sideA;
                else if (sideB >= x0 && sideB <= x1) px = sideB;
                else py = y0;
            }
            if (!moved) break;
        }
        const LabelRect sq{px - half, py - half, px + half, py + half};
        //The ARC kite's pointed tail reaches 1.3 * as back, past sq: a second square, tail px further back, covers it
        const float tail = e.arc ? 0.5f * as : 0.0f;
        const LabelRect tsq{sq.x0 - (float)ux * tail, sq.y0 - (float)uy * tail, sq.x1 - (float)ux * tail, sq.y1 - (float)uy * tail};
        if (avoid && std::any_of(avoid->begin(), avoid->end(), [&](const LabelRect& r) { return RectsOverlap(sq, r) || RectsOverlap(tsq, r); })) continue;
        std::snprintf(buf, sizeof buf, "%.0fm", e.dist);
        //Far enough back to clear the arrow's own reserved square (half + the label's card or outline, LabelReach), or
        //a wide or tall label would always be dropped; never closer than two arrow lengths (the old 30 px at k = 1). The
        //distance is in the full Text size.
        const float ls = config::labelSize;
        const ImVec2 ts = LabelSize(buf, ls), reach = LabelReach(ls);
        const float ax = std::max(std::abs((float)ux), 1e-3f), ay = std::max(std::abs((float)uy), 1e-3f);
        const float back = std::max(2.0f * as, tail + 1.0f + std::min((half + reach.x + ts.x / 2) / ax, (half + reach.y + ts.y / 2) / ay));
        const float lx = px - (float)ux * back, ly = py - (float)uy * back;
        if (e.arc) DrawKiteArrow(px, py, (float)ux, (float)uy, as, e.color);
        else DrawArrow(px, py, (float)ux, (float)uy, as, e.color);
        QueueReserve(sq);
        if (e.arc) QueueReserve(tsq);
        QueueLabel(lx, ly, buf, ls, e.arc ? 1 : 0, e.dist); //ARC: same rank as its marker labels
        used.push_back(sq);
        used.push_back(LabelAt(lx, ly, ts));
    }
    return used;
}

//The skeleton's colour (ESP tab, Skeleton colour): the box colour, white or yellow, as faint as the box (hidden players)
inline ImU32 SkeletonColor(ImU32 box) {
    const int c = (int)config::skeletonColor;
    if (c <= 0) return box;
    const ImU32 base = c == 2 && !COLOR::mono() ? IM_COL32(255, 230, 0, 255) : COLOR::white; //monochrome: white
    return COLOR::withAlpha(base, (int)((box >> IM_COL32_A_SHIFT) & 0xFF));
}

//The stick figure: every dark outline first (lines, round joints, head), then every colour, so no outline cuts into a
//line. Each joint is a round dot as wide as the lines (no notches at elbows and knees), the wrists a bigger dot.
//Lines are cut at the camera and the screen's edge (View::projectSegment): a limb right next to the camera is still
//drawn and never streaks across the screen. t: line thickness (SkeletonWidth); simple: 0 the full figure to 1 the simple
//far figure (SkeletonSimple); guess: the pose is an estimate (the game is not animating him), so the head is a dashed ring.
inline void DrawSkeleton(const View& v, const skeleton::Pose& pose, ImU32 color, float t, float simple = 0, bool guess = false) {
    using namespace skeleton;
    //color already carries the hidden-player dimming (dimHidden / hiddenAlpha): no second fade here
    Pose p;
    simplify(pose, simple, p);
    //every full line lies on a simple one now: the few simple lines look the same. Not with a joint missing: simplify
    //leaves that arm or leg as it is, and the simple table would drop it all at once
    const bool simpleOnly = simple >= 1 && whole(p);
    const int n = simpleOnly ? SIMPLE_COUNT : LINE_COUNT;
    const int (*lines)[2] = simpleOnly ? SIMPLE_LINES : LINES;
    Vector2 a[LINE_COUNT], b[LINE_COUNT];
    int segs = 0;
    bool end[JOINT_COUNT] = {};
    for (int l = 0; l < n; l++) {
        const int ja = lines[l][0], jb = lines[l][1];
        if (!jointOk(p.j[ja]) || !jointOk(p.j[jb]) || !v.projectSegment(p.j[ja], p.j[jb], a[segs], b[segs])) continue;
        end[ja] = end[jb] = true;
        segs++;
    }
    Vector2 dot[JOINT_COUNT];
    bool hasDot[JOINT_COUNT];
    for (int k = 0; k < JOINT_COUNT; k++) {
        double x, y, z;
        v.toCamera(p.j[k], x, y, z);
        hasDot[k] = end[k] && z >= View::NEAR && v.project(p.j[k], dot[k]) && v.onPicture(dot[k]);
    }
    Vector2 c;
    float rx = 0, ry = 0;
    const bool head = HeadOnScreen(v, p, t, c, rx, ry) && v.onPicture(c, 60 + rx);
    const bool filled = (int)config::headMarker == 1 && !guess;
    const ImU32 dark = IM_COL32(0, 0, 0, 200);
    for (int pass = 0; pass < 2; pass++) {
        const ImU32 col = pass ? color : dark;
        const float w = pass ? t : t + 4.0f, grow = pass ? 0.0f : 2.0f; //the dark edge: 2 px past every side
        for (int i = 0; i < segs; i++) DrawLine((float)a[i].x, (float)a[i].y, (float)b[i].x, (float)b[i].y, col, w);
        for (int k = 0; k < JOINT_COUNT; k++)
            if (hasDot[k]) DrawCircleFilled((float)dot[k].x, (float)dot[k].y, (k == HAND_A || k == HAND_B ? 0.9f * t : t / 2) + grow, col);
        if (!head) continue;
        if (filled) DrawEllipseFilled((float)c.x, (float)c.y, rx + grow, ry + grow, col);
        else if (guess) DrawEllipseDashes((float)c.x, (float)c.y, rx, ry, col, w);
        else DrawEllipseRing((float)c.x, (float)c.y, rx, ry, col, w);
    }
}

//The radar panel reaches R + 14 below its centre and R + 14 to the sides, wider when the title needs it; the title strip on
//top grows with the title, which follows Text size (never under 12 px)
inline float RadarTitleSize() { return std::max(12.0f, config::labelSize); }
inline std::string RadarTitle() {
    char t[48];
    std::snprintf(t, sizeof t, "RADAR  %.0f m", config::radarRadius * config::scale / 100.0); //metres from the centre to the edge
    return t;
}
inline float RadarHalfWidth() { return std::max(config::radarRadius + 14, LabelSize(RadarTitle().c_str(), RadarTitleSize()).x / 2 + 8); }
inline float RadarTop() { return config::radarRadius + 14 + RadarTitleSize(); } //from the centre up to the panel's top edge

//Fit the saved centre to this screen, so a position saved on another monitor or scale stays visible
inline ImVec2 RadarCenter() {
    const float R = config::radarRadius, hw = RadarHalfWidth(), top = RadarTop();
    return ImVec2(std::clamp(config::radarCenterX, hw, std::max(hw, (float)config::SCREEN_W - hw)),
                  std::clamp(config::radarCenterY, top, std::max(top, (float)config::SCREEN_H - R - 14)));
}

//The radar's dark panel, title included
inline LabelRect RadarPanel() {
    const float R = config::radarRadius, hw = RadarHalfWidth(), top = RadarTop();
    const ImVec2 c = RadarCenter();
    return {c.x - hw, c.y - top, c.x + hw, c.y + R + 14};
}

//A live player's name line: "TEAM  name" for your squad, "MARKED  name" for a player you marked as a teammate by hand
//(F6 or the player list), so a teammate is told apart by a word and not only by colour (also in monochrome). Just the
//word when there is no name to show.
inline std::string PlayerNameText(const std::string& name, bool squad, bool marked) {
    const char* tag = squad ? "TEAM" : marked ? "MARKED" : nullptr;
    if (!tag) return name;
    return name.empty() ? std::string(tag) : std::string(tag) + "  " + name;
}

//The label by a down player's cross ("DOWN  12m": a player at 0 health, as the player list says) and a destroyed ARC's
//word ("DEAD  Tick  12m")
constexpr const char* DOWN_WORD = "DOWN";
constexpr const char* DEAD_WORD = "DEAD";

inline void DrawESP(const std::vector<RenderEntity>& entities, FminimalViewInfo vm, int localPlayerTeam) {
    const View v(vm, MakeProjection());
    if (!v.ok) return;
    if (config::enableRadar) QueueReserve(RadarPanel()); //markers are drawn over the radar; labels move off it
    if (config::showSelfHealth) {
        QueueReserve(SelfHealthRect()); //the HP readout is drawn later, over this spot
        if (g_lowHealthRect.x1 > g_lowHealthRect.x0) QueueReserve(g_lowHealthRect); //and LOW HEALTH where it was last frame
    }
    const float W = (float)config::SCREEN_W, H = (float)config::SCREEN_H;
    const float ls = config::labelSize, ls2 = SmallTextSize(0.9f);
    const float k = SizeScale(); //markers grow with the text size
    auto onScreen = [&](double x, double y) { return x > -60 && x < W + 60 && y > -60 && y < H + 60; };
    std::vector<EdgeArrow> edge, arcEdge;
    char buf[64];
    if (config::highlightValuable) itemvalues::reloadIfChanged(); //values.txt: a saved change shows within 2 s

    for (const auto& ent : entities) {
        if (ent.type == Object::NONE && !isDebugMode) continue;
        //From the camera as it is now, not as it was when the entity was read
        const double dist = ent.pos.Dist(vm.Location) / 100.0;
        Vector2 sp;
        const bool front = v.project(ent.pos, sp);

        if (isDebugMode) {
            if (front && dist < 150) {
                snprintf(buf, sizeof buf, "0x%lx [%.0fm]", (unsigned long)ent.vt, dist);
                DrawTextImGui(sp.x, sp.y, COLOR::white, buf);
            }
            continue;
        }

        //To consider. 1) Dist, 2) dead?, 3) visible, 4) team
        if (ent.type == Object::PLAYER) {
            const bool squad = localPlayerTeam >= 0 && ent.teamID == localPlayerTeam;
            const bool marked = marks::isMarked(ent.account);
            const bool mate = squad || marked; //your squad, or marked by hand
            if (!config::showPlayers) continue;
            if (dist > config::maxPlayerDist) continue;
            if (squad && !config::showTeammates) continue; //a hand mark is always drawn (in the teammate colour): it may be stale
            if (!mate && config::hideUnknownTeams && localPlayerTeam < 0) continue; //no team to compare with
            if (ent.isDead) {
                if (!config::showDeadPlayers || !front || !onScreen(sp.x, sp.y)) continue;
                const double rad = getProportionalRadius(dist, config::maxPlayerDist) * k;
                DrawCross((float)sp.x, (float)sp.y, (float)rad, COLOR::deadPlayer());
                snprintf(buf, sizeof buf, "%s  %.0fm", DOWN_WORD, dist);
                QueueMarkerLabel((float)sp.x, (float)sp.y, (float)rad, buf, ls2, 2, dist);
                //the name line as on a live player: TEAM or MARKED in the teammate colour, also with Player names off,
                //so a down squad mate is told from a down enemy
                if (config::showPlayerNames || mate)
                    QueueLabel((float)sp.x, (float)(sp.y - rad - 3 - LinePad(ls) - ls / 2), PlayerNameText(config::showPlayerNames ? ent.label : std::string(), squad, marked),
                               ls, 1, dist, false, 0, mate && !COLOR::mono() ? COLOR::mate() : COLOR::white);
                continue;
            }

            //should be rendered from this point on
            ImU32 color = mate ? COLOR::mate() : COLOR::enemy();
            if (config::dimHidden && !ent.isVisible) color = COLOR::withAlpha(color, (int)config::hiddenAlpha); //not drawn by the game right now: behind cover
            float bx, y0, w, h; //bx: the box's middle
            if (!PlayerScreenBox(v, ent, bx, y0, w, h)) {
                if (!squad && config::showOffscreen) edge.push_back({&ent, dist, mate ? COLOR::mate() : COLOR::enemy(), false, !mate && dist <= config::warnDist});
                continue;
            }
            const float x0 = bx - w / 2;
            const float th = BoxThickness();
            const float m = std::max(3.0f, th / 2 + 1); //how far the outline's dark edge reaches past the box (3 px up to th 4)
            const int style = (int)config::boxStyle;
            const bool skel = SkeletonShown(v, ent);
            if (style == 3) { if (!skel) DrawBoxOutlined(x0, y0, w, h, color, th); } //Skeleton only: the full box for a player without one
            else if (style == 1) DrawBoxOutlined(x0, y0, w, h, color, th);
            else if (style == 2) DrawSidesBox(x0, y0, w, h, color, th);
            else DrawCornerBox(x0, y0, w, h, color, th);

            Vector2 headC;
            bool headOk = false;
            float hrx = 0, hry = 0;
            if (skel) {
                const float st = SkeletonWidth(v, ent);
                DrawSkeleton(v, *ent.pose, SkeletonColor(color), st, SkeletonSimple(StandingBoxPx(v, ent.pos)), !ent.poseLive);
                headOk = HeadOnScreen(v, *ent.pose, st, headC, hrx, hry);
            }
            const bool filledHead = headOk && (int)config::headMarker == 1 && ent.poseLive; //as DrawSkeleton draws it

            //Which way he faces, seen from you: an arrow from the middle of the box (from the head with a skeleton, not over
            //the spine; just under a filled head, which is in the same colour and would swallow it), pointing down when he
            //faces you. It stays inside the box, clear of his labels, except on a tiny far box: it is never under 12 px, so
            //it still reads.
            float fx = 0, fy = 0;
            const double toHim = std::atan2(ent.pos.y - vm.Location.y, ent.pos.x - vm.Location.x) * 180.0 / 3.14159265358979;
            if (config::showFacing && FacingOnScreen(ent.yaw, toHim, fx, fy)) {
                //The head grows with the shaft (1.1 * tip wide vs lineT + 2 with the dark edge), so thick lines still read as an arrow
                const float lineT = std::max(3.0f, th + 1.0f), len0 = std::clamp(std::min(w, h) * 0.4f, 12.0f, 40.0f);
                const float tip = std::max({7.0f, len0 * 0.45f, 1.6f * (lineT + 2.0f)}), len = std::max(len0, tip);
                float mxs = bx, mys = y0 + h / 2;
                if (filledHead) { //all of it under the disc and its 2 px dark edge (going up it ends there), and in the box the way it points
                    const float under = (float)headC.y + hry + 3 + 0.55f * tip + std::max(0.0f, -fy) * len;
                    mys = std::max(y0 + std::max(0.0f, -fy) * len, std::min(under, y0 + h - std::max(0.0f, fy) * len));
                    if (2 * len <= w) mxs = std::clamp((float)headC.x, x0 + len, x0 + w - len);
                } else if (headOk) { //at the head, moved in just enough that the arrow stays in the box whichever way it points
                    if (2 * len <= h) mys = std::clamp((float)headC.y, y0 + len, y0 + h - len);
                    if (2 * len <= w) mxs = std::clamp((float)headC.x, x0 + len, x0 + w - len);
                }
                DrawFacingLine(mxs, mys, fx, fy, 0.0f, len - tip * 0.5f, color, lineT);
                DrawArrow(mxs + fx * len, mys + fy * len, fx, fy, tip, color);
            }

            //name above, distance and guns below, clear of the box and its bar numbers and 2 px apart (cards included). With no room
            //below the box the distance and guns go above the name; the lines are kept on the screen. Players are always
            //drawn: when crowded they move a line further out, and only over other labels when nothing is free. A teammate's
            //name line starts with TEAM or MARKED (PlayerNameText) and is in the teammate colour (white in monochrome, where
            //the word tells them apart); it shows even with Player names off.
            const std::string nameText = PlayerNameText(config::showPlayerNames ? ent.label : std::string(), squad, marked);
            const bool nameLine = config::showPlayerNames || mate;
            const float padN = LinePad(ls), padL = LinePad(ls2);
            const float nameH = nameLine ? LineSlot(ls) : 0, lineH = LineSlot(ls2); //a line and its gap
            const float linesH = config::showPlayerGuns ? 2 * lineH : lineH;
            float right = x0 + w;
            LabelRect nums{x0, y0, x0, y0}; //where the bar numbers went (none: a point inside the box)
            if (config::showBars) right = DrawPlayerBars(x0, y0, w, h, ent, m, &nums, nameH + 2); //health, and the shield next to it
            //loot, ARC and dead-body labels move off an enemy's box, bars and numbers, or drop; m covers the outline.
            //A teammate's box does not move them, but their numbers do (text over text cannot be read)
            if (!mate) QueueReserve({std::min(x0 - m, nums.x0 - m), std::min(y0 - m, nums.y0), std::max(right, nums.x1) + m,
                                     std::max(y0 + h + m, nums.y1)}, true);
            else if (nums.x1 > nums.x0) QueueReserve(nums, true);
            //Box, bars and numbers: the numbers can stand taller than a far player's box. (y0 + 2 - m: the same rounding
            //as without numbers, so nothing moves by a pixel when they are off)
            const float above = std::min(y0 + 2 - m, nums.y0 + 2), bottom = std::max(y0 + h + m, nums.y1);
            float nameTop = std::max(2.0f, above - nameH), lineTop = std::max(nameTop + nameH, bottom), lineStep = lineH;
            if (lineTop + linesH > H) {
                lineTop = std::max(2.0f, std::min(above - nameH - linesH, H - linesH - nameH));
                nameTop = lineTop + linesH;
                lineStep = -lineH;
            }
            if (nameLine) QueueLabel(bx, nameTop + padN + ls / 2, nameText, ls, 0, dist, true, -nameH, mate && !COLOR::mono() ? COLOR::mate() : COLOR::white);
            snprintf(buf, sizeof buf, "%.0fm", dist);
            const float below = lineTop + padL + ls2 / 2;
            QueueLabel(bx, below, buf, ls2, 0, dist, true, lineStep);
            if (config::showPlayerGuns) QueueLabel(bx, below + lineH, ent.sub, ls2, 0, dist, true, lineStep);
        } else {
            if (!front || !onScreen(sp.x, sp.y)) {
                //a live ARC within ARC arrow range (and ARC range) that is off screen or behind you: an arrow at the screen edge
                if (ent.type == Object::ARC && !ent.isDead && config::showArc && config::showArcOffscreen &&
                    (!ent.isTurret || config::showTurrets) && dist <= std::min(config::maxArcDist, config::arcArrowDist))
                    arcEdge.push_back({&ent, dist, COLOR::arc(false), true});
                continue;
            }
            ImU32 color;
            double radius;
            int prio;
            bool nameOn;
            long price = -1; //a valuable ground item's price ("Highlight valuable items"), else -1
            void (*shape)(float, float, float, ImU32); //each kind its own shape, in colour and in monochrome
            if (ent.type == Object::ARC) {
                if (!config::showArc || (ent.isDead && !config::showDeadArc)) continue;
                if (ent.isTurret && !config::showTurrets) continue;
                if (dist > config::maxArcDist) continue;
                color = COLOR::arc(ent.isDead);
                radius = getProportionalRadius(dist, config::maxArcDist) * 1.25 * k; //a diamond covers 2 r^2: about the circle (pi r^2) it replaced
                prio = 1;
                nameOn = config::showArcNames;
                shape = DrawDiamondFilled;
            } else if (ent.type == Object::SEARCH) {
                if (!config::showContainers || dist > config::maxLootDist) continue;
                if (!KindShown(ContainerSwitches(), labels::GROUP_COUNT, ent.itemKind)) continue; //Loot tab, CONTAINERS
                color = COLOR::container();
                radius = getProportionalRadius(dist, config::maxLootDist) * k;
                prio = 3;
                nameOn = config::showContainerNames && dist <= config::nameMaxLootDist;
                shape = DrawSquareFilled;
            } else if (ent.type == Object::PICKUP) {
                if (!config::showPickups || dist > config::maxLootDist) continue;
                if (!KindShown(LootSwitches(), labels::KIND_COUNT, ent.itemKind)) continue; //Loot tab, GROUND ITEMS
                if (config::highlightValuable && !ent.label.empty()) {
                    const long p = ItemPrice(ent.itemKey, ent.label, ent.gameName);
                    if (p >= 0 && p >= std::lrint(config::valuableFrom)) price = p; //the price the menu shows (%.0f)
                }
                color = price >= 0 ? COLOR::valuable() : COLOR::pickup(ent.itemKind);
                radius = getProportionalRadius(dist, config::maxLootDist) * 1.25 * k; //the same for the triangle
                if (price >= 0) radius *= 1.5; //a valuable item: bigger, and named anywhere within Loot range
                prio = 2;
                nameOn = price >= 0 || (config::showItemNames && dist <= config::nameMaxLootDist);
                shape = DrawTriangleFilled;
            } else {
                continue;
            }

            shape((float)sp.x, (float)sp.y, (float)radius, color);
            std::string text = ent.type == Object::ARC && ent.isDead ? std::string(DEAD_WORD) + "  " : std::string();
            if (nameOn && !ent.label.empty()) text += ent.label + "  ";
            if (price >= 0) text += ShortPrice(price) + "  ";
            snprintf(buf, sizeof buf, "%.0fm", dist);
            text += buf;
            QueueMarkerLabel((float)sp.x, (float)sp.y, (float)radius, text, ls2, prio, dist);
        }
    }
    if (g_listRect.x1 > g_listRect.x0) QueueReserve(g_listRect); //labels stay off the player list
    //ARC arrows go under the player arrows (channel 0 is drawn before channel 1), and an ARC arrow that would cover a
    //player arrow or its distance is left out
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    const std::vector<LabelRect> players = DrawEdgeArrows(v, edge);
    dl->ChannelsSetCurrent(0);
    DrawEdgeArrows(v, arcEdge, 4, &players);
    dl->ChannelsMerge();
    FlushLabels();
}

//Where the close-enemy card went last frame: 0 its own place, 1 below the nearest enemy, 2 above him. It keeps that side
//while the side still fits within WARN_SLACK_PX (pushed back onto the screen that far), and stays moved until he is
//WARN_STICK_PX clear of its own place, so small steps never make it jump back and forth. 0 again whenever no card shows.
inline int g_warnSide = 0;
constexpr float WARN_SLACK_PX = 10, WARN_STICK_PX = 24;
//The close-enemy card was moved right off the HP/SH readout last frame: it stays there until it is WARN_STICK_PX clear of
//the readout's height, so small steps never make it jump sideways back and forth. False whenever no card shows.
inline bool g_warnBeside = false;
//The close-enemy card stayed at its own place last frame, over part of the HP/SH readout, so LOW HEALTH kept its place
//under it: just under the readout, the card would have sent LOW HEALTH up over the nearest enemy or the messages at the
//top. It stays so until LOW HEALTH's place up there is WARN_STICK_PX clear of them. False whenever no card shows.
inline bool g_warnLowKept = false;

//A big card under the middle of the screen for the nearest enemy closer than warnDist: "ENEMY 9 m BEHIND", or
//"3 ENEMIES - NEAREST 9 m LEFT", with ", ABOVE" / ", BELOW" when he is more than 3 m higher or lower, and a big white
//arrow on its left pointing the way he is (up: ahead, down: behind). White text on a dark card framed in the enemy
//colour, never faded by Overlay opacity. Returns the text drawn; bottom and top (if given) get the card's lower and upper
//edges, left alone when nothing is drawn. extraBelow: the room LOW HEALTH needs under the card (LowHealthRoom), kept free
//above the player list and the screen's bottom. dl: where to draw (null: the background list; DrawWarnings passes the
//foreground list, so no window ever hides the card).
inline std::string DrawCloseEnemyWarning(const std::vector<RenderEntity>& entities, FminimalViewInfo vm, int localPlayerTeam,
                                         float* bottom = nullptr, float extraBelow = 0, ImDrawList* dl = nullptr,
                                         float* top = nullptr) {
    const int side = g_warnSide;
    const bool wasBeside = g_warnBeside, wasKept = g_warnLowKept;
    g_warnSide = 0;
    g_warnBeside = false;
    g_warnLowKept = false;
    g_warnRect = {};
    if (!config::closeWarning) return {};
    const View v(vm, MakeProjection());
    if (!v.ok) return {};
    const RenderEntity* nearest = nullptr;
    double best = 0;
    int count = 0;
    for (const auto& ent : entities) {
        if (!IsWarnedEnemy(ent, localPlayerTeam)) continue;
        const double dist = ent.pos.Dist(vm.Location) / 100.0;
        if (!(dist <= config::warnDist)) continue; //NaN too
        count++;
        if (!nearest || dist < best) { best = dist; nearest = &ent; }
    }
    if (!nearest) return {};
    const FullStrength solid;
    static const char* const words[] = {"AHEAD", "BEHIND", "LEFT", "RIGHT"};
    const int d = FlatDir(vm, nearest->pos), up = HeightDir(vm, nearest->pos); //the same way as the spoken warning says
    const char* where = d < 0 ? "NEAR" : words[d];
    const char* height = up > 0 ? ", ABOVE" : up < 0 ? ", BELOW" : "";
    char buf[112];
    if (count == 1) std::snprintf(buf, sizeof buf, "ENEMY %.0f m %s%s", best, where, height);
    else std::snprintf(buf, sizeof buf, "%d ENEMIES - NEAREST %.0f m %s%s", count, best, where, height);
    double f, r;
    FlatOffset(vm, nearest->pos, f, r);
    const double flat = std::hypot(f, r);
    const bool arrow = flat > 1e-6 && std::isfinite(flat); //not when he is straight above or below you
    const float big = config::labelSize * 2.0f;
    //the card, arrow and frame fit across the screen (the arrow's column is about 1.2 x the text size)
    const float size = FitSize(big, LabelSize(buf, big).x + (arrow ? 1.2f * big + 8 : 0), (float)config::SCREEN_W - 28);
    float cy = (float)v.cy + 140.0f;
    const ImVec2 ts = LabelSize(buf, size);
    const float hy = ts.y / 2 + 6;
    //The arrow: 0.8 x the text's height long, as long as it fits the card's height whichever way it points (it reaches
    //0.75 x its length from its middle); its column is 1.5 x that long, 8 px from the text
    const float L = arrow ? std::min(0.8f * ts.y, (hy - 3) / 0.75f) : 0, aw = arrow ? 1.5f * L + 8 : 0;
    const float hx = (ts.x + aw) / 2 + 10;
    //The card and LOW HEALTH under it end above the player list (or the screen's bottom): moved up if needed, but its
    //top stays 18 px or more below the crosshair
    const float F = WarnFloor();
    cy = std::min(cy, std::max(F - extraBelow - hy - 2, (float)v.cy + hy + 20));
    //An enemy's box, bars and labels when DrawESP shows him (false when it does not). His box is the standing one (90 cm
    //up and down from him), not his animated pose, so the card does not shake with every step he takes.
    auto shownRect = [&](const RenderEntity& e, double dist, LabelRect& out) {
        float bx, by, bw, bh;
        RenderEntity standing = e;
        standing.pose = nullptr;
        if (!(config::showPlayers && dist <= config::maxPlayerDist && PlayerScreenBox(v, standing, bx, by, bw, bh))) return false;
        const float ls = config::labelSize, ls2 = SmallTextSize(0.9f);
        const float m = std::max(3.0f, std::min(10.0f, std::max(1.0f, config::boxThickness)) / 2 + 1); //as in DrawESP
        const float nameH = LineSlot(ls), linesH = 2 * LineSlot(ls2); //name above; distance and guns below (or above)
        float half = bw / 2 + 2 * m + BarsReach(); //the health and shield bars on the right, as wide as Text size makes them
        const float nh = config::showBars && config::barNumbers ? ls + 1 + std::max(2.0f, LabelReach(ls).y) : 0; //half the height of the bar numbers
        if (nh > 0) half += LabelSize("HP 100", ls).x + 8; //beside the bars, on either side
        for (const std::string& s : {e.label, e.sub, std::string("999m")}) half = std::max(half, LabelSize(s.c_str(), ls).x / 2 + 4);
        //the bar numbers sit at his box's middle, under the name at the screen's top (DrawPlayerBars' minTop)
        const float numMid = nh > 0 ? std::max(by + bh / 2, nameH + 2 + nh) : by + bh / 2;
        out = {bx - half, std::min(by - m, numMid - nh) - nameH - linesH, bx + half, std::max(by + bh + m, numMid + nh) + linesH};
        return true;
    };
    //The nearest enemy's: the card keeps off them (below), and so does LOW HEALTH
    LabelRect him{};
    const bool showsHim = shownRect(*nearest, best, him);
    //Under the crosshair, but kept on the screen (Shift X moves the crosshair sideways; FitSize made the card fit the
    //width; + the 3 px frame and its soft edge) and off the big HP/SH readout (its nearly black fill hid the shield
    //number): moved right just past it, as LOW HEALTH is. The widest readout counts, so the card does not move as the
    //numbers change. On a screen too narrow for both side by side, the card goes just under the readout (LOW HEALTH's
    //room under it still above the floor), or else just above it (its top still 18 px or more below the crosshair), or
    //else just under it without LOW HEALTH's room (LOW HEALTH then goes above the crosshair, but not when it would lie
    //over an enemy or the messages at the top there while it still fits under the card at the card's own place: drawn
    //solid red over them, it hid his head or a message). Where none of these fits (a short screen and a long player
    //list: 1280x800 at Text size 40 with 5 or more players listed) the readout gives way: the card stays under the
    //crosshair, moved right as far as the screen allows, and covers part of the readout. It never goes over the
    //crosshair or the player list instead.
    //ponytail: once moved below or above the nearest enemy on such a narrow screen, it can still reach over the readout.
    const LabelRect hpCard = config::showSelfHealth ? SelfHealthRect() : LabelRect{};
    const float gap = 8, xLo = hx + 3, xHi = std::max(xLo, (float)config::SCREEN_W - hx - 3);
    auto onReadout = [&](float x, float y, float g) {
        return hpCard.x1 > hpCard.x0 && RectsOverlap({x - hx - 3 - g, y - hy - 2 - g, x + hx + 3 + g, y + hy + 2 + g}, hpCard);
    };
    const float centre = std::clamp((float)v.cx, xLo, xHi), beside = std::min(std::max(centre, hpCard.x1 + gap + hx + 3), xHi);
    bool movedOff = false; //moved under or over the readout: clear of it, so no reason to be beside it
    if (onReadout(centre, cy, gap) && onReadout(beside, cy, gap)) {
        const float under = hpCard.y1 + gap + hy + 2, over = hpCard.y0 - gap - hy - 2;
        movedOff = true;
        //LOW HEALTH's place above the crosshair, where the last of these sends it (kept off them last frame: until it
        //is WARN_STICK_PX clear of them)
        const LabelRect up = LowHealthRect(LowHealthY(under + hy + 2, under - hy - 2));
        const float st = wasKept ? WARN_STICK_PX : 0;
        const LabelRect upGrown{up.x0 - st, up.y0 - st, up.x1 + st, up.y1 + st};
        //The card stays at its own place when LOW HEALTH fits under it there, and up there it would lie over the messages
        //or over an enemy DrawESP shows: any of them, not only the nearest (one straight ahead, the nearest behind you)
        auto keepLow = [&] {
            if (cy + hy + 2 + extraBelow > F) return false;
            if (up.y0 - st < g_msgEnd) return true;
            for (const RenderEntity& e : entities) {
                LabelRect r;
                if (IsWarnedEnemy(e, localPlayerTeam) && shownRect(e, e.pos.Dist(vm.Location) / 100.0, r) && RectsOverlap(r, upGrown)) return true;
            }
            return false;
        };
        if (under + hy + 2 + extraBelow <= F) cy = under;
        else if (over - hy - 2 >= (float)v.cy + 18) cy = over;
        else if (under + hy + 2 <= F && keepLow()) { movedOff = false; g_warnLowKept = true; }
        else if (under + hy + 2 <= F) cy = under;
        else movedOff = false;
    }
    //moved beside it last frame: it stays there a little longer (not once moved under or over it)
    const float stickGap = wasBeside && !movedOff ? gap + WARN_STICK_PX : gap;
    const bool besideHere = onReadout(centre, cy, stickGap);       //beside it at its own place: also once moved for him
    float cx = besideHere ? beside : centre;
    //Never over the nearest enemy when DrawESP shows him: below his box, bars and labels, above them when there is no
    //room below (LOW HEALTH follows the card, and is kept off him too, but never by moving the card up to the
    //crosshair). When he fills the screen top to bottom the card stays put.
    int newSide = 0;
    if (showsHim) {
        //The card, and LOW HEALTH under it (extraBelow) where DrawSelfHealth draws it: the screen's middle, or beside the
        //HP/SH readout (LowHealthX at LOW HEALTH's middle: 10 px under the card plus half its height). One box round both:
        //he stays clear of the pair, and the card does not keep moving in and out of the gap between them
        const float lowHalf = extraBelow > 0 ? LabelSize("LOW HEALTH", config::labelSize * 2.4f).x / 2 + 14 : 0;
        const float lx = extraBelow > 0 ? LowHealthX(cy + hy + 2 + 10 + (extraBelow - 10) / 2) : (float)config::SCREEN_W * 0.5f;
        const LabelRect pair{std::min(cx - hx, lx - lowHalf) - 2, cy - hy - 2, std::max(cx + hx, lx + lowHalf) + 2, cy + hy + 2 + extraBelow};
        const float stick = side ? WARN_STICK_PX : 0; //moved last frame: it stays moved until he is this far clear
        auto grown = [&](const LabelRect& a) { return LabelRect{a.x0 - stick, a.y0 - stick, a.x1 + stick, a.y1 + stick}; };
        if (RectsOverlap(him, grown(pair))) {
            const bool onCard = RectsOverlap(him, grown({cx - hx - 2, cy - hy - 2, cx + hx + 2, cy + hy + 2}));
            const float lowest = F - extraBelow - hy - 2; //the card's lowest middle: LOW HEALTH under it, above the floor
            const float under = him.y1 + gap + hy + 2;    //its middle right under him
            //above him: for the card itself as always; for LOW HEALTH alone only while the card's top stays 20 px or
            //more below the crosshair (as the card's lowest place keeps it above)
            const float highest = onCard ? hy : std::max(hy, (float)v.cy + 20 + hy);
            const float over = him.y0 - gap - extraBelow - hy - 2;
            if (side == 1 && under <= lowest + WARN_SLACK_PX) { newSide = 1; cy = std::min(under, lowest); }
            else if (side == 2 && over >= highest - WARN_SLACK_PX) { newSide = 2; cy = std::max(over, highest); }
            else if (under <= lowest) { newSide = 1; cy = under; }
            else if (over >= highest) { newSide = 2; cy = over; }
        }
    }
    g_warnSide = newSide;
    if (!besideHere && onReadout(centre, cy, stickGap)) cx = beside; //moved below or above him to the readout's height
    g_warnBeside = cx != centre;
    DrawRectFilled(cx - hx, cy - hy, cx + hx, cy + hy, IM_COL32(0, 0, 0, 220), dl);
    DrawRectOutline(cx - hx, cy - hy, cx + hx, cy + hy, COLOR::enemy(), 3.0f, dl);
    if (arrow) { //its middle in the middle of its column; screen up is ahead of you, right is your right
        const float ux = (float)(r / flat), uy = (float)(-f / flat), ax = cx - hx + 10 + 0.75f * L;
        DrawArrow(ax + ux * L / 2, cy + uy * L / 2, ux, uy, L, COLOR::white, dl);
    }
    DrawLabelCentered(cx + aw / 2, cy, buf, size, COLOR::white, false, dl);
    if (bottom) *bottom = cy + hy + 2; //+ half the 3 px frame
    if (top) *top = cy - hy - 2;
    g_warnRect = {cx - hx - 2, cy - hy - 2, cx + hx + 2, cy + hy + 2};
    return buf;
}

//Last of all (main.cpp, with live data): your HP/SH readout, the close-enemy card (it keeps off the readout; where a
//narrow screen leaves it no room, it is drawn over it), then LOW HEALTH, which moves below or above the card. The card
//goes on the foreground list, over every window (the menu, the player list, inventory windows), so nothing ever hides
//it: with a big Menu text size the player list grew over the place under the crosshair it moves up to. The readout and
//LOW HEALTH go there too while the menu is closed (that taller list reached over them), LOW HEALTH over the readout;
//with the menu open they stay under the menu (on top they would block menu rows for long).
inline void DrawWarnings(const std::vector<RenderEntity>& ents, FminimalViewInfo vm, int localTeam, double hp, double shield, bool menuOpen) {
    ImDrawList* const fg = ImGui::GetForegroundDrawList();
    ImDrawList* const self = menuOpen ? nullptr : fg;
    float warnBottom = 0, warnTop = 0;
    g_lowHealthRect = g_warnRect = {}; //nothing left over from an earlier frame when one is switched off
    if (config::showSelfHealth) DrawSelfReadout(hp, shield, self); //old data: never called, never an old number
    if (config::espEnabled) DrawCloseEnemyWarning(ents, vm, localTeam, &warnBottom, LowHealthRoom(hp), fg, &warnTop);
    if (config::showSelfHealth) DrawLowHealth(hp, warnBottom, warnTop, self);
}

//The radar: a panel with three rings (each with its range), your own arrow, and the edges of your view. Its lines grow
//with the text size and are at least half solid (RADAR_LINE_ALPHA), so they show over a bright map.
constexpr int RADAR_LINE_ALPHA = 120;
inline void DrawRadar(const std::vector<RenderEntity>& entities, FminimalViewInfo cameraInfo, int localPlayerTeam) {
    const float R = config::radarRadius, ts = RadarTitleSize();
    const ImVec2 rc = RadarCenter();
    const float cx = rc.x, cy = rc.y;
    const double rangeM = R * config::scale / 100.0; //metres from the centre to the edge
    const float k = SizeScale(); //lines and blips grow with the text size
    const ImU32 line = IM_COL32(255, 255, 255, RADAR_LINE_ALPHA);

    //Panel, rings, crosshairs
    const LabelRect P = RadarPanel(); //wider and taller with the title, which follows Text size
    DrawRectFilled(P.x0, P.y0, P.x1, P.y1, IM_COL32(0, 0, 0, 150));
    DrawRectOutline(P.x0, P.y0, P.x1, P.y1, line, 1.0f);
    DrawCircleFilled(cx, cy, R, IM_COL32(0, 0, 0, 110));
    const float ringT = 1.5f * k, edgeT = 2.0f * k; //the inner rings and the cross; the outer ring
    for (int i = 1; i <= 3; i++) DrawCircleRing(cx, cy, R * i / 3.0f, i == 3 ? IM_COL32(255, 255, 255, 200) : line, i == 3 ? edgeT : ringT);
    DrawLine(cx - R, cy, cx + R, cy, line, ringT);
    DrawLine(cx, cy - R, cx, cy + R, line, ringT);
    //Each ring's range ("67m") just right of the line down the middle, inside the ring behind you. The inner two only
    //when they fit between the rings (a big Text size on a small radar: the outer one alone)
    const float rs = std::max(12.0f, SmallTextSize(0.8f));
    for (int i = 3; i >= 1; i--) {
        char t[24];
        std::snprintf(t, sizeof t, "%.0fm", rangeM * i / 3.0);
        const ImVec2 sz = LabelSize(t, rs);
        if (i < 3 && sz.y + 2 * (ringT / 2 + OutlineWidth(rs)) + 2 > R / 3.0f) break;
        const float lift = (i == 3 ? edgeT : ringT) / 2 + OutlineWidth(rs) + 2; //clear of its ring, outline included
        DrawLabelCentered(cx + ringT / 2 + OutlineWidth(rs) + 2 + sz.x / 2, cy + R * i / 3.0f - lift - sz.y / 2, t, rs, COLOR::white);
    }

    //What you can see: the edges of the view, drawn as two lines
    if (viewUsable(cameraInfo)) {
        double tanH = std::tan(cameraInfo.FOV * 3.14159265358979 / 360.0);
        const Projection pr = MakeProjection(); //the game's own picture shape, not the screen's
        if (!config::fovIsHorizontal) tanH *= (double)pr.width / std::max(1, pr.height);
        tanH /= std::max(0.01, pr.scale); //the Zoom calibration narrows or widens the view, as in View
        const float a = (float)std::atan(tanH);
        const ImU32 view = IM_COL32(255, 255, 255, 170);
        DrawLine(cx, cy, cx - R * std::sin(a), cy - R * std::cos(a), view, 2.0f * k);
        DrawLine(cx, cy, cx + R * std::sin(a), cy - R * std::cos(a), view, 2.0f * k);
    }

    // Convert yaw from degrees to radians
    float yawRad = (cameraInfo.Rotation.y + 90.0f) * 3.14159265358979323846f / 180.0f;
    float cosA = cos(yawRad);
    float sinA = sin(yawRad);

    for (const auto& ent : entities) {
        if (ent.type != Object::PLAYER && ent.type != Object::ARC) continue;
        if (ent.type == Object::PLAYER && !config::showPlayers) continue;
        if (ent.type == Object::ARC && !config::showArc) continue;
        if (std::hypot(ent.pos.x - cameraInfo.Location.x, ent.pos.y - cameraInfo.Location.y) / 100.0 > rangeM) continue; //beyond the radar range (flat: the radar is a 2D disk)
        if (ent.isDead) continue;
        //the same switches as the main view: Teammates (a hand mark always shows), Turrets, Hide players with unknown team
        if (ent.type == Object::PLAYER && localPlayerTeam >= 0 && ent.teamID == localPlayerTeam && !config::showTeammates) continue;
        if (ent.type == Object::PLAYER && !IsTeammate(ent, localPlayerTeam) && config::hideUnknownTeams && localPlayerTeam < 0) continue;
        if (ent.isTurret && !config::showTurrets) continue;

        float deltaX = ent.pos.x - cameraInfo.Location.x;
        float deltaY = ent.pos.y - cameraInfo.Location.y;

        // Rotate by camera yaw
        float rotatedX = deltaX * cosA + deltaY * sinA;
        float rotatedY = -deltaX * sinA + deltaY * cosA;

        float screenX = cx + rotatedX / config::scale;
        float screenY = cy + rotatedY / config::scale;

        // Clamp to radar circle
        float dx = screenX - cx;
        float dy = screenY - cy;
        float dist = sqrt(dx * dx + dy * dy);
        if (dist > R - 3) {
            float clampScale = (R - 3) / dist;
            screenX = cx + dx * clampScale;
            screenY = cy + dy * clampScale;
        }

        //ARC a diamond, enemy a filled dot, teammate a hollow ring: told apart by shape, not only by colour
        const bool mate = ent.type != Object::ARC && IsTeammate(ent, localPlayerTeam);
        const ImU32 col = ent.type == Object::ARC ? COLOR::arc(false) : mate ? COLOR::mate() : COLOR::enemy();
        //The way it faces, on the map: a thick line out of the dot (under it), growing with the dot. Not for turrets: their gun may turn without their base.
        float fx = 0, fy = 0;
        if (config::showFacing && !ent.isTurret && FacingOnScreen(ent.yaw, cameraInfo.Rotation.y, fx, fy))
            DrawFacingLine(screenX, screenY, fx, fy, 4.0f * k, 16.0f * k, col, 3.0f * k);
        if (ent.type == Object::ARC) DrawDiamondFilled(screenX, screenY, 5 * k, col);
        else if (mate) DrawCircleRing(screenX, screenY, 4.5f * k, col, 2.0f * k);
        else DrawCircleFilled(screenX, screenY, 5 * k, col);
        //more than 3 m above or below you (HeightDir): a small caret over or under the dot, pointing up or down
        if (const int up = HeightDir(cameraInfo, ent.pos)) {
            const float cs = 5 * k;
            DrawArrow(screenX, screenY - (float)up * (5 * k + 2 + cs), 0, -(float)up, cs, col);
        }
    }

    DrawArrow(cx, cy - 7 * k, 0, -1, 10 * k, COLOR::white); //you, looking up
    //In the middle of the title strip, in white, drawn last so a facing line reaching up into the strip never covers it
    DrawLabelCentered(cx, cy - R - 7 - ts / 2, RadarTitle().c_str(), ts, COLOR::white);
}

#endif //KWINHELPER_ESP_H
