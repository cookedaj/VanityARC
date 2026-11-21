#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "gamestate/Tracker.h"
#include "draw/ESP.h"
#include "draw/Marking.h"
#include "draw/Menu.h"
#include "draw/OverlayBackend.h"
#include "utils/labels.h"
#include "utils/marks.h"
#include "utils/offsets.h"
#include "utils/settings.h"
#include "memory/memory.h"

inline std::atomic<bool> g_Running(true);
void SignalHandler(int) {g_Running = false;}

//Data older than this is not drawn: after a stall the markers would be where things were, not where they are
constexpr double STALE_MS = 250.0;

int main() {
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler); //pkill / logout: leave cleanly so settings and names are saved
    std::signal(SIGHUP, SignalHandler);
    if (!InitOverlay()) {
        std::cerr << "[-] Failed to init Overlay (GLFW/Wayland)" << std::endl;
        return 1;
    }

    settings::load(); //menu settings and your own names from the last run (every value kept inside its safe range)
    labels::load();
    marks::load();    //teammates you marked by hand
    live::load();     //offsets found with the probes (offsets.txt)
    std::thread hotkey = StartHotkey(); //Insert opens the menu
    ESPInit();
    if (const char* tab = std::getenv("VANITYARC_TAB")) g_MenuTab = std::atoi(tab); //for screenshots: start on this page

    //Reading the game happens on its own thread (finding the process, the actor list, positions, names), so a slow
    //read can never make the drawing stutter. This loop only draws the newest result.
    Tracker tracker;
    tracker.start();

    const std::vector<RenderEntity> noEntities;
    while (g_Running && !OverlayShouldClose()) {
        MenuPollToggle();
        MenuFeedKeys(); //typing into the menu's text boxes
        RenderBegin();

        g_trackerHz = (int)config::trackerHz; //the tracker thread reads the update rate from here

        //Where the game's picture is on screen (differs from the screen with gamescope / a stretched resolution)
        GameViewSettings gvs;
        gvs.autoDetect = config::gameViewAuto;
        gvs.manualW = (int)config::gameRenderW;
        gvs.manualH = (int)config::gameRenderH;
        gvs.manualStretch = config::gameStretch;
        g_gameView = ComputeGameView(config::SCREEN_W, config::SCREEN_H, tracker.gamescopeInfo(), gvs);

        const std::shared_ptr<const Snapshot> snap = tracker.latest();
        //The camera is read again right now: the markers follow the view exactly, not as it was a few milliseconds ago
        FminimalViewInfo vm = snap->vm;
        if (snap->valid) {
            FminimalViewInfo fresh;
            if (tracker.freshCamera(fresh)) vm = fresh;
        }
        const double ageMs = snap->capturedNs ? (std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch()).count() - snap->capturedNs) / 1e6 : 1e9;
        const bool live = snap->valid && ageMs <= STALE_MS; //in a raid and the data is current
        const std::vector<RenderEntity>& ents = live ? snap->entities : noEntities;

        //What the player list, the inventory windows and the F6 key work with
        g_ui.ents = &ents;
        g_ui.inv = snap->inventories;
        g_ui.vm = vm;
        g_ui.localTeam = snap->localPlayerTeam;
        tracker.setInspected(g_MenuOpen ? InspectedActors() : std::vector<ptr>{});

        //Hotkeys: F6 marks the player at the crosshair, F7 clears the marks, F10 quits
        if (config::hotkeysEnabled) {
            if (g_KeyQuit.exchange(false)) g_Running = false;
            if (g_KeyClear.exchange(false)) { marks::clear(); marks::save(); Toast("Teammate marks cleared"); }
            if (g_KeyMark.exchange(false)) {
                const MarkResult r = MarkNearestAtCrosshair(ents, View(vm, MakeProjection()), snap->localPlayerTeam);
                Toast(r.text());
                if (r.kind == MarkResult::Marked || r.kind == MarkResult::Unmarked) marks::save();
            }
        } else {
            g_KeyQuit = g_KeyClear = g_KeyMark = false;
        }

        if (!snap->gameFound) DrawLabelCentered(110, 14, "Waiting for ARC Raiders...", 14.0f);
        float below = 20;
        if (snap->gameFound && config::showDebugInfo) {
            const Tracker::Stats st = tracker.stats();
            char l1[200], l2[200];
            size_t players = 0;
            for (const RenderEntity& e : ents) players += e.type == Object::PLAYER;
            if (!snap->valid) std::snprintf(l1, sizeof l1, "VANITYARC  -  waiting for a raid");
            else if (!live) std::snprintf(l1, sizeof l1, "VANITYARC  -  waiting for fresh data (%.0f ms old)", ageMs);
            else std::snprintf(l1, sizeof l1, "VANITYARC  -  live  -  %.0f Hz  -  data %.0f ms  -  %zu players", st.updatesPerSec, ageMs, players);
            std::snprintf(l2, sizeof l2, "%.0f fps   step %.2f ms   scan %.1f ms   %zu moving, %zu fixed",
                          (double)ImGui::GetIO().Framerate, st.stepMs, st.scanMs, st.dynamics, st.statics);
            below = DrawStatusCard(l1, l2);
        }
        DrawToast(below);

        if (live && config::espEnabled) DrawESP(ents, vm, snap->localPlayerTeam);
        if (config::enableRadar) DrawRadar(ents, vm, snap->localPlayerTeam);

        RenderEnd(DrawUi);
    }

    g_TextGrab = false;
    tracker.stop();
    settings::clampAll();
    settings::save();
    labels::save();
    marks::save();
    g_HotkeyStop = true;
    if (hotkey.joinable()) hotkey.join();

    std::cout << "[+] Destructing Window" << std::endl;

    OverlayShutdown();

    return 0;
}
