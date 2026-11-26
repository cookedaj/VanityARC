#pragma once
//The player list (at the bottom of the screen and in the menu's Players tab) and the inventory windows that open
//when you click a name. Everything here only displays what the tracker read.
#ifndef KWINHELPER_PLAYERLIST_H
#define KWINHELPER_PLAYERLIST_H

#include <algorithm>
#include <cctype>
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
    else Toast("At most 6 inventory windows: close one first");
}

//A window opened while the account was unknown is keyed "@<actor>": switch it to the account once that is read
inline void RemapInspectKeys() {
    if (!g_ui.ents) return;
    for (const RenderEntity& e : *g_ui.ents) {
        if (e.type != Object::PLAYER || e.account.empty()) continue;
        char b[32];
        std::snprintf(b, sizeof b, "@%llx", (unsigned long long)e.actor);
        auto it = std::find(g_inspect.begin(), g_inspect.end(), std::string(b));
        if (it == g_inspect.end()) continue;
        if (IsInspected(e.account)) g_inspect.erase(it); //already open under the account
        else *it = e.account;
    }
}

//The actors whose inventory the tracker should read
inline std::vector<ptr> InspectedActors() {
    std::vector<ptr> a;
    if (!g_ui.ents) return a;
    RemapInspectKeys();
    for (const RenderEntity& e : *g_ui.ents) if (e.type == Object::PLAYER && IsInspected(PlayerKey(e))) a.push_back(e.actor);
    return a;
}

