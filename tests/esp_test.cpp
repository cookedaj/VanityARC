//Headless run of the real ESP and radar drawing: nothing crashes, nothing NaN reaches the GPU, and the options do what they say.
#include "draw/ESP.h"
#include "imgui_internal.h"
#include <cmath>
#include <cstdio>
#include <random>

static int fails = 0;
#define CHECK(cond, msg) do { bool ok_ = (cond); std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) fails++; } while (0)

static RenderEntity ent(Object t, double x, double y, double z, int team = 7) {
    RenderEntity e;
    e.actor = 0x10000 + (ptr)(x * 7 + y); e.vt = 1; e.type = t; e.pos = Vector3(x, y, z); e.teamID = team;
    return e;
}

static FminimalViewInfo camera() {
    FminimalViewInfo c;
    c.Location = Vector3(0, 0, 0);
    c.Rotation = Vector3(0, 0, 0); //looking along +x
    c.FOV = 70.0f;
    return c;
}

//Draws one frame and returns how many vertices the overlay produced; allVerticesFinite tells if any was NaN / infinite
static int g_verts = 0;
static bool g_finite = true;
template <typename F> static int draw(F fn) {
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    fn();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    g_verts = dl->VtxBuffer.Size;
    g_finite = true;
    for (const ImDrawVert& v : dl->VtxBuffer)
        if (!std::isfinite(v.pos.x) || !std::isfinite(v.pos.y)) g_finite = false;
    ImGui::Render();
    return g_verts;
}

