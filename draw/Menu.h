#ifndef KWINHELPER_MENU_H
#define KWINHELPER_MENU_H
//The Insert menu: black and white, tabs on the left (ESP, Loot, Players, Radar, Settings).
//Settings and your own names are saved in ~/.config/kwin-helper/ (see settings.h and labels.h).
#include <atomic>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ESP.h"
#include "Overlay.h"
#include "PlayerList.h"
#include "UiKit.h"
#include "../utils/cue.h"
#include "../utils/hotkey.h"
#include "../utils/keytext.h"
#include "../utils/gameview.h"
#include "../utils/labels.h"
#include "../utils/marks.h"
#include "../utils/settings.h"

inline bool g_MenuOpen = false;
inline std::atomic<bool> g_MenuQuit{false}; //the menu's Quit button; the main loop ends the program
inline int g_MenuTab = 0; //which page is shown
inline const char* const MENU_TABS[] = {"ESP", "Loot", "Players", "Radar", "Settings"};
inline constexpr int MENU_TAB_COUNT = 5;

//Call once per frame before RenderBegin(). Flips the menu on Insert. The overlay is click-through
//normally; while the menu is open it takes the mouse so the menu can be clicked.
inline void MenuPollToggle() {
    if (!g_MenuToggle.exchange(false)) return;
    g_MenuOpen = !g_MenuOpen;
    if (g_SetInteractive) g_SetInteractive(g_MenuOpen); //the overlay takes the mouse while the menu is open
    if (!g_MenuOpen) {
        settings::clampAll();
        settings::save(); //labels.txt is only read; marks are saved the moment they change
    }
}

//The key presses the hotkey thread queued while the menu had the keyboard, as ImGui input. Call once per frame
//before RenderBegin(). The overlay never has keyboard focus, so typing comes from the keyboard device itself.
//The menu has the keyboard while a text box is active, and with "Keyboard controls the menu" on the whole time it is
//open: then arrows and Tab move the focus frame, Space presses, Enter types an exact slider value, Esc closes the menu.
inline void MenuFeedKeys() {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    if (g_MenuOpen) io.AddFocusEvent(true); //the overlay window is never "focused" for the desktop; ImGui must not care
    if (config::menuKeyboard) io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    else io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;

    const bool typing = io.WantTextInput; //a text box is active (decided last frame)
    const bool want = g_MenuOpen && (typing || config::menuKeyboard);
    if (want) g_GrabBeatMs = NowMs(); //set before the flag so the watchdog never sees a stale beat
    g_TextGrab = want;

    std::vector<KeyEv> evs;
    {
        std::lock_guard<std::mutex> lk(g_KeyMutex);
        evs.swap(g_KeyQueue);
    }
    static bool shift = false, ctrl = false, alt = false, caps = false;
    //Keys ImGui was told are down. When typing ends their key-ups are no longer queued, so they are let go here,
    //or ImGui would hold them forever. Caps Lock starts off each time: the desktop never sees the grabbed presses.
    static std::set<ImGuiKey> down;
    if (!want) {
        for (ImGuiKey k : down) io.AddKeyEvent(k, false);
        down.clear();
        shift = ctrl = alt = caps = false;
        return;
    }
    auto send = [&](ImGuiKey k, bool d) {
        if (d) down.insert(k); else down.erase(k);
        io.AddKeyEvent(k, d);
    };

    for (const KeyEv& ev : evs) {
        if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
            shift = ev.value != 0;
            send(ImGuiMod_Shift, shift);
            continue;
        }
        if (ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL) {
            ctrl = ev.value != 0;
            send(ImGuiMod_Ctrl, ctrl);
            continue;
        }
        if (ev.code == KEY_LEFTALT || ev.code == KEY_RIGHTALT) { alt = ev.value != 0; continue; }
        if (ev.code == KEY_CAPSLOCK) { if (ev.value == 1) caps = !caps; continue; }
        //Esc closes the menu like Insert, unless a text box, an open drop-down or a slider being moved takes it first
        if (ev.code == KEY_ESC && ev.value == 1 && !typing && !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
            g_MenuToggle = true;
            continue;
        }
        ImGuiKey k = ImGuiKey_None;
        switch (ev.code) {
            case KEY_BACKSPACE: k = ImGuiKey_Backspace; break;
            case KEY_DELETE: k = ImGuiKey_Delete; break;
            case KEY_ENTER: case KEY_KPENTER: k = ImGuiKey_Enter; break;
            case KEY_ESC: k = ImGuiKey_Escape; break;
            case KEY_LEFT: k = ImGuiKey_LeftArrow; break;
            case KEY_RIGHT: k = ImGuiKey_RightArrow; break;
            case KEY_UP: k = ImGuiKey_UpArrow; break;
            case KEY_DOWN: k = ImGuiKey_DownArrow; break;
            case KEY_TAB: k = ImGuiKey_Tab; break;
            case KEY_SPACE: k = ImGuiKey_Space; break;
            case KEY_PAGEUP: k = ImGuiKey_PageUp; break;
            case KEY_PAGEDOWN: k = ImGuiKey_PageDown; break;
            case KEY_HOME: k = ImGuiKey_Home; break;
            case KEY_END: k = ImGuiKey_End; break;
            case KEY_A: k = ImGuiKey_A; break; //with Ctrl: select all, copy, paste, cut, undo
            case KEY_C: k = ImGuiKey_C; break;
            case KEY_V: k = ImGuiKey_V; break;
            case KEY_X: k = ImGuiKey_X; break;
            case KEY_Z: k = ImGuiKey_Z; break;
            default: break;
        }
        if (k != ImGuiKey_None && ev.value != 2) send(k, ev.value != 0); //ImGui repeats held keys itself
        //One character per press, only into a text box, none while Ctrl or Alt is held. The keyboard's own repeat is not
        //used: it would double letters for a slow typist. Backspace, Enter, the arrows and so on give 0 from KeyToChar.
        if (typing && ev.value == 1 && !ctrl && !alt) {
            char c = KeyToChar(ev.code, shift, caps);
            if (c) io.AddInputCharacter((unsigned)c);
        }
    }
}

