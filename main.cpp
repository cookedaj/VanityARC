#include <fcntl.h>
#include <sys/file.h>
#include <sys/prctl.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gamestate/Tracker.h"
#include "draw/ESP.h"
#include "draw/Marking.h"
#include "draw/Menu.h"
#include "draw/OverlayBackend.h"
#include "utils/cue.h"
#include "utils/labels.h"
#include "utils/itemvalues.h"
#include "utils/marks.h"
#include "utils/obfstr.h"
#include "utils/offsets.h"
#include "utils/settings.h"
#include "memory/memory.h"

inline std::atomic<bool> g_Running(true);
void SignalHandler(int) {g_Running = false;}

//Data older than this is not drawn: after a stall the markers would be where things were, not where they are
constexpr double STALE_MS = 250.0;
//Without the live camera only this new: through the snapshot's own (old) camera the markers would slide off what they mark
constexpr double NO_CAMERA_MS = 50.0;

//Settings used to live in ~/.config/vanityarc. Copy that folder across once; never overwrite or delete anything.
void migrateConfigDir() {
    const char* home = std::getenv("HOME");
    if (!home) return;
    namespace fs = std::filesystem;
    const fs::path oldDir = fs::path(home) / ".config" / OBF("vanityarc");
    const fs::path newDir = fs::path(home) / ".config" / "kwin-helper";
    std::error_code ec;
    if (fs::exists(newDir, ec) || !fs::is_directory(oldDir, ec)) return;
    fs::copy(oldDir, newDir, fs::copy_options::recursive, ec);
}