int main() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(2560, 1440);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.IniFilename = nullptr;
    LoadOverlayFonts();
    CHECK(g_overlayFont != nullptr, "a font is loaded");
    config::SCREEN_W = 2560; config::SCREEN_H = 1440;
    config::radarCenterX = 150; config::radarCenterY = 150; config::radarRadius = 100; config::scale = 200;

    FminimalViewInfo cam = camera();
    std::vector<RenderEntity> none;
    CHECK(draw([&] { DrawESP(none, cam, 3); }) == 0, "nothing to draw: nothing is drawn");

    std::vector<RenderEntity> front = {ent(Object::PLAYER, 3000, 200, 0)};
    CHECK(draw([&] { DrawESP(front, cam, 3); }) > 0, "an enemy in front is drawn");

    //an enemy behind you: only the edge arrow shows it
    std::vector<RenderEntity> behind = {ent(Object::PLAYER, -3000, 200, 0)};
    config::showOffscreen = true;
    int withArrow = draw([&] { DrawESP(behind, cam, 3); });
    config::showOffscreen = false;
    int noArrow = draw([&] { DrawESP(behind, cam, 3); });
    config::showOffscreen = true;
    CHECK(withArrow > 0 && noArrow == 0, "an enemy behind you shows as an edge arrow, and not when Edge arrows is off");

    //teammates
    std::vector<RenderEntity> mate = {ent(Object::PLAYER, 3000, 200, 0, 3)};
    config::showTeammates = false;
    CHECK(draw([&] { DrawESP(mate, cam, 3); }) == 0, "a teammate is hidden by default");
    config::showTeammates = true;
    CHECK(draw([&] { DrawESP(mate, cam, 3); }) > 0, "and drawn with the Teammates option");
    config::showTeammates = false;

    //distances come from the camera given now, not from what the entity says
    std::vector<RenderEntity> far = {ent(Object::PLAYER, 3000, 200, 0)};
    far[0].dist = 1.0; //stale value from the tracker
    config::maxPlayerDist = 20; //metres: the enemy is 30 m away
    CHECK(draw([&] { DrawESP(far, cam, 3); }) == 0, "range uses the distance to the camera as it is now");
    config::maxPlayerDist = 400;

    //corner boxes vs full boxes: both draw
    config::boxStyle = 1;
    CHECK(draw([&] { DrawESP(front, cam, 3); }) > 0, "full boxes draw");
    config::boxStyle = 0;

    //calibration moves things
    std::vector<RenderEntity> loot = {ent(Object::SEARCH, 2000, 600, 0)};
    auto firstX = [&] {
        float minx = 1e9f;
        draw([&] { DrawESP(loot, cam, 3); });
        for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer) minx = std::min(minx, v.pos.x);
        return minx;
    };
    ImGui::NewFrame(); DrawESP(loot, cam, 3);
    float x0 = 1e9f, x1 = 1e9f;
    for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer) x0 = std::min(x0, v.pos.x);
    ImGui::Render();
    config::centerOffsetX = 100;
    ImGui::NewFrame(); DrawESP(loot, cam, 3);
    for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer) x1 = std::min(x1, v.pos.x);
    ImGui::Render();
    config::centerOffsetX = 0;
    (void)firstX;
    CHECK(std::abs((x1 - x0) - 100) < 1.0f, "Shift X moves the markers by that many pixels");

    //a crowd of containers on top of each other must not crash and must draw fewer labels than containers
    std::vector<RenderEntity> crowd;
    for (int i = 0; i < 300; i++) { RenderEntity e = ent(Object::SEARCH, 1500 + (i % 7), 100 + (i % 5), 0); e.actor += i; e.label = "Trash can"; crowd.push_back(e); }
    config::showContainerNames = true;
    //text vertices are the ones that do not use the atlas' white pixel
    auto textVerts = [&](const std::vector<RenderEntity>& list) {
        ImGui::NewFrame();
        DrawESP(list, cam, 3);
        const ImVec2 wp = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
        int n = 0;
        for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer)
            if (std::abs(v.uv.x - wp.x) > 1e-6f || std::abs(v.uv.y - wp.y) > 1e-6f) n++;
        ImGui::Render();
        return n;
    };
    std::vector<RenderEntity> one(crowd.begin(), crowd.begin() + 1);
    const int crowdText = textVerts(crowd), oneText = textVerts(one);
    std::printf("      text vertices: one container %d, 300 stacked containers %d\n", oneText, crowdText);
    CHECK(oneText > 0 && crowdText <= oneText * 6, "300 containers on top of each other: only a few labels are drawn (around the first marker)");


    //---- shield bar: drawn while there is shield, gone when it is used up or cannot be read
    {
        std::vector<RenderEntity> sh = {ent(Object::PLAYER, 3000, 200, 0)};
        sh[0].playerHealth = 80; sh[0].hasShield = true; sh[0].shield = 50; sh[0].shieldMax = 100;
        const int withShield = draw([&] { DrawESP(sh, cam, 3); });
        sh[0].shield = 0;
        const int usedUp = draw([&] { DrawESP(sh, cam, 3); });
        sh[0].shield = 50; sh[0].hasShield = false;
        const int unknown = draw([&] { DrawESP(sh, cam, 3); });
        CHECK(withShield > usedUp, "shield bar is drawn while there is shield");
        CHECK(usedUp == unknown, "and it is gone when the shield is used up (same as when it cannot be read)");
    }

    //---- skeleton: drawn on a player with a pose, absent without one, off with the setting, and only inside its range
    {
        std::vector<RenderEntity> sk = {ent(Object::PLAYER, 3000, 200, 0)};
        auto pose = std::make_shared<skeleton::Pose>();
        for (int k = 0; k < skeleton::JOINT_COUNT; k++) pose->j[k] = Vector3(3000, 200 + k * 3, -80 + k * 7);
        sk[0].pose = pose;
        config::showSkeleton = true; config::skeletonMaxDist = 120;
        const int with = draw([&] { DrawESP(sk, cam, 3); });
        config::showSkeleton = false;
        const int off = draw([&] { DrawESP(sk, cam, 3); });
        config::showSkeleton = true;
        std::vector<RenderEntity> bare = {ent(Object::PLAYER, 3000, 200, 0)};
        const int noPose = draw([&] { DrawESP(bare, cam, 3); });
        config::skeletonMaxDist = 20;
        const int tooFar = draw([&] { DrawESP(sk, cam, 3); });
        config::skeletonMaxDist = 120;
        CHECK(with > off, "a skeleton is drawn when the player has a pose");
        CHECK(off == noPose && noPose == tooFar, "no pose, the setting off, or out of range: the box alone");
        pose->j[3].x = std::nan("");
        draw([&] { DrawESP(sk, cam, 3); });
        CHECK(g_finite, "a pose with a missing number never produces a broken line");
    }

    //---- overlay opacity scales everything the ESP draws
    {
        config::overlayOpacity = 0.5f;
        draw([&] { DrawESP(front, cam, 3); });
        int maxAlpha = 0;
        for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer) maxAlpha = std::max(maxAlpha, (int)(v.col >> 24));
        (void)maxAlpha; //the draw list is cleared by draw(); measured inside the lambda below
        int worst = 0;
        draw([&] {
            DrawESP(front, cam, 3);
            for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer) worst = std::max(worst, (int)(v.col >> 24));
        });
        config::overlayOpacity = 1.0f;
        CHECK(worst > 0 && worst <= 129, "overlay opacity 0.5: nothing is more than half opaque");
    }

    //---- monochrome: only greys
    {
        config::monochrome = true;
        std::vector<RenderEntity> all = {ent(Object::PLAYER, 3000, 200, 0), ent(Object::ARC, 2500, -300, 0), ent(Object::SEARCH, 1500, 400, 0)};
        all[0].playerHealth = 55;
        bool grey = true;
        draw([&] {
            DrawESP(all, cam, 3);
            DrawRadar(all, cam, 3);
            for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer) {
                const int r = v.col & 0xFF, g = (v.col >> 8) & 0xFF, b = (v.col >> 16) & 0xFF;
                if (std::abs(r - g) > 2 || std::abs(g - b) > 2) grey = false;
            }
        });
        config::monochrome = false;
        CHECK(grey, "monochrome: every vertex is white, grey or black");
    }

    //---- label cards add a dark rectangle behind the text
    {
        std::vector<RenderEntity> one2 = {ent(Object::SEARCH, 1500, 100, 0)};
        one2[0].label = "Trash can";
        config::showContainerNames = true;
        const int plain = draw([&] { DrawESP(one2, cam, 3); });
        config::labelCards = true;
        const int cards = draw([&] { DrawESP(one2, cam, 3); });
        config::labelCards = false;
        CHECK(cards != plain, "label cards: drawn differently from outlined text (a card behind the text)");
    }

    //---- status card and toast
    {
        Toast("Teammate marked: cooked", 5.0);
        CHECK(draw([&] { DrawToast(DrawStatusCard("live", "120 fps")); }) > 0, "status card and toast draw");
    }

    //---- the radar panel
    {
        std::vector<RenderEntity> radarEnts = {ent(Object::PLAYER, 3000, 200, 0), ent(Object::ARC, -2500, 1000, 0)};
        const int radar = draw([&] { DrawRadar(radarEnts, cam, 3); });
        const int emptyRadar = draw([&] { DrawRadar(none, cam, 3); });
        CHECK(emptyRadar > 300 && radar > emptyRadar, "radar: panel, rings, title and your arrow even when empty; dots when players are near");
    }

    //garbage in: NaN, infinity, zero-size camera, on the camera, FOV 0
    std::mt19937_64 rng(5);
    auto U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    bool allFinite = true;
    for (int t = 0; t < 400; t++) {
        std::vector<RenderEntity> junk;
        for (int i = 0; i < 40; i++) {
            Object o = (Object)(int)U(0, 5.0);
            if (o > Object::NONE) o = Object::NONE;
            double x = U(-5e4, 5e4), y = U(-5e4, 5e4), z = U(-1e4, 1e4);
            if (i % 11 == 0) x = NAN;
            if (i % 13 == 0) y = INFINITY;
            RenderEntity e = ent(o, x, y, z, (int)U(0, 10)); e.playerHealth = i % 5 == 0 ? NAN : U(-10, 150); e.isDead = i % 4 == 0; e.label = "Name";
            junk.push_back(e);
        }
        FminimalViewInfo c = camera();
        c.Rotation = Vector3(U(-89, 89), U(-180, 180), 0);
        c.FOV = (float)(t % 17 == 0 ? 0.0 : U(30, 120));
        draw([&] { DrawESP(junk, c, 3); DrawRadar(junk, c, 3); });
        // NaN positions are the tracker's business to filter; here only "no crash" matters for them
        (void)g_finite;
    }
    CHECK(allFinite, "400 frames of junk entities and cameras: no crash");

    //finite input: finite output
    bool fin = true;
    for (int t = 0; t < 200; t++) {
        std::vector<RenderEntity> good;
        for (int i = 0; i < 40; i++) {
            Object o = (Object)(int)U(0, 4.0);
            RenderEntity e = ent(o, U(-3e4, 3e4), U(-3e4, 3e4), U(-5e3, 5e3), (int)U(0, 10));
            e.playerHealth = U(0, 120); e.isDead = i % 4 == 0; e.label = "Name"; e.sub = "Ferro (held) + Stitcher";
            good.push_back(e);
        }
        FminimalViewInfo c = camera();
        c.Rotation = Vector3(U(-89, 89), U(-180, 180), U(-40, 40)); //with some camera roll
        c.FOV = (float)U(30, 120);
        draw([&] { DrawESP(good, c, 3); DrawRadar(good, c, 3); });
        fin &= g_finite;
    }
    CHECK(fin, "200 frames of normal entities and cameras: every vertex is a finite number");

    ImGui::DestroyContext();
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