//One row per player: name, distance, health, shield, team, guns. Click a name for the inventory, the TEAM button marks a
//teammate. Clickable only while the menu is open: DrawPlayerListWindow adds NoInputs otherwise.
inline constexpr float LIST_ROW_W = 780; //a row's width at 15 px text
inline void DrawPlayerRows(int maxRows) {
    const auto players = PlayersByDistance();
    if (players.empty()) { ui::Note(g_ui.live ? "No players in view." : "No live data from the game."); return; }
    using ui::S; //column positions for 15 px text, grown with the menu text size
    const float xDist = S(190), xHp = S(245), xSh = S(295), xSeen = S(360), xTeam = S(415), xGun = S(495), rowW = S(LIST_ROW_W);
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
        //An outline, not a fill (a lit fill would wash out the grey and dim names): white while its inventory is open,
        //grey under the mouse. Its left side sits before the name; where the window clips it, the other three sides show.
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32_BLACK_TRANS);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32_BLACK_TRANS);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32_BLACK_TRANS);
        if (ImGui::Selectable("##row", IsInspected(key), 0, ImVec2(rowW, S(20)))) ToggleInspect(key);
        ImGui::PopStyleColor(3);
        const bool rowHov = ImGui::IsItemHovered();
        if (rowHov || IsInspected(key))
            ImGui::GetWindowDrawList()->AddRect(ImVec2(ImGui::GetWindowPos().x + S(2), ImGui::GetItemRectMin().y), ImGui::GetItemRectMax(),
                                                IsInspected(key) ? ui::WHITE : ui::GREY, 0.0f, 0, S(2));
        if (rowHov) ImGui::SetTooltip("Click for the inventory");
        ImGui::SameLine(S(6));
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
    //the menu text size, but never so big that a row (and the window's padding) is wider than the screen: the list is
    //centred, so both ends would be cut off. ponytail: big text is capped on a narrow screen; rows on two lines would keep it.
    const float fit = ui::BASE_TEXT * ((float)config::SCREEN_W - 40) / (LIST_ROW_W + 28);
    ui::PushTheme(std::min(0.9f, config::menuOpacity), std::min(MenuTextSize(), std::max(ui::BASE_TEXT, fit)));
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
    if (config::showItemValues) itemvalues::reloadIfChanged(); //a price added to values.txt shows within 2 s, no restart
    RemapInspectKeys();
    ui::PushTheme(std::min(0.96f, config::menuOpacity + 0.04f), MenuTextSize());
    using ui::S;
    std::vector<std::string> close;
    int n = 0;
    for (const std::string& key : g_inspect) {
        const RenderEntity* e = nullptr;
        for (const RenderEntity& x : *g_ui.ents) if (x.type == Object::PLAYER && PlayerKey(x) == key) { e = &x; break; }
        const std::string who = e && !e->label.empty() ? e->label : (e && !e->account.empty() ? e->account.substr(0, 8) : "Player");
        const std::string title = who + "  -  inventory###inv" + key;
        //360 x 420 at 15 px text, beside the menu (820 wide at x 120 at 15 px text), not on its player list; kept on the screen
        ImGui::SetNextWindowSize(ImVec2(std::min(S(360), (float)config::SCREEN_W), std::min(S(420), (float)config::SCREEN_H)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(std::max(0.0f, std::min(120.0f + S(840) + 36.0f * n, (float)config::SCREEN_W - S(380))),
                                       std::max(0.0f, std::min(110.0f + 36.0f * n, (float)config::SCREEN_H - S(430)))), ImGuiCond_FirstUseEver);
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
                if (e->hasShield && e->shieldMax > 0) { std::snprintf(b, sizeof b, "   Shield %.0f / %.0f", e->shield, e->shieldMax); line += b; }
                ImGui::TextUnformatted(line.c_str());
                const std::vector<ItemRow>* rows = nullptr;
                if (g_ui.inv) { auto it = g_ui.inv->find(e->actor); if (it != g_ui.inv->end()) rows = &it->second; }
                ui::Section("EQUIPPED"); //the guns as the player label has them, with their level ("Stitcher 4", "AMP Il Toro")
                if (e->sub.empty()) ui::Note("No guns read.");
                for (size_t start = 0; start < e->sub.size();) {
                    size_t end = e->sub.find(" + ", start);
                    if (end == std::string::npos) end = e->sub.size();
                    ImGui::TextUnformatted(e->sub.substr(start, end - start).c_str());
                    start = end + 3;
                }
                ui::Section("CARRYING");
                if (rows && !rows->empty()) {
                    //values.txt may name the internal key or the display name
                    auto price = [](const ItemRow& r) { long v = itemvalues::valueOf(r.key); return v >= 0 ? v : itemvalues::valueOf(r.name); };
                    const bool values = config::showItemValues; //off (the default): no prices, no total, no note
                    const bool havePrices = values && itemvalues::count() > 0;
                    auto slotsOnly = [](const std::string& cat) { //stack sizes not read for this list (no qty in offsets.txt): counts are slots
                        for (const live::InvArray& a : live::invArrays) if (a.category == cat && a.qty < 0) return true;
                        return false;
                    };
                    if (values && !havePrices) ui::Note("No prices yet: add lines like 'Snap Hook | 14000' to ~/.config/kwin-helper/values.txt");
                    else if (havePrices) {
                        long total = 0;
                        int unpriced = 0;
                        bool atLeast = false;
                        for (const ItemRow& r : *rows) {
                            long v = price(r);
                            if (v < 0) unpriced++;
                            else { total += v * r.count; atLeast |= slotsOnly(r.category); }
                        }
                        char tb[120];
                        const char* lower = atLeast ? "at least " : ""; //slot counts: stacks are counted as one
                        if (unpriced) std::snprintf(tb, sizeof tb, "Value at least %ld  (%d with no price)", total, unpriced);
                        else std::snprintf(tb, sizeof tb, "Value %s%ld", lower, total);
                        ImGui::TextUnformatted(tb);
                    }
                    std::string lastCat;
                    for (const ItemRow& r : *rows) {
                        if (r.category != lastCat) { lastCat = r.category; ImGui::TextColored(ImColor(ui::GREY), "%s", r.category.c_str()); }
                        const char* np = havePrices && price(r) < 0 ? "  (no price)" : "";
                        if (r.count > 1) ImGui::Text(slotsOnly(r.category) ? "  %s  %d slots%s" : "  %s  x%d%s", r.name.c_str(), r.count, np); else ImGui::Text("  %s%s", r.name.c_str(), np);
                    }
                } else if (!rows) {
                    ui::Note("Reading inventory..."); //the tracker has not read this player yet
                } else {
                    ui::Note(e->sub.empty() ? "Nothing read. The game may not send this, or a game update moved it."
                                            : "Nothing besides their guns: only what the game sends about this player can be read.");
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
