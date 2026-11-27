#ifndef KWINHELPER_MENU_H
#define KWINHELPER_MENU_H
//The Insert menu: tabs on the left (ESP, Loot, Radar, Settings). Every page is a list of headed groups (ui::Section),
//each control one of UiKit.h's widgets, so the look (colours, rounding, padding) comes from UiKit.h alone.
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
#include "../utils/identity.h"   // APP_ID: the ImGui window id, one source with the "kwin-helper" cover
#include "../utils/keytext.h"
#include "../utils/gameview.h"
#include "../utils/labels.h"
#include "../utils/marks.h"
#include "../utils/settings.h"

inline bool g_MenuOpen = false;
inline std::atomic<bool> g_MenuQuit{false}; //the menu's Quit button; the main loop ends the program
inline int g_MenuTab = 0; //which page is shown
inline const char* const MENU_TABS[] = {"ESP", "Loot", "Radar", "Settings"};
inline constexpr int MENU_TAB_COUNT = 4;
inline constexpr float MENU_W = 820, MENU_H = 640; //the menu's size at 15 px menu text

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

//How wide ImGui::Button(text) is, for ui::SameLineIfFits
inline float ButtonW(const char* text) { return ImGui::CalcTextSize(text).x + ImGui::GetStyle().FramePadding.x * 2; }

//A heading inside a section: white words with a little room above, no rule (the section's heading has that)
inline void SubHeading(const char* text) {
    ImGui::Dummy(ImVec2(0, ui::S(4)));
    ImGui::TextUnformatted(text);
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
        const float x0 = ImGui::GetCursorPosX();
        if (ImGui::Button("Cancel###arm", ImVec2(ButtonW(label), 0))) *armedAt = -1;
        ui::SameLineIfFits(ButtonW(confirm), x0);
        if (ImGui::Button(confirm) && *armedAt >= 0 && now - *armedAt >= 0.6) {
            yes = true; *armedAt = -1;
            //the focus frame goes back to the restored button (same ID as Cancel###arm; ImHashStr restarts at "###")
            if (ImGui::IsItemFocused()) ImGui::SetFocusID(ImGui::GetID("###arm"), ImGui::GetCurrentWindow());
        }
    }
    ImGui::PopID();
    return yes;
}

//The readable setup in one go: text size at least 28, menu text size at least 24, Full-size small text, box and skeleton
//lines at least 4 px (the skeleton is never thinner than the box), full boxes, label cards, overlay and menu fully solid,
//hidden players at least 200 (Hidden player opacity). Sizes only ever grow: bigger values the user picked stay. Saved
//at once.
inline void ApplyHighVisibility() {
    config::labelSize = std::max(config::labelSize, 28.0f);
    config::menuTextSize = std::max(config::menuTextSize, 24.0f);
    config::fullSizeText = true; //distances, guns and marker labels in the full text size
    config::boxThickness = std::max(config::boxThickness, 4.0f); //only ever thicker
    config::labelCards = true;
    config::boxStyle = 1; //full rectangle
    config::showBoxes = true; //"full boxes" also when Boxes on players was off (as it always did for Box style Skeleton only)
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
    ImGui::PushFont(OverlayFont(), ui::S(22));
    if (ImGui::Button("High visibility", ImVec2(ui::S(320), ui::S(46)))) { ApplyHighVisibility(); Toast("High visibility on"); }
    ImGui::PopFont();
    ui::Note("Raises text to 28, menu text to 24, lines to 4 px; Full-size small text on; all solid.");
}

//Short switches side by side in two aligned columns, filled row by row. id: the grid's own ID on its page.
struct SwitchItem { const char* label; bool* v; };
inline void SwitchGrid(const char* id, std::initializer_list<SwitchItem> items) {
    ImGui::Columns(2, id, false);
    for (const SwitchItem& s : items) { ui::Switch(s.label, s.v); ImGui::NextColumn(); }
    ImGui::Columns(1);
}

//A colour drop-down for a kind of marker other than enemies: line 0 is the colour it always had (named in own), the
//rest are Box colour's without red, which stays the enemies' (config.h: arcColor, containerColor, itemColor)
inline void MarkerColour(const char* label, double* v, const char* own) {
    ui::Choice(label, v, {own, "White", "Yellow", "Blue", "Green", "Violet"});
}