//A button for something that cannot be undone. The first click arms it: "Cancel" takes its place (same width) and
//the real action appears to its right. The action only counts 0.6 s after arming, so a double-click never reaches
//it, and the offer goes away by itself after 6 s. armedAt: a static the caller keeps, below 0 = not armed.
//The button and Cancel share one ID ("###arm"), so the keyboard focus frame stays on them when one replaces the other.
inline bool ConfirmButton(const char* label, const char* confirm, double* armedAt) {
    const double now = ImGui::GetTime();
    if (*armedAt >= 0 && now - *armedAt > 6.0) *armedAt = -1;
    ImGui::PushID(label);
    bool yes = false;
    if (*armedAt < 0) {
        if (ImGui::Button((std::string(label) + "###arm").c_str())) *armedAt = now;
    } else {
        const float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
        if (ImGui::Button("Cancel###arm", ImVec2(w, 0))) *armedAt = -1;
        ImGui::SameLine();
        if (ImGui::Button(confirm) && *armedAt >= 0 && now - *armedAt >= 0.6) {
            yes = true; *armedAt = -1;
            //the focus frame goes back to the restored button (same ID as Cancel###arm; ImHashStr restarts at "###")
            if (ImGui::IsItemFocused()) ImGui::SetFocusID(ImGui::GetID("###arm"), ImGui::GetCurrentWindow());
        }
    }
    ImGui::PopID();
    return yes;
}

//The readable setup in one go: text size 28, box and skeleton lines at least 4 px (the skeleton is never thinner than the box), full boxes, label cards, overlay and menu fully solid,
//hidden players at least 200 (Hidden player opacity). Saved at once.
inline void ApplyHighVisibility() {
    config::labelSize = 28.0f;
    config::boxThickness = std::max(config::boxThickness, 4.0f); //only ever thicker
    config::labelCards = true;
    config::boxStyle = 1; //full rectangle
    config::overlayOpacity = 1.0f;
    config::menuOpacity = 1.0f;
    config::hiddenAlpha = std::max(config::hiddenAlpha, 200.0); //only ever more solid
    settings::save();
}

