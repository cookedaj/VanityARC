#pragma once
//The player list (at the bottom of the screen and in the menu's Players tab) and the inventory windows that open
//when you click a name. Everything here only displays what the tracker read.
#ifndef KWINHELPER_PLAYERLIST_H
#define KWINHELPER_PLAYERLIST_H

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include "../utils/itemvalues.h"
#include "../utils/amp.h"
#include <vector>

#include "ESP.h"
#include "UiKit.h"
#include "../utils/offsets.h"

//What the draw loop hands to the UI each frame
struct UiFrame {
    const std::vector<RenderEntity>* ents = nullptr;
    std::shared_ptr<const std::unordered_map<ptr, std::vector<ItemRow>>> inv;
    FminimalViewInfo vm{};
    int localTeam = -1;
    bool live = true; //false: no live data from the game, so an empty list means "no data", not "nobody here"
};
inline UiFrame g_ui;
inline std::vector<std::string> g_inspect; //players whose inventory window is open (see PlayerKey)

//Who a player is across frames: the account id, or the actor address when the account is not known yet
inline std::string PlayerKey(const RenderEntity& e) {
    if (!e.account.empty()) return e.account;
    char b[32];
    std::snprintf(b, sizeof b, "@%llx", (unsigned long long)e.actor);
    return b;
}

inline std::vector<const RenderEntity*> PlayersByDistance() {
    std::vector<const RenderEntity*> v;
    if (!g_ui.ents) return v;
    for (const RenderEntity& e : *g_ui.ents) if (e.type == Object::PLAYER) v.push_back(&e);
    const Vector3 cam = g_ui.vm.Location;
    std::stable_sort(v.begin(), v.end(), [&](const RenderEntity* a, const RenderEntity* b) { return a->pos.Dist(cam) < b->pos.Dist(cam); });
    return v;
}

inline bool IsInspected(const std::string& key) { return std::find(g_inspect.begin(), g_inspect.end(), key) != g_inspect.end(); }
inline void ToggleInspect(const std::string& key) {
    auto it = std::find(g_inspect.begin(), g_inspect.end(), key);
    if (it != g_inspect.end()) g_inspect.erase(it);
    else if (g_inspect.size() < 6) g_inspect.push_back(key);
}

//The actors whose inventory the tracker should read
inline std::vector<ptr> InspectedActors() {
    std::vector<ptr> a;
    if (!g_ui.ents) return a;
    for (const RenderEntity& e : *g_ui.ents) if (e.type == Object::PLAYER && IsInspected(PlayerKey(e))) a.push_back(e.actor);
    return a;
}

