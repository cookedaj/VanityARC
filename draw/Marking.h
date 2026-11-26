#pragma once
//F6: mark (or unmark) the player nearest the crosshair as a teammate. Only when it is clear which player is meant.
#ifndef KWINHELPER_MARKING_H
#define KWINHELPER_MARKING_H

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ESP.h" //PlayerScreenBox
#include "W2S.h"
#include "../utils/localUtil.h"
#include "../utils/marks.h"

struct MarkResult {
    enum Kind { Marked, Unmarked, NoneNearby, Ambiguous, NoId, NotMarkable } kind = NoneNearby;
    std::string name;
    bool down = false; //NotMarkable: the player is down (else: already in your squad)
    std::string text() const {
        switch (kind) {
            case Marked: return "Teammate marked: " + name;
            case Unmarked: return "Teammate mark removed: " + name;
            case Ambiguous: return "Two players near the crosshair: aim at one";
            case NoId: return "No account id for that player yet";
            case NotMarkable: return down ? "That player is down" : "That player is already in your squad";
            default: return "No player near the crosshair";
        }
    }
};

//maxPx: how close to the crosshair (screen pixels) a player's figure, head to feet, has to be to count
inline MarkResult MarkNearestAtCrosshair(const std::vector<RenderEntity>& ents, const View& v, int localTeam, double maxPx = 170.0) {
    MarkResult r;
    if (!v.ok) return r;
    const RenderEntity* best = nullptr;
    double d1 = 1e18, d2 = 1e18, dOut = 1e18; //dOut: the nearest player past the Players range
    for (const RenderEntity& e : ents) {
        if (e.type != Object::PLAYER) continue; //down players and your squad still count, so aiming at one never marks someone else
        //The figure on screen: his box as the ESP draws it, top to bottom along its middle. It follows the pose, so a
        //crouching player ends at his head, not 90 cm up where the player behind him may be. Just his middle when the box
        //is not on the screen (or one end is behind the camera).
        float cx, y0, w, h;
        Vector2 a, b;
        if (PlayerScreenBox(v, e, cx, y0, w, h)) { a = Vector2(cx, y0); b = Vector2(cx, y0 + h); }
        else if (v.project(e.pos, a)) b = a;
        else continue;
        const double ex = b.x - a.x, ey = b.y - a.y, len2 = ex * ex + ey * ey;
        const double t = len2 > 0 ? std::clamp(((v.cx - a.x) * ex + (v.cy - a.y) * ey) / len2, 0.0, 1.0) : 0.0;
        const double d = std::hypot(a.x + ex * t - v.cx, a.y + ey * t - v.cy);
        if (e.pos.Dist(v.loc) / 100.0 > config::maxPlayerDist) { dOut = std::min(dOut, d); continue; } //only players the ESP can show are marked
        if (d < d1) { d2 = d1; d1 = d; best = &e; }
        else if (d < d2) d2 = d;
    }
    if (!best || d1 > maxPx || dOut < d1) return r; //aiming at a player past the range never marks someone else
    if (d2 < std::max(d1 * 1.6, d1 + 40.0) && d2 <= maxPx * 1.5) { r.kind = MarkResult::Ambiguous; return r; }
    if (best->isDead || (localTeam >= 0 && best->teamID == localTeam)) { r.kind = MarkResult::NotMarkable; r.down = best->isDead; return r; } //your squad is not marked by hand
    r.name = !best->label.empty() ? best->label : best->account;
    if (best->account.empty()) { r.kind = MarkResult::NoId; return r; }
    marks::toggle(best->account);
    r.kind = marks::isMarked(best->account) ? MarkResult::Marked : MarkResult::Unmarked;
    return r;
}

#endif //KWINHELPER_MARKING_H
