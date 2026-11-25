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

namespace COLOR {
    inline ImU32 red = IM_COL32(207, 56, 56, 255);
    inline ImU32 darkred = IM_COL32(147, 36, 36, 255);
    inline ImU32 blue = IM_COL32(58, 45, 237, 255);
    inline ImU32 orange = IM_COL32(240, 168, 43, 255);
    inline ImU32 darkorange = IM_COL32(120, 85, 21, 255);
    inline ImU32 green = IM_COL32(34, 189, 39, 255);
    inline ImU32 greenHealth = IM_COL32(34, 189, 39, 255); //because the green for items and for health CAN be different
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
    inline ImU32 arc(bool dead) { return mono() ? (dead ? IM_COL32(110, 110, 110, 150) : white) : (dead ? darkorange : orange); }
    inline ImU32 container() { return mono() ? IM_COL32(150, 150, 150, 170) : green; }
    inline ImU32 pickup() { return mono() ? IM_COL32(215, 215, 215, 200) : cyan; }
    inline ImU32 deadPlayer() { return mono() ? IM_COL32(110, 110, 110, 150) : darkred; }
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
    if (config::makeDeadBotESPTranslucent) COLOR::darkorange = COLOR::withAlpha(COLOR::darkorange, 128);
    if (config::makeDeadPlayerESPTranslucent) COLOR::darkred = COLOR::withAlpha(COLOR::darkred, 128);
}

//Your squad (same team id) or players you marked by hand
inline bool IsTeammate(const RenderEntity& e, int localTeam) {
    return (localTeam >= 0 && e.teamID == localTeam) || marks::isMarked(e.account);
}

//--- a short message at the top of the screen ("Teammate marked: ...")
inline std::string g_toast;
inline double g_toastUntil = 0;
inline void Toast(const std::string& text, double seconds = 3.0) {
    g_toast = text;
    g_toastUntil = ImGui::GetTime() + seconds;
}

//Status card at the top, in the middle. Returns where the next thing can go.
inline float DrawStatusCard(const std::string& line1, const std::string& line2, float y = 20) {
    const float x = (float)config::SCREEN_W * 0.5f, ls = config::labelSize;
    DrawLabelCentered(x, y, line1.c_str(), ls * 0.9f, COLOR::white, true);
    y += ls + 10;
    if (!line2.empty()) { DrawLabelCentered(x, y, line2.c_str(), ls * 0.8f, COLOR::grey, true); y += ls * 0.8f + 10; }
    return y;
}

inline void DrawToast(float y) {
    if (g_toast.empty() || ImGui::GetTime() >= g_toastUntil) return;
    DrawLabelCentered((float)config::SCREEN_W * 0.5f, y, g_toast.c_str(), config::labelSize, COLOR::white, true);
}

//--- the spoken warning when an enemy player first comes within config::cueRange ("Enemy behind, 30 meters").
//At most one every 2 s, the nearest first; one who has to wait is said at the next free moment if he is still that
//close. He is said again only after going more than 5 m past the range, so walking along its edge does not repeat it.
struct NearCue {
    std::set<ptr> said; //enemies already said and still within the range (+ 5 m)
    double last = -1e9; //when the last warning was given (seconds)

