#ifndef KWINHELPER_ESP_H
#define KWINHELPER_ESP_H
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
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
inline float DrawStatusCard(const std::string& line1, const std::string& line2) {
    const float x = (float)config::SCREEN_W * 0.5f, ls = config::labelSize;
    float y = 20;
    DrawLabelCentered(x, y, line1.c_str(), ls * 0.9f, COLOR::white, true);
    y += ls + 10;
    if (!line2.empty()) { DrawLabelCentered(x, y, line2.c_str(), ls * 0.8f, COLOR::grey, true); y += ls * 0.8f + 10; }
    return y;
}

inline void DrawToast(float y) {
    if (g_toast.empty() || ImGui::GetTime() >= g_toastUntil) return;
    DrawLabelCentered((float)config::SCREEN_W * 0.5f, y, g_toast.c_str(), config::labelSize, COLOR::white, true);
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

inline void QueueLabel(float x, float y, const std::string& text, float size, int prio, double dist, bool force = false) {
    if (text.empty()) return;
    const ImVec2 ts = LabelSize(text.c_str(), size);
    PendingLabel p;
    p.x = x; p.y = y; p.size = size; p.text = text;
    const LabelRect r = LabelAt(x, y, ts);
    p.box.x0 = r.x0; p.box.x1 = r.x1; p.box.y0 = r.y0; p.box.y1 = r.y1;
    p.box.prio = prio; p.box.dist = dist; p.box.force = force;
    PendingLabels().push_back(std::move(p));
}

//A label for a round marker at (cx, cy): below it if that is free, else above, to the right, to the left, or lower
inline void QueueMarkerLabel(float cx, float cy, float radius, const std::string& text, float size, int prio, double dist) {
    if (text.empty()) return;
    const ImVec2 ts = LabelSize(text.c_str(), size);
    const float below = cy + radius + 9;
    QueueLabel(cx, below, text, size, prio, dist);
    PendingLabel& p = PendingLabels().back();
    const ImVec2 alts[4] = {ImVec2(cx, cy - radius - 9), ImVec2(cx + radius + 8 + ts.x / 2, cy),
                            ImVec2(cx - radius - 8 - ts.x / 2, cy), ImVec2(cx, below + ts.y + 2)};
    for (const ImVec2& c : alts) { p.centers.push_back(c); p.box.alts.push_back(LabelAt(c.x, c.y, ts)); }
}

//Keeps labels away from this area (an arrow, a panel) without drawing anything
inline void QueueReserve(const LabelRect& r) {
    PendingLabel p;
    p.box.x0 = r.x0; p.box.y0 = r.y0; p.box.x1 = r.x1; p.box.y1 = r.y1;
    p.box.prio = -1; p.box.force = true;
    PendingLabels().push_back(std::move(p));
}

inline void FlushLabels() {
    auto& v = PendingLabels();
    std::vector<LabelReq> reqs;
    reqs.reserve(v.size());
    for (const PendingLabel& p : v) reqs.push_back(p.box);
    for (const LabelPick& pick : ChoosePlaces(reqs)) {
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
//when the shield is used up (or when the shield cannot be read).
inline void DrawPlayerBars(float x0, float y0, float w, float h, const RenderEntity& ent) {
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
        bar(bx + bw + 3.0f, sh, COLOR::shieldBar());
    }
}

//Where the player list at the bottom was drawn (set by DrawPlayerListWindow, one frame old; all zero when it is not shown)
inline LabelRect g_listRect;

//Arrows at the screen edge for players outside your squad who are not on screen (to the side, or behind you). The arrows
//keep other labels away from them; their distance labels only appear where there is room.
struct EdgeArrow { const RenderEntity* ent; double dist; ImU32 color; }; //color: enemy, or teammate for a hand mark
inline void DrawEdgeArrows(const View& v, std::vector<EdgeArrow>& edge) {
    if (edge.empty()) return;
    std::sort(edge.begin(), edge.end(), [](const EdgeArrow& a, const EdgeArrow& b) { return a.dist < b.dist; });
    if (edge.size() > 8) edge.resize(8);
    const float hx = std::max(20.0f, (float)v.rw / 2 - 46), hy = std::max(20.0f, (float)v.rh / 2 - 46); //edges of the game's picture
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
        DrawArrow(px, py, (float)ux, (float)uy, 15.0f, e.color);
        QueueReserve({px - 14, py - 14, px + 14, py + 14});
        std::snprintf(buf, sizeof buf, "%.0fm", e.dist);
        //Far enough back to clear the arrow's own reserved square (16 = 14 + LabelAt's x pad, 15 = 14 + its y pad), or a
        //wide or tall label would always be dropped; never closer than the old 30 px
        const float ls = config::labelSize * 0.9f;
        const ImVec2 ts = LabelSize(buf, ls);
        const float ax = std::max(std::abs((float)ux), 1e-3f), ay = std::max(std::abs((float)uy), 1e-3f);
        const float back = std::max(30.0f, 1.0f + std::min((16.0f + ts.x / 2) / ax, (15.0f + ts.y / 2) / ay));
        QueueLabel(px - (float)ux * back, py - (float)uy * back, buf, ls, 0, e.dist);
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
        const float r = std::max(2.0f, (float)std::hypot(e.x - c.x, e.y - c.y));
        DrawCircleRing((float)c.x, (float)c.y, r, dark, thickness + 2.0f);
        DrawCircleRing((float)c.x, (float)c.y, r, color, thickness);
    }
}

inline void DrawESP(const std::vector<RenderEntity>& entities, FminimalViewInfo vm, int localPlayerTeam) {
    const View v(vm, MakeProjection());
    if (!v.ok) return;
    const float W = (float)config::SCREEN_W, H = (float)config::SCREEN_H;
    const float ls = config::labelSize, ls2 = config::labelSize * 0.9f;
    auto onScreen = [&](double x, double y) { return x > -60 && x < W + 60 && y > -60 && y < H + 60; };
    std::vector<EdgeArrow> edge;
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
                if (config::showPlayerNames) QueueLabel((float)sp.x, (float)(sp.y - rad - 10), ent.label, ls, 1, dist);
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
            const float th = std::min(4.0f, std::max(1.0f, config::boxThickness));
            const int style = (int)config::boxStyle;
            if (style == 1) DrawBoxOutlined(x0, y0, w, h, color, th);
            else if (style == 2) DrawSidesBox(x0, y0, w, h, color, th);
            else DrawCornerBox(x0, y0, w, h, color, th);

            if (config::showSkeleton && ent.pose && dist <= config::skeletonMaxDist) DrawSkeleton(v, *ent.pose, color, th);

            if (config::showBars) DrawPlayerBars(x0, y0, w, h, ent); //health, and the shield next to it

            //name above, distance and guns below. Players are always drawn, even over other labels.
            if (config::showPlayerNames) QueueLabel((float)sHead.x, y0 - 10, ent.label, ls, 0, dist, true);
            snprintf(buf, sizeof buf, "%.0fm", dist);
            float below = y0 + h + 10;
            QueueLabel((float)sHead.x, below, buf, ls2, 0, dist, true);
            below += ls2 + 2;
            if (config::showPlayerGuns) QueueLabel((float)sHead.x, below, ent.sub, ls2, 0, dist, true);
        } else {
            if (!front || !onScreen(sp.x, sp.y)) continue;
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
    FlushLabels();
}

//The radar: a panel with three rings, your own arrow, and the edges of your view
inline void DrawRadar(const std::vector<RenderEntity>& entities, FminimalViewInfo cameraInfo, int localPlayerTeam) {
    const float cx = config::radarCenterX, cy = config::radarCenterY, R = config::radarRadius;
    const double rangeM = R * config::scale / 100.0; //metres from the centre to the edge

    //Panel, rings, crosshairs
    DrawRectFilled(cx - R - 14, cy - R - 26, cx + R + 14, cy + R + 14, IM_COL32(0, 0, 0, 150));
    DrawRectOutline(cx - R - 14, cy - R - 26, cx + R + 14, cy + R + 14, IM_COL32(255, 255, 255, 70), 1.0f);
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
        const float a = (float)std::atan(tanH);
        DrawLine(cx, cy, cx - R * std::sin(a), cy - R * std::cos(a), IM_COL32(255, 255, 255, 70), 1);
        DrawLine(cx, cy, cx + R * std::sin(a), cy - R * std::cos(a), IM_COL32(255, 255, 255, 70), 1);
    }

    // Convert yaw from degrees to radians
    float yawRad = (cameraInfo.Rotation.y + 90.0f) * 3.14159265358979323846f / 180.0f;
    float cosA = cos(yawRad);
    float sinA = sin(yawRad);

    for (const auto& ent : entities) {
        if (ent.type != Object::PLAYER && ent.type != Object::ARC) continue;
        if (ent.type == Object::PLAYER && !config::showPlayers) continue;
        if (ent.type == Object::ARC && !config::showArc) continue;
        if (ent.pos.Dist(cameraInfo.Location) / 100.0 > rangeM) continue; //beyond the radar range
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
        if (ent.type == Object::ARC) DrawDiamondFilled(screenX, screenY, 5, COLOR::arc(false));
        else if (IsTeammate(ent, localPlayerTeam)) DrawCircleRing(screenX, screenY, 4.5f, COLOR::mate(), 2.0f);
        else DrawCircleFilled(screenX, screenY, 5, COLOR::enemy());
    }

    DrawArrow(cx, cy - 7, 0, -1, 10, COLOR::white); //you, looking up
}

#endif //KWINHELPER_ESP_H