//One row per player: name, distance, health, shield, team, guns. Click a name for the inventory, the TEAM button marks a
//teammate. interactive = false: only shown (the overlay is click-through then).
inline void DrawPlayerRows(int maxRows) {
    const auto players = PlayersByDistance();
    if (players.empty()) { ui::Note(g_ui.live ? "No players in view." : "No live data from the game."); return; }
    const float xDist = 190, xHp = 245, xSh = 295, xSeen = 360, xTeam = 415, xGun = 495, rowW = 780;
    ImGui::TextColored(ImColor(ui::GREY), "NAME");
    ImGui::SameLine(xDist); ImGui::TextColored(ImColor(ui::GREY), "DIST");
    ImGui::SameLine(xHp); ImGui::TextColored(ImColor(ui::GREY), "HP");
    ImGui::SameLine(xSh); ImGui::TextColored(ImColor(ui::GREY), "SHIELD");
    ImGui::SameLine(xSeen); ImGui::TextColored(ImColor(ui::GREY), "SEEN");
    ImGui::SameLine(xTeam); ImGui::TextColored(ImColor(ui::GREY), "TEAM");
    ImGui::SameLine(xGun); ImGui::TextColored(ImColor(ui::GREY), "GUNS");
    int shown = 0;
    for (const RenderEntity* e : players) {
        if (shown++ >= maxRows) break;
        const std::string key = PlayerKey(*e);
        const bool squad = g_ui.localTeam >= 0 && e->teamID == g_ui.localTeam;
        const bool marked = marks::isMarked(e->account);
        const bool mate = squad || marked;
        const ImU32 col = e->isDead ? ui::DIM : (mate ? ui::GREY : ui::WHITE);
        ImGui::PushID(key.c_str());
        const std::string name = !e->label.empty() ? e->label : (e->account.empty() ? "Unknown" : e->account.substr(0, 8));
        ImGui::SetNextItemAllowOverlap(); //the TEAM button on top of the row takes its own clicks (and hides the row tooltip)
        if (ImGui::Selectable("##row", IsInspected(key), 0, ImVec2(rowW, 20))) ToggleInspect(key);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click for the inventory");
        ImGui::SameLine(6);
        ImGui::PushStyleColor(ImGuiCol_Text, ImColor(col).Value);
        ImGui::TextUnformatted(name.c_str());
        char b[32];
        std::snprintf(b, sizeof b, "%.0f m", e->pos.Dist(g_ui.vm.Location) / 100.0);
        ImGui::SameLine(xDist); ImGui::TextUnformatted(b);
        std::snprintf(b, sizeof b, e->isDead ? "down" : "%.0f", e->playerHealth);
        ImGui::SameLine(xHp); ImGui::TextUnformatted(b);
        if (e->hasShield && e->shield >= 1.0) std::snprintf(b, sizeof b, "%.0f", e->shield); else std::snprintf(b, sizeof b, "-");
        ImGui::SameLine(xSh); ImGui::TextUnformatted(b);
        ImGui::SameLine(xSeen); ImGui::TextUnformatted(e->isDead ? "" : (e->isVisible ? "yes" : "-")); //the game is drawing him right now
        ImGui::PopStyleColor();
        ImGui::SameLine(xTeam);
        if (squad) ImGui::TextColored(ImColor(ui::GREY), "squad");
        else if (!e->account.empty()) {
            if (ImGui::SmallButton(marked ? "TEAMMATE" : "mark")) { marks::toggle(e->account); marks::save(); }
        } else ImGui::TextColored(ImColor(ui::DIM), "no id");
        ImGui::SameLine(xGun);
        ImGui::PushStyleColor(ImGuiCol_Text, ImColor(col).Value);
        ImGui::TextUnformatted(e->sub.c_str());
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if ((int)players.size() > maxRows) ImGui::TextColored(ImColor(ui::GREY), "+ %d more", (int)players.size() - maxRows);
}

//Every saved hand mark (they never expire), each with a button to remove it. The name shows when that player is in view.
inline void DrawSavedMarks() {
    const std::vector<std::string> ids = marks::all();
    char head[48];
    std::snprintf(head, sizeof head, "SAVED MARKS (%d)", (int)ids.size());
    ui::Section(head);
    if (ids.empty()) { ui::Note("No saved marks."); return; }
    for (const std::string& id : ids) {
        std::string who = id;
        if (g_ui.ents)
            for (const RenderEntity& e : *g_ui.ents)
                if (e.type == Object::PLAYER && e.account == id && !e.label.empty()) { who = e.label + "  (" + id + ")"; break; }
        ImGui::PushID(id.c_str());
        if (ImGui::SmallButton("remove")) { marks::setMarked(id, false); marks::save(); }
        ImGui::SameLine();
        ImGui::TextUnformatted(who.c_str());
        ImGui::PopID();
    }
}

//The list at the bottom of the screen. menuOpen: clickable; otherwise it is only shown.
inline void DrawPlayerListWindow(bool menuOpen) {
    g_listRect = {}; //not shown: the edge arrows need not avoid it
    const int mode = (int)config::listMode;
    if (mode == 0 || (mode == 2 && !menuOpen) || !g_ui.ents) return;
    if (PlayersByDistance().empty()) return;
    ui::PushTheme(std::min(0.9f, config::menuOpacity));
    ImGui::SetNextWindowPos(ImVec2((float)config::SCREEN_W * 0.5f, (float)config::SCREEN_H - 14), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGuiWindowFlags f = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (!menuOpen) f |= ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##playerlist", nullptr, f)) DrawPlayerRows(std::max(1, (int)config::listRows));
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    g_listRect = {wp.x, wp.y, wp.x + ws.x, wp.y + ws.y}; //the edge arrows and labels keep off it (see ESP.h)
    ImGui::End();
    ui::PopTheme();
}

//One window per player you clicked: who they are and everything we can read of what they carry
inline void DrawInspectWindows() {
    if (g_inspect.empty() || !g_ui.ents) return;
    ui::PushTheme(std::min(0.96f, config::menuOpacity + 0.04f));
    std::vector<std::string> close;
    int n = 0;
    for (const std::string& key : g_inspect) {
        const RenderEntity* e = nullptr;
        for (const RenderEntity& x : *g_ui.ents) if (x.type == Object::PLAYER && PlayerKey(x) == key) { e = &x; break; }
        const std::string who = e && !e->label.empty() ? e->label : (e && !e->account.empty() ? e->account.substr(0, 8) : "Player");
        const std::string title = who + "  -  inventory###inv" + key;
        ImGui::SetNextWindowSize(ImVec2(360, 420), ImGuiCond_FirstUseEver);
        //beside the menu (820 wide at x 120), not on its player list; kept on the screen
        ImGui::SetNextWindowPos(ImVec2(std::max(0.0f, std::min(960.0f + 36.0f * n, (float)config::SCREEN_W - 380.0f)),
                                       std::max(0.0f, std::min(110.0f + 36.0f * n, (float)config::SCREEN_H - 430.0f))), ImGuiCond_FirstUseEver);
        n++;
        bool open = true;
        if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse)) {
            if (!e) {
                ui::Note(g_ui.live ? "Not in view right now." : "No live data from the game.");
            } else {
                char b[160];
                std::snprintf(b, sizeof b, "%.0f m   -   %s", e->pos.Dist(g_ui.vm.Location) / 100.0, e->isDead ? "down" : "alive");
                ImGui::TextUnformatted(b);
                std::snprintf(b, sizeof b, "Health %.0f", e->playerHealth);
                std::string line = b;
                if (e->hasShield) { std::snprintf(b, sizeof b, "   Shield %.0f / %.0f", e->shield, e->shieldMax); line += b; }
                ImGui::TextUnformatted(line.c_str());
                const std::vector<ItemRow>* rows = nullptr;
                if (g_ui.inv) { auto it = g_ui.inv->find(e->actor); if (it != g_ui.inv->end()) rows = &it->second; }
                //Amplification: the highest module level in the mod list (MK I..IV, MK V = AMP)
                int ampLvl = 0;
                if (rows) for (const ItemRow& r : *rows) if (r.category == "mod") ampLvl = std::max(ampLvl, amp::moduleLevel(r.name));
                std::vector<std::string> guns;
                size_t start = 0;
                while (start < e->sub.size()) {
                    size_t end = e->sub.find(" + ", start);
                    if (end == std::string::npos) end = e->sub.size();
                    guns.push_back(e->sub.substr(start, end - start));
                    start = end + 3;
                }
                ui::Section("EQUIPPED");
                if (guns.empty()) ui::Note("No guns read.");
                if (guns.size() == 1 && ampLvl > 0) {
                    ImGui::TextUnformatted(amp::gunLabel(guns[0], ampLvl).c_str()); //one gun: the module is on it
                } else {
                    for (const std::string& g : guns) ImGui::TextUnformatted(g.c_str());
                    if (ampLvl == 5 && guns.size() > 1) ui::Note("AMP module fitted (which gun: not known)");
                    else if (ampLvl > 0 && guns.size() > 1) ui::Note("Amplification module fitted (which gun: not known)");
                }
                ui::Section("CARRYING");
                if (rows && !rows->empty()) {
                    long total = 0;
                    int unpriced = 0;
                    for (const ItemRow& r : *rows) {
                        long v = itemvalues::valueOf(r.name);
                        if (v < 0) unpriced++;
                        else total += v * r.count;
                    }
                    char tb[120];
                    if (unpriced) std::snprintf(tb, sizeof tb, "Value %ld  (%d item%s with no price)", total, unpriced, unpriced == 1 ? "" : "s");
                    else std::snprintf(tb, sizeof tb, "Value %ld", total);
                    ImGui::TextUnformatted(tb);
                    std::string lastCat;
                    for (const ItemRow& r : *rows) {
                        if (r.category != lastCat) { lastCat = r.category; ImGui::TextColored(ImColor(ui::GREY), "%s", r.category.c_str()); }
                        if (r.count > 1) ImGui::Text("  %s  x%d", r.name.c_str(), r.count); else ImGui::Text("  %s", r.name.c_str());
                    }
                } else if (live::invArrays.empty()) {
                    ui::Note("The inventory lists are not set up yet (see offsets.txt in the config folder).");
                } else {
                    ui::Note("Nothing readable: the game may not send this player's backpack to you.");
                }
            }
        }
        ImGui::End();
        if (!open) close.push_back(key);
    }
    for (const std::string& k : close) g_inspect.erase(std::remove(g_inspect.begin(), g_inspect.end(), k), g_inspect.end());
    ui::PopTheme();
}

#endif //KWINHELPER_PLAYERLIST_H