//"Reset to high visibility": every setting back to its default, then the high visibility values. Show item values is kept.
inline void ResetToHighVisibility() {
    const bool values = config::showItemValues;
    settings::resetAll();
    config::showItemValues = values;
    ApplyHighVisibility();
}

//A large button with large text that applies ApplyHighVisibility()
inline void HighVisibilityButton() {
    ImGui::PushFont(OverlayFont(), 22.0f);
    if (ImGui::Button("High visibility", ImVec2(320, 46))) { ApplyHighVisibility(); Toast("High visibility on"); }
    ImGui::PopFont();
    ui::Note("One click: text size 28, box and skeleton lines at least 4 px, full boxes, label cards, overlay and menu fully solid, hidden players at least 200 (Hidden player opacity).");
}

//--- the pages
inline void PageEsp() {
    using namespace ui;
    HighVisibilityButton();
    Section("MASTER");
    Switch("ESP on", &config::espEnabled);
    Section("SHOW");
    ImGui::Columns(2, "##show", false);
    Switch("Players", &config::showPlayers); ImGui::NextColumn();
    Switch("Dead players", &config::showDeadPlayers); ImGui::NextColumn();
    Switch("Teammates", &config::showTeammates); ImGui::NextColumn();
    Switch("Edge arrows", &config::showOffscreen); ImGui::NextColumn();
    Switch("ARC", &config::showArc); ImGui::NextColumn();
    Switch("Dead ARC", &config::showDeadArc); ImGui::NextColumn();
    Switch("Turrets", &config::showTurrets); ImGui::NextColumn();
    Switch("ARC edge arrows", &config::showArcOffscreen); ImGui::NextColumn();
    Switch("Ground items", &config::showPickups); ImGui::NextColumn();
    Switch("Containers", &config::showContainers); ImGui::NextColumn();
    ImGui::Columns(1);
    Note("To show only guns, only ammo, only meds or other kinds of ground items and containers, use the Loot tab.");
    Section("NAMES");
    ImGui::Columns(2, "##names", false);
    Switch("Player names", &config::showPlayerNames); ImGui::NextColumn();
    Switch("Player guns", &config::showPlayerGuns); ImGui::NextColumn();
    Switch("ARC names", &config::showArcNames); ImGui::NextColumn();
    Switch("Container names", &config::showContainerNames); ImGui::NextColumn();
    Switch("Item names", &config::showItemNames); ImGui::NextColumn();
    Switch("Label cards", &config::labelCards); ImGui::NextColumn();
    ImGui::Columns(1);
    SliderD("Loot name range", &config::nameMaxLootDist, 3, 150, "%.0f m");
    Section("BOXES");
    Choice("Box style", &config::boxStyle, {"Corners", "Full", "Sides", "Skeleton only"});
    Choice("Box colour", &config::boxColor, {"Red", "White", "Yellow", "Blue", "Green", "Violet"});
    Switch("Monochrome (white and grey)", &config::monochrome);
    Slider("Line thickness", &config::boxThickness, 1.0f, 10.0f,"%.1f px");
    Switch("Health and shield bars", &config::showBars);
    if (config::showBars) {
        Choice("Bar style", &config::barStyle, {"Health + shield", "Health only", "One bar for both"});
        Switch("Numbers on the bars", &config::barNumbers);
    }
    Switch("Which way they face", &config::showFacing);
    Note("An arrow in each player's box (pointing down: facing you) and a line on each radar dot (players and ARC) show which way they face.");
    Section("YOUR HEALTH");
    Switch("Big health and shield readout", &config::showSelfHealth);
    Note("Your own health and shield in big text at the bottom left, and a red LOW HEALTH card below the crosshair under 35.");
    Section("SKELETON");
    Switch("Skeleton on players", &config::showSkeleton);
    Choice("Head marker", &config::headMarker, {"Ring", "Filled"});
    Choice("Skeleton colour", &config::skeletonColor, {"Same as box", "White", "Yellow"});
    Note("Lines grow thicker as players come closer; far away the figure becomes a few thick lines. A dashed ring for a "
         "head means the game is not animating that player right now, so the pose is an estimate. Box style Skeleton "
         "only leaves the box out where a skeleton is drawn.");
    SliderD("Skeleton range", &config::skeletonMaxDist, 20, 300, "%.0f m");
    Switch("Dim players behind cover", &config::dimHidden);
    if (config::dimHidden) SliderD("Hidden player opacity", &config::hiddenAlpha, 40, 255, "%.0f");
    Note("Dimming makes the box and skeleton of a player the game is not drawing right now (behind cover) fainter.");
    Section("DISTANCE");
    SliderD("Players range", &config::maxPlayerDist, 50, 800, "%.0f m");
    SliderD("ARC range", &config::maxArcDist, 20, 500, "%.0f m");
    SliderD("Loot range", &config::maxLootDist, 5, 150, "%.0f m");
    Section("CLOSE ENEMY WARNING");
    Switch("Big warning for a close enemy", &config::closeWarning);
    if (config::closeWarning) SliderD("Warning card range", &config::warnDist, 5, 100, "%.0f m");
    Note("The spoken warning has its own range: Settings tab, Spoken warning range.");
}

