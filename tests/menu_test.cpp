//Headless run of the real Insert menu: draws every frame, then types into a slider's number box through the key queue.
#include "draw/Menu.h"
#include "imgui_internal.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
//A throw-away folder under the system temp directory (tests never touch your real ~/.config/kwin-helper)
static std::string testDir(const char* name) {
    return (std::filesystem::temp_directory_path() / (std::string("kwinhelper_test_") + name)).string();
}


static int fails = 0;
#define CHECK(cond, msg) do { bool ok_ = (cond); std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) fails++; } while (0)

template <typename F> static void frame(F fn) {
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    fn();
    ImGui::Render();
}

int main() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 800);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.IniFilename = nullptr;
    ImGui::GetStyle().WindowBorderSize = 0;
    settings::defaults(); //remember the config.h values before anything below changes them

    //things to list in the Rename tabs
    labels::setDir(testDir("menucfg"));
    labels::display(labels::Cat::Arc, "DA_EnemyType_Tick");
    labels::display(labels::Cat::Container, "BP_SocketContainer_Gvt_BinTrash_01_A_C");
    labels::display(labels::Cat::Item, "BattleRifle_BreachAction_01");
    labels::display(labels::Cat::Player, "793381ba5d093c9f", "cooked");

    //three players for the Players page and the player list
    static std::vector<RenderEntity> fake(3);
    for (int i = 0; i < 3; i++) {
        fake[i].type = Object::PLAYER; fake[i].actor = 0x1000 + (ptr)i * 0x100; fake[i].pos = Vector3(2000.0 * (i + 1), 0, 0);
        fake[i].label = i == 1 ? "" : (i == 0 ? "cooked" : "Rattlerfan"); fake[i].account = i == 2 ? "" : (i == 0 ? "793381ba5d093c9f" : "1122334455667788");
        fake[i].teamID = i; fake[i].playerHealth = 90.0 - 30 * i; fake[i].hasShield = i != 1; fake[i].shield = 40; fake[i].shieldMax = 100; fake[i].sub = "Ferro (held) + Stitcher";
    }
    g_ui.ents = &fake; g_ui.localTeam = 7;

    g_MenuOpen = true;
    for (int i = 0; i < 3; i++) frame([] { DrawMenu(); });
    CHECK(true, "menu draws");

    ImGuiWindow* w = ImGui::FindWindowByName("kwin-helper");
    CHECK(w != nullptr, "menu window exists");
    g_MenuTab = 4; //the Settings page (it has the game view part too)
    g_OverlayNote = "Monitor 'DP-9' not found (have: DP-1)\nThe desktop chose the monitor"; //the overlay window part shows it
    for (int i = 0; i < 3; i++) frame([] { DrawMenu(); });
    CHECK(true, "menu draws the Settings page");
    auto yellow = [] { for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer) if (v.col == GUIDE_YELLOW) return true; return false; };
    ImGuiWindow* pageWin = nullptr;
    for (ImGuiWindow* win : ImGui::GetCurrentContext()->Windows) if (std::strstr(win->Name, "##page")) pageWin = win;
    bool guide = false; //scrolled down to the game view part
    for (float at = 0; pageWin && !guide && at <= pageWin->ScrollMax.y + 100; at += 100) {
        ImGui::SetScrollY(pageWin, at);
        frame([] { DrawMenu(); }); frame([] { DrawMenu(); });
        guide = yellow();
    }
    CHECK(guide, "the Settings page draws the yellow game picture frame and centre cross while its game view part is on screen");
    const float settingsScroll = pageWin ? pageWin->Scroll.y : 0;
    g_MenuTab = 0; frame([] { DrawMenu(); });
    CHECK(!yellow(), "other pages do not draw the calibration guide");
    CHECK(settingsScroll > 0 && pageWin->Scroll.y == 0, "another tab opens at the top of its page, not part way down the last one");
    g_OverlayNote.clear();

    //typing: Ctrl+click turns a menu slider into a number box, then keys arrive through the same queue the keyboard thread fills
    double typed = 50;
    auto box = [&] { ImGui::Begin("typebox"); ui::SliderD("Typed value", &typed, 0, 500); ImGui::End(); };
    for (int i = 0; i < 3; i++) frame(box);
    ImGuiWindow* tw = ImGui::FindWindowByName("typebox");
    const ImGuiID sliderId = ImGui::GetIDWithSeed("##v", nullptr, ImGui::GetIDWithSeed("Typed value", nullptr, tw->ID));
    bool onSlider = false;
    for (float y = tw->Pos.y; y < tw->Pos.y + tw->Size.y && !onSlider; y += 2) {
        io.AddMousePosEvent(tw->Pos.x + ui::LABEL_W + 40, y); frame(box); frame(box);
        onSlider = ImGui::GetCurrentContext()->HoveredId == sliderId;
    }
    CHECK(onSlider, "the mouse can hover the slider");
    //Ctrl reaches ImGui only through the key queue while the menu has the keyboard, so Ctrl+click typing needs
    //"Keyboard controls the menu" on (with it off the overlay never learns that Ctrl is held)
    config::menuKeyboard = true;
    { std::lock_guard<std::mutex> lk(g_KeyMutex); g_KeyQueue.push_back({KEY_LEFTCTRL, 1}); }
    MenuFeedKeys(); frame(box);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); frame(box);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); frame(box);
    { std::lock_guard<std::mutex> lk(g_KeyMutex); g_KeyQueue.push_back({KEY_LEFTCTRL, 0}); }
    MenuFeedKeys();
    config::menuKeyboard = false; //the rest of the typing runs with the switch off: the open number box holds the keyboard
    int n = 0;
    while (!io.WantTextInput && n++ < 12) frame(box);
    std::printf("frames until active: %d, activeId %u\n", n, ImGui::GetCurrentContext()->ActiveId);
    CHECK(io.WantTextInput, "Ctrl+click on a slider opens a number box that asks for typing");

    MenuFeedKeys(); //sets the keyboard grab flag
    CHECK(g_TextGrab.load(), "keyboard grab requested while typing");

    auto press = [&](int code) {
        std::lock_guard<std::mutex> lk(g_KeyMutex);
        g_KeyQueue.push_back({code, 1});
        g_KeyQueue.push_back({code, 0});
    };
    press(KEY_1); press(KEY_2); press(KEY_5); //the old number is selected, so typing replaces it
    MenuFeedKeys(); for (int i = 0; i < 30; i++) frame(box); //ImGui hands queued keys out over a few frames
    std::printf("typed: %g\n", typed);
    CHECK(typed == 125, "a typed number arrives in the slider");

    press(KEY_BACKSPACE);
    MenuFeedKeys(); for (int i = 0; i < 10; i++) frame(box);
    CHECK(typed == 12, "backspace deletes a digit");

    { std::lock_guard<std::mutex> lk(g_KeyMutex); g_KeyQueue.push_back({KEY_LEFTCTRL, 1}); }
    press(KEY_A);
    { std::lock_guard<std::mutex> lk(g_KeyMutex); g_KeyQueue.push_back({KEY_LEFTCTRL, 0}); }
    press(KEY_7);
    MenuFeedKeys(); for (int i = 0; i < 20; i++) frame(box);
    CHECK(typed == 7, "Ctrl+A selects all and types no 'a'"); //an 'a' would leave "a7", which is not a number

    { std::lock_guard<std::mutex> lk(g_KeyMutex); g_KeyQueue.push_back({KEY_ENTER, 1}); } //its key-up comes after typing ended, so is never queued
    MenuFeedKeys(); for (int i = 0; i < 10; i++) frame(box);
    MenuFeedKeys(); //one more pass: nothing active now
    CHECK(!io.WantTextInput && !g_TextGrab.load(), "Enter finishes typing and releases the keyboard");
    frame(box);
    CHECK(!ImGui::IsKeyDown(ImGuiKey_Enter), "a key held when typing ends is let go");
    CHECK(typed == 7, "the typed number is kept");
    io.AddMousePosEvent(-1000, -1000); frame(box); frame(box);

    //typed keys are ignored (and not kept) when no text box is active
    press(KEY_A);
    MenuFeedKeys();
    { std::lock_guard<std::mutex> lk(g_KeyMutex); CHECK(g_KeyQueue.empty(), "key queue is drained when nobody is typing"); }

    //---- "Keyboard controls the menu": while the menu is open the keyboard is the menu's (Tab, Space, typed slider values, Esc)
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        auto tap = [](int code) { std::lock_guard<std::mutex> lk(g_KeyMutex); g_KeyQueue.push_back({code, 1}); g_KeyQueue.push_back({code, 0}); };
        auto run = [](int n) { for (int i = 0; i < n; i++) { MenuFeedKeys(); frame([] { DrawMenu(); }); } };
        const bool espWas = config::espEnabled;
        const double lootWas = config::nameMaxLootDist;
        config::menuKeyboard = true;
        g_MenuOpen = true;
        g_MenuTab = 0; //the ESP page
        run(3);
        CHECK(g_TextGrab.load() && (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard), "with the switch on the open menu takes the keyboard");

        ImGuiWindow* page = nullptr;
        for (ImGuiWindow* win : g.Windows) if (std::strstr(win->Name, "##page")) page = win;
        const ImGuiID pid = page ? page->ID : 0;
        const ImGuiID espOn = ImGui::GetIDWithSeed("##sw", nullptr, ImGui::GetIDWithSeed("ESP on", nullptr, pid));
        const ImGuiID lootRange = ImGui::GetIDWithSeed("##v", nullptr, ImGui::GetIDWithSeed("Loot name range", nullptr, pid));
        int tabs = 0;
        while (g.NavId != espOn && tabs++ < 40) { tap(KEY_TAB); run(3); }
        CHECK(page && g.NavId == espOn && g.NavCursorVisible, "Tab walks from the tabs on the left into the page");
        CHECK(ImGui::GetForegroundDrawList()->VtxBuffer.Size > 0, "the focused control gets the white frame");
        tap(KEY_SPACE); run(3);
        CHECK(config::espEnabled != espWas, "Space flips the focused switch");

        tabs = 0;
        while (g.NavId != lootRange && tabs++ < 40) { tap(KEY_TAB); run(3); }
        run(2);
        CHECK(g.NavId == lootRange && io.WantTextInput, "Tab onto a slider opens it for typing");
        tap(KEY_4); tap(KEY_2); run(8);
        tap(KEY_ENTER); run(4);
        CHECK(config::nameMaxLootDist == 42.0 && !io.WantTextInput, "an exact slider value can be typed");

        g_MenuToggle = false;
        tap(KEY_ESC); MenuFeedKeys();
        CHECK(g_MenuToggle.exchange(false), "Esc closes the menu");

        g_MenuOpen = false; //what closing the menu does (MenuPollToggle would also save the settings)
        MenuFeedKeys();
        CHECK(!g_TextGrab.load(), "a closed menu lets go of the keyboard");
        config::menuKeyboard = false;
        MenuFeedKeys();
        CHECK(!(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard), "with the switch off the arrows do not move the menu");
        config::espEnabled = espWas;
        config::nameMaxLootDist = lootWas;
    }

    //---- ID conflicts: hover every part of the menu and look for ImGui's "conflicting ID" error
    io.DisplaySize = ImVec2(1280, 1700);
    io.ConfigDebugHighlightIdConflicts = true;
    ImGuiContext* G = ImGui::GetCurrentContext();
    g_MenuOpen = false;

    //does the detector work at all? two buttons with the same label must be caught
    {
        auto bad = [] { ImGui::Begin("bad"); ImGui::Button("Same"); ImGui::Button("Same"); ImGui::End(); };
        bool caught = false;
        for (int i = 0; i < 4; i++) frame(bad);
        ImGuiWindow* bw = ImGui::FindWindowByName("bad");
        for (float y = bw->Pos.y; y < bw->Pos.y + 60 && !caught; y += 4) {
            io.AddMousePosEvent(bw->Pos.x + 20, y); frame(bad); frame(bad);
            caught = G->DebugDrawIdConflictsId != 0;
        }
        CHECK(caught, "detector catches two widgets with the same label");
        io.AddMousePosEvent(-1000, -1000); frame(bad); frame(bad); frame(bad);
    }

    //Game view with "Detect automatically" off: a size that is not in the list shows the big step buttons, and they stop at the limit
    config::gameViewAuto = false; config::gameRenderW = 8000; config::gameRenderH = 1080; //left off, so the Settings scan below covers the buttons
    {
        auto gv = [] {
            ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(700, 1000));
            ImGui::Begin("gv"); DrawGameViewSettings(); ImGui::End();
        };
        for (int i = 0; i < 3; i++) frame(gv);
        ImGuiWindow* gw = ImGui::FindWindowByName("gv");
        //a button in an ID scope ("Game width", "+100"): move the mouse onto it; then click it n times
        auto hoverOn = [&](const char* scope, const char* label) {
            const ImGuiID id = ImGui::GetIDWithSeed(label, nullptr, ImGui::GetIDWithSeed(scope, nullptr, gw->ID));
            for (float y = 0; y < 1000; y += 4)
                for (float x = 0; x < 700; x += 8) {
                    io.AddMousePosEvent(x, y); frame(gv); frame(gv);
                    if (G->HoveredId == id) return true;
                }
            return false;
        };
        auto click = [&](int n) {
            for (int i = 0; i < n; i++) {
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); frame(gv);
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); frame(gv); frame(gv);
            }
        };
        CHECK(hoverOn("Game width", "+100"), "the mouse can hover the Game width +100 button");
        click(2);
        CHECK(config::gameRenderW == 8192, "+100 twice from 8000 stops at 8192");
        CHECK(config::gameRenderH == 1080, "the width buttons leave the height alone");
        //the calibration's fine step buttons under the Zoom and Shift sliders
        config::fovScale = 1.0f; config::centerOffsetX = 0; config::centerOffsetY = 0;
        CHECK(hoverOn("Zoom steps", "+0.01"), "the mouse can hover the Zoom +0.01 button");
        click(3);
        CHECK(std::abs(config::fovScale - 1.03f) < 1e-4f, "Zoom +0.01 three times: 1.000 becomes 1.030");
        config::fovScale = 1.98f;
        CHECK(hoverOn("Zoom steps", "+0.05"), "the mouse can hover the Zoom +0.05 button");
        click(1);
        CHECK(config::fovScale == 2.0f, "Zoom +0.05 from 1.98 stops at 2.00, the slider's end");
        CHECK(hoverOn("Shift X steps", "-10"), "the mouse can hover the Shift X -10 button");
        click(2);
        CHECK(config::centerOffsetX == -20.0f && config::centerOffsetY == 0.0f, "Shift X -10 twice: -20 px, Shift Y left alone");
        CHECK(hoverOn("Shift Y steps", "+1"), "the mouse can hover the Shift Y +1 button");
        click(1);
        CHECK(config::centerOffsetY == 1.0f, "Shift Y +1: 1 px");
        //"Reset calibration" asks first ("Yes, reset calibration"), then puts all five back to the config.h values
        config::fovIsHorizontal = true; config::useCameraRoll = false; config::boxThickness = 6;
        CHECK(hoverOn("Reset calibration", "###arm"), "the mouse can hover Reset calibration");
        click(1);
        CHECK(config::fovScale == 2.0f && config::fovIsHorizontal, "Reset calibration does nothing on the first click (it asks first)");
        for (int i = 0; i < 40; i++) frame(gv); //the real action only counts 0.6 s after arming (and is offered for 6 s)
        const ImGuiID yesId = ImGui::GetIDWithSeed("Yes, reset calibration", nullptr, ImGui::GetIDWithSeed("Reset calibration", nullptr, gw->ID));
        bool yes = false;
        for (float x = 0, y = io.MousePos.y; x < 700 && !yes; x += 8) { io.AddMousePosEvent(x, y); frame(gv); frame(gv); yes = G->HoveredId == yesId; }
        CHECK(yes, "Reset calibration then offers \"Yes, reset calibration\" beside it");
        click(1);
        const std::set<const void*> cal = {&config::fovIsHorizontal, &config::fovScale, &config::centerOffsetX, &config::centerOffsetY, &config::useCameraRoll};
        int n = 0;
        bool same = true;
        for (size_t i = 0; i < settings::table().size(); i++)
            if (cal.count(settings::table()[i].p)) { n++; same &= settings::value(settings::table()[i]) == settings::defaults()[i]; }
        CHECK(n == 5 && same, "Reset calibration puts FOV side, Zoom, Shift X / Y and camera roll back to their defaults");
        CHECK(config::boxThickness == 6, "Reset calibration leaves every other setting alone");
        config::boxThickness = 1.5f;
        io.AddMousePosEvent(-1000, -1000); frame(gv); frame(gv);
    }

    //---- the Loot tab: every switch on it is saved; its "Show" drop-downs set the switches and always say what is on
    {
        std::set<void*> saved;
        for (const settings::Entry& e : settings::table()) saved.insert(e.p);
        bool all = saved.count(&config::colorLootKinds) && saved.count(&config::highlightValuable) && saved.count(&config::valuableFrom);
        for (int k = 0; k < labels::KIND_COUNT; k++) all &= saved.count(LootSwitches()[k]) > 0;
        for (int g = 0; g < labels::GROUP_COUNT; g++) all &= saved.count(ContainerSwitches()[g]) > 0;
        CHECK(all, "every Loot tab switch is in the settings table (saved and reset)");

        bool* const* s = LootSwitches();
        const int N = labels::KIND_COUNT, ammo = (int)labels::Kind::Ammo, meds = (int)labels::Kind::Meds;
        std::string shown; //the text the menu wrote (ImGui's log): a drop-down's shown line is in it
        auto page = [&] { ImGui::LogToBuffer(); DrawMenu(); shown = G->LogBuffer.c_str(); ImGui::LogFinish(); };
        g_MenuOpen = true; g_MenuTab = 1;
        SetSwitchPreset(s, N, 1 + ammo);
        bool only = true;
        for (int k = 0; k < N; k++) only &= *s[k] == (k == ammo);
        for (int i = 0; i < 3; i++) frame(page);
        CHECK(only && SwitchPreset(s, N) == 1 + ammo && shown.find("Only ammo") != std::string::npos,
              "Show: Only ammo leaves only Ammo on, and the drop-down says Only ammo");
        *s[meds] = true;
        frame(page);
        CHECK(SwitchPreset(s, N) == N + 1 && shown.find("Mixed (switches below)") != std::string::npos, "ammo and meds on: the drop-down says Mixed");
        SetSwitchPreset(s, N, N + 1);
        CHECK(*s[ammo] && *s[meds] && !*s[0], "picking Mixed leaves the switches as they are");
        SetSwitchPreset(s, N, 0);
        bool allOn = true;
        for (int k = 0; k < N; k++) allOn &= *s[k];
        frame(page);
        CHECK(allOn && SwitchPreset(s, N) == 0 && shown.find("All kinds") != std::string::npos, "Show: All kinds turns every kind back on");
        SetSwitchPreset(ContainerSwitches(), labels::GROUP_COUNT, 1 + (int)labels::Group::Arc);
        frame(page);
        CHECK(config::showContainerArc && !config::showContainerMedical && !config::showContainerOther && shown.find("Only ARC") != std::string::npos,
              "containers: Only ARC (in capitals) leaves only the ARC group on");
        SetSwitchPreset(ContainerSwitches(), labels::GROUP_COUNT, 0);
        frame(page);
        CHECK(config::showContainerMedical && config::showContainerArc && shown.find("All groups") != std::string::npos, "containers: All groups");
        g_MenuOpen = false;
    }

    //Every page from its top to its end: the page is scrolled down in steps that overlap, and at each step every point
    //of it is hovered (at the top the tabs and the header too). ImGui only notices a clash between widgets on screen
    //together, so this covers everything below the first screen as well. hovered: every ID the mouse reached on the page.
    std::set<ImGuiID> hovered;
    ImGuiID pageId = 0;
    auto scan = [&](const char* what) {
        g_MenuOpen = true;
        const float k = config::menuTextSize / ui::BASE_TEXT; //bigger text, bigger widgets: the grid can be as much coarser
        frame([] { DrawMenu(); }); frame([] { DrawMenu(); });
        ImGuiWindow* mw = ImGui::FindWindowByName("kwin-helper");
        ImGuiWindow* page = nullptr;
        for (ImGuiWindow* win : G->Windows) if (std::strstr(win->Name, "##page")) page = win;
        pageId = page ? page->ID : 0;
        int hits = 0, steps = 0;
        hovered.clear();
        for (float at = 0; page;) {
            steps++;
            ImGui::SetScrollY(page, at);
            frame([] { DrawMenu(); }); frame([] { DrawMenu(); });
            const ImGuiWindow* area = at > 0 ? page : mw; //scrolled: only the page moved
            for (float y = area->Pos.y + 2; y < area->Pos.y + area->Size.y; y += 5 * k)
                for (float x = area->Pos.x + 4; x < area->Pos.x + area->Size.x; x += 9 * k) {
                    io.AddMousePosEvent(x, y);
                    frame([] { DrawMenu(); }); frame([] { DrawMenu(); });
                    if (G->DebugDrawIdConflictsId != 0) hits++;
                    hovered.insert(G->HoveredId);
                }
            if (at >= page->ScrollMax.y) break;
            at = std::min(page->ScrollMax.y, at + std::max(50.0f, page->InnerRect.GetHeight() * 0.8f));
        }
        io.AddMousePosEvent(-1000, -1000);
        if (page) ImGui::SetScrollY(page, 0); //the pages share one scroll position
        frame([] { DrawMenu(); }); frame([] { DrawMenu(); });
        char msg[160];
        std::snprintf(msg, sizeof msg, "no conflicting IDs when hovering the menu (%s, menu text %.0f px, %d page steps to its end)", what, config::menuTextSize, steps);
        CHECK(page && hits == 0, msg);
    };
    auto sw = [&](const char* label) { return ImGui::GetIDWithSeed("##sw", nullptr, ImGui::GetIDWithSeed(label, nullptr, pageId)); };
    auto choice = [&](const char* label) { return ImGui::GetIDWithSeed("##c", nullptr, ImGui::GetIDWithSeed(label, nullptr, pageId)); };
    auto slider = [&](const char* label) { return ImGui::GetIDWithSeed("##v", nullptr, ImGui::GetIDWithSeed(label, nullptr, pageId)); };
    auto in = [&](const char* scope, const char* label) { return ImGui::GetIDWithSeed(label, nullptr, ImGui::GetIDWithSeed(scope, nullptr, pageId)); };
    //at 15 px menu text (the default), then at 30 px on a 1280x720 screen: the menu stays on that screen
    for (float text : {15.0f, 30.0f}) {
        const bool big = text > 15;
        config::menuTextSize = text;
        if (big) { config::SCREEN_W = 1280; config::SCREEN_H = 720; io.DisplaySize = ImVec2(1280, 720); }
        char msg[160];
        for (int tab = 0; tab < MENU_TAB_COUNT; tab++) {
            g_MenuTab = tab;
            scan(MENU_TABS[tab]);
            if (tab == 0) {
                std::snprintf(msg, sizeof msg, "ESP tab (menu text %.0f px): the mouse reaches Box style and every skeleton option below the first screen", text);
                CHECK(hovered.count(choice("Box style")) && hovered.count(sw("Skeleton on players")) && hovered.count(choice("Head marker")) &&
                      hovered.count(choice("Skeleton colour")) && hovered.count(sw("Dim players behind cover")) && hovered.count(sw("Big warning for a close enemy")), msg);
                std::snprintf(msg, sizeof msg, "ESP tab (menu text %.0f px): the mouse reaches Low health from and ARC arrow range", text);
                CHECK(hovered.count(slider("Low health from")) && hovered.count(slider("ARC arrow range")), msg);
            }
            if (tab == 1) {
                std::snprintf(msg, sizeof msg, "Loot tab (menu text %.0f px): the mouse reaches Ground items, Guns and the container switches at its end", text);
                CHECK(hovered.count(sw("Ground items")) && hovered.count(sw("Guns")) && hovered.count(sw("Containers")) &&
                      hovered.count(sw("Other##box")) && hovered.count(sw("Medical##box")), msg);
            }
            if (tab == 4) {
                std::snprintf(msg, sizeof msg, "Settings tab (menu text %.0f px): the mouse reaches Menu text size, the calibration step buttons and Quit", text);
                CHECK(hovered.count(slider("Menu text size")) && hovered.count(in("Zoom steps", "+0.01")) && hovered.count(in("Shift X steps", "-10")) &&
                      hovered.count(in("Shift Y steps", "+10")) && hovered.count(in("Reset calibration", "###arm")) && hovered.count(ImGui::GetIDWithSeed("Quit overlay", nullptr, pageId)), msg);
                std::snprintf(msg, sizeof msg, "Settings tab (menu text %.0f px): the mouse reaches Speech speed and the Hotkeys (F6, F7, F9, F10) switch", text);
                CHECK(hovered.count(slider("Speech speed")) && hovered.count(sw("Hotkeys (F6, F7, F9, F10)")), msg);
                std::snprintf(msg, sizeof msg, "Settings tab (menu text %.0f px): the mouse reaches the Full-size small text switch", text);
                CHECK(hovered.count(sw("Full-size small text")), msg);
            }
        }
        ImGuiWindow* mw = ImGui::FindWindowByName("kwin-helper");
        std::snprintf(msg, sizeof msg, "menu text %.0f px: the whole menu is on the %dx%d screen (%.0f x %.0f)", text, config::SCREEN_W, config::SCREEN_H, mw->Size.x, mw->Size.y);
        CHECK(mw->Pos.x >= 0 && mw->Pos.y >= 0 && mw->Pos.x + mw->Size.x <= config::SCREEN_W && mw->Pos.y + mw->Size.y <= config::SCREEN_H, msg);
        if (big) {
            CHECK(mw->Size.x == 1280 && mw->Size.y == 720, "menu text 30 px on 1280x720: the menu grows to the whole screen and no further");
            CHECK(ui::g_scale == 1.0f, "the menu text size is only used inside the menu's windows");
        }
    }
    config::menuTextSize = 15; config::SCREEN_W = 2560; config::SCREEN_H = 1440; io.DisplaySize = ImVec2(1280, 1700);
    frame([] { DrawMenu(); }); frame([] { DrawMenu(); });

    //the player list and an inventory window next to the menu draw without clashing IDs either
    g_MenuTab = 2;
    g_inspect = {"793381ba5d093c9f", "@1200"};
    {
        int hits = 0;
        auto both = [] { DrawUi(); };
        for (int i = 0; i < 4; i++) frame(both);
        for (ImGuiWindow* win : ImGui::GetCurrentContext()->Windows) {
            if (!win->WasActive || win->Hidden || win->Flags & ImGuiWindowFlags_ChildWindow) continue;
            for (float y = win->Pos.y + 2; y < win->Pos.y + win->Size.y; y += 6)
                for (float x = win->Pos.x + 4; x < win->Pos.x + win->Size.x; x += 12) {
                    io.AddMousePosEvent(x, y);
                    frame(both); frame(both);
                    if (G->DebugDrawIdConflictsId != 0) hits++;
                }
        }
        io.AddMousePosEvent(-1000, -1000); frame(both); frame(both);
        CHECK(hits == 0, "no conflicting IDs in the player list and the inventory windows");
        CHECK(ImGui::FindWindowByName("##playerlist") != nullptr, "the player list is drawn");
    }

    //the 'mark' button sits on top of a row: it must take the click itself, not the row (which opens the inventory)
    {
        setenv("HOME", testDir("menuhome").c_str(), 1); //marks::save() writes under $HOME/.config/kwin-helper
        io.DisplaySize = ImVec2(2600, 1700);            //the whole player list on screen
        g_inspect.clear();
        auto list = [] { DrawPlayerListWindow(true); };
        for (int i = 0; i < 4; i++) frame(list);
        ImGuiWindow* pw = ImGui::FindWindowByName("##playerlist");
        const ImGuiID markId = ImGui::GetIDWithSeed("mark", nullptr, ImGui::GetIDWithSeed("793381ba5d093c9f", nullptr, pw->ID));
        bool over = false;
        for (float y = pw->Pos.y + 2; y < pw->Pos.y + pw->Size.y && !over; y += 3)
            for (float x = pw->Pos.x + 4; x < pw->Pos.x + pw->Size.x && !over; x += 6) {
                io.AddMousePosEvent(x, y);
                frame(list); frame(list);
                over = G->HoveredId == markId;
            }
        CHECK(over, "the mouse can hover the 'mark' button in the player list");
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); frame(list);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); frame(list); frame(list);
        CHECK(marks::isMarked("793381ba5d093c9f"), "clicking 'mark' marks the player as a teammate");
        CHECK(g_inspect.empty(), "clicking 'mark' does not open the inventory window");
        io.AddMousePosEvent(-1000, -1000); frame(list); frame(list);
    }

    //menu text 30 px: the player list and a newly opened inventory window grow with it, and stay on the screen
    {
        auto windowAt = [&](const char* name, float text, int sw, int sh) {
            config::menuTextSize = text; config::SCREEN_W = sw; config::SCREEN_H = sh; io.DisplaySize = ImVec2((float)sw, (float)sh);
            for (int i = 0; i < 4; i++) frame([] { DrawPlayerListWindow(true); DrawInspectWindows(); });
            ImGuiWindow* win = ImGui::FindWindowByName(name);
            return win ? ImRect(win->Pos.x, win->Pos.y, win->Pos.x + win->Size.x, win->Pos.y + win->Size.y) : ImRect();
        };
        auto inside = [](const ImRect& r, int sw, int sh) { return r.GetWidth() > 0 && r.Min.x >= 0 && r.Min.y >= 0 && r.Max.x <= sw && r.Max.y <= sh; };
        const ImRect small = windowAt("##playerlist", 15, 1920, 1080), big = windowAt("##playerlist", 30, 1920, 1080),
                     narrow = windowAt("##playerlist", 30, 1280, 720);
        std::printf("player list width: %.0f at 15 px, %.0f at 30 px (1920 wide), %.0f at 30 px (1280 wide)\n", small.GetWidth(), big.GetWidth(), narrow.GetWidth());
        CHECK(big.GetWidth() > 1.8f * small.GetWidth() && inside(big, 1920, 1080), "menu text 30 px: the player list is twice as wide on a 1920x1080 screen, and on it");
        CHECK(narrow.GetWidth() > 1.4f * small.GetWidth() && inside(narrow, 1280, 720),
              "menu text 30 px on 1280x720: the player list grows only as far as the screen's width");
        g_inspect = {"1122334455667788"}; //a window not opened before: it gets the size for 30 px text
        const ImRect inv = windowAt("###inv1122334455667788", 30, 1280, 720); //its ID comes from the part after ###
        CHECK(inv.GetWidth() > 1.8f * 360 && inside(inv, 1280, 720), "menu text 30 px on 1280x720: a new inventory window is twice as wide and fits on the screen");
        g_inspect.clear();
        config::menuTextSize = 15; config::SCREEN_W = 2560; config::SCREEN_H = 1440; io.DisplaySize = ImVec2(1280, 1700);
        frame([] { DrawUi(); });
    }

    //the High visibility preset: every value it sets is inside its saved range, and it is written to settings.txt
    {
        std::error_code ec;
        std::filesystem::remove(settings::path(), ec); //$HOME is the test folder (set above)
        config::fullSizeText = false;
        ApplyHighVisibility();
        CHECK(config::labelSize == 28.0f && config::menuTextSize == 24.0f && config::boxThickness >= 4.0f && config::labelCards && config::boxStyle == 1 &&
              config::overlayOpacity == 1.0f && config::menuOpacity == 1.0f && config::hiddenAlpha >= 200, "High visibility sets the readable values");
        CHECK(config::fullSizeText, "High visibility turns Full-size small text on (distances, guns and marker labels in the full text size)");
        {   //its note says so
            std::string shown;
            g_MenuOpen = true; g_MenuTab = 0;
            for (int i = 0; i < 3; i++) frame([&] { ImGui::LogToBuffer(); DrawMenu(); shown = G->LogBuffer.c_str(); ImGui::LogFinish(); });
            g_MenuOpen = false;
            CHECK(shown.find("Full-size small text on") != std::string::npos, "the High visibility note names Full-size small text");
        }
        settings::clampAll();
        CHECK(config::labelSize == 28.0f && config::menuTextSize == 24.0f && config::boxThickness >= 4.0f && config::hiddenAlpha >= 200,
              "High visibility values are inside their safe ranges");
        CHECK(std::filesystem::exists(settings::path()), "High visibility is saved at once");
        config::boxThickness = 8.0f; config::hiddenAlpha = 255; config::labelSize = 36.0f; config::menuTextSize = 30.0f;
        ApplyHighVisibility();
        CHECK(config::boxThickness == 8.0f && config::hiddenAlpha == 255, "High visibility never makes thicker lines thinner or solid hidden players fainter");
        CHECK(config::labelSize == 36.0f && config::menuTextSize == 30.0f, "High visibility never makes a bigger Text size or Menu text size smaller");
        config::showItemValues = true;
        ApplyHighVisibility();
        ResetToHighVisibility();
        CHECK(config::showItemValues && config::labelSize == 28.0f, "High visibility and Reset to high visibility keep Show item values");
        CHECK(config::fullSizeText, "Reset to high visibility: Full-size small text on");
        settings::resetAll();
        CHECK(!config::showItemValues, "Reset all settings turns Show item values off");
        CHECK(!config::fullSizeText, "Reset all settings turns Full-size small text off");
    }

    //"Show item values": off, the inventory window shows no prices and does not read values.txt; on, it shows the total
    {
        const std::string vdir = testDir("menuvalues");
        std::filesystem::create_directories(vdir);
        std::ofstream(vdir + "/values.txt", std::ios::trunc) << "Snap Hook | 14000\n";
        itemvalues::setDir(vdir);
        auto inv = std::make_shared<std::unordered_map<ptr, std::vector<ItemRow>>>();
        (*inv)[fake[0].actor] = {ItemRow{"Snap Hook", "SnapHook", "Backpack", 1}, ItemRow{"Bandage", "Bandage_01", "Backpack", 2}};
        g_ui.inv = inv;
        g_inspect = {PlayerKey(fake[0])};
        std::string shown; //the text the inventory window wrote (ImGui's log)
        auto win = [&] { ImGui::LogToBuffer(); DrawInspectWindows(); shown = G->LogBuffer.c_str(); ImGui::LogFinish(); };
        config::showItemValues = false;
        for (int i = 0; i < 3; i++) frame(win);
        CHECK(shown.find("Snap Hook") != std::string::npos, "the inventory window lists the items");
        CHECK(shown.find("Value") == std::string::npos && shown.find("price") == std::string::npos, "Show item values off: no prices, no total, no note");
        CHECK(itemvalues::count() == 0, "Show item values off: values.txt is not read");
        config::showItemValues = true;
        for (int i = 0; i < 3; i++) frame(win);
        CHECK(itemvalues::count() == 1, "Show item values on: values.txt is read");
        CHECK(shown.find("Value at least 14000") != std::string::npos && shown.find("(no price)") != std::string::npos,
              "Show item values on: the total and the unpriced items show");
        config::showItemValues = false; g_ui.inv.reset(); g_inspect.clear(); itemvalues::clear();
    }

    ImGui::DestroyContext();
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