//--- the pages. Each is a list of headed groups: what is shown first, then how it looks, then ranges and warnings.
inline void PageEsp() {
    using namespace ui;
    Section("QUICK SETUP");
    HighVisibilityButton();
    Section("MASTER");
    Switch("ESP on", &config::espEnabled);
    Section("PLAYERS");
    SwitchGrid("##players", {{"Players", &config::showPlayers}, {"Teammates", &config::showTeammates},
                             {"Dead players", &config::showDeadPlayers}, {"Edge arrows", &config::showOffscreen}});
    Section("ARC");
    SwitchGrid("##arc", {{"ARC", &config::showArc}, {"Turrets", &config::showTurrets},
                         {"Dead ARC", &config::showDeadArc}, {"ARC edge arrows", &config::showArcOffscreen}});
    MarkerColour("ARC colour", &config::arcColor, "Orange (as before)");
    Note("Ground items and containers have their own switches on the Loot tab.");
    Section("NAMES");
    SwitchGrid("##names", {{"Player names", &config::showPlayerNames}, {"Player guns", &config::showPlayerGuns},
                           {"ARC names", &config::showArcNames}, {"Container names", &config::showContainerNames},
                           {"Item names", &config::showItemNames}, {"Label cards", &config::labelCards}});
    SliderD("Loot name range", &config::nameMaxLootDist, 3, 150, "%.0f m");
    Section("BOXES");
    Switch("Boxes on players", &config::showBoxes);
    Note("Off: no box where a skeleton is drawn; a player without one keeps his box.");
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
    Note("An arrow in each box (down: facing you) and a line on each radar dot show the facing.");
    Section("SKELETON");
    Switch("Skeleton on players", &config::showSkeletons);
    Choice("Head marker", &config::headMarker, {"Ring", "Filled"});
    Choice("Skeleton colour", &config::skeletonColor, {"Same as box", "White", "Yellow"});
    SliderD("Skeleton range", &config::skeletonMaxDist, 20, 300, "%.0f m");
    Note("Lines thicken up close; a dashed head ring means the pose is only an estimate.");
    Section("PLAYERS BEHIND COVER");
    Switch("Dim players behind cover", &config::dimHidden);
    if (config::dimHidden) SliderD("Hidden player opacity", &config::hiddenAlpha, 40, 255, "%.0f");
    Note("Draws the box and skeleton of a player behind cover fainter.");
    Section("DISTANCE");
    SliderD("Players range", &config::maxPlayerDist, 50, 800, "%.0f m");
    SliderD("ARC range", &config::maxArcDist, 20, 500, "%.0f m");
    SliderD("ARC arrow range", &config::arcArrowDist, 10, 150, "%.0f m");
    SliderD("Loot range", &config::maxLootDist, 5, 150, "%.0f m");
    Note("ARC arrow range: an unseen live ARC this near gets an edge arrow, up to ARC range.");
    Section("YOUR HEALTH");
    Switch("Big health and shield readout", &config::showSelfHealth);
    SliderD("Low health from", &config::lowHealthFrom, 10, 90, "%.0f");
    Note("Big health and shield at the bottom left; a red LOW HEALTH card under Low health from.");
    Section("CLOSE ENEMY WARNING");
    Switch("Big warning for a close enemy", &config::closeWarning);
    SliderD("Warning card range", &config::warnDist, 5, 100, "%.0f m"); //shown with the card off too: it also sets the spoken "Enemy close" and the bigger edge arrows
    Note("Points at the nearest enemy; the range also sets bigger edge arrows and \"Enemy close\".");
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
    Section("SHOW");
    Switch("Ground items", &config::showGroundItems);
    Switch("Containers", &config::showContainers);
    Switch("Show item values", &config::showItemValues);
    Note("Show item values adds prices from values.txt and a total to the inventory windows.");
    Section("GROUND ITEMS");
    KindSwitches("Show##items", "All kinds", LootSwitches(), labels::KIND_COUNT,
                 [](int k) { return labels::kindName((labels::Kind)k); }, "##kinds", "");
    Note("Items whose name is not read yet count as Other.");
    MarkerColour("Item colour", &config::itemColor, "Cyan (as before)");
    Switch("Colour items by kind", &config::colorLootKinds);
    Note("Guns magenta, ammo white, meds green, shields blue; the rest keep the Item colour.");
    Section("VALUABLE ITEMS");
    Switch("Highlight valuable items", &config::highlightValuable);
    SliderD("Valuable from", &config::valuableFrom, 1000, 100000, "%.0f");
    Note("Items priced at least this in values.txt are drawn gold and bigger, with their price.");
    Section("CONTAINERS");
    KindSwitches("Show##containers", "All groups", ContainerSwitches(), labels::GROUP_COUNT,
                 [](int g) { return labels::groupName((labels::Group)g); }, "##groups", "##box");
    Note("Containers whose name is not read yet count as Other.");
    MarkerColour("Container colour", &config::containerColor, "Green (as before)");
}