    //Returns what to say this frame, "" for nothing. now: seconds.
    std::string update(const std::vector<RenderEntity>& ents, const FminimalViewInfo& cam, int localTeam, double now) {
        std::set<ptr> keep;
        const RenderEntity* best = nullptr;
        double bestD = 1e18;
        for (const RenderEntity& e : ents) {
            if (e.type != Object::PLAYER || e.isDead || IsTeammate(e, localTeam)) continue;
            if (config::hideUnknownTeams && localTeam < 0) continue; //as on screen: no team to compare with
            const double d = e.pos.Dist(cam.Location) / 100.0;
            if (!std::isfinite(d)) continue;
            if (said.count(e.actor) && d <= config::cueRange + 5) keep.insert(e.actor);
            else if (d <= config::cueRange && d < bestD) { best = &e; bestD = d; }
        }
        std::string text;
        if (best && now - last >= 2.0) {
            keep.insert(best->actor);
            last = now;
            //Which way, flat (the camera's yaw only): ahead, behind, left or right, each a quarter of the circle
            const double yaw = cam.Rotation.y * 3.14159265358979 / 180.0;
            const double dx = best->pos.x - cam.Location.x, dy = best->pos.y - cam.Location.y;
            const double f = dx * std::cos(yaw) + dy * std::sin(yaw), r = dy * std::cos(yaw) - dx * std::sin(yaw);
            const char* dir = !std::isfinite(f + r) ? "near" : f >= std::abs(r) ? "ahead" : -f >= std::abs(r) ? "behind" : r > 0 ? "right" : "left";
            text = std::string("Enemy ") + dir + ", " + std::to_string((long)std::lround(bestD)) + " meters";
        }
        said.swap(keep);
        return text;
    }
};

//Your own health and shield in big white text on a dark card, at the bottom left just above the game's health bar,
//and a big red LOW HEALTH card below the crosshair under 35. hp < 0: not read, so nothing (never a guessed number).
//shield < 0: not read or no shield offset, so health only. below: the bottom of the close-enemy card (0: none);
//LOW HEALTH then goes under it instead of on top of it.
inline void DrawSelfHealth(double hp, double shield, float below = 0) {
    if (!(hp >= 0)) return; //also NaN
    const int h = (int)std::lround(hp);
    char buf[48];
    if (shield >= 0) std::snprintf(buf, sizeof buf, "HP %d   SH %.0f", h, shield);
    else std::snprintf(buf, sizeof buf, "HP %d", h);
    const float ls = config::labelSize, big = ls * 1.8f;
    DrawLabelCentered(40 + LabelSize(buf, big).x / 2, (float)config::SCREEN_H - 300, buf, big, COLOR::white, true);
    if (h < 1 || h >= 35) return; //at 0 you are down: the game shows that itself
    const char* warn = "LOW HEALTH";
    const float huge = ls * 2.4f, x = (float)config::SCREEN_W * 0.5f;
    const ImVec2 ts = LabelSize(warn, huge);
    const float y = std::max((float)config::SCREEN_H * 0.62f, below + 10 + ts.y / 2 + 6);
    if (!COLOR::mono()) //white on solid red; monochrome: white on the dark card
        DrawRectFilled(x - ts.x / 2 - 14, y - ts.y / 2 - 6, x + ts.x / 2 + 14, y + ts.y / 2 + 6, COLOR::red);
    DrawLabelCentered(x, y, warn, huge, COLOR::white, COLOR::mono());
}

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

inline LabelRect LabelAt(float x, float y, const ImVec2& ts) {
    return {x - ts.x / 2 - 2, y - ts.y / 2 - 1, x + ts.x / 2 + 2, y + ts.y / 2 + 1};
}

