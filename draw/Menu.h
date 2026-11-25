#ifndef KWINHELPER_MENU_H
#define KWINHELPER_MENU_H
//The Insert menu: black and white, tabs on the left (ESP, Players, Radar, Settings).
//Settings and your own names are saved in ~/.config/kwin-helper/ (see settings.h and labels.h).
#include <atomic>
#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "Overlay.h"
#include "PlayerList.h"
#include "UiKit.h"
#include "../utils/hotkey.h"
#include "../utils/keytext.h"
#include "../utils/gameview.h"
#include "../utils/labels.h"
#include "../utils/marks.h"
#include "../utils/settings.h"

inline bool g_MenuOpen = false;
inline std::atomic<bool> g_MenuQuit{false}; //the menu's Quit button; the main loop ends the program
inline int g_MenuTab = 0; //which page is shown
inline const char* const MENU_TABS[] = {"ESP", "Players", "Radar", "Settings"};
inline constexpr int MENU_TAB_COUNT = 4;

//Call once per frame before RenderBegin(). Flips the menu on Insert. The overlay is click-through
//normally; while the menu is open it takes the mouse so the menu can be clicked.
inline void MenuPollToggle() {
    if (!g_MenuToggle.exchange(false)) return;
    g_MenuOpen = !g_MenuOpen;
    if (g_SetInteractive) g_SetInteractive(g_MenuOpen); //the overlay takes the mouse while the menu is open
    if (!g_MenuOpen) {
        settings::clampAll();
        settings::save();
        labels::save();
        marks::save();
    }
}

//The key presses the hotkey thread queued while a text box was active, as ImGui input. Call once per frame
//before RenderBegin(). The overlay never has keyboard focus, so typing comes from the keyboard device itself.
inline void MenuFeedKeys() {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    if (g_MenuOpen) io.AddFocusEvent(true); //the overlay window is never "focused" for the desktop; ImGui must not care

    const bool want = g_MenuOpen && io.WantTextInput; //a text box is active (decided last frame)
    if (want) g_GrabBeatMs = NowMs(); //set before the flag so the watchdog never sees a stale beat
    g_TextGrab = want;

    std::vector<KeyEv> evs;
    {
        std::lock_guard<std::mutex> lk(g_KeyMutex);
        evs.swap(g_KeyQueue);
    }
    static bool shift = false;
    if (!want) { shift = false; return; }

    for (const KeyEv& ev : evs) {
        if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
            shift = ev.value != 0;
            io.AddKeyEvent(ImGuiMod_Shift, shift);
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
            case KEY_HOME: k = ImGuiKey_Home; break;
            case KEY_END: k = ImGuiKey_End; break;
            default: break;
        }
        if (k != ImGuiKey_None) {
            if (ev.value != 2) io.AddKeyEvent(k, ev.value != 0); //ImGui repeats held keys itself
            continue;
        }
        if (ev.value >= 1) {
            char c = KeyToChar(ev.code, shift);
            if (c) io.AddInputCharacter((unsigned)c);
        }
    }
}

//--- the pages
inline void PageEsp() {
    using namespace ui;
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
    Switch("Ground items", &config::showPickups); ImGui::NextColumn();
    Switch("Containers", &config::showContainers); ImGui::NextColumn();
    ImGui::Columns(1);
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
    Choice("Box style", &config::boxStyle, {"Corners", "Full", "Sides"});
    Choice("Box colour", &config::boxColor, {"Red", "White", "Yellow", "Blue", "Green", "Violet"});
    Switch("Monochrome (white and grey)", &config::monochrome);
    Slider("Line thickness", &config::boxThickness, 1.0f, 4.0f, "%.1f px");
    Switch("Health and shield bars", &config::showBars);
    Section("SKELETON");
    Switch("Skeleton on players", &config::showSkeleton);
    Note("A faint skeleton means the game is not drawing that player right now, so the pose is an estimate.");
    SliderD("Skeleton range", &config::skeletonMaxDist, 20, 300, "%.0f m");
    Switch("Dim players behind cover", &config::dimHidden);
    if (config::dimHidden) SliderD("Hidden player opacity", &config::hiddenAlpha, 40, 255, "%.0f");
    Section("DISTANCE");
    SliderD("Players range", &config::maxPlayerDist, 50, 800, "%.0f m");
    SliderD("ARC range", &config::maxArcDist, 20, 500, "%.0f m");
    SliderD("Loot range", &config::maxLootDist, 5, 150, "%.0f m");
}