inline void PageRadar() {
    using namespace ui;
    Section("MASTER");
    Switch("Radar", &config::enableRadar);
    if (!config::enableRadar) return;
    Section("SIZE AND RANGE");
    float range = config::radarRadius * config::scale / 100.0f; //metres from the centre to the edge
    if (Slider("Size", &config::radarRadius, 50.0f, 300.0f, "%.0f px")) config::scale = range * 100.0f / config::radarRadius; //keep the range
    if (Slider("Range", &range, 50.0f, 500.0f, "%.0f m")) config::scale = range * 100.0f / config::radarRadius;
    Note("Size is how big the radar is; Range is how far it reaches to its edge.");
    Section("POSITION");
    const float R = config::radarRadius, x0 = ImGui::GetCursorPosX();
    if (ImGui::Button("Top left")) { config::radarCenterX = R + 40; config::radarCenterY = R + 50; }
    SameLineIfFits(ButtonW("Left, above health"), x0); //the row wraps with big menu text on a narrow screen
    //panel bottom (centre + R + 14) 6 px above the HP/SH readout's card, which grows with Text size
    if (ImGui::Button("Left, above health")) { config::radarCenterX = R + 40; config::radarCenterY = SelfHealthRect().y0 - 6 - R - 14; }
    SameLineIfFits(ButtonW("Top right"), x0);
    if (ImGui::Button("Top right")) { config::radarCenterX = (float)config::SCREEN_W - R - 40; config::radarCenterY = R + 50; }
    Slider("Left / right", &config::radarCenterX, R, std::max(R + 1, (float)config::SCREEN_W - R), "%.0f");
    Slider("Up / down", &config::radarCenterY, R, std::max(R + 1, (float)config::SCREEN_H - R), "%.0f");
}

//Common game picture sizes for a stretched resolution, in the order of the "Game picture size" list (then "Custom")
inline constexpr int GAME_SIZES[][2] = {{1024, 768}, {1280, 960}, {1440, 1080}, {1600, 1200}, {1920, 1440},
                                        {1280, 1024}, {1680, 1050}, {1728, 1080}, {1920, 1080}};
inline constexpr int GAME_SIZE_CUSTOM = 9;

//Big step buttons from the cursor on ("-100 -10 -1 +1 +10 +100"): each adds its step to *v, kept within lo..hi. fmt
//writes a step on its button ("%+.0f"). They wrap onto a second line when the page is too narrow. id: their ID scope.
inline constexpr float STEP_PAD_Y = 10; //tall buttons, easy to hit
template <typename T> inline void StepButtons(const char* id, T* v, double lo, double hi, std::initializer_list<double> steps, const char* fmt) {
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ui::S(8), ui::S(STEP_PAD_Y)));
    const float x0 = ImGui::GetCursorPosX();
    bool first = true;
    for (double s : steps) {
        char t[16];
        std::snprintf(t, sizeof t, fmt, s);
        const float w = std::max(ui::S(56), ImGui::CalcTextSize(t).x + ImGui::GetStyle().FramePadding.x * 2);
        if (!first) ui::SameLineIfFits(w, x0);
        first = false;
        if (ImGui::Button(t, ImVec2(w, 0))) *v = (T)std::clamp((double)*v + s, lo, hi);
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
}