//step: when the place is taken, try one and two steps further (in y), as long as that stays on the screen
inline void QueueLabel(float x, float y, const std::string& text, float size, int prio, double dist, bool force = false, float step = 0) {
    if (text.empty()) return;
    const ImVec2 ts = LabelSize(text.c_str(), size);
    PendingLabel p;
    p.x = x; p.y = y; p.size = size; p.text = text;
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
//At the bottom edge of the screen it goes above first. It stays 3 px clear of the marker, card included.
inline void QueueMarkerLabel(float cx, float cy, float radius, const std::string& text, float size, int prio, double dist) {
    if (text.empty()) return;
    const ImVec2 ts = LabelSize(text.c_str(), size);
    const float pad = config::labelCards ? 2.0f : 0.0f; //a card reaches 2 px past its text, top and bottom
    const float gap = radius + 3 + pad + ts.y / 2, step = ts.y + 2 + 2 * pad;
    float first = cy + gap, second = cy - gap, out = step;
    if (first + ts.y / 2 + pad > (float)config::SCREEN_H) { std::swap(first, second); out = -step; }
    QueueLabel(cx, first, text, size, prio, dist);
    PendingLabel& p = PendingLabels().back();
    const ImVec2 alts[4] = {ImVec2(cx, second), ImVec2(cx + radius + 8 + ts.x / 2, cy),
                            ImVec2(cx - radius - 8 - ts.x / 2, cy), ImVec2(cx, first + out)};
    for (const ImVec2& c : alts) { p.centers.push_back(c); p.box.alts.push_back(LabelAt(c.x, c.y, ts)); }
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

//Top and bottom of a player's box, as heights in the world. Standing, that is 90 cm each way from his position; when a
//pose is known (crouching, rolling) the box follows the real figure instead of staying standing-sized.
inline void PlayerBoxZ(const RenderEntity& ent, double& top, double& bottom) {
    top = ent.pos.z + 90; bottom = ent.pos.z - 90;
    if (!ent.pose) return;
    double lo = 1e18, hi = -1e18;
    for (const Vector3& j : ent.pose->j) {
        if (!std::isfinite(j.z)) return;
        lo = std::min(lo, j.z); hi = std::max(hi, j.z);
    }
    if (hi - lo < 40.0 || hi - lo > 260.0) return; //not a believable figure: keep the standing box
    top = hi + 12.0;   //the head ring sits above the head bone
    bottom = lo - 3.0;
}

//The two bars on the right of a player box: health (red to green) and, next to it, the shield. The shield bar is gone
//when the shield is used up (or when the shield cannot be read). Returns the right edge of what it drew.
inline float DrawPlayerBars(float x0, float y0, float w, float h, const RenderEntity& ent) {
    const float bw = std::min(5.0f, std::max(2.0f, h * 0.05f));
    auto bar = [&](float bx, float frac, ImU32 col) {
        DrawRectFilled(bx - 1, y0 - 1, bx + bw + 1, y0 + h + 1, IM_COL32(0, 0, 0, 200));
        DrawRectFilled(bx, y0 + h - h * frac, bx + bw, y0 + h, col);
    };
    float bx = x0 + w + 4.0f;
    const float hp = (float)std::min(1.0, std::max(0.0, ent.playerHealth / 100.0)); //never taller than the box
    bar(bx, hp, COLOR::health(hp));
    if (ent.hasShield && ent.shield >= 1.0 && ent.shieldMax > 0) {
        const float sh = (float)std::min(1.0, std::max(0.0, ent.shield / ent.shieldMax));
        bx += bw + 3.0f;
        bar(bx, sh, COLOR::shieldBar());
    }
    return bx + bw + 1;
}

//Where the player list at the bottom was drawn (set by DrawPlayerListWindow, one frame old; all zero when it is not shown)
inline LabelRect g_listRect;

//Arrows at the screen edge for players outside your squad who are not on screen (to the side, or behind you). The arrows
//keep other labels away from them; their distance labels only appear where there is room. Nearby live ARC get the same
//arrows, kite-shaped and in the ARC colour. cap: the most arrows drawn (the nearest ones).
struct EdgeArrow { const RenderEntity* ent; double dist; ImU32 color; bool arc = false; }; //color: enemy, or teammate for a hand mark
inline void DrawEdgeArrows(const View& v, std::vector<EdgeArrow>& edge, size_t cap = 8) {
    if (edge.empty()) return;
    std::sort(edge.begin(), edge.end(), [](const EdgeArrow& a, const EdgeArrow& b) { return a.dist < b.dist; });
    if (edge.size() > cap) edge.resize(cap);
    const float hx = std::max(20.0f, (float)v.rw / 2 - 46), hy = std::max(20.0f, (float)v.rh / 2 - 46); //edges of the game's picture
    const float k = std::max(1.0f, config::labelSize / 15.0f); //grows with the text size, never below the 15 px default
    const float as = 15.0f * k, half = 14.0f * k;              //arrow length, half its reserved square
    char buf[32];
    for (const auto& e : edge) {
        double x, y, z;
        v.toCamera(e.ent->pos, x, y, z);
        double ux, uy;
        if (z >= 1.0) { ux = x / z * v.focal * v.sx; uy = -(y / z) * v.focal * v.sy; } //where it would be on screen, relative to the middle
        else { ux = x; uy = -z; }                                          //behind: left/right as it is, down on screen
        double len = std::hypot(ux, uy);
        if (len < 1e-6) { ux = 0; uy = 1; len = 1; }
        ux /= len; uy /= len;
        const float t = std::min(hx / (float)std::max(std::abs(ux), 1e-6), hy / (float)std::max(std::abs(uy), 1e-6));
        const float px = (float)v.cx + (float)ux * t;
        float py = (float)v.cy + (float)uy * t;
        const LabelRect& L = g_listRect;
        if (L.x1 > L.x0 && px > L.x0 - 20 && px < L.x1 + 20 && py > L.y0 - 20 && py < L.y1 + 20) py = L.y0 - 20; //not under the player list: just above it
        (e.arc ? DrawKiteArrow : DrawArrow)(px, py, (float)ux, (float)uy, as, e.color);
        QueueReserve({px - half, py - half, px + half, py + half});
        std::snprintf(buf, sizeof buf, "%.0fm", e.dist);
        //Far enough back to clear the arrow's own reserved square (half + LabelAt's 2 px x pad / 1 px y pad), or a
        //wide or tall label would always be dropped; never closer than two arrow lengths (the old 30 px at k = 1)
        const float ls = config::labelSize * 0.9f;
        const ImVec2 ts = LabelSize(buf, ls);
        const float ax = std::max(std::abs((float)ux), 1e-3f), ay = std::max(std::abs((float)uy), 1e-3f);
        const float back = std::max(2.0f * as, 1.0f + std::min((half + 2.0f + ts.x / 2) / ax, (half + 1.0f + ts.y / 2) / ay));
        QueueLabel(px - (float)ux * back, py - (float)uy * back, buf, ls, e.arc ? 1 : 0, e.dist); //ARC: same rank as its marker labels
    }
}

//The stick figure: dark outline first so it shows on any background, then the colour. A line is drawn only when both of
//its ends are in front of the camera.
inline void DrawSkeleton(const View& v, const skeleton::Pose& pose, ImU32 color, float thickness) {
    //color already carries the hidden-player dimming (dimHidden / hiddenAlpha): no second fade here
    Vector2 s[skeleton::JOINT_COUNT];
    bool ok[skeleton::JOINT_COUNT];
    for (int k = 0; k < skeleton::JOINT_COUNT; k++) ok[k] = v.project(pose.j[k], s[k]) && std::isfinite(s[k].x + s[k].y);
    const ImU32 dark = IM_COL32(0, 0, 0, 200);
    for (int pass = 0; pass < 2; pass++)
        for (int l = 0; l < skeleton::LINE_COUNT; l++) {
            const int a = skeleton::LINES[l][0], b = skeleton::LINES[l][1];
            if (!ok[a] || !ok[b]) continue;
            if (pass == 0) DrawLine((float)s[a].x, (float)s[a].y, (float)s[b].x, (float)s[b].y, dark, thickness + 2.0f);
            else DrawLine((float)s[a].x, (float)s[a].y, (float)s[b].x, (float)s[b].y, color, thickness);
        }

    //The head: a ring around where the skull is, which is a little further up the neck's direction than the head bone
    const Vector3& head = pose.j[skeleton::HEAD];
    const Vector3& neck = pose.j[skeleton::NECK2];
    Vector3 d(head.x - neck.x, head.y - neck.y, head.z - neck.z);
    double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (!(len > 1e-3) || !std::isfinite(len)) { d = Vector3(0, 0, 1); len = 1; }
    const double up = 12.0 / len; //the skull's centre is about 12 cm beyond the head bone
    const Vector3 centre(head.x + d.x * up, head.y + d.y * up, head.z + d.z * up);
    const Vector3 edge(centre.x + v.right.x * 9.0, centre.y + v.right.y * 9.0, centre.z + v.right.z * 9.0); //a head is about 9 cm in radius
    Vector2 c, e;
    if (v.project(centre, c) && v.project(edge, e) && std::isfinite(c.x + c.y + e.x + e.y)) {
        const float r = (float)std::hypot(e.x - c.x, e.y - c.y); //across only: a stretched picture makes it sy/sx as tall
        const float rx = std::max(2.0f, r), ry = std::max(2.0f, r * (float)(v.sy / v.sx));
        DrawEllipseRing((float)c.x, (float)c.y, rx, ry, dark, thickness + 2.0f);
        DrawEllipseRing((float)c.x, (float)c.y, rx, ry, color, thickness);
    }
}

//Fit the saved centre to this screen, so a position saved on another monitor or scale stays visible
inline ImVec2 RadarCenter() {
    const float R = config::radarRadius;
    return ImVec2(std::clamp(config::radarCenterX, R + 14, std::max(R + 14, (float)config::SCREEN_W - R - 14)),
                  std::clamp(config::radarCenterY, R + 26, std::max(R + 26, (float)config::SCREEN_H - R - 14)));
}

//The radar's dark panel, title included
inline LabelRect RadarPanel() {
    const float R = config::radarRadius;
    const ImVec2 c = RadarCenter();
    return {c.x - R - 14, c.y - R - 26, c.x + R + 14, c.y + R + 14};
}

inline void DrawESP(const std::vector<RenderEntity>& entities, FminimalViewInfo vm, int localPlayerTeam) {
    const View v(vm, MakeProjection());
    if (!v.ok) return;
    if (config::enableRadar) QueueReserve(RadarPanel()); //markers are drawn over the radar; labels move off it
    const float W = (float)config::SCREEN_W, H = (float)config::SCREEN_H;
    const float ls = config::labelSize, ls2 = config::labelSize * 0.9f;
    const float pad = config::labelCards ? 2.0f : 0.0f; //a card reaches 2 px past its text, top and bottom
    auto onScreen = [&](double x, double y) { return x > -60 && x < W + 60 && y > -60 && y < H + 60; };
    std::vector<EdgeArrow> edge, arcEdge;
    char buf[64];

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
            const bool mate = squad || marks::isMarked(ent.account); //your squad, or marked by hand
            if (!config::showPlayers) continue;
            if (dist > config::maxPlayerDist) continue;
            if (squad && !config::showTeammates) continue; //a hand mark is always drawn (in the teammate colour): it may be stale
            if (!mate && config::hideUnknownTeams && localPlayerTeam < 0) continue; //no team to compare with
            if (ent.isDead) {
                if (!config::showDeadPlayers || !front || !onScreen(sp.x, sp.y)) continue;
                const double rad = getProportionalRadius(dist, config::maxPlayerDist);
                DrawCross((float)sp.x, (float)sp.y, (float)rad, COLOR::deadPlayer());
                snprintf(buf, sizeof buf, "%.0fm", dist);
                QueueMarkerLabel((float)sp.x, (float)sp.y, (float)rad, buf, ls2, 2, dist);
                if (config::showPlayerNames) QueueLabel((float)sp.x, (float)(sp.y - rad - 3 - pad - ls / 2), ent.label, ls, 1, dist);
                continue;
            }

            //should be rendered from this point on
            ImU32 color = mate ? COLOR::mate() : COLOR::enemy();
            if (config::dimHidden && !ent.isVisible) color = COLOR::withAlpha(color, (int)config::hiddenAlpha); //not drawn by the game right now: behind cover
            double boxTop, boxBottom;
            PlayerBoxZ(ent, boxTop, boxBottom);
            Vector3 headPos = ent.pos; headPos.z = boxTop;
            Vector3 feetPos = ent.pos; feetPos.z = boxBottom;
            Vector2 sHead, sFeet;
            if (!v.project(headPos, sHead) || !v.project(feetPos, sFeet) || !onScreen(sHead.x, (sHead.y + sFeet.y) / 2)) {
                if (!squad && config::showOffscreen) edge.push_back({&ent, dist, mate ? COLOR::mate() : COLOR::enemy()});
                continue;
            }

            const float rawH = (float)(sFeet.y - sHead.y);
            if (!std::isfinite(rawH)) continue;
            const float h = std::max(6.0f, rawH); //a far player still gets a box, name and distance
            //About 90 cm wide whatever the pose (only the height follows it), at the depth of the box's middle; head and
            //feet are both in front, so the middle is too. A stretched picture makes people wider. Never under 3 px, so a
            //far player's box stays visible.
            Vector3 midPos = ent.pos; midPos.z = (boxTop + boxBottom) / 2;
            double mx, my, mz;
            v.toCamera(midPos, mx, my, mz);
            const float w = std::max(3.0f, (float)(90.0 * v.focal * v.sx / mz)), x0 = (float)sHead.x - w / 2, y0 = (float)sHead.y;
            const float th = std::min(10.0f, std::max(1.0f, config::boxThickness));
            const int style = (int)config::boxStyle;
            if (style == 1) DrawBoxOutlined(x0, y0, w, h, color, th);
            else if (style == 2) DrawSidesBox(x0, y0, w, h, color, th);
            else DrawCornerBox(x0, y0, w, h, color, th);

            if (config::showSkeleton && ent.pose && dist <= config::skeletonMaxDist) DrawSkeleton(v, *ent.pose, color, th);

            float right = x0 + w;
            if (config::showBars) right = DrawPlayerBars(x0, y0, w, h, ent); //health, and the shield next to it
            //loot, ARC and dead-body labels move off an enemy's box and bars, or drop; 3 px covers the thickest outline
            if (!mate) QueueReserve({x0 - 3, y0 - 3, right + 3, y0 + h + 3}, true);

            //name above, distance and guns below, 3 px clear of the box and 2 px apart (cards included). With no room
            //below the box the distance and guns go above the name; the lines are kept on the screen. Players are always
            //drawn: when crowded they move a line further out, and only over other labels when nothing is free.
            const float nameH = config::showPlayerNames ? ls + 2 * pad + 2 : 0, lineH = ls2 + 2 * pad + 2; //a line and its gap
            const float linesH = config::showPlayerGuns ? 2 * lineH : lineH;
            float nameTop = std::max(2.0f, y0 - 1 - nameH), lineTop = std::max(nameTop + nameH, y0 + h + 3), lineStep = lineH;
            if (lineTop + linesH > H) {
                lineTop = std::max(2.0f, std::min(y0 - 1 - nameH - linesH, H - linesH - nameH));
                nameTop = lineTop + linesH;
                lineStep = -lineH;
            }
            if (config::showPlayerNames) QueueLabel((float)sHead.x, nameTop + pad + ls / 2, ent.label, ls, 0, dist, true, -nameH);
            snprintf(buf, sizeof buf, "%.0fm", dist);
            const float below = lineTop + pad + ls2 / 2;
            QueueLabel((float)sHead.x, below, buf, ls2, 0, dist, true, lineStep);
            if (config::showPlayerGuns) QueueLabel((float)sHead.x, below + lineH, ent.sub, ls2, 0, dist, true, lineStep);
        } else {
            if (!front || !onScreen(sp.x, sp.y)) {
                //a live ARC within 50 m that is off screen or behind you: an arrow at the screen edge
                if (ent.type == Object::ARC && !ent.isDead && config::showArc && config::showArcOffscreen &&
                    (!ent.isTurret || config::showTurrets) && dist <= std::min(config::maxArcDist, 50.0))
                    arcEdge.push_back({&ent, dist, COLOR::arc(false), true});
                continue;
            }
            ImU32 color;
            double radius;
            int prio;
            bool nameOn;
            void (*shape)(float, float, float, ImU32); //each kind its own shape, in colour and in monochrome
            if (ent.type == Object::ARC) {
                if (!config::showArc || (ent.isDead && !config::showDeadArc)) continue;
                if (ent.isTurret && !config::showTurrets) continue;
                if (dist > config::maxArcDist) continue;
                color = COLOR::arc(ent.isDead);
                radius = getProportionalRadius(dist, config::maxArcDist);
                prio = 1;
                nameOn = config::showArcNames;
                shape = DrawDiamondFilled;
            } else if (ent.type == Object::SEARCH) {
                if (!config::showContainers || dist > config::maxLootDist) continue;
                color = COLOR::container();
                radius = getProportionalRadius(dist, config::maxLootDist);
                prio = 3;
                nameOn = config::showContainerNames && dist <= config::nameMaxLootDist;
                shape = DrawSquareFilled;
            } else if (ent.type == Object::PICKUP) {
                if (!config::showPickups || dist > config::maxLootDist) continue;
                color = COLOR::pickup();
                radius = getProportionalRadius(dist, config::maxLootDist);
                prio = 2;
                nameOn = config::showItemNames && dist <= config::nameMaxLootDist;
                shape = DrawTriangleFilled;
            } else {
                continue;
            }

            shape((float)sp.x, (float)sp.y, (float)radius, color);
            std::string text;
            if (nameOn && !ent.label.empty()) text = ent.label + "  ";
            snprintf(buf, sizeof buf, "%.0fm", dist);
            text += buf;
            QueueMarkerLabel((float)sp.x, (float)sp.y, (float)radius, text, ls2, prio, dist);
        }
    }
    if (g_listRect.x1 > g_listRect.x0) QueueReserve(g_listRect); //labels stay off the player list
    DrawEdgeArrows(v, edge);
    DrawEdgeArrows(v, arcEdge, 4); //after the players, so player arrows keep their room first
    FlushLabels();
}