//The "Show" drop-down over a block of switches: 0 all on, 1 + k only switch k on, n + 1 anything else (Mixed). Worked out
//from the switches every frame, so it keeps no state of its own and always says what is on.
inline int SwitchPreset(bool* const* s, int n) {
    int on = 0, last = -1;
    for (int k = 0; k < n; k++) if (*s[k]) { on++; last = k; }
    return on == n ? 0 : on == 1 ? 1 + last : n + 1;
}
//A line of that drop-down picked: all on, or only one on. Mixed leaves the switches as they are.
inline void SetSwitchPreset(bool* const* s, int n, int p) {
    if (p < 0 || p > n) return;
    for (int k = 0; k < n; k++) *s[k] = p == 0 || k == p - 1;
}

//A "Show" drop-down (all, only one kind, mixed), then one switch per kind in two columns. name(k): the kind's name.
//suffix: added to each switch's ID, so two blocks on one page can both have an "Other".
inline void KindSwitches(const char* label, const char* all, bool* const* s, int n, const char* (*name)(int), const char* cols,
                         const char* suffix) {
    std::vector<std::string> text{all};
    for (int k = 0; k < n; k++) {
        std::string t = name(k);
        if (t.size() > 1 && std::islower((unsigned char)t[1])) t[0] = (char)std::tolower((unsigned char)t[0]); //"ARC" stays
        text.push_back("Only " + t);
    }
    text.push_back("Mixed (switches below)");
    std::vector<const char*> items;
    for (const std::string& t : text) items.push_back(t.c_str());
    double pick = SwitchPreset(s, n);
    if (ui::Choice(label, &pick, items.data(), (int)items.size())) SetSwitchPreset(s, n, (int)pick);
    ImGui::Columns(2, cols, false);
    for (int k = 0; k < n; k++) { ui::Switch((std::string(name(k)) + suffix).c_str(), s[k]); ImGui::NextColumn(); }
    ImGui::Columns(1);
}