inline void PagePlayers() {
    using namespace ui;
    Section("LIST");
    Choice("Player list", &config::listMode, {"Off", "Always", "Only with menu"});
    SliderD("List rows", &config::listRows, 3, 16, "%.0f");
    Section("TEAMMATES");
    Switch("Hide players with unknown team", &config::hideUnknownTeams);
    if (ImGui::Button("Clear teammate marks")) { marks::clear(); marks::save(); Toast("Teammate marks cleared"); }
    ImGui::SameLine();
    Note("F6 marks the player at your crosshair, F7 clears all.");
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
    if (ImGui::Button("Left, above health")) { config::radarCenterX = R + 40; config::radarCenterY = (float)config::SCREEN_H - 330 - R; }
    ImGui::SameLine();
    if (ImGui::Button("Top right")) { config::radarCenterX = (float)config::SCREEN_W - R - 40; config::radarCenterY = R + 50; }
    Slider("Left / right", &config::radarCenterX, R, std::max(R + 1, (float)config::SCREEN_W - R), "%.0f");
    Slider("Up / down", &config::radarCenterY, R, std::max(R + 1, (float)config::SCREEN_H - R), "%.0f");
}

//The game view part of the Settings tab (stretched resolution, gamescope, calibration)
inline void DrawGameViewSettings() {
    using namespace ui;
    Section("GAME VIEW: STRETCHED RESOLUTION / GAMESCOPE");
    Switch("Detect automatically", &config::gameViewAuto);
    Note(g_gameView.how.c_str());
    if (!config::gameViewAuto) {
        int gw = (int)config::gameRenderW, gh = (int)config::gameRenderH;
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("Game width", &gw, 0, 0)) config::gameRenderW = std::min(8192, std::max(320, gw));
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("Game height", &gh, 0, 0)) config::gameRenderH = std::min(8192, std::max(240, gh));
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
}

inline void PageSettings() {
    using namespace ui;
    Section("LOOK");
    Slider("Overlay opacity", &config::overlayOpacity, 0.2f, 1.0f, "%.2f");
    Slider("Menu opacity", &config::menuOpacity, 0.5f, 1.0f, "%.2f");
    Slider("Text size", &config::labelSize, 10.0f, 28.0f, "%.0f px");
    Section("SPEED");
    SliderD("Update rate", &config::trackerHz, 30, 240, "%.0f Hz");
    Note("Higher follows moving players more closely and uses more CPU.");
    DrawGameViewSettings();
    Section("OTHER");
    Switch("Status card", &config::showDebugInfo);
    Switch("Hotkeys (F6, F7, F10)", &config::hotkeysEnabled);
    Switch("Stream hide (F8)", &config::streamHide);
    Note("Stream hide draws nothing, so recordings and streams show no overlay. F8 always works, even with Hotkeys off. While it is on the menu cannot be opened: press F8 first.");
    Section("FILES");
    Note("Settings, names, offsets and marks are in ~/.config/kwin-helper/");
    static bool armed = false;
    if (ImGui::Button(armed ? "Click again to reset everything" : "Reset all settings")) {
        if (armed) { settings::resetAll(); armed = false; Toast("Settings reset"); }
        else armed = true;
    }
    if (armed) { ImGui::SameLine(); if (ImGui::Button("Cancel")) armed = false; }
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

    ImGui::PushFont(OverlayFont(), 20.0f);
    ImGui::TextUnformatted("Overlay");
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 150);
    ImGui::TextColored(ImColor(ui::GREY), "Insert closes");
    ImGui::Separator();

    const float footer = 30.0f;
    ImGui::BeginChild("##nav", ImVec2(150, -footer), false);
    for (int i = 0; i < MENU_TAB_COUNT; i++)
        if (ui::Tab(MENU_TABS[i], g_MenuTab == i, 140)) g_MenuTab = i;
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##page", ImVec2(0, -footer), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    switch (g_MenuTab) {
        case 0: PageEsp(); break;
        case 1: PagePlayers(); break;
        case 2: PageRadar(); break;
        default: PageSettings(); break;
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextColored(ImColor(ui::GREY), "F6 mark teammate    F7 clear marks    F8 stream hide    F10 quit overlay");
    ImGui::End();
    ui::PopTheme();
}

//Everything the overlay shows on top of the ESP: the player list, inventory windows, the menu. Pass to RenderEnd().
inline void DrawUi() {
    DrawPlayerListWindow(g_MenuOpen);
    if (g_MenuOpen) DrawInspectWindows();
    DrawMenu();
}

#endif //KWINHELPER_MENU_H