//A big card under the middle of the screen for the nearest enemy closer than warnDist: "ENEMY 9 m BEHIND", or
//"3 ENEMIES - NEAREST 9 m LEFT". White text on a dark card framed in the enemy colour. Returns the text drawn;
//bottom (if given) gets the card's lower edge, left alone when nothing is drawn.
inline std::string DrawCloseEnemyWarning(const std::vector<RenderEntity>& entities, FminimalViewInfo vm, int localPlayerTeam,
                                         float* bottom = nullptr) {
    if (!config::closeWarning) return {};
    if (config::hideUnknownTeams && localPlayerTeam < 0) return {}; //no team to compare with: everyone is hidden
    const View v(vm, MakeProjection());
    if (!v.ok) return {};
    const RenderEntity* nearest = nullptr;
    double best = 0;
    int count = 0;
    for (const auto& ent : entities) {
        if (ent.type != Object::PLAYER || ent.isDead || IsTeammate(ent, localPlayerTeam)) continue;
        const double dist = ent.pos.Dist(vm.Location) / 100.0;
        if (!(dist <= config::warnDist)) continue; //NaN too
        count++;
        if (!nearest || dist < best) { best = dist; nearest = &ent; }
    }
    if (!nearest) return {};
    double x, y, z;
    v.toCamera(nearest->pos, x, y, z);
    const char* where = z < -std::abs(x) ? "BEHIND" : x < -std::abs(z) ? "LEFT" : x > std::abs(z) ? "RIGHT" : "AHEAD";
    char buf[96];
    if (count == 1) std::snprintf(buf, sizeof buf, "ENEMY %.0f m %s", best, where);
    else std::snprintf(buf, sizeof buf, "%d ENEMIES - NEAREST %.0f m %s", count, best, where);
    const float size = config::labelSize * 2.0f, cx = (float)v.cx, cy = (float)v.cy + 140.0f;
    const ImVec2 ts = LabelSize(buf, size);
    const float hx = ts.x / 2 + 10, hy = ts.y / 2 + 6;
    DrawRectFilled(cx - hx, cy - hy, cx + hx, cy + hy, IM_COL32(0, 0, 0, 220));
    DrawRectOutline(cx - hx, cy - hy, cx + hx, cy + hy, COLOR::enemy(), 3.0f);
    DrawLabelCentered(cx, cy, buf, size, COLOR::white);
    if (bottom) *bottom = cy + hy + 2; //+ half the 3 px frame
    return buf;
}

