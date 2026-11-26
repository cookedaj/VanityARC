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
    CHECK(yellow(), "the Settings page draws the yellow game picture frame and centre cross");
    g_MenuTab = 0; frame([] { DrawMenu(); });
    CHECK(!yellow(), "other pages do not draw the calibration guide");
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
            ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(700, 400));
            ImGui::Begin("gv"); DrawGameViewSettings(); ImGui::End();
        };
        for (int i = 0; i < 3; i++) frame(gv);
        ImGuiWindow* gw = ImGui::FindWindowByName("gv");
        const ImGuiID plusId = ImGui::GetIDWithSeed("+100", nullptr, ImGui::GetIDWithSeed("Game width", nullptr, gw->ID));
        bool over = false;
        for (float y = 0; y < 400 && !over; y += 4)
            for (float x = 0; x < 700 && !over; x += 8) {
                io.AddMousePosEvent(x, y); frame(gv); frame(gv);
                over = G->HoveredId == plusId;
            }
        CHECK(over, "the mouse can hover the Game width +100 button");
        for (int i = 0; i < 2; i++) {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); frame(gv);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); frame(gv); frame(gv);
        }
        CHECK(config::gameRenderW == 8192, "+100 twice from 8000 stops at 8192");
        CHECK(config::gameRenderH == 1080, "the width buttons leave the height alone");
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

    //at: scroll the page that far first (1: to its end, 0: not at all), and hover only the page (what lies below the first screen)
    std::set<ImGuiID> hovered;
    ImGuiID pageId = 0;
    auto scan = [&](const char* what, float at) {
        const bool bottom = at > 0;
        g_MenuOpen = true;
        frame([] { DrawMenu(); }); frame([] { DrawMenu(); });
        ImGuiWindow* mw = ImGui::FindWindowByName("kwin-helper");
        ImGuiWindow* page = nullptr;
        for (ImGuiWindow* win : G->Windows) if (std::strstr(win->Name, "##page")) page = win;
        pageId = page ? page->ID : 0;
        float x0 = mw->Pos.x + 4;
        if (bottom && page) {
            ImGui::SetScrollY(page, page->ScrollMax.y * at);
            frame([] { DrawMenu(); }); frame([] { DrawMenu(); });
            x0 = page->Pos.x + 2;
        }
        int hits = 0;
        hovered.clear();
        for (float y = mw->Pos.y + 2; y < mw->Pos.y + mw->Size.y; y += 5)
            for (float x = x0; x < mw->Pos.x + mw->Size.x; x += 9) {
                io.AddMousePosEvent(x, y);
                frame([] { DrawMenu(); }); frame([] { DrawMenu(); });
                if (G->DebugDrawIdConflictsId != 0) hits++;
                hovered.insert(G->HoveredId);
            }
        io.AddMousePosEvent(-1000, -1000);
        if (page) ImGui::SetScrollY(page, 0); //the pages share one scroll position
        frame([] { DrawMenu(); }); frame([] { DrawMenu(); });
        char msg[112]; std::snprintf(msg, sizeof msg, "no conflicting IDs when hovering the menu (%s%s)", what, at >= 1 ? ", scrolled to the end" : bottom ? ", scrolled part way" : "");
        CHECK(hits == 0, msg);
    };
    auto sw = [&](const char* label) { return ImGui::GetIDWithSeed("##sw", nullptr, ImGui::GetIDWithSeed(label, nullptr, pageId)); };
    auto choice = [&](const char* label) { return ImGui::GetIDWithSeed("##c", nullptr, ImGui::GetIDWithSeed(label, nullptr, pageId)); };
    for (int tab = 0; tab < MENU_TAB_COUNT; tab++) {
        g_MenuTab = tab;
        scan(MENU_TABS[tab], 0);
        if (tab == 1) CHECK(hovered.count(sw("Ground items")) && hovered.count(sw("Guns")), "Loot tab: the mouse reaches Ground items and Guns");
    }
    g_MenuTab = 1;
    scan("Loot", 1);
    CHECK(hovered.count(sw("Containers")) && hovered.count(sw("Other##box")) && hovered.count(sw("Medical##box")),
          "Loot tab scrolled to the end: the mouse reaches the container switches");
    //the ESP tab's BOXES and SKELETON parts lie below its first screen
    g_MenuTab = 0;
    std::set<ImGuiID> espSeen;
    for (float at : {0.5f, 0.75f, 1.0f}) { scan("ESP", at); espSeen.insert(hovered.begin(), hovered.end()); }
    CHECK(espSeen.count(choice("Box style")) && espSeen.count(sw("Skeleton on players")) && espSeen.count(choice("Head marker")) &&
          espSeen.count(choice("Skeleton colour")) && espSeen.count(sw("Dim players behind cover")),
          "ESP tab scrolled down: the mouse reaches Box style and every skeleton option");

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

    //the High visibility preset: every value it sets is inside its saved range, and it is written to settings.txt
    {
        settings::defaults(); //remember the config.h values before anything changes them
        std::error_code ec;
        std::filesystem::remove(settings::path(), ec); //$HOME is the test folder (set above)
        ApplyHighVisibility();
        CHECK(config::labelSize == 28.0f && config::boxThickness >= 4.0f && config::labelCards && config::boxStyle == 1 &&
              config::overlayOpacity == 1.0f && config::menuOpacity == 1.0f && config::hiddenAlpha >= 200, "High visibility sets the readable values");
        settings::clampAll();
        CHECK(config::labelSize == 28.0f && config::boxThickness >= 4.0f && config::hiddenAlpha >= 200, "High visibility values are inside their safe ranges");
        CHECK(std::filesystem::exists(settings::path()), "High visibility is saved at once");
        config::boxThickness = 8.0f; config::hiddenAlpha = 255;
        ApplyHighVisibility();
        CHECK(config::boxThickness == 8.0f && config::hiddenAlpha == 255, "High visibility never makes thicker lines thinner or solid hidden players fainter");
        config::showItemValues = true;
        ApplyHighVisibility();
        ResetToHighVisibility();
        CHECK(config::showItemValues && config::labelSize == 28.0f, "High visibility and Reset to high visibility keep Show item values");
        settings::resetAll();
        CHECK(!config::showItemValues, "Reset all settings turns Show item values off");
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