//One number with big step buttons: "Game width  1440    -100 -10 -1 +1 +10 +100", kept within lo..hi
inline void StepRow(const char* label, double* v, double lo, double hi) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ui::S(8), ui::S(STEP_PAD_Y))); //the text stays level with the buttons
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s  %.0f", label, *v);
    ImGui::PopStyleVar();
    ImGui::SameLine(ui::S(ui::LABEL_W));
    StepButtons(label, v, lo, hi, {-100, -10, -1, 1, 10, 100}, "%+.0f");
}

//Fine step buttons under a calibration slider, lined up with its track
inline void FineSteps(const char* id, float* v, double lo, double hi, std::initializer_list<double> steps, const char* fmt) {
    ImGui::SetCursorPosX(ui::S(ui::LABEL_W));
    StepButtons(id, v, lo, hi, steps, fmt);
}

//"Reset calibration": FOV side, Zoom, Shift X / Y and camera roll back to their config.h values
inline void ResetCalibration() {
    settings::reset({&config::fovIsHorizontal, &config::fovScale, &config::centerOffsetX, &config::centerOffsetY, &config::useCameraRoll});
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
    Note("The yellow frame should sit on the game's edges and the cross on its crosshair.");
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
    Note("If markers drift near the edges, flip FOV side first, then adjust Zoom.");
    Switch("FOV is horizontal", &config::fovIsHorizontal);
    Slider("Zoom", &config::fovScale, 0.5f, 2.0f, "%.3f");
    FineSteps("Zoom steps", &config::fovScale, 0.5, 2.0, {-0.05, -0.01, 0.01, 0.05}, "%+.2f");
    Slider("Shift X", &config::centerOffsetX, -300.0f, 300.0f, "%.0f px");
    FineSteps("Shift X steps", &config::centerOffsetX, -300, 300, {-10, -1, 1, 10}, "%+.0f");
    Slider("Shift Y", &config::centerOffsetY, -300.0f, 300.0f, "%.0f px");
    FineSteps("Shift Y steps", &config::centerOffsetY, -300, 300, {-10, -1, 1, 10}, "%+.0f");
    Switch("Use camera roll", &config::useCameraRoll);
    static double calArmedAt = -1;
    if (ConfirmButton("Reset calibration", "Yes, reset calibration", &calArmedAt)) { ResetCalibration(); Toast("Calibration reset"); }
    //only while some of this part is scrolled into view on the page
    const float left = ImGui::GetWindowPos().x;
    if (ImGui::IsRectVisible(ImVec2(left, top), ImVec2(left + ImGui::GetWindowWidth(), ImGui::GetCursorScreenPos().y))) DrawCalibrationGuide();
}

