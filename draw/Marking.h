#pragma once
//F6: mark (or unmark) the player nearest the crosshair as a teammate. Only when it is clear which player is meant.
#ifndef KWINHELPER_MARKING_H
#define KWINHELPER_MARKING_H

#include <cmath>
#include <string>
#include <vector>

#include "W2S.h"
#include "../utils/localUtil.h"
#include "../utils/marks.h"

struct MarkResult {
    enum Kind { Marked, Unmarked, NoneNearby, Ambiguous, NoId } kind = NoneNearby;
    std::string name;
    std::string text() const {
        switch (kind) {
            case Marked: return "Teammate marked: " + name;
            case Unmarked: return "Teammate mark removed: " + name;
            case Ambiguous: return "Two players near the crosshair: aim at one";
            case NoId: return "No account id for that player yet";
            default: return "No player near the crosshair";
        }
    }
};

//maxPx: how close to the crosshair (screen pixels) a player has to be to count
inline MarkResult MarkNearestAtCrosshair(const std::vector<RenderEntity>& ents, const View& v, int localTeam, double maxPx = 170.0) {
    MarkResult r;
    if (!v.ok) return r;
    const RenderEntity* best = nullptr;
    double d1 = 1e18, d2 = 1e18;
    for (const RenderEntity& e : ents) {
        if (e.type != Object::PLAYER || e.isDead) continue;
        if (localTeam >= 0 && e.teamID == localTeam) continue; //your squad is not marked by hand
        Vector3 mid = e.pos; //the middle of the body, not the feet
        Vector2 s;
        if (!v.project(mid, s)) continue;
        const double d = std::hypot(s.x - v.cx, s.y - v.cy);
        if (d < d1) { d2 = d1; d1 = d; best = &e; }
        else if (d < d2) d2 = d;
    }
    if (!best || d1 > maxPx) return r;
    if (d2 < std::max(d1 * 1.6, d1 + 40.0) && d2 <= maxPx * 1.5) { r.kind = MarkResult::Ambiguous; return r; }
    r.name = !best->label.empty() ? best->label : best->account;
    if (best->account.empty()) { r.kind = MarkResult::NoId; return r; }
    marks::toggle(best->account);
    r.kind = marks::isMarked(best->account) ? MarkResult::Marked : MarkResult::Unmarked;
    return r;
}

#endif //KWINHELPER_MARKING_H