inline void PageLoot() {
    using namespace ui;
    Section("GROUND ITEMS");
    Switch("Ground items", &config::showPickups);
    KindSwitches("Show##items", "All kinds", LootSwitches(), labels::KIND_COUNT,
                 [](int k) { return labels::kindName((labels::Kind)k); }, "##kinds", "");
    Note("Items whose name is not read yet count as Other.");
    Switch("Colour items by kind", &config::colorLootKinds);
    Note("Guns magenta, ammo white, meds green, shields blue; everything else stays cyan. Monochrome keeps them all grey.");
    Section("VALUABLE ITEMS");
    Switch("Highlight valuable items", &config::highlightValuable);
    SliderD("Valuable from", &config::valuableFrom, 1000, 100000, "%.0f");
    Note("Ground items with at least this price in ~/.config/kwin-helper/values.txt are drawn gold (white in monochrome) and "
         "half as big again, always with their name and price (\"14k\"), anywhere within the ESP tab's Loot range.");
    Section("CONTAINERS");
    Switch("Containers", &config::showContainers);
    KindSwitches("Show##containers", "All groups", ContainerSwitches(), labels::GROUP_COUNT,
                 [](int g) { return labels::groupName((labels::Group)g); }, "##groups", "##box");
    Note("Containers whose name is not read yet count as Other.");
}

inline void PagePlayers() {
    using namespace ui;
    Section("LIST");
    Choice("Player list", &config::listMode, {"Off", "Always", "Only with menu"});
    SliderD("List rows", &config::listRows, 3, 16, "%.0f");
    Switch("Show item values", &config::showItemValues);
    Note("Sell prices and a total in the inventory windows. Prices come from ~/.config/kwin-helper/values.txt.");
    Section("TEAMMATES");
    Switch("Hide players with unknown team", &config::hideUnknownTeams);
    static double clearArmedAt = -1;
    if (ConfirmButton("Clear teammate marks", "Yes, clear all marks", &clearArmedAt)) { marks::clear(); marks::save(); Toast("Teammate marks cleared"); }
    ImGui::SameLine();
    Note("F6 marks the player at your crosshair, F7 pressed twice clears all.");
    DrawSavedMarks();
    Section("PLAYERS IN VIEW");
    DrawPlayerRows(16);
}

inline void PageRadar() {
    using namespace ui;
    Switch("Radar", &config::enableRadar);
    if (!config::enableRadar) return;
    float range = config::radarRadius * config::scale / 100.0f; //metres from the centre to the edge
    if (Slider("Size", &config::radarRadius, 50.0f, 300.0f, "%.0f px")) config::scale = range * 100.0f / config::radarRadius; //keep the range
    if (Slider("Range", &range, 50.0f, 500.0f, "%.0f m")) config::scale = range * 100.0f / config::radarRadius;
    Section("POSITION");
    const float R = config::radarRadius;
    if (ImGui::Button("Top left")) { config::radarCenterX = R + 40; config::radarCenterY = R + 50; }
    ImGui::SameLine();
    //panel bottom (centre + R + 14) 6 px above the HP/SH readout's card, which grows with Text size
    if (ImGui::Button("Left, above health")) { config::radarCenterX = R + 40; config::radarCenterY = SelfHealthRect().y0 - 6 - R - 14; }
    ImGui::SameLine();
    if (ImGui::Button("Top right")) { config::radarCenterX = (float)config::SCREEN_W - R - 40; config::radarCenterY = R + 50; }
    Slider("Left / right", &config::radarCenterX, R, std::max(R + 1, (float)config::SCREEN_W - R), "%.0f");
    Slider("Up / down", &config::radarCenterY, R, std::max(R + 1, (float)config::SCREEN_H - R), "%.0f");
}

//Common game picture sizes for a stretched resolution, in the order of the "Game picture size" list (then "Custom")
inline constexpr int GAME_SIZES[][2] = {{1024, 768}, {1280, 960}, {1440, 1080}, {1600, 1200}, {1920, 1440},
                                        {1280, 1024}, {1680, 1050}, {1728, 1080}, {1920, 1080}};
inline constexpr int GAME_SIZE_CUSTOM = 9;

//One number with big step buttons: "Game width  1440    -100 -10 -1 +1 +10 +100", kept within lo..hi
inline void StepRow(const char* label, double* v, double lo, double hi) {
    static const int steps[] = {-100, -10, -1, 1, 10, 100};
    ImGui::PushID(label);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 10)); //tall buttons, and the text stays level with them
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s  %.0f", label, *v);
    for (int i = 0; i < 6; i++) {
        ImGui::SameLine(i == 0 ? ui::LABEL_W : 0.0f);
        char t[8];
        std::snprintf(t, sizeof t, "%+d", steps[i]);
        if (ImGui::Button(t, ImVec2(56, 0))) *v = std::clamp(*v + steps[i], lo, hi);
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
}