int main() {
    prctl(PR_SET_NAME, "kwin-helper", 0, 0, 0);
    migrateConfigDir();
    //One copy at a time: two would each save their own settings and marks over the other's. The lock is held until
    //the process ends (the fd is never closed). Only a lock held by another copy stops the start, never a lock error.
    {
        const char* home = std::getenv("HOME");
        const std::string cfgDir = std::string(home ? home : ".") + "/.config/kwin-helper";
        std::error_code ec;
        std::filesystem::create_directories(cfgDir, ec);
        const int lockFd = open((cfgDir + "/running.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (lockFd >= 0 && flock(lockFd, LOCK_EX | LOCK_NB) != 0 && errno == EWOULDBLOCK) {
            std::cerr << "[-] kwin-helper is already running. If you cannot see it, stream hide may be on: press F8." << std::endl;
            return 1;
        }
    }
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler); //pkill / logout: leave cleanly so settings are saved
    std::signal(SIGHUP, SignalHandler);
    std::signal(SIGUSR1, MenuSignalHandler);   //before InitOverlay: an early pkill -USR1/-USR2 must not kill the overlay
    std::signal(SIGUSR2, StreamSignalHandler);
    cue::init(); //the spoken cues' spd-say processes are cleaned up by the kernel when they end
    if (!InitOverlay()) {
        std::cerr << "[-] Failed to init Overlay (GLFW/Wayland)" << std::endl;
        return 1;
    }

    //menu settings and your own names from the last run (every value kept inside its safe range)
    const int badSettings = settings::load(), badLabels = labels::load(), badValues = itemvalues::load();
    marks::load();    //teammates you marked by hand
    live::load();     //offsets found with the probes (offsets.txt)
    {   //hand-edited lines that were not understood: one toast at start (the terminal lists them with line numbers)
        std::string ignored;
        const std::pair<const char*, int> bad[] = {{"settings.txt", badSettings}, {"labels.txt", badLabels}, {"values.txt", badValues}};
        for (const auto& [file, n] : bad)
            if (n > 0) ignored += (ignored.empty() ? "" : ", ") + std::to_string(n) + (n == 1 ? " line in " : " lines in ") + file;
        if (!ignored.empty()) Toast("Ignored " + ignored + " (see the terminal)", 10.0);
    }
    std::thread hotkey = StartHotkey(); //Insert opens the menu
    ESPInit();
    if (const char* tab = std::getenv("KWINHELPER_TAB")) g_MenuTab = std::atoi(tab); //for screenshots: start on this page

    //Reading the game happens on its own thread (finding the process, the actor list, positions, names), so a slow
    //read can never make the drawing stutter. This loop only draws the newest result.
    Tracker tracker;
    tracker.start();

    const std::vector<RenderEntity> noEntities;
    auto lastLive = std::chrono::steady_clock::now(); //the last frame with live data (or with no game running)
    double quitArmedUntil = 0, clearArmedUntil = 0; //F10 and F7 act only on a second press within 2 s
    int lastKeyboards = -1; //what g_KeyboardCount was last frame
    NearCue nearCue;        //the spoken warning when an enemy comes close
    //A message at the top of the screen that is also said out loud (when spoken cues are on)
    auto tell = [](const std::string& text, double seconds = 3.0) { Toast(text, seconds); cue::say(text); };
    bool hadKeyboard = false; //a keyboard was open at some point in this run
    std::string noteSeen;   //the window setup problem that was on screen when the menu was opened
    while (g_Running && !OverlayShouldClose()) {
        MenuPollToggle();
        //Stream-hide (F8, SIGUSR2 or the menu switch): while on, the overlay draws nothing and takes no mouse, so a screen
        //recording or stream shows none of it. The menu is closed, because it would be invisible and would block clicks.
        if (g_MenuQuit.exchange(false)) g_Running = false;
        if (g_KeyStream.exchange(false)) {
            config::streamHide = !config::streamHide; //not saved: every launch starts visible
            std::cout << (config::streamHide ? "[+] Stream hide ON: the overlay draws nothing (F8 to show it)" : "[+] Stream hide OFF: the overlay is visible") << std::endl;
            cue::say(config::streamHide ? "Overlay hidden" : "Overlay shown");
        }
        SetOverlayHidden(config::streamHide);
        if (config::streamHide) {
            g_MenuToggle = false;
            if (g_MenuOpen) {
                g_MenuOpen = false;
                if (g_SetInteractive) g_SetInteractive(false);
                settings::clampAll(); settings::save(); //what a normal menu close does
            }
            g_TextGrab = false;
        }
        if (!OverlayReady()) { //the overlay surface is gone and is being made again; the tracker keeps running
            g_TextGrab = false; //no text box to type into: the keyboard stays with the game
            if (config::hotkeysEnabled && g_KeyQuit.exchange(false)) g_Running = false; //F10 still quits
            g_KeyMark = false; g_KeyClear = false; //no stale F6/F7 when the surface comes back
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        MenuFeedKeys(); //typing into the menu's text boxes
        RenderBegin();

        g_trackerHz = (int)config::trackerHz; //the tracker thread reads the update rate from here
        g_skeletonOn = config::showSkeleton && config::showPlayers && config::espEnabled; //poses are read only when they will be drawn
        g_skeletonDist = (int)config::skeletonMaxDist;

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
        bool freshCam = false;
        if (snap->valid) {
            FminimalViewInfo fresh;
            if (tracker.freshCamera(fresh)) { vm = fresh; freshCam = true; }
        }
        const double ageMs = snap->capturedNs ? (std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch()).count() - snap->capturedNs) / 1e6 : 1e9;
        const bool live = snap->valid && ageMs <= (freshCam ? STALE_MS : NO_CAMERA_MS); //in a raid and the data is current
        const std::vector<RenderEntity>& ents = live ? snap->entities : noEntities;
        const auto frameNow = std::chrono::steady_clock::now();
        if (live || !snap->gameFound) lastLive = frameNow;
        const bool lost = snap->gameFound && !live && frameNow - lastLive > std::chrono::milliseconds(1500); //the game runs, but no live data for a while

        //What the player list, the inventory windows and the F6 key work with
        g_ui.ents = &ents;
        g_ui.live = live;
        g_ui.inv = snap->inventories;
        g_ui.vm = vm;
        g_ui.localTeam = snap->localPlayerTeam;
        tracker.setInspected(g_MenuOpen ? InspectedActors() : std::vector<ptr>{});

        //Hotkeys: F6 marks the player at the crosshair, F7 clears the marks, F10 quits (F7 and F10: press twice)
        if (config::hotkeysEnabled) {
            const double now = ImGui::GetTime();
            if (g_KeyQuit.exchange(false)) {
                if (now < quitArmedUntil) { g_Running = false; cue::say("Overlay closed"); }
                else { quitArmedUntil = now + 2.0; tell("F10 again to quit", 2.0); }
            }
            if (g_KeyClear.exchange(false)) {
                if (now < clearArmedUntil) { clearArmedUntil = 0; marks::clear(); marks::save(); tell("Teammate marks cleared"); }
                else {
                    const size_t n = marks::all().size();
                    clearArmedUntil = now + 2.0;
                    tell("F7 again to clear " + std::to_string(n) + (n == 1 ? " mark" : " marks"), 2.0);
                }
            }
            if (g_KeyMark.exchange(false)) {
                if (!live) tell("No live game data: F6 did nothing");
                else {
                    const MarkResult r = MarkNearestAtCrosshair(ents, View(vm, MakeProjection()), snap->localPlayerTeam);
                    tell(r.text());
                    if (r.kind == MarkResult::Marked || r.kind == MarkResult::Unmarked) marks::save();
                }
            }
        } else {
            g_KeyQuit = g_KeyClear = g_KeyMark = false;
        }
        //Insert and the F-keys need a keyboard: say so when one is back (no keyboard at all: the card further down)
        const int keyboards = g_KeyboardCount;
        if (keyboards != lastKeyboards) {
            //no keyboard: the card further down shows it for as long as it lasts; also said once out loud
            if (keyboards == 0 && lastKeyboards > 0) cue::say("Keyboard lost: Insert will not work");
            else if (keyboards > 0 && lastKeyboards == 0) tell("Keyboard found");
            if (keyboards > 0) hadKeyboard = true;
            lastKeyboards = keyboards;
        }
        //Said out loud when an enemy first comes close. Quiet while stream hide is on (the overlay then gives nothing
        //away); when cues come back on, whoever is near is said again.
        if (!config::soundCues || config::streamHide) nearCue = NearCue{};
        else if (live) cue::say(nearCue.update(ents, vm, snap->localPlayerTeam, ImGui::GetTime()));

        //Drawn in this order, each over the one before: radar, then markers, then the messages at the top
        if (config::enableRadar) {
            DrawRadar(ents, vm, snap->localPlayerTeam);
            //An empty radar must not read as "nobody near" when there is simply no data
            if (!live) { const ImVec2 rc = RadarCenter(); DrawLabelCentered(rc.x, rc.y, snap->gameFound ? "NO DATA" : "NO GAME", config::labelSize, COLOR::white, true); }
        }
        if (live && config::espEnabled) DrawESP(ents, vm, snap->localPlayerTeam);

        float top = 12; //the next free line at the top middle of the screen
        if (snap->gameUpdated) {
            //The game's build differs from the one config.h is for: shown all the time, big, whatever the debug switch says
            const float big = config::labelSize * 1.6f;
            DrawLabelCentered((float)config::SCREEN_W * 0.5f, top + big / 2, "GAME UPDATED: MARKERS MAY BE WRONG", big, COLOR::white, true);
            top += big + 8;
        }
        float below = top + 8;
        if (!snap->gameFound) {
            //A card at top centre in the Text size (never under the old 14 px), the toast goes under it
            const float ws = std::max(14.0f, config::labelSize), y = top + ws / 2;
            DrawLabelCentered((float)config::SCREEN_W * 0.5f, y, OBF("Waiting for ARC Raiders...").c_str(), ws, COLOR::white, true);
            below = y + ws / 2 + 10 + config::labelSize / 2;
        } else if (lost) {
            //The game runs but nothing has been live for a while: say so and why, big, whatever the debug switch says
            char why[160];
            if (snap->valid) std::snprintf(why, sizeof why, "the data stopped updating (%.1f s old)", ageMs / 1000.0);
            else std::snprintf(why, sizeof why, "%s", snap->fault ? snap->fault : "waiting for a raid");
            const float x = (float)config::SCREEN_W * 0.5f, ls = config::labelSize, big = ls * 1.6f;
            float y = top + big / 2;
            DrawLabelCentered(x, y, "NO LIVE GAME DATA", big, COLOR::white, true);
            y += big / 2 + 8 + ls / 2;
            DrawLabelCentered(x, y, why, ls, COLOR::white, true);
            below = y + ls + 10;
        } else if (snap->gameFound && config::showDebugInfo) {
            const Tracker::Stats st = tracker.stats();
            char l1[200], l2[200];
            size_t players = 0;
            for (const RenderEntity& e : ents) players += e.type == Object::PLAYER;
            if (!snap->valid) std::snprintf(l1, sizeof l1, "waiting for a raid");
            else if (!live) std::snprintf(l1, sizeof l1, "waiting for fresh data (%.0f ms old)", ageMs);
            else std::snprintf(l1, sizeof l1, "live  -  %.0f Hz  -  data %.0f ms  -  %zu players", st.updatesPerSec, ageMs, players);
            std::snprintf(l2, sizeof l2, "%.0f fps   step %.2f ms   scan %.1f ms   %zu moving, %zu fixed",
                          (double)ImGui::GetIO().Framerate, st.stepMs, st.scanMs, st.dynamics, st.statics);
            below = DrawStatusCard(l1, l2, below);
        }
        //Insert cannot open the menu: say why and how to fix it, big, for as long as it lasts (from the start, too)
        if (const HotkeyHelp kh = HotkeyProblem(keyboards, g_KeyboardDenied, hadKeyboard); kh.why) {
            const float x = (float)config::SCREEN_W * 0.5f, ls = config::labelSize, big = ls * 1.3f;
            const std::string title = std::string("INSERT AND F-KEYS OFF: ") + kh.why;
            float y = below + (big - ls) / 2;
            DrawLabelCentered(x, y, title.c_str(), big, COLOR::white, true);
            y += big / 2 + 8 + ls / 2;
            DrawLabelCentered(x, y, kh.fix, ls, COLOR::white, true);
            y += ls + 8;
            DrawLabelCentered(x, y, "Open the menu meanwhile with:  pkill -USR1 -x kwin-helper", ls, COLOR::white, true);
            below = y + ls + 10;
        }
        //A window setup problem (monitor name not found, no layer-shell): big on the overlay, not only in the terminal,
        //until the menu is opened. A different problem later (the surface made again) shows again.
        if (g_MenuOpen) noteSeen = g_OverlayNote;
        if (!g_OverlayNote.empty() && g_OverlayNote != noteSeen) {
            const std::string body = g_OverlayNote + "\nOpen the menu (Insert) to hide this message";
            const float x = (float)config::SCREEN_W * 0.5f, ls = config::labelSize, big = ls * 1.6f, bh = LabelSize(body.c_str(), ls).y;
            float y = below + big / 2;
            DrawLabelCentered(x, y, "OVERLAY WINDOW PROBLEM", big, COLOR::white, true);
            y += big / 2 + 8 + bh / 2;
            DrawLabelCentered(x, y, body.c_str(), ls, COLOR::white, true);
            below = y + bh / 2 + 10;
        }
        DrawToast(below);

        //The close-enemy warning goes last so nothing covers it; LOW HEALTH moves below it when both show
        float warnBottom = 0;
        if (live && config::espEnabled) DrawCloseEnemyWarning(ents, vm, snap->localPlayerTeam, &warnBottom);
        if (live && config::showSelfHealth) DrawSelfHealth(snap->selfHealth, snap->selfShield, warnBottom); //old data: nothing, never an old number

        RenderEnd(DrawUi);
    }

    g_TextGrab = false;
    tracker.stop();
    settings::clampAll();
    settings::save();
    g_HotkeyStop = true;
    if (hotkey.joinable()) hotkey.join();

    std::cout << "[+] Destructing Window" << std::endl;

    OverlayShutdown();

    return 0;
}
