#pragma once
//Decides which labels to draw, and where, when many of them would land on top of each other (a room full of
//containers). A label can offer several places (below, above, beside its marker): the first free one is used. No
//ImGui in here: it only works with boxes, so it can be tested without a window.
#ifndef KWINHELPER_DECLUTTER_H
#define KWINHELPER_DECLUTTER_H

#include <algorithm>
#include <numeric>
#include <vector>

struct LabelRect { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

struct LabelReq {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0; //the area the text covers in its first-choice place, in pixels
    int prio = 0;                         //lower is more important: it is placed first and keeps its spot
    double dist = 0;                      //metres: for the same prio, closer wins
    bool force = false;                   //always drawn: in a free place if any, else in its first place over another label (players)
    std::vector<LabelRect> alts;          //other places to try, in order, when the first one is taken
    bool body = false;                    //an enemy's box kept clear: other labels move off it, players' (forced or prio <= 0) ignore it
};

//Areas that only touch do not overlap. Lines of text stacked one under the other touch exactly; without the 0.01 px
//slack, rounding made them "overlap" on some frames, and the lower line (a player's guns) flickered a line further down.
inline bool RectsOverlap(const LabelRect& a, const LabelRect& b) {
    constexpr float TOUCH = 0.01f;
    return a.x0 < b.x1 - TOUCH && b.x0 < a.x1 - TOUCH && a.y0 < b.y1 - TOUCH && b.y0 < a.y1 - TOUCH;
}

inline bool LabelsOverlap(const LabelReq& a, const LabelReq& b) {
    return RectsOverlap({a.x0, a.y0, a.x1, a.y1}, {b.x0, b.y0, b.x1, b.y1});
}

struct LabelPick {
    size_t index;  //into the requests
    int alt;       //-1: the first-choice place, otherwise the number of the alternative used
    LabelRect where;
};

//The labels to draw, most important first, each with the place it got
inline std::vector<LabelPick> ChoosePlaces(const std::vector<LabelReq>& reqs) {
    std::vector<size_t> order(reqs.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (reqs[a].prio != reqs[b].prio) return reqs[a].prio < reqs[b].prio;
        return reqs[a].dist < reqs[b].dist;
    });
    std::vector<LabelPick> chosen;
    chosen.reserve(order.size());
    auto free = [&](const LabelRect& r, bool skipBody) {
        for (const LabelPick& c : chosen)
            if (RectsOverlap(r, c.where) && !(skipBody && reqs[c.index].body)) return false;
        return true;
    };
    for (size_t i : order) {
        const LabelReq& q = reqs[i];
        const LabelRect first{q.x0, q.y0, q.x1, q.y1};
        const bool skipBody = q.force || q.prio <= 0; //players' labels, the distance by an edge arrow included
        if (free(first, skipBody)) { chosen.push_back({i, -1, first}); continue; }
        bool placed = false;
        for (size_t k = 0; k < q.alts.size() && !placed; k++)
            if (free(q.alts[k], skipBody)) { chosen.push_back({i, (int)k, q.alts[k]}); placed = true; }
        if (!placed && q.force) chosen.push_back({i, -1, first}); //nowhere free: forced labels still go in their first place
    }
    return chosen;
}

//Same, when only the indices matter
inline std::vector<size_t> ChooseLabels(const std::vector<LabelReq>& reqs) {
    std::vector<size_t> out;
    for (const LabelPick& p : ChoosePlaces(reqs)) out.push_back(p.index);
    return out;
}

#endif //KWINHELPER_DECLUTTER_H