inline constexpr ImU32 GUIDE_YELLOW = IM_COL32(255, 230, 0, 255);

//A thick yellow frame round the game's picture and a big cross on the middle of the projection (Shift X / Y included),
//both edged in black so they show on any background. Worked out from the projection alone, not from a camera View:
//in the lobby the camera has no FOV and a View would put the middle at 0,0. Under the menu, so it covers no control.
inline void DrawCalibrationGuide() {
    const Projection p = MakeProjection();
    const float x = (float)p.dx, y = (float)p.dy;
    const float w = (float)(p.dw > 0 ? p.dw : p.width), h = (float)(p.dh > 0 ? p.dh : p.height);
    const float cx = x + w / 2 + (float)p.offX, cy = y + h / 2 + (float)p.offY; //as View works out cx, cy
    const ImU32 black = IM_COL32(0, 0, 0, 255);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float in = 5; //the black edge's outer side lies on the picture's edge, so none of it falls off screen
    dl->AddRect(ImVec2(x + in, y + in), ImVec2(x + w - in, y + h - in), black, 0.0f, 0, 10.0f);
    dl->AddRect(ImVec2(x + in, y + in), ImVec2(x + w - in, y + h - in), GUIDE_YELLOW, 0.0f, 0, 6.0f);
    const float gap = 10, len = 70; //the gap leaves the game's own crosshair visible in the middle
    const ImVec2 dirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const ImVec2& d : dirs) dl->AddLine(ImVec2(cx + d.x * (gap - 2), cy + d.y * (gap - 2)), ImVec2(cx + d.x * (len + 2), cy + d.y * (len + 2)), black, 10.0f);
    for (const ImVec2& d : dirs) dl->AddLine(ImVec2(cx + d.x * gap, cy + d.y * gap), ImVec2(cx + d.x * len, cy + d.y * len), GUIDE_YELLOW, 6.0f);
}

//The game view part of the Settings tab (stretched resolution, gamescope, calibration)
inline void DrawGameViewSettings() {
    using namespace ui;
    const float top = ImGui::GetCursorScreenPos().y;
    Section("GAME VIEW: STRETCHED RESOLUTION / GAMESCOPE");
    Switch("Detect automatically", &config::gameViewAuto);
    Note(g_gameView.how.c_str());
    Note("While this part is on screen, a yellow frame shows where the overlay thinks the game's picture is and a yellow cross shows its middle. "
         "The frame should sit on the picture's edges (if not, fix the game view here). The cross should sit on the game's crosshair (if not, fix Shift X / Shift Y).");
    if (!config::gameViewAuto) {
        static bool custom = false; //"Custom" was picked: keep the step buttons even when the size matches a listed one
        int idx = GAME_SIZE_CUSTOM;
        if (!custom)
            for (int i = 0; i < GAME_SIZE_CUSTOM; i++)
                if (GAME_SIZES[i][0] == (int)config::gameRenderW && GAME_SIZES[i][1] == (int)config::gameRenderH) idx = i;
        double pick = idx;
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX)); //the open list shows all ten, no scrolling
        if (Choice("Game picture size", &pick, {"1024 x 768 (4:3)", "1280 x 960 (4:3)", "1440 x 1080 (4:3)", "1600 x 1200 (4:3)",
                                                "1920 x 1440 (4:3)", "1280 x 1024 (5:4)", "1680 x 1050 (16:10)", "1728 x 1080 (16:10)",
                                                "1920 x 1080 (16:9)", "Custom"})) {
            idx = (int)pick;
            custom = idx == GAME_SIZE_CUSTOM;
            if (!custom) { config::gameRenderW = GAME_SIZES[idx][0]; config::gameRenderH = GAME_SIZES[idx][1]; }
        }
        if (idx == GAME_SIZE_CUSTOM) {
            StepRow("Game width", &config::gameRenderW, 320, 8192); //the same limits settings.h keeps
            StepRow("Game height", &config::gameRenderH, 240, 8192);
        }
        Switch("Stretch to fill the screen", &config::gameStretch);
    }
    Section("CALIBRATION");
    Note("If markers slide away from targets near the screen edge, flip the FOV side first, then adjust Zoom.");
    Switch("FOV is horizontal", &config::fovIsHorizontal);
    Slider("Zoom", &config::fovScale, 0.5f, 2.0f, "%.3f");
    Slider("Shift X", &config::centerOffsetX, -300.0f, 300.0f, "%.0f px");
    Slider("Shift Y", &config::centerOffsetY, -300.0f, 300.0f, "%.0f px");
    Switch("Use camera roll", &config::useCameraRoll);
    if (ImGui::Button("Reset calibration")) {
        config::fovIsHorizontal = false;
        config::fovScale = 1.0f;
        config::centerOffsetX = config::centerOffsetY = 0.0f;
        config::useCameraRoll = true;
    }
    //only while some of this part is scrolled into view on the page
    const float left = ImGui::GetWindowPos().x;
    if (ImGui::IsRectVisible(ImVec2(left, top), ImVec2(left + ImGui::GetWindowWidth(), ImGui::GetCursorScreenPos().y))) DrawCalibrationGuide();
}