//CONFIGS on the Settings tab: named copies of every setting (settings.h profiles), then the main things drawn. Those
//switches are the same values as on the ESP and Loot tabs (menu_test looks for them there), so a profile is set up here.
inline void DrawConfigs() {
    using namespace ui;
    static char name[25] = "";         //24 characters: the most cleanProfileName keeps
    static std::string chosen, result; //the profile picked in the list; what the last button did
    static std::vector<std::string> profiles;
    static double listedAt = -1e9;     //read again once a second, so profiles copied in by hand show up too
    if (ImGui::GetTime() - listedAt > 1.0) { profiles = settings::listProfiles(); listedAt = ImGui::GetTime(); }
    Section("CONFIGS");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Profile name");
    ImGui::SameLine(S(LABEL_W));
    ImGui::SetNextItemWidth(ControlW(0));
    const bool enter = ImGui::InputText("##profilename", name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SetCursorPosX(S(LABEL_W));
    if (ImGui::Button("Save profile") || enter) {
        const std::string n = settings::cleanProfileName(name);
        if (n.empty()) result = "Type a name first (letters, digits, spaces, - and _).";
        else if (settings::saveProfile(n)) { result = "Saved \"" + n + "\"."; chosen = n; profiles = settings::listProfiles(); }
        else result = "Could not save \"" + n + "\".";
    }
    if (profiles.empty()) Note("No saved profiles yet.");
    else {
        double pick = 0;
        std::vector<const char*> items;
        for (size_t i = 0; i < profiles.size(); i++) {
            items.push_back(profiles[i].c_str());
            if (profiles[i] == chosen) pick = (double)i;
        }
        const bool picked = Choice("Saved profiles", &pick, items.data(), (int)items.size());
        chosen = profiles[(size_t)pick];
        if (picked) std::snprintf(name, sizeof name, "%s", chosen.c_str()); //so Save writes over it
        ImGui::SetCursorPosX(S(LABEL_W));
        const float x0 = ImGui::GetCursorPosX();
        if (ImGui::Button("Load profile")) result = settings::loadProfile(chosen) ? "Loaded \"" + chosen + "\"." : "Could not load \"" + chosen + "\".";
        SameLineIfFits(ButtonW("Delete profile"), x0);
        static double delArmedAt = -1;
        if (ConfirmButton("Delete profile", "Yes, delete profile", &delArmedAt)) {
            result = settings::deleteProfile(chosen) ? "Deleted \"" + chosen + "\"." : "Could not delete \"" + chosen + "\".";
            profiles = settings::listProfiles();
        }
    }
    if (!result.empty()) ImGui::TextWrapped("%s", result.c_str()); //white: it answers a button
}

inline void PageSettings() {
    using namespace ui;
    DrawConfigs();
    Section("LOOK");
    Slider("Text size", &config::labelSize, 10.0f, 40.0f, "%.0f px");
    Switch("Full-size small text", &config::fullSizeText);
    Slider("Marker size", &config::markerScale, 1.0f, 3.0f, "%.1f x"); //1 is the size they always had: it only goes up
    Slider("Menu text size", &config::menuTextSize, 15.0f, 30.0f, "%.0f px");
    Slider("Overlay opacity", &config::overlayOpacity, 0.2f, 1.0f, "%.2f");
    Slider("Menu opacity", &config::menuOpacity, 0.5f, 1.0f, "%.2f");
    Note("Menu text size applies when you let go; Overlay opacity never fades the warnings.");
    SubHeading("Appearance");
    double accent = config::menuAccent; //an int in config.h; Choice keeps a double
    if (Choice("Accent colour", &accent, {"White (as before)", "Red", "Yellow", "Blue", "Green", "Violet"})) config::menuAccent = (int)accent;
    SliderD("Corner roundness", &config::menuRoundness, 0.0, 1.5, "%.1f x");
    Switch("Section rules", &config::menuSectionRules);
    Section("SPOKEN CUES");
    if (Switch("Say hotkey results and near enemies out loud", &config::soundCues) && config::soundCues) cue::say("Spoken cues on");
    if (config::soundCues) {
        SliderD("Spoken warning range", &config::cueRange, 10, 150, "%.0f m");
        SliderD("Speech speed", &config::speechRate, -50, 50, "%.0f");
        if (cue::missing) ImGui::TextWrapped("No sound: spd-say was not found; install speech-dispatcher.");
        else if (cue::failed) ImGui::TextWrapped("No sound: spd-say cannot reach speech-dispatcher; run 'spd-say test' to see why.");
    }
    Note("Says F-key results and warns once when an enemy comes in range, at most every 2 s.");
    Section("HOTKEYS AND KEYBOARD");
    Switch("Hotkeys (F6, F7, F9, F10)", &config::hotkeysEnabled);
    Note("F9 says your health, shield and nearby enemies, and shows it at the top.");
    Switch("Keyboard controls the menu", &config::menuKeyboard);
    Note("Arrows and Tab move the white frame, Space presses, Enter types a value, Esc closes.");
    Switch("Stream hide (F8)", &config::streamHide);
    Note("Draws nothing for streams; while on, the menu will not open until you press F8.");
    Section("SPEED");
    SliderD("Update rate", &config::trackerHz, 30, 240, "%.0f Hz");
    Note("Higher follows moving players more closely and uses more CPU.");
    Switch("Status card", &config::showDebugInfo);
    Note("Shows FPS, update rate and tracked count at the top middle while the game is found.");
    DrawGameViewSettings();
    Section("OVERLAY WINDOW");
    if (g_OverlayWhere) ImGui::TextWrapped("%s", g_OverlayWhere().c_str());
    if (!g_OverlayNote.empty()) ImGui::TextWrapped("%s", g_OverlayNote.c_str()); //the setup problem shown at the start
    Note("Pick the monitor with: bash start.sh DP-1 (names: kscreen-doctor -o)");
    Section("FILES AND RESET");
    Note("Settings, names, offsets, marks and profiles are in ~/.config/kwin-helper/");
    static double resetArmedAt = -1;
    if (ConfirmButton("Reset all settings", "Yes, reset everything", &resetArmedAt)) { settings::resetAll(); ImGui::SetWindowPos(APP_ID, ImVec2(120, 110)); Toast("Settings reset"); }
    static double resetHvArmedAt = -1;
    if (ConfirmButton("Reset to high visibility", "Yes, reset to high visibility", &resetHvArmedAt)) {
        ResetToHighVisibility(); ImGui::SetWindowPos(APP_ID, ImVec2(120, 110)); Toast("Settings reset to high visibility");
    }
    Section("QUIT");
    if (ImGui::Button("Quit overlay")) g_MenuQuit = true;
}

//Pass to RenderEnd(). Draws nothing while the menu is closed.
inline void DrawMenu() {
    if (!g_MenuOpen) return;
    g_MenuTab = std::min(MENU_TAB_COUNT - 1, std::max(0, g_MenuTab));

    ui::PushTheme(std::min(1.0f, std::max(0.5f, config::menuOpacity)), MenuTextSize());
    ImGui::SetNextWindowPos(ImVec2(120, 110), ImGuiCond_FirstUseEver);
    //820 x 640 at 15 px text, growing with the menu text size, but never bigger than the screen (the page scrolls)
    ImGui::SetNextWindowSize(ImVec2(std::min(ui::S(MENU_W), (float)config::SCREEN_W), std::min(ui::S(MENU_H), (float)config::SCREEN_H)), ImGuiCond_Always);
    //NoBringToFrontOnFocus: a click on the menu must not hide the inventory windows behind it
    ImGui::Begin(APP_ID, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus);
    {   //keep the whole menu on screen (a drag or a position saved on a bigger monitor can leave only a sliver showing)
        const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
        const ImVec2 c(std::clamp(p.x, 0.0f, std::max(0.0f, (float)config::SCREEN_W - s.x)),
                       std::clamp(p.y, 0.0f, std::max(0.0f, (float)config::SCREEN_H - s.y)));
        if (c.x != p.x || c.y != p.y) ImGui::SetWindowPos(c);
    }

    ImGui::PushFont(OverlayFont(), ui::S(20));
    ImGui::TextUnformatted("Overlay");
    ImGui::PopFont();
    const char* closeLabel = "Close menu (Insert)"; //a mouse way out too: Insert and SIGUSR1 need the hotkey thread
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::SameLine(ImGui::GetWindowWidth() - st.WindowPadding.x - ImGui::CalcTextSize(closeLabel).x - st.FramePadding.x * 2);
    if (ImGui::Button(closeLabel)) g_MenuToggle = true; //MenuPollToggle closes and saves exactly as Insert does
    ImGui::Separator();

    //the hotkey line under the page (30 px at 15 px text); big menu text on a narrow screen wraps it onto two lines
    const char* keys = "F6 mark teammate    F7 twice: clear marks    F8 stream hide    F9 say status    F10 twice: quit";
    const float footer = ImGui::CalcTextSize(keys, nullptr, false, ImGui::GetContentRegionAvail().x).y + st.ItemSpacing.y * 2 + 1;
    //NavFlattened: the arrows and Tab move straight between the tabs and the page
    ImGui::BeginChild("##nav", ImVec2(ui::S(150), -footer), ImGuiChildFlags_NavFlattened);
    for (int i = 0; i < MENU_TAB_COUNT; i++)
        if (ui::Tab(MENU_TABS[i], g_MenuTab == i, ui::S(140))) g_MenuTab = i;
    ImGui::EndChild();

    ImGui::SameLine();
    static int shownTab = -1; //another tab opens at its top: the pages share one scroll position
    if (g_MenuTab != shownTab) ImGui::SetNextWindowScroll(ImVec2(0, 0));
    shownTab = g_MenuTab;
    ImGui::BeginChild("##page", ImVec2(0, -footer), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    switch (g_MenuTab) {
        case 0: PageEsp(); break;
        case 1: PageLoot(); break;
        case 2: PageRadar(); break;
        default: PageSettings(); break;
    }
    ImGui::EndChild();

    ImGui::Separator();
    ui::Note(keys); //wraps at the menu's width, in the notes' own colour
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
