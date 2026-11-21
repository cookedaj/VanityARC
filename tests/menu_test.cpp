//Headless run of the real Insert menu: draws every frame, then types into a text box through the key queue.
#include "draw/Menu.h"
#include "imgui_internal.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
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
    g_MenuTab = 3; //the Names page, with the rename lists
    for (int i = 0; i < 3; i++) frame([] { DrawMenu(); });
    CHECK(true, "menu draws the Names page");

    //typing: a text box that is made active, then keys arrive through the same queue the keyboard thread fills
    char buf[48] = "";
    bool active = false;
    auto box = [&] {
        ImGui::Begin("typebox");
        if (!active) { ImGui::SetKeyboardFocusHere(); active = true; }
        ImGui::InputText("##t", buf, sizeof buf);
        ImGui::End();
    };
    int n = 0;
    while (!io.WantTextInput && n++ < 12) frame(box);
    std::printf("frames until active: %d, activeId %u\n", n, ImGui::GetCurrentContext()->ActiveId);
    CHECK(io.WantTextInput, "an active text box asks for typing");

    MenuFeedKeys(); //sets the keyboard grab flag
    CHECK(g_TextGrab.load(), "keyboard grab requested while typing");

    auto press = [&](int code, bool shift = false) {
        std::lock_guard<std::mutex> lk(g_KeyMutex);
        if (shift) g_KeyQueue.push_back({KEY_LEFTSHIFT, 1});
        g_KeyQueue.push_back({code, 1});
        g_KeyQueue.push_back({code, 0});
        if (shift) g_KeyQueue.push_back({KEY_LEFTSHIFT, 0});
    };
    press(KEY_C, true); press(KEY_O); press(KEY_O); press(KEY_K); press(KEY_E); press(KEY_D); press(KEY_X);
    MenuFeedKeys(); for (int i = 0; i < 30; i++) frame(box); //ImGui hands queued keys out over a few frames
    std::printf("typed: '%s'\n", buf);
    CHECK(std::strcmp(buf, "Cookedx") == 0, "typed text arrives in the box (shift gives a capital)");

    press(KEY_BACKSPACE);
    MenuFeedKeys(); for (int i = 0; i < 10; i++) frame(box);
    CHECK(std::strcmp(buf, "Cooked") == 0, "backspace deletes a character");

    press(KEY_ENTER);
    MenuFeedKeys(); for (int i = 0; i < 10; i++) frame(box);
    MenuFeedKeys(); //one more pass: nothing active now
    CHECK(!io.WantTextInput && !g_TextGrab.load(), "Enter finishes typing and releases the keyboard");

    //typed keys are ignored (and not kept) when no text box is active
    press(KEY_A);
    MenuFeedKeys();
    { std::lock_guard<std::mutex> lk(g_KeyMutex); CHECK(g_KeyQueue.empty(), "key queue is drained when nobody is typing"); }

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

    auto scan = [&](const char* what) {
        g_MenuOpen = true;
        frame([] { DrawMenu(); }); frame([] { DrawMenu(); });
        ImGuiWindow* mw = ImGui::FindWindowByName("kwin-helper");
        int hits = 0;
        for (float y = mw->Pos.y + 2; y < mw->Pos.y + mw->Size.y; y += 5)
            for (float x = mw->Pos.x + 4; x < mw->Pos.x + mw->Size.x; x += 9) {
                io.AddMousePosEvent(x, y);
                frame([] { DrawMenu(); }); frame([] { DrawMenu(); });
                if (G->DebugDrawIdConflictsId != 0) hits++;
            }
        io.AddMousePosEvent(-1000, -1000);
        frame([] { DrawMenu(); }); frame([] { DrawMenu(); });
        char msg[96]; std::snprintf(msg, sizeof msg, "no conflicting IDs when hovering the menu (%s)", what);
        CHECK(hits == 0, msg);
    };
    for (int tab = 0; tab < MENU_TAB_COUNT; tab++) {
        g_MenuTab = tab;
        if (tab == 3) { //the Names page has four lists of its own
            for (int sub = 0; sub < 4; sub++) {
                g_RenameTab = sub; frame([] { DrawMenu(); }); frame([] { DrawMenu(); }); g_RenameTab = -1;
                char what[48]; std::snprintf(what, sizeof what, "Names, list %d", sub);
                scan(what);
            }
        } else {
            scan(MENU_TABS[tab]);
        }
    }

    //the player list and an inventory window next to the menu draw without clashing IDs either
    g_MenuTab = 1;
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

    ImGui::DestroyContext();
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