inline void PageSettings() {
    using namespace ui;
    Section("LOOK");
    HighVisibilityButton();
    Slider("Overlay opacity", &config::overlayOpacity, 0.2f, 1.0f, "%.2f");
    Slider("Menu opacity", &config::menuOpacity, 0.5f, 1.0f, "%.2f");
    Slider("Text size", &config::labelSize, 10.0f, 28.0f, "%.0f px");
    Section("SPOKEN CUES");
    if (Switch("Say hotkey results and near enemies out loud", &config::soundCues) && config::soundCues) cue::say("Spoken cues on");
    if (config::soundCues) {
        SliderD("Spoken warning range", &config::cueRange, 10, 150, "%.0f m");
        if (cue::missing) ImGui::TextWrapped("No sound: spd-say was not found. Install speech-dispatcher to hear the cues.");
        else if (cue::failed) ImGui::TextWrapped("No sound: spd-say could not reach speech-dispatcher. Run 'spd-say test' in a terminal to see why.");
    }
    Note("Says the result of F6, F7, F8 and F10, and warns when an enemy player first comes within range (at most one warning every 2 s). Enemy warnings are quiet while stream hide is on. "
         "The big on-screen card has its own range: ESP tab, Warning card range.");
    Section("SPEED");
    SliderD("Update rate", &config::trackerHz, 30, 240, "%.0f Hz");
    Note("Higher follows moving players more closely and uses more CPU.");
    DrawGameViewSettings();
    Section("OTHER");
    Switch("Status card", &config::showDebugInfo);
    Switch("Hotkeys (F6, F7, F10)", &config::hotkeysEnabled);
    Switch("Keyboard controls the menu", &config::menuKeyboard);
    Note("While the menu is open the game gets no keys. Arrows and Tab move a thick white frame, Space presses or "
         "switches, Esc or Insert closes the menu. Sliders: Enter, Tab or Ctrl+click lets you type an exact number "
         "(Enter when done), or Space and then Left / Right.");
    Switch("Stream hide (F8)", &config::streamHide);
    Note("Stream hide draws nothing, so recordings and streams show no overlay. F8 works even with Hotkeys off, but not while Shift, Ctrl, Alt or Meta is held (let go of Shift first). While it is on the menu cannot be opened: press F8 first.");
    Section("OVERLAY WINDOW");
    if (g_OverlayWhere) ImGui::TextWrapped("%s", g_OverlayWhere().c_str());
    if (!g_OverlayNote.empty()) ImGui::TextWrapped("%s", g_OverlayNote.c_str()); //the setup problem shown at the start
    Note("Pick the monitor with: bash start.sh DP-1 (names: kscreen-doctor -o)");
    Section("FILES");
    Note("Settings, names, offsets and marks are in ~/.config/kwin-helper/");
    static double resetArmedAt = -1;
    if (ConfirmButton("Reset all settings", "Yes, reset everything", &resetArmedAt)) { settings::resetAll(); ImGui::SetWindowPos("kwin-helper", ImVec2(120, 110)); Toast("Settings reset"); }
    static double resetHvArmedAt = -1;
    if (ConfirmButton("Reset to high visibility", "Yes, reset to high visibility", &resetHvArmedAt)) {
        ResetToHighVisibility(); ImGui::SetWindowPos("kwin-helper", ImVec2(120, 110)); Toast("Settings reset to high visibility");
    }
    Section("QUIT");
    if (ImGui::Button("Quit overlay")) g_MenuQuit = true;
}