//The radar: a panel with three rings, your own arrow, and the edges of your view
inline void DrawRadar(const std::vector<RenderEntity>& entities, FminimalViewInfo cameraInfo, int localPlayerTeam) {
    const float R = config::radarRadius;
    const ImVec2 rc = RadarCenter();
    const float cx = rc.x, cy = rc.y;
    const double rangeM = R * config::scale / 100.0; //metres from the centre to the edge

    //Panel, rings, crosshairs
    const LabelRect P = RadarPanel();
    DrawRectFilled(P.x0, P.y0, P.x1, P.y1, IM_COL32(0, 0, 0, 150));
    DrawRectOutline(P.x0, P.y0, P.x1, P.y1, IM_COL32(255, 255, 255, 70), 1.0f);
    DrawCircleFilled(cx, cy, R, IM_COL32(0, 0, 0, 110));
    for (int i = 1; i <= 3; i++) DrawCircleRing(cx, cy, R * i / 3.0f, IM_COL32(255, 255, 255, i == 3 ? 90 : 35), 1.0f);
    DrawLine(cx - R, cy, cx + R, cy, IM_COL32(255, 255, 255, 40), 1);
    DrawLine(cx, cy - R, cx, cy + R, IM_COL32(255, 255, 255, 40), 1);
    char title[48];
    std::snprintf(title, sizeof title, "RADAR  %.0f m", rangeM);
    DrawLabelCentered(cx, cy - R - 13, title, 12.0f, IM_COL32(190, 190, 190, 255));

    //What you can see: the edges of the view, drawn as two lines
    if (viewUsable(cameraInfo)) {
        double tanH = std::tan(cameraInfo.FOV * 3.14159265358979 / 360.0);
        const Projection pr = MakeProjection(); //the game's own picture shape, not the screen's
        if (!config::fovIsHorizontal) tanH *= (double)pr.width / std::max(1, pr.height);
        tanH /= std::max(0.01, pr.scale); //the Zoom calibration narrows or widens the view, as in View
        const float a = (float)std::atan(tanH);
        DrawLine(cx, cy, cx - R * std::sin(a), cy - R * std::cos(a), IM_COL32(255, 255, 255, 70), 1);
        DrawLine(cx, cy, cx + R * std::sin(a), cy - R * std::cos(a), IM_COL32(255, 255, 255, 70), 1);
    }

    // Convert yaw from degrees to radians
    float yawRad = (cameraInfo.Rotation.y + 90.0f) * 3.14159265358979323846f / 180.0f;
    float cosA = cos(yawRad);
    float sinA = sin(yawRad);
    const float k = std::max(1.0f, config::labelSize / 15.0f); //blips grow with the text size, never below the 15 px default

    for (const auto& ent : entities) {
        if (ent.type != Object::PLAYER && ent.type != Object::ARC) continue;
        if (ent.type == Object::PLAYER && !config::showPlayers) continue;
        if (ent.type == Object::ARC && !config::showArc) continue;
        if (std::hypot(ent.pos.x - cameraInfo.Location.x, ent.pos.y - cameraInfo.Location.y) / 100.0 > rangeM) continue; //beyond the radar range (flat: the radar is a 2D disk)
        if (ent.isDead) continue;

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
        if (ent.type == Object::ARC) DrawDiamondFilled(screenX, screenY, 5 * k, COLOR::arc(false));
        else if (IsTeammate(ent, localPlayerTeam)) DrawCircleRing(screenX, screenY, 4.5f * k, COLOR::mate(), 2.0f * k);
        else DrawCircleFilled(screenX, screenY, 5 * k, COLOR::enemy());
    }

    DrawArrow(cx, cy - 7 * k, 0, -1, 10 * k, COLOR::white); //you, looking up
}

#endif //KWINHELPER_ESP_H