//Pass to RenderEnd(). Draws nothing while the menu is closed.
inline void DrawMenu() {
    if (!g_MenuOpen) return;
    g_MenuTab = std::min(MENU_TAB_COUNT - 1, std::max(0, g_MenuTab));

    ui::PushTheme(std::min(1.0f, std::max(0.5f, config::menuOpacity)));
    ImGui::SetNextWindowPos(ImVec2(120, 110), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(820, 640), ImGuiCond_Always);
    //NoBringToFrontOnFocus: a click on the menu must not hide the inventory windows behind it
    ImGui::Begin("kwin-helper", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus);
    {   //keep the whole menu on screen (a drag or a position saved on a bigger monitor can leave only a sliver showing)
        const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
        const ImVec2 c(std::clamp(p.x, 0.0f, std::max(0.0f, (float)config::SCREEN_W - s.x)),
                       std::clamp(p.y, 0.0f, std::max(0.0f, (float)config::SCREEN_H - s.y)));
        if (c.x != p.x || c.y != p.y) ImGui::SetWindowPos(c);
    }

    ImGui::PushFont(OverlayFont(), 20.0f);
    ImGui::TextUnformatted("Overlay");
    ImGui::PopFont();
    const char* closeLabel = "Close menu (Insert)"; //a mouse way out too: Insert and SIGUSR1 need the hotkey thread
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::SameLine(ImGui::GetWindowWidth() - st.WindowPadding.x - ImGui::CalcTextSize(closeLabel).x - st.FramePadding.x * 2);
    if (ImGui::Button(closeLabel)) g_MenuToggle = true; //MenuPollToggle closes and saves exactly as Insert does
    ImGui::Separator();

    const float footer = 30.0f;
    //NavFlattened: the arrows and Tab move straight between the tabs and the page
    ImGui::BeginChild("##nav", ImVec2(150, -footer), ImGuiChildFlags_NavFlattened);
    for (int i = 0; i < MENU_TAB_COUNT; i++)
        if (ui::Tab(MENU_TABS[i], g_MenuTab == i, 140)) g_MenuTab = i;
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##page", ImVec2(0, -footer), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    switch (g_MenuTab) {
        case 0: PageEsp(); break;
        case 1: PageLoot(); break;
        case 2: PagePlayers(); break;
        case 3: PageRadar(); break;
        default: PageSettings(); break;
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextColored(ImColor(ui::GREY), "F6 mark teammate    F7 twice: clear marks    F8 stream hide    F10 twice: quit");
    ImGui::End();
    ui::PopTheme();
    ui::DrawNavFrame(); //last: the inventory windows and the player list are drawn before the menu
}

//Everything the overlay shows on top of the ESP: the player list, inventory windows, the menu. Pass to RenderEnd().
inline void DrawUi() {
    DrawPlayerListWindow(g_MenuOpen);
    if (g_MenuOpen) DrawInspectWindows();
    DrawMenu();
}

#endif //KWINHELPER_MENU_H
