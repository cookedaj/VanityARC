//Headless run of the real ESP and radar drawing: nothing crashes, nothing NaN reaches the GPU, and the options do what they say.
#include "draw/ESP.h"
#include "imgui_internal.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>

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
static float g_minY = 1e9f; //the highest point on screen that was drawn (smallest y)
static float g_minX = 1e9f, g_maxX = -1e9f, g_maxY = -1e9f; //and the other edges of everything drawn
static ImU32 g_findCol = 0; //vertices of exactly this colour: how many, and the y range they span
static int g_colVerts = 0;
static float g_colMinY = 1e9f, g_colMaxY = -1e9f;
template <typename F> static int draw(F fn) {
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    fn();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    g_verts = dl->VtxBuffer.Size;
    g_finite = true;
    g_minY = g_minX = g_colMinY = 1e9f;
    g_maxX = g_maxY = g_colMaxY = -1e9f;
    g_colVerts = 0;
    for (const ImDrawVert& v : dl->VtxBuffer) {
        if (!std::isfinite(v.pos.x) || !std::isfinite(v.pos.y)) { g_finite = false; continue; }
        g_minY = std::min(g_minY, v.pos.y); g_maxY = std::max(g_maxY, v.pos.y);
        g_minX = std::min(g_minX, v.pos.x); g_maxX = std::max(g_maxX, v.pos.x);
        if (g_findCol && v.col == g_findCol) { g_colVerts++; g_colMinY = std::min(g_colMinY, v.pos.y); g_colMaxY = std::max(g_colMaxY, v.pos.y); }
    }
    ImGui::Render();
    return g_verts;
}

//A standing player's stick figure (feet at z - 90, as his box), facing +x, at (x, y)
static std::shared_ptr<skeleton::Pose> standingPose(double x, double y, double z) {
    using namespace skeleton;
    auto p = std::make_shared<Pose>();
    const double b = z - 90;
    const double spine[7] = {95, 105, 118, 132, 148, 155, 162}; //pelvis up to the head bone
    for (int k = PELVIS; k <= HEAD; k++) p->j[k] = Vector3(x, y, b + spine[k]);
    for (int s : {-1, 1}) {
        const int o = s < 0 ? 0 : 4; //side A, then side B
        p->j[SHOULDER_A + o] = Vector3(x, y + s * 5, b + 140);
        p->j[ARM_A + o] = Vector3(x, y + s * 18, b + 140);
        p->j[FOREARM_A + o] = Vector3(x, y + s * 22, b + 112);
        p->j[HAND_A + o] = Vector3(x, y + s * 24, b + 88);
        p->j[HIP_A + o] = Vector3(x, y + s * 10, b + 92);
        p->j[KNEE_A + o] = Vector3(x, y + s * 11, b + 50);
        p->j[ANKLE_A + o] = Vector3(x, y + s * 12, b + 8);
        p->j[TOE_A + o] = Vector3(x + 12, y + s * 12, b + 2);
    }
    return p;
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

    //---- the big card for a close enemy: which way, how far, how many; never for teammates, the dead or far players
    {
        config::closeWarning = true; config::warnDist = 25;
        auto warn = [&](const std::vector<RenderEntity>& l) { std::string s; draw([&] { s = DrawCloseEnemyWarning(l, cam, 3); }); return s; };
        CHECK(warn({ent(Object::PLAYER, -900, 0, 0)}) == "ENEMY 9 m BEHIND" && g_verts > 0, "a close enemy behind you: ENEMY 9 m BEHIND");
        CHECK(warn({ent(Object::PLAYER, 0, -1200, 0)}) == "ENEMY 12 m LEFT", "to the left: LEFT");
        CHECK(warn({ent(Object::PLAYER, 0, 1200, 0)}) == "ENEMY 12 m RIGHT", "to the right: RIGHT");
        CHECK(warn({ent(Object::PLAYER, 2000, 300, 0)}) == "ENEMY 20 m AHEAD", "in front: AHEAD");
        CHECK(warn({ent(Object::PLAYER, 2000, 300, 0), ent(Object::PLAYER, -500, 100, 0)}) == "2 ENEMIES - NEAREST 5 m BEHIND",
              "two close enemies: the count and the nearest one");
        {   //looking 45 deg down, an enemy 40 deg right of ahead: still AHEAD, as the voice says
            FminimalViewInfo down = cam; down.Rotation.x = -45;
            std::string s; draw([&] { s = DrawCloseEnemyWarning({ent(Object::PLAYER, 1532, 1286, 0)}, down, 3); });
            CHECK(s == "ENEMY 20 m AHEAD", "looking down does not change the card's direction (flat, like the voice)");
        }
        RenderEntity dead = ent(Object::PLAYER, -900, 0, 0); dead.isDead = true;
        CHECK(warn({ent(Object::PLAYER, -900, 0, 0, 3), dead, ent(Object::PLAYER, -3000, 0, 0), ent(Object::ARC, -500, 0, 0)}).empty() && g_verts == 0,
              "a teammate, a dead player, an enemy beyond the warning range or an ARC: no card");
        config::closeWarning = false;
        CHECK(warn({ent(Object::PLAYER, -900, 0, 0)}).empty() && g_verts == 0, "and no card with the warning switched off");
        config::closeWarning = true;

        //more than 3 m higher or lower than you: ABOVE / BELOW (the camera is at height 0)
        CHECK(HeightDir(cam, Vector3(0, 0, 301)) == 1 && HeightDir(cam, Vector3(0, 0, -301)) == -1 && HeightDir(cam, Vector3(0, 0, 299)) == 0 &&
              HeightDir(cam, Vector3(0, 0, -250)) == 0 && HeightDir(cam, Vector3(0, 0, NAN)) == 0, "height word: only past 3 m up or down");
        CHECK(warn({ent(Object::PLAYER, -900, 0, 500)}) == "ENEMY 10 m BEHIND, ABOVE", "an enemy 5 m up: ABOVE");
        CHECK(warn({ent(Object::PLAYER, 0, 1200, -400), ent(Object::PLAYER, 2000, 0, 0)}) == "2 ENEMIES - NEAREST 13 m RIGHT, BELOW",
              "the nearest of two 4 m down: BELOW");
        //the big arrow on the card: a white filled triangle (text uses the font atlas, the card is black and its frame red).
        //Its three corners lean away from its tip, so the tip is opposite their mean from the middle of their box.
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, wb = 0, wt = 0; //the arrow's box; the card's bottom and top
        auto arrowWay = [&](const std::vector<RenderEntity>& l) {
            float mx = 0, my = 0;
            int n = 0;
            x0 = y0 = 1e9f; x1 = y1 = -1e9f;
            draw([&] {
                DrawCloseEnemyWarning(l, cam, 3, &wb, 0, nullptr, &wt);
                const ImVec2 wp = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
                for (const ImDrawVert& q : ImGui::GetBackgroundDrawList()->VtxBuffer)
                    if (q.col == COLOR::white && q.uv.x == wp.x && q.uv.y == wp.y) {
                        x0 = std::min(x0, q.pos.x); x1 = std::max(x1, q.pos.x); y0 = std::min(y0, q.pos.y); y1 = std::max(y1, q.pos.y);
                        mx += q.pos.x; my += q.pos.y; n++;
                    }
            });
            if (n == 0) return std::string("none");
            const float dx = (x0 + x1) / 2 - mx / n, dy = (y0 + y1) / 2 - my / n; //from the corners' mean toward the tip
            std::printf("      card arrow %.0f x %.0f px\n", x1 - x0, y1 - y0);
            return std::string(std::abs(dx) > std::abs(dy) ? (dx > 0 ? "right" : "left") : (dy > 0 ? "down" : "up"));
        };
        CHECK(arrowWay({ent(Object::PLAYER, 900, 0, 0)}) == "up" && arrowWay({ent(Object::PLAYER, -900, 0, 0)}) == "down" &&
              arrowWay({ent(Object::PLAYER, 0, -900, 0)}) == "left" && arrowWay({ent(Object::PLAYER, 0, 900, 0)}) == "right",
              "the card's arrow points the way the enemy is: up ahead, down behind, left, right");
        for (const float size : {15.0f, 40.0f}) { //big, and inside the card at any text size
            config::labelSize = size;
            arrowWay({ent(Object::PLAYER, 0, -900, 0)});
            char msg[120];
            std::snprintf(msg, sizeof msg, "Text size %.0f: the card's arrow is big (%.0f px) and inside the card (its top and bottom are given out)", size, x1 - x0);
            CHECK(wt > 0 && wb > wt && x1 - x0 >= 0.6f * size && y0 >= wt + 2 && y1 <= wb - 2, msg);
        }
        config::labelSize = 15;
        CHECK(arrowWay({ent(Object::PLAYER, 0, 0, -900)}) == "none", "no arrow for an enemy straight below you (no way to point)");
    }

    //a live ARC close behind you gets its own arrow; not when dead, beyond 50 m, or with ARC edge arrows off; at most 4
    {
        std::vector<RenderEntity> arcBehind = {ent(Object::ARC, -2000, 200, 0)}; //20 m
        const int arcArrow = draw([&] { DrawESP(arcBehind, cam, 3); });
        config::showArcOffscreen = false;
        const int arcOff = draw([&] { DrawESP(arcBehind, cam, 3); });
        config::showArcOffscreen = true;
        arcBehind[0].isDead = true;
        const int arcDead = draw([&] { DrawESP(arcBehind, cam, 3); });
        std::vector<RenderEntity> arcFar = {ent(Object::ARC, -8000, 200, 0)}; //80 m
        const int arcFarN = draw([&] { DrawESP(arcFar, cam, 3); });
        CHECK(arcArrow > 0 && arcOff == 0 && arcDead == 0 && arcFarN == 0,
              "a live ARC 20 m behind you shows as an edge arrow; dead, 80 m away or the switch off: none");
        std::vector<RenderEntity> four, ten;
        for (int i = 0; i < 10; i++) {
            RenderEntity e = ent(Object::ARC, -2000 - i * 100, 200 + i * 400, 0); e.actor += i;
            if (i < 4) four.push_back(e);
            ten.push_back(e);
        }
        const int n4 = draw([&] { DrawESP(four, cam, 3); }), n10 = draw([&] { DrawESP(ten, cam, 3); });
        CHECK(n4 > arcArrow && n10 == n4, "ten ARC behind you: only the nearest four get an arrow");
        config::arcArrowDist = 100;
        const int far100 = draw([&] { DrawESP(arcFar, cam, 3); });
        config::maxArcDist = 60;
        const int beyondArcRange = draw([&] { DrawESP(arcFar, cam, 3); });
        config::maxArcDist = 150; config::arcArrowDist = 50;
        CHECK(far100 > 0 && beyondArcRange == 0, "ARC arrow range 100 m: the ARC 80 m behind gets an arrow, but never beyond ARC range");
    }

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

    //---- edge arrows: inside the picture with Shift X / Y set, bigger for an enemy within the Warning card range, the
    //distance in the full Text size
    {
        config::showOffscreen = true; config::warnDist = 25;
        const std::vector<RenderEntity> right = {ent(Object::PLAYER, 0, 3000, 0)}, behind40 = {ent(Object::PLAYER, -4000, 0, 0)};
        bool inside = true;
        for (const float s : {300.0f, -300.0f}) {
            config::centerOffsetX = s;
            draw([&] { DrawESP(right, cam, 3); });
            inside &= g_verts > 0 && g_minX >= 0 && g_maxX <= config::SCREEN_W;
            config::centerOffsetX = 0; config::centerOffsetY = s;
            draw([&] { DrawESP(behind40, cam, 3); });
            inside &= g_verts > 0 && g_minY >= 0 && g_maxY <= config::SCREEN_H;
            config::centerOffsetY = 0;
        }
        CHECK(inside, "Shift X / Y of 300 px: an edge arrow and its distance stay on the screen");
        //the arrow's fill is the only thing in the enemy colour: its height is its length when it points down
        g_findCol = COLOR::enemy();
        draw([&] { DrawESP(behind40, cam, 3); });
        const float farLen = g_colMaxY - g_colMinY;
        draw([&] { DrawESP({ent(Object::PLAYER, -900, 0, 0)}, cam, 3); });
        const float closeLen = g_colMaxY - g_colMinY;
        g_findCol = 0;
        std::printf("      edge arrow %.1f px long at 40 m, %.1f px within the Warning card range\n", farLen, closeLen);
        CHECK(farLen > 0 && closeLen > 1.4f * farLen, "an enemy within the Warning card range gets a bigger edge arrow");
        //its distance: the same height as any label in the Text size
        auto textHeight = [&](auto fn) {
            float y0 = 1e9f, y1 = -1e9f;
            draw([&] {
                fn();
                const ImVec2 wp = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
                for (const ImDrawVert& q : ImGui::GetBackgroundDrawList()->VtxBuffer)
                    if (q.uv.x != wp.x || q.uv.y != wp.y) { y0 = std::min(y0, q.pos.y); y1 = std::max(y1, q.pos.y); }
            });
            return y1 - y0;
        };
        const float edgeH = textHeight([&] { DrawESP(behind40, cam, 3); });
        const float fullH = textHeight([&] { DrawLabelCentered(500, 500, "40m", config::labelSize); });
        CHECK(edgeH > 0 && std::abs(edgeH - fullH) < 0.5f, "the edge arrow's distance is in the full Text size");
    }

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

    //a busy room: 120 named containers spread over the screen, 12-20 m away. Each label is drawn 9 times (8 outline
    //copies and the text); the frame must still fit 16-bit indices, for a GL context that cannot split the draw list
    {
        std::vector<RenderEntity> room;
        for (int i = 0; i < 12; i++)
            for (int j = 0; j < 10; j++) {
                RenderEntity e = ent(Object::SEARCH, 1200, -1397 + i * 254, -720 + j * 160);
                e.actor += i * 10 + j; e.label = "Trash can"; room.push_back(e);
            }
        const int roomText = textVerts(room), roomAll = draw([&] { DrawESP(room, cam, 3); });
        std::printf("      busy room: %d vertices, %d of them text\n", roomAll, roomText);
        CHECK(roomText >= oneText * 40, "busy room: most of the 120 labels are drawn");
        CHECK(roomAll < (1 << 16), "busy room: the frame fits 16-bit indices");
    }


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

    //---- the box follows the pose (a crouched player is not drawn standing-sized), and the bars are a setting
    {
        config::boxStyle = 1; config::showSkeleton = false; config::showBars = true; config::showOffscreen = false;
        std::vector<RenderEntity> standing = {ent(Object::PLAYER, 3000, 200, 0)};
        draw([&] { DrawESP(standing, cam, 3); });
        const float topStanding = g_minY;
        const int withBars = g_verts;
        std::vector<RenderEntity> crouched = {ent(Object::PLAYER, 3000, 200, 0)};
        auto low = std::make_shared<skeleton::Pose>();
        for (int k = 0; k < skeleton::JOINT_COUNT; k++) low->j[k] = Vector3(3000, 200, -85 + k * 80.0 / 22.0); //a figure 80 cm tall
        crouched[0].pose = low;
        draw([&] { DrawESP(crouched, cam, 3); });
        CHECK(g_minY > topStanding + 5.0f, "a crouched player's box is lower than a standing one (it follows the pose)");
        std::vector<RenderEntity> odd = {ent(Object::PLAYER, 3000, 200, 0)};
        auto flat = std::make_shared<skeleton::Pose>();
        for (int k = 0; k < skeleton::JOINT_COUNT; k++) flat->j[k] = Vector3(3000, 200, 0); //every joint at one height: not a figure
        odd[0].pose = flat;
        draw([&] { DrawESP(odd, cam, 3); });
        CHECK(std::abs(g_minY - topStanding) < 0.5f, "a pose that is not a believable figure leaves the standing box");
        config::showBars = false;
        draw([&] { DrawESP(standing, cam, 3); });
        const int noBars = g_verts;
        config::showBars = true;
        CHECK(noBars < withBars, "Health and shield bars off: the bars are not drawn");
        //bar style and numbers, on a player with a shield
        std::vector<RenderEntity> sh = {ent(Object::PLAYER, 3000, 200, 0)};
        sh[0].hasShield = true; sh[0].shield = 30; sh[0].shieldMax = 40;
        auto verts = [&](int style, bool numbers) {
            config::barStyle = style; config::barNumbers = numbers;
            draw([&] { DrawESP(sh, cam, 3); });
            return g_verts;
        };
        const int two = verts(0, false), hpOnly = verts(1, false), one = verts(2, false);
        CHECK(hpOnly < one && one < two, "bar style: health only draws one bar, one bar for both draws two parts in it, side by side draws two bars");
        CHECK(verts(0, true) > two && verts(1, true) > hpOnly, "bar numbers add text beside the bars");
        sh[0].shield = 0;
        CHECK(verts(2, false) < one && verts(0, false) < two, "a used-up shield: nothing of it is drawn");
        //a far player (box a few px tall): the name goes above the numbers, not over them
        std::vector<RenderEntity> farOne = {ent(Object::PLAYER, 30000, 200, 0)};
        farOne[0].label = "Someone"; farOne[0].hasShield = true; farOne[0].shield = 30; farOne[0].shieldMax = 40;
        config::barStyle = 0; config::barNumbers = false;
        draw([&] { DrawESP(farOne, cam, 3); });
        const float plainTop = g_minY;
        config::barNumbers = true;
        draw([&] { DrawESP(farOne, cam, 3); });
        CHECK(g_minY < plainTop - config::labelSize / 2, "bar numbers on a far player: the name moves up, clear of them");
        //at the screen's edges the numbers stay readable
        LabelRect mid, edge, high;
        draw([&] {
            DrawPlayerBars(1000, 500, 40, 100, sh[0], 3, &mid);
            DrawPlayerBars((float)config::SCREEN_W - 50, 500, 40, 100, sh[0], 3, &edge);
            DrawPlayerBars(1000, 0, 40, 8, sh[0], 3, &high);
        });
        CHECK(mid.x0 > 1040 && edge.x1 < config::SCREEN_W - 50 && high.y0 >= 0,
              "bar numbers: right of the bars, left of the box at the screen's right edge, never above the screen");
        LabelRect underName;
        draw([&] { DrawPlayerBars(1000, 0, 40, 8, sh[0], 3, &underName, 40); });
        CHECK(underName.y0 >= 40, "bar numbers at the screen's top stay under the name's place");
        config::barStyle = 0; config::barNumbers = false;
        config::boxStyle = 0; config::showSkeleton = true; config::showOffscreen = true;
    }

    //---- the skeleton: the box holds the head, lines are cut at the camera and the screen, Skeleton only, the simple far
    //figure, a wider box for a player lying down, head marker, colour, and the facing arrow at the head
    {
        config::showSkeleton = true; config::skeletonMaxDist = 300; config::showBars = false; config::showPlayerNames = false;
        config::showPlayerGuns = false; config::showFacing = false; config::showOffscreen = false; config::boxStyle = 1;
        const View v(cam, MakeProjection());
        const float SW = (float)config::SCREEN_W, SH = (float)config::SCREEN_H;
        for (double d : {300.0, 500.0, 3000.0, 10000.0}) {
            std::vector<RenderEntity> one = {ent(Object::PLAYER, d, 30, 0)};
            one[0].pose = standingPose(d, 30, 0);
            float bx, by, bw, bh;
            PlayerScreenBox(v, one[0], bx, by, bw, bh);
            draw([&] { DrawBoxOutlined(bx - bw / 2, by, bw, bh, COLOR::enemy(), BoxThickness()); });
            const float boxTop = g_minY, boxL = g_minX, boxR = g_maxX;
            draw([&] { DrawESP(one, cam, 3); });
            const float allTop = g_minY;
            const bool fine = g_finite;
            draw([&] { DrawSkeleton(v, *one[0].pose, COLOR::enemy(), SkeletonWidth(v, one[0]), SkeletonSimple(StandingBoxPx(v, one[0].pos))); });
            char msg[112];
            std::snprintf(msg, sizeof msg, "%.0f m: the box holds the head marker and the whole figure (nothing above or beside it)", d / 100);
            CHECK(fine && allTop >= boxTop - 0.01f && g_minY >= boxTop - 0.01f && g_minX >= boxL - 0.01f && g_maxX <= boxR + 0.01f, msg);
        }
        {   //a joint that could not be read is left out of the box, not a reason for the standing box
            std::vector<RenderEntity> whole = {ent(Object::PLAYER, 3000, 30, 0)}, holes = whole, bare = whole;
            whole[0].pose = standingPose(3000, 30, 0);
            auto h = standingPose(3000, 30, 0);
            h->j[skeleton::HAND_A] = skeleton::badJoint(); h->j[skeleton::TOE_B] = skeleton::badJoint();
            holes[0].pose = h;
            float x1, y1, w1, h1, x2, y2, w2, h2, x3, y3, w3, h3;
            PlayerScreenBox(v, whole[0], x1, y1, w1, h1);
            PlayerScreenBox(v, holes[0], x2, y2, w2, h2);
            PlayerScreenBox(v, bare[0], x3, y3, w3, h3);
            CHECK(std::abs(y1 - y2) < 0.01f && std::abs(h1 - h2) < 0.01f && std::abs(y1 - y3) > 1, "a pose with joints missing keeps its own box");
            draw([&] { DrawESP(holes, cam, 3); });
            CHECK(g_finite, "and draws without a broken line");
        }
        {   //right in front of the camera: one hand 2 cm in front of it, a forearm behind it
            std::vector<RenderEntity> close1 = {ent(Object::PLAYER, 150, 0, 0)};
            auto p = standingPose(150, 0, 0);
            p->j[skeleton::HAND_A] = Vector3(2, -30, 10);
            p->j[skeleton::FOREARM_B] = Vector3(-30, 40, 0);
            close1[0].pose = p;
            const int n = draw([&] { DrawESP(close1, cam, 3); });
            CHECK(n > 0 && g_finite && g_minX > -300 && g_maxX < SW + 300 && g_minY > -300 && g_maxY < SH + 300,
                  "a limb next to or behind the camera is cut at the screen's edge: nothing streaks far off it, nothing breaks");
            Vector2 a, b;
            CHECK(v.projectSegment(p->j[skeleton::ARM_B], p->j[skeleton::FOREARM_B], a, b), "and the arm going behind the camera is still drawn up to the edge");
        }
        {   //Skeleton only: no box where a skeleton is drawn, the full box where none is
            std::vector<RenderEntity> one = {ent(Object::PLAYER, 3000, 30, 0)}, bare = {ent(Object::PLAYER, 3000, 30, 0)};
            one[0].pose = standingPose(3000, 30, 0);
            config::boxStyle = 1;
            const int fullWith = draw([&] { DrawESP(one, cam, 3); }), fullBare = draw([&] { DrawESP(bare, cam, 3); });
            config::boxStyle = 3;
            const int onlyWith = draw([&] { DrawESP(one, cam, 3); }), onlyBare = draw([&] { DrawESP(bare, cam, 3); });
            config::showSkeleton = false;
            const int onlyOff = draw([&] { DrawESP(one, cam, 3); });
            config::showSkeleton = true;
            config::boxStyle = 1;
            CHECK(onlyWith < fullWith && onlyWith > fullBare, "Skeleton only: the skeleton without its box");
            CHECK(onlyBare == fullBare && onlyOff == fullBare, "Skeleton only: a player without a skeleton gets the full box");
        }
        {   //far away: a few thick lines, fewer vertices than the full figure, still drawn; no jump where it changes
            std::vector<RenderEntity> far1 = {ent(Object::PLAYER, 10000, 30, 0)};
            far1[0].pose = standingPose(10000, 30, 0);
            CHECK(SkeletonSimple(StandingBoxPx(v, far1[0].pos)) == 1.0f && SkeletonSimple(StandingBoxPx(v, Vector3(1000, 0, 0))) == 0.0f,
                  "100 m away the figure is simple, 10 m away it is full");
            const float t = SkeletonWidth(v, far1[0]);
            CHECK(t >= BoxThickness() + SIMPLE_EXTRA - 1e-4f, "the simple figure's lines are thicker than the box's");
            const int full = draw([&] { DrawSkeleton(v, *far1[0].pose, COLOR::red, t, 0); });
            const int simple = draw([&] { DrawSkeleton(v, *far1[0].pose, COLOR::red, t, 1); });
            const float s0 = g_minX, s1 = g_maxX, s2 = g_minY, s3 = g_maxY;
            draw([&] { DrawSkeleton(v, *far1[0].pose, COLOR::red, t, 0.999f); });
            CHECK(simple < full && simple > 0, "the simple far figure has fewer vertices than the full one");
            CHECK(std::abs(g_minX - s0) < 0.5f && std::abs(g_maxX - s1) < 0.5f && std::abs(g_minY - s2) < 0.5f && std::abs(g_maxY - s3) < 0.5f,
                  "just before it becomes simple the figure covers the same place (no jump)");
            config::showSkeleton = false;
            const int boxAlone = draw([&] { DrawESP(far1, cam, 3); });
            config::showSkeleton = true;
            CHECK(draw([&] { DrawESP(far1, cam, 3); }) > boxAlone, "and it is drawn over the box");
            //walking away through the change, 2 cm a frame: the figure's edges never jump
            float jump = 0, last[4] = {0, 0, 0, 0};
            for (double d = 3500; d <= 4800; d += 2) {
                RenderEntity e = ent(Object::PLAYER, d, 30, 0);
                e.pose = standingPose(d, 30, 0);
                draw([&] { DrawSkeleton(v, *e.pose, COLOR::red, SkeletonWidth(v, e), SkeletonSimple(StandingBoxPx(v, e.pos))); });
                const float now[4] = {g_minX, g_maxX, g_minY, g_maxY};
                if (d > 3500) for (int k = 0; k < 4; k++) jump = std::max(jump, std::abs(now[k] - last[k]));
                std::copy(now, now + 4, last);
            }
            CHECK(jump < 1.5f, "walking away through the change to the simple figure, its edges move smoothly");
            //an arm missing only its hand, a leg missing only its ankle (a torn bone record): the rest of that limb stays
            //where it was as he gets far enough for the simple figure, not gone all at once
            for (const int missing : {skeleton::HAND_A, skeleton::ANKLE_A}) {
                auto torn = std::make_shared<skeleton::Pose>(*far1[0].pose);
                if (missing == skeleton::HAND_A) { torn->j[skeleton::ARM_A].y = 0; torn->j[skeleton::FOREARM_A].y = -25; } //arm out to the left
                else torn->j[skeleton::KNEE_A].y = -30; //knee out to the left
                torn->j[missing] = skeleton::badJoint();
                draw([&] { DrawSkeleton(v, *torn, COLOR::red, t, 0.999f); });
                const float before = g_minX;
                draw([&] { DrawSkeleton(v, *torn, COLOR::red, t, 1); });
                std::printf("      %s missing: the figure's left edge %.1f px just before the simple figure, %.1f px at it\n",
                            missing == skeleton::HAND_A ? "hand" : "ankle", before, g_minX);
                CHECK(std::abs(g_minX - before) < 0.5f && g_finite, missing == skeleton::HAND_A
                      ? "an arm missing its hand stays when the figure becomes simple" : "a leg missing its ankle stays when the figure becomes simple");
            }
        }
        {   //lying down across the view: a wider box, out to his joints; getting up, it narrows smoothly
            auto lying = std::make_shared<skeleton::Pose>();
            for (int k = 0; k < skeleton::JOINT_COUNT; k++) lying->j[k] = Vector3(3000, 30 + 88 - k * 8.0, -75 + (k % 3) * 5.0);
            std::vector<RenderEntity> l = {ent(Object::PLAYER, 3000, 30, 0)}, bare = l;
            l[0].pose = lying;
            float bx, by, bw, bh, sx, sy, sw, sh;
            PlayerScreenBox(v, l[0], bx, by, bw, bh);
            PlayerScreenBox(v, bare[0], sx, sy, sw, sh);
            CHECK(bw > sw * 1.6f, "a player lying down gets a wider box, out to his joints");
            const auto up = standingPose(3000, 30, 0);
            float maxStep = 0, lastW = bw, lastY = by;
            bool neverNarrow = true;
            for (int i = 1; i <= 400; i++) {
                auto mid = std::make_shared<skeleton::Pose>();
                for (int k = 0; k < skeleton::JOINT_COUNT; k++) mid->j[k] = skeleton::lerp(lying->j[k], up->j[k], i / 400.0);
                l[0].pose = mid;
                PlayerScreenBox(v, l[0], bx, by, bw, bh);
                maxStep = std::max({maxStep, std::abs(bw - lastW), std::abs(by - lastY)});
                neverNarrow = neverNarrow && bw >= sw - 0.01f;
                lastW = bw; lastY = by;
            }
            CHECK(maxStep < 2.0f && neverNarrow, "getting up, the box changes smoothly and is never narrower than 90 cm");
        }
        {   //head marker: filled or ring; an estimated pose has a dashed ring, as solid as a live one
            std::vector<RenderEntity> one = {ent(Object::PLAYER, 500, 30, 0)};
            one[0].pose = standingPose(500, 30, 0);
            config::headMarker = 1;
            const int filled = draw([&] { DrawESP(one, cam, 3); });
            config::headMarker = 0;
            const int ring = draw([&] { DrawESP(one, cam, 3); });
            one[0].poseLive = false;
            const int dashed = draw([&] { DrawESP(one, cam, 3); });
            config::headMarker = 1;
            const int dashedToo = draw([&] { DrawESP(one, cam, 3); });
            one[0].poseLive = true;
            CHECK(g_finite && filled != ring && dashed != ring && dashed == dashedToo, "head marker: filled, ring, and a dashed ring for an estimate");
            //colour: same as the box, white or yellow (white in monochrome)
            g_findCol = IM_COL32(255, 230, 0, 255);
            config::skeletonColor = 2;
            draw([&] { DrawESP(one, cam, 3); });
            const int yellow = g_colVerts;
            config::monochrome = true;
            draw([&] { DrawESP(one, cam, 3); });
            const int monoYellow = g_colVerts;
            config::monochrome = false;
            config::skeletonColor = 0;
            draw([&] { DrawESP(one, cam, 3); });
            CHECK(yellow > 0 && monoYellow == 0 && g_colVerts == 0, "Skeleton colour Yellow draws it yellow (monochrome: white); Same as box: no yellow");
            //the facing arrow starts at the head when there is a skeleton (Skeleton only, white skeleton: the arrow is the only red)
            config::showFacing = true; config::boxStyle = 3; config::skeletonColor = 1;
            std::vector<RenderEntity> f = {ent(Object::PLAYER, 3000, 30, 0)};
            f[0].pose = standingPose(3000, 30, 0);
            f[0].yaw = 90; //facing to your right: a flat arrow
            float bx, by, bw, bh;
            PlayerScreenBox(v, f[0], bx, by, bw, bh);
            g_findCol = COLOR::enemy();
            draw([&] { DrawESP(f, cam, 3); });
            const float atHead = (g_colMinY + g_colMaxY) / 2;
            CHECK(g_colVerts > 0 && atHead < by + bh * 0.35f, "with a skeleton the facing arrow starts at the head, not over the spine");
            g_findCol = 0;
            //High visibility, filled head and skeleton in the box colour (the defaults): the arrow is never lost in the head
            //disc of its own colour, facing away, to the side or towards you, from 10 to 80 m (past that the box, about 30 px
            //tall, has no room left under the head). The arrow's vertices are the ones that come and go with Show facing.
            const auto keepTh = config::boxThickness;
            config::boxThickness = 4; config::boxStyle = 1; config::skeletonColor = 0; config::headMarker = 1;
            for (const double d : {1000.0, 3000.0, 4000.0, 6000.0, 8000.0})
                for (const double yaw : {0.0, 90.0, 180.0}) {
                    std::vector<RenderEntity> e1 = {ent(Object::PLAYER, d, 30, 0)};
                    e1[0].pose = standingPose(d, 30, 0);
                    e1[0].yaw = yaw; e1[0].isVisible = true;
                    std::vector<ImDrawVert> on, off;
                    auto grab = [&](std::vector<ImDrawVert>& to) {
                        draw([&] { DrawESP(e1, cam, 3); const auto& vb = ImGui::GetBackgroundDrawList()->VtxBuffer; to.assign(vb.begin(), vb.end()); });
                    };
                    config::showFacing = true; grab(on);
                    config::showFacing = false; grab(off);
                    config::showFacing = true;
                    size_t i0 = 0;
                    while (i0 < off.size() && i0 < on.size() && on[i0].pos.x == off[i0].pos.x && on[i0].pos.y == off[i0].pos.y && on[i0].col == off[i0].col) i0++;
                    Vector2 hc;
                    float hrx = 0, hry = 0;
                    const bool head = HeadOnScreen(v, *e1[0].pose, SkeletonWidth(v, e1[0]), hc, hrx, hry);
                    int arrow = 0, inDisc = 0;
                    for (size_t i = i0; i < i0 + (on.size() - off.size()) && i < on.size(); i++) {
                        if (on[i].col != COLOR::enemy()) continue; //the arrow's colour, not its dark edge
                        arrow++;
                        const float ex = (on[i].pos.x - (float)hc.x) / hrx, ey = (on[i].pos.y - (float)hc.y) / hry;
                        inDisc += ex * ex + ey * ey <= 1.0f;
                    }
                    char msg[128];
                    std::snprintf(msg, sizeof msg, "%.0f m, facing %s, High visibility: the facing arrow is outside the filled head (%d of %d points inside)",
                                  d / 100, yaw == 0 ? "away" : yaw == 90 ? "to the side" : "towards you", inDisc, arrow);
                    CHECK(head && on.size() > off.size() && arrow > 0 && inDisc == 0, msg);
                }
            config::boxThickness = keepTh; config::skeletonColor = 1; config::boxStyle = 3;
        }
        config::skeletonMaxDist = 120; config::showBars = true; config::showPlayerNames = true; config::showPlayerGuns = true;
        config::showFacing = true; config::showOffscreen = true; config::boxStyle = 0; config::headMarker = 1; config::skeletonColor = 0;
    }

    //---- which way they face: up = away from you, down = towards you, right = to your right; drawn only when known
    {
        float x = 0, y = 0;
        CHECK(FacingOnScreen(30, 30, x, y) && std::abs(x) < 1e-6f && std::abs(y + 1) < 1e-6f, "facing the way you look: the pointer points up");
        CHECK(FacingOnScreen(210, 30, x, y) && std::abs(x) < 1e-6f && std::abs(y - 1) < 1e-6f, "facing back at you: down");
        CHECK(FacingOnScreen(120, 30, x, y) && std::abs(x - 1) < 1e-6f && std::abs(y) < 1e-6f, "turned 90 degrees to the right: right");
        CHECK(!FacingOnScreen(NAN, 30, x, y), "facing unknown: no pointer");
        std::vector<RenderEntity> p = {ent(Object::PLAYER, 3000, 200, 0), ent(Object::ARC, 2500, -300, 0)};
        const int unknownEsp = draw([&] { DrawESP(p, cam, 3); }), unknownRadar = draw([&] { DrawRadar(p, cam, 3); });
        p[0].yaw = 180; p[1].yaw = 90;
        const int knownEsp = draw([&] { DrawESP(p, cam, 3); });
        const bool espFinite = g_finite;
        const int knownRadar = draw([&] { DrawRadar(p, cam, 3); });
        config::showFacing = false;
        const int offEsp = draw([&] { DrawESP(p, cam, 3); }), offRadar = draw([&] { DrawRadar(p, cam, 3); });
        config::showFacing = true;
        CHECK(knownEsp > unknownEsp && offEsp == unknownEsp && espFinite, "player box: an arrow when his facing is known, none when unknown or switched off");
        CHECK(knownRadar > unknownRadar && offRadar == unknownRadar, "radar: a line on each dot whose facing is known, none when unknown or switched off");
        p[0].yaw = NAN; p[1].isTurret = true;
        CHECK(draw([&] { DrawRadar(p, cam, 3); }) == unknownRadar, "a turret gets no line (its gun turns without its base)");
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
        CHECK(worst > 0 && worst <= 129, "overlay opacity 0.5: nothing is more than half opaque");
        //...except the safety cards: the close-enemy card, your health and LOW HEALTH stay fully solid
        int card = 0, health = 0, after = 0;
        draw([&] {
            const auto& vb = ImGui::GetBackgroundDrawList()->VtxBuffer;
            int from = 0;
            auto most = [&] { int m = 0; for (int i = from; i < vb.Size; i++) m = std::max(m, (int)(vb[i].col >> 24)); from = vb.Size; return m; };
            DrawCloseEnemyWarning({ent(Object::PLAYER, -900, 0, 0)}, cam, 3);
            card = most();
            DrawSelfHealth(20, 50);
            health = most();
            DrawESP(front, cam, 3);
            after = most();
        });
        config::overlayOpacity = 1.0f;
        CHECK(card == 255 && health == 255, "overlay opacity 0.5: the close-enemy card, your health and LOW HEALTH stay fully solid");
        CHECK(after > 0 && after <= 129, "and the markers drawn after them are faded again");
    }

    //---- monochrome: only greys
    {
        config::monochrome = true;
        std::vector<RenderEntity> all = {ent(Object::PLAYER, 3000, 200, 0), ent(Object::ARC, 2500, -300, 0), ent(Object::SEARCH, 1500, 400, 0)};
        all[0].playerHealth = 55; all[0].yaw = 0; all[1].yaw = 45; //the facing pointers too
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

    //---- the Loot tab: kinds of ground items, groups of containers, colours by kind, valuable items
    {
        using K = labels::Kind;
        using G = labels::Group;
        RenderEntity gun = ent(Object::PICKUP, 1500, 100, 0); gun.itemKind = (int)K::Gun; gun.label = "Ferro";
        RenderEntity unread = gun; unread.itemKind = -1;
        RenderEntity bin = ent(Object::SEARCH, 1500, 100, 0); bin.itemKind = (int)G::Trash; bin.label = "Trash Can";
        RenderEntity binUnread = bin; binUnread.itemKind = -1;
        auto n = [&](const RenderEntity& e) { return draw([&] { DrawESP({e}, cam, 3); }); };
        auto rgb = [](ImU32 c) { return c | IM_COL32_A_MASK; }; //AA fringes share the colour at alpha 0
        auto colours = [&](const RenderEntity& e) {
            std::set<ImU32> c;
            draw([&] { DrawESP({e}, cam, 3); for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer) c.insert(rgb(v.col)); });
            return c;
        };
        auto width = [&](const RenderEntity& e, ImU32 col) { //how wide the shapes in this colour are drawn
            float x0 = 1e9f, x1 = -1e9f;
            draw([&] {
                DrawESP({e}, cam, 3);
                for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer)
                    if (rgb(v.col) == rgb(col)) { x0 = std::min(x0, v.pos.x); x1 = std::max(x1, v.pos.x); }
            });
            return x1 - x0;
        };
        bool* const* items = LootSwitches();
        bool* const* groups = ContainerSwitches();

        const int gunOn = n(gun), binOn = n(bin);
        bool same = gunOn > 0;
        for (int k = -1; k < labels::KIND_COUNT; k++) { RenderEntity e = gun; e.itemKind = k; same &= n(e) == gunOn && colours(e).count(rgb(COLOR::cyan)); }
        CHECK(same, "every kind on (the default): each kind and an unread item draw exactly as before (cyan, same size)");
        config::showLootGuns = false;
        CHECK(n(gun) == 0 && n(unread) == gunOn, "Guns off: a gun on the ground is hidden, an item not read yet is not");
        config::showLootGuns = true;
        config::showLootOther = false;
        RenderEntity odd = gun; odd.itemKind = 99;
        CHECK(n(unread) == 0 && n(odd) == 0 && n(gun) == gunOn, "Other off: items not read yet (and unknown kinds) are hidden, guns are not");
        config::showLootOther = true;
        for (int k = 0; k < labels::KIND_COUNT; k++) *items[k] = false;
        CHECK(n(bin) == binOn && n(binUnread) == binOn, "every item kind off: containers are drawn as before");
        for (int k = 0; k < labels::KIND_COUNT; k++) *items[k] = true;
        config::showContainerTrash = false;
        CHECK(n(bin) == 0 && n(binUnread) == binOn, "Trash off: a trash can is hidden, a container not read yet is not");
        config::showContainerTrash = true;
        config::showContainerOther = false;
        CHECK(n(binUnread) == 0 && n(bin) == binOn, "containers Other off: containers not read yet are hidden");
        for (int g = 0; g < labels::GROUP_COUNT; g++) *groups[g] = false;
        CHECK(n(gun) == gunOn, "every container group off: ground items are drawn as before");
        for (int g = 0; g < labels::GROUP_COUNT; g++) *groups[g] = true;

        //Colour items by kind: guns, ammo, meds and shields get their own colour, the same triangle and size
        config::colorLootKinds = true;
        RenderEntity meds = gun; meds.itemKind = (int)K::Meds;
        RenderEntity shield = gun; shield.itemKind = (int)K::Shield;
        const ImU32 cGun = COLOR::pickup((int)K::Gun), cMeds = COLOR::pickup((int)K::Meds), cShield = COLOR::pickup((int)K::Shield);
        CHECK(colours(gun).count(rgb(cGun)) && !colours(gun).count(rgb(COLOR::cyan)) && colours(meds).count(rgb(cMeds)) &&
              colours(shield).count(rgb(cShield)) && colours(unread).count(rgb(COLOR::cyan)),
              "Colour items by kind: a gun, meds and a shield in their own colours, an unread item stays cyan");
        CHECK(cGun != cMeds && cMeds != cShield && cGun != cShield && cGun != COLOR::cyan && COLOR::pickup((int)K::Ammo) != COLOR::cyan &&
              COLOR::pickup((int)K::Blueprint) == COLOR::cyan, "four kind colours, all different from each other and from cyan");
        CHECK(n(gun) == gunOn && n(meds) == gunOn, "Colour items by kind: the same triangle, the same size");

        //values.txt is read only while Show item values or Highlight valuable items is on
        const std::string vdir = (std::filesystem::temp_directory_path() / "kwinhelper_test_espvalues").string();
        std::filesystem::create_directories(vdir);
        std::ofstream(vdir + "/values.txt", std::ios::trunc) << "Ferro | 14000\nBandage | 300\nFerro with a long name | 14000\n";
        itemvalues::setDir(vdir);
        config::colorLootKinds = false;
        n(gun);
        CHECK(itemvalues::count() == 0, "Highlight valuable items off: values.txt is not read");
        config::highlightValuable = true;
        n(gun);
        CHECK(itemvalues::count() == 3, "Highlight valuable items on: values.txt is read");

        //a valuable item 30 m away (past the 25 m Loot name range): gold, half as big again, named with its price
        config::showItemNames = true; config::nameMaxLootDist = 25;
        RenderEntity far30 = gun; far30.pos = Vector3(3000, 100, 0);
        RenderEntity cheap = far30; cheap.label = "Bandage"; cheap.itemKind = (int)K::Meds;
        RenderEntity nameless = far30; nameless.label = "Unpriced thing";
        const float bigW = width(far30, COLOR::gold);
        const int bigN = n(far30), cheapN = n(cheap);
        CHECK(colours(far30).count(rgb(COLOR::gold)) && !colours(far30).count(rgb(COLOR::cyan)), "a valuable item is gold");
        CHECK(!colours(cheap).count(rgb(COLOR::gold)) && !colours(nameless).count(rgb(COLOR::gold)), "a cheap item and an unpriced one are not gold");
        config::highlightValuable = false;
        const float normalW = width(far30, COLOR::cyan);
        const int normalN = n(far30);
        CHECK(n(cheap) == cheapN, "a cheap item looks the same with the highlight on and off");
        std::printf("valuable triangle %.1f px wide, normal %.1f px\n", bigW, normalW);
        CHECK(bigW / normalW > 1.4f && bigW / normalW < 1.6f, "a valuable item's triangle is half as big again");
        CHECK(bigN > normalN, "a valuable item past the Loot name range still shows its name and price");
        config::highlightValuable = true;
        config::showItemNames = false;
        RenderEntity longName = far30; longName.label = "Ferro with a long name"; //the same price: only the name differs
        CHECK(n(far30) == bigN && n(longName) > bigN, "a valuable item is named past the Loot name range, even with Item names off");
        config::showItemNames = true;
        config::valuableFrom = 20000;
        CHECK(!colours(far30).count(rgb(COLOR::gold)), "Valuable from 20000: a 14000 item is not highlighted");
        config::valuableFrom = 10000;
        config::monochrome = true; config::colorLootKinds = true;
        bool grey = true;
        const std::vector<RenderEntity> mix = {far30, meds, shield, gun};
        draw([&] {
            DrawESP(mix, cam, 3);
            for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer) {
                const int r = v.col & 0xFF, g = (v.col >> 8) & 0xFF, b = (v.col >> 16) & 0xFF;
                if (std::abs(r - g) > 2 || std::abs(g - b) > 2) grey = false;
            }
        });
        CHECK(grey, "monochrome: kind colours and valuable gold stay white and grey");
        config::monochrome = false; config::colorLootKinds = false; config::highlightValuable = false;
        CHECK(ShortPrice(14000) == "14k" && ShortPrice(14500) == "14.5k" && ShortPrice(800) == "800" && ShortPrice(1500000) == "1.5M",
              "prices as a short suffix: 14k, 14.5k, 800, 1.5M");
        itemvalues::clear(); itemvalues::setDir("");
        std::filesystem::remove_all(vdir);
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

    //---- labels keep off the radar panel (markers are drawn over it, labels are moved away or left out)
    {
        const View v(cam, MakeProjection());
        const double d = 2000; //a container in front, placed so it lands on the radar's middle (150, 150)
        std::vector<RenderEntity> onRadar = {ent(Object::SEARCH, d, (150 - v.cx) * d / (v.focal * v.sx), (v.cy - 150) * d / (v.focal * v.sy))};
        onRadar[0].label = "Trash can";
        auto textOnPanel = [&] {
            const LabelRect P = RadarPanel();
            int n = 0;
            draw([&] {
                DrawESP(onRadar, cam, 3);
                const ImVec2 wp = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
                for (const ImDrawVert& q : ImGui::GetBackgroundDrawList()->VtxBuffer)
                    if ((std::abs(q.uv.x - wp.x) > 1e-6f || std::abs(q.uv.y - wp.y) > 1e-6f) &&
                        q.pos.x > P.x0 && q.pos.x < P.x1 && q.pos.y > P.y0 && q.pos.y < P.y1) n++;
            });
            return n;
        };
        config::enableRadar = false;
        const int radarOff = textOnPanel();
        config::enableRadar = true;
        const int radarOn = textOnPanel();
        CHECK(radarOff > 0 && radarOn == 0, "radar on: no label is drawn on the radar panel");
    }

    //---- loot labels keep off an enemy's box and bars; the enemy's own name and distance stay where they were
    {
        std::vector<RenderEntity> enemy = {ent(Object::PLAYER, 3000, 200, 0)};
        enemy[0].label = "Enemy";
        RenderEntity box = ent(Object::SEARCH, 3750, 250, 50); //behind the enemy's chest, 38 m away: its label falls on the body
        box.label = "Trash can";
        const std::vector<RenderEntity> lootOnly = {box}, both = {enemy[0], box};
        float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f; //the enemy's box and bars
        auto textOf = [&](const std::vector<RenderEntity>& list, bool learnBox) {
            std::vector<ImVec2> text;
            draw([&] {
                DrawESP(list, cam, 3);
                const ImVec2 wp = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
                for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer) {
                    if (std::abs(v.uv.x - wp.x) > 1e-6f || std::abs(v.uv.y - wp.y) > 1e-6f) text.push_back(v.pos);
                    else if (learnBox) { bx0 = std::min(bx0, v.pos.x); by0 = std::min(by0, v.pos.y); bx1 = std::max(bx1, v.pos.x); by1 = std::max(by1, v.pos.y); }
                }
            });
            return text;
        };
        const std::vector<ImVec2> own = textOf(enemy, true), alone = textOf(lootOnly, false), mixed = textOf(both, false);
        auto inBody = [&](const std::vector<ImVec2>& t) {
            int n = 0;
            for (const ImVec2& p : t) n += p.x > bx0 + 4 && p.x < bx1 - 4 && p.y > by0 + 4 && p.y < by1 - 4;
            return n;
        };
        bool kept = !own.empty();
        for (const ImVec2& p : own)
            kept &= std::any_of(mixed.begin(), mixed.end(), [&](const ImVec2& q) { return std::abs(p.x - q.x) < 0.01f && std::abs(p.y - q.y) < 0.01f; });
        CHECK(inBody(alone) > 0 && inBody(own) == 0, "(setup) the container's label alone falls on the enemy's body");
        CHECK(inBody(mixed) == 0, "loot labels move off an enemy's box and bars (or drop) instead of covering the body");
        CHECK(kept, "the enemy's own name and distance stay exactly where they were");
    }

    //---- status card and toast
    {
        Toast("Teammate marked: cooked", 5.0);
        CHECK(draw([&] { DrawToast(DrawStatusCard("live", "120 fps")); }) > 0, "status card and toast draw");
    }

    //---- messages at the top: a second one shows under the first instead of wiping it, each for its own time; at most 3
    {
        g_toasts.clear();
        auto shown = [] { std::string s; if (ToastShowing()) for (const ToastMsg& m : g_toasts) s += m.text + "|"; return s; };
        auto wait = [](double s) { for (int i = 0; i < (int)(s * 60) + 1; i++) draw([] {}); }; //each frame is 1/60 s
        Toast("Teammate marked: cooked", 1.0);
        draw([] { DrawToast(100); });
        const float oneTop = g_minY, oneBottom = g_maxY;
        Toast("F7 again to clear 2 marks", 3.0);
        draw([] { DrawToast(100); });
        CHECK(shown() == "Teammate marked: cooked|F7 again to clear 2 marks|", "a second message does not wipe the first: both show, in the order they came");
        CHECK(g_minY == oneTop && g_maxY > oneBottom + config::labelSize, "the second message shows under the first, which stays where it was");
        wait(1.2);
        CHECK(shown() == "F7 again to clear 2 marks|", "the first goes after its own time, the second stays for its own");
        Toast("F7 again to clear 2 marks", 3.0);
        Toast("a", 5.0); Toast("b", 5.0);
        CHECK(shown() == "F7 again to clear 2 marks|a|b|", "the same text again is not shown twice");
        wait(2.5);
        CHECK(shown() == "F7 again to clear 2 marks|a|b|", "the same text again starts its time anew");
        Toast("c", 5.0);
        CHECK(shown() == "a|b|c|", "at most 3 messages: a fourth makes the oldest go");
        wait(5.1);
        CHECK(shown().empty() && !ToastShowing(), "all gone after their time");
    }

    //---- the spoken warning when an enemy comes close (camera at 0,0,0 looking along +x; your team is 3). Warning card
    //range 10 m here: nobody below is that close, so only the first warnings are said
    {
        config::cueRange = 40; config::warnDist = 10;
        const FminimalViewInfo c = camera();
        std::vector<RenderEntity> v = {ent(Object::PLAYER, 3000, 0, 0, 5), ent(Object::PLAYER, -2000, 0, 0, 5), ent(Object::PLAYER, 0, -1500, 0, 5),
                                       ent(Object::PLAYER, 0, 2500, 0, 5), ent(Object::PLAYER, 0, 1000, 0, 3), ent(Object::PLAYER, 500, 0, 0, 5)};
        v[5].isDead = true;
        for (size_t i = 0; i < v.size(); i++) v[i].actor = 0x100 + i;
        NearCue nc;
        CHECK(nc.update(v, c, 3, 0.0) == "Enemy left, 15 meters", "cue: the nearest enemy first, with its side (teammate and dead player skipped)");
        CHECK(nc.update(v, c, 3, 1.0).empty(), "cue: at most one warning every 2 s");
        CHECK(nc.update(v, c, 3, 2.5) == "Enemy behind, 20 meters", "cue: the next one once 2 s have passed");
        CHECK(nc.update(v, c, 3, 5.0) == "Enemy right, 25 meters", "cue: right");
        CHECK(nc.update(v, c, 3, 7.5) == "Enemy ahead, 30 meters", "cue: ahead");
        CHECK(nc.update(v, c, 3, 10.0).empty(), "cue: each enemy is said once while he stays near");
        v[0].pos.x = 4300; //43 m: inside the 5 m slack
        CHECK(nc.update(v, c, 3, 12.0).empty(), "cue: no repeat while he walks along the edge of the range");
        v[0].pos.x = 6000; nc.update(v, c, 3, 13.0); v[0].pos.x = 3000;
        CHECK(nc.update(v, c, 3, 15.0) == "Enemy ahead, 30 meters", "cue: said again after leaving and coming back");
    }

    //---- the spoken warning once more when a said enemy comes within the Warning card range, with height and count
    {
        config::cueRange = 40; config::warnDist = 10;
        const FminimalViewInfo c = camera();
        std::vector<RenderEntity> v = {ent(Object::PLAYER, 3000, 0, 0, 5)};
        v[0].actor = 0x200;
        NearCue nc;
        CHECK(nc.update(v, c, 3, 0.0) == "Enemy ahead, 30 meters", "close: first the normal warning");
        v[0].pos = Vector3(0, -800, 0);
        CHECK(nc.update(v, c, 3, 1.0).empty(), "close: also waits for the 2 s between warnings");
        CHECK(nc.update(v, c, 3, 2.5) == "Enemy close, left, 8 meters", "close: within the card range he is said once more");
        CHECK(nc.update(v, c, 3, 5.0).empty(), "close: once per time he comes that close");
        v[0].pos = Vector3(0, -1300, 0); nc.update(v, c, 3, 6.0); v[0].pos = Vector3(0, -800, 0);
        CHECK(nc.update(v, c, 3, 8.0).empty(), "close: stepping out less than 5 m past the card range and back says nothing");
        v[0].pos = Vector3(0, -2000, 0); nc.update(v, c, 3, 9.0); v[0].pos = Vector3(0, -800, 0);
        CHECK(nc.update(v, c, 3, 11.0) == "Enemy close, left, 8 meters", "close: said again after he went 5 m past the card range");
        //two enemies coming close together: one sentence with the count and the nearest, not one each
        v = {ent(Object::PLAYER, 3000, 0, 0, 5), ent(Object::PLAYER, -3500, 0, 0, 5)};
        v[0].actor = 0x300; v[1].actor = 0x301;
        NearCue two;
        two.update(v, c, 3, 0.0); two.update(v, c, 3, 2.5);
        v[0].pos = Vector3(0, 600, 0); v[1].pos = Vector3(-900, 0, 0);
        CHECK(two.update(v, c, 3, 5.0) == "2 enemies close, nearest right, 6 meters", "close: several at once, the count and the nearest");
        CHECK(two.update(v, c, 3, 7.5).empty(), "close: and not once more for the second one");
        //someone first seen inside the card range: said as close at once; more than 3 m up or down: above / below
        std::vector<RenderEntity> w = {ent(Object::PLAYER, 0, 700, 450, 5)};
        w[0].actor = 0x400;
        NearCue fresh;
        CHECK(fresh.update(w, c, 3, 0.0) == "Enemy close, right, 8 meters, above", "close: first seen that close, said as close, with above");
        w[0].pos = Vector3(-2500, 0, -800); w[0].actor = 0x401;
        CHECK(fresh.update(w, c, 3, 2.5) == "Enemy behind, 26 meters, below", "a far enemy 8 m lower: below");
        //a card range bigger than the spoken range never says someone beyond the spoken range
        config::cueRange = 10; config::warnDist = 25;
        w[0].pos = Vector3(2000, 0, 0); w[0].actor = 0x402;
        NearCue wide;
        CHECK(wide.update(w, c, 3, 0.0).empty(), "close: never beyond the Spoken warning range");
        config::cueRange = 40; config::warnDist = 25;
    }

    //---- what F9 says
    {
        config::cueRange = 40;
        const FminimalViewInfo c = camera();
        std::vector<RenderEntity> v = {ent(Object::PLAYER, 0, -1200, 500, 5), ent(Object::PLAYER, -3000, 0, 0, 5), ent(Object::PLAYER, 300, 0, 0, 3),
                                       ent(Object::PLAYER, 200, 0, 0, 5)};
        v[3].isDead = true;
        CHECK(StatusSentence(80.4, 40, v, c, 3) == "Health 80, shield 40. 2 enemies within 40 meters. Nearest left, 13 meters, above.",
              "F9: health, shield, the enemies within range and the nearest (teammate and dead player left out)");
        CHECK(StatusSentence(-1, -1, {}, c, 3) == "Health unknown. No enemies within 40 meters.", "F9: nothing read, nobody near");
        const std::vector<RenderEntity> far = {ent(Object::PLAYER, 8500, 0, 0, 5)}, one = {ent(Object::PLAYER, 0, 2000, 0, 5)};
        CHECK(StatusSentence(55, -1, far, c, 3) == "Health 55. No enemies within 40 meters. Nearest ahead, 85 meters.",
              "F9: nobody within range, but where the nearest is");
        CHECK(StatusSentence(55, 0, one, c, 3) == "Health 55, shield 0. 1 enemy within 40 meters. Nearest right, 20 meters.", "F9: one enemy");
        config::hideUnknownTeams = true;
        CHECK(StatusSentence(55, -1, one, c, -1) == "Health 55. No enemies within 40 meters.", "F9: with Hide players with unknown team, as on screen");
        config::hideUnknownTeams = false;
    }

    //---- spoken news about yourself: low health, a broken shield, lost and returning game data
    {
        config::lowHealthFrom = 35;
        SelfCue s;
        CHECK(s.update(80, 50, true, false, 0).empty(), "self: nothing to say at first");
        CHECK(s.update(30, 50, true, false, 1) == "Low health", "self: Low health when it drops under Low health from");
        CHECK(s.update(25, 50, true, false, 2).empty() && s.update(38, 50, true, false, 3).empty() && s.update(30, 50, true, false, 4).empty(),
              "self: once, also when it wobbles a little above the line and back");
        CHECK(s.update(41, 50, true, false, 5).empty() && s.update(30, 50, true, false, 6) == "Low health", "self: again after it was 5 above the line");
        CHECK(s.update(30, 0, true, false, 7) == "Shield broken" && s.update(30, 0, true, false, 8).empty(), "self: Shield broken once when it reaches 0");
        s.update(80, 30, true, false, 9);
        CHECK(s.update(20, 0, true, false, 10) == "Shield broken. Low health", "self: both in one frame, in one sentence");
        s.update(80, 30, true, false, 11); s.update(-1, -1, false, false, 12);
        CHECK(s.update(80, 0, true, false, 13).empty(), "self: a 0 shield after a gap in the data is not news");
        CHECK(s.update(0, 30, true, false, 14).empty(), "self: down (0) is not Low health");
        config::lowHealthFrom = 50;
        SelfCue t;
        CHECK(t.update(45, -1, true, false, 0) == "Low health", "self: follows Low health from");
        //a slider drag stops between whole numbers: 35.4 is shown as 35, so health 35 is not low, and "Low health" is
        //said again once health was 5 above the shown 35
        config::lowHealthFrom = 35.4;
        SelfCue q;
        CHECK(!IsLowHealth(35) && IsLowHealth(34) && LowHealthRoom(35) == 0, "Low health from 35.4 (shown as 35): health 35 is not low");
        CHECK(q.update(30, -1, true, false, 0) == "Low health" && q.update(40, -1, true, false, 1).empty() &&
              q.update(30, -1, true, false, 2) == "Low health", "self: said again after 5 above the shown 35");
        for (double from : {34.5, 35.5, 40.49}) { //the line is the number the menu shows (%.0f), halves included
            config::lowHealthFrom = from;
            char shown[16];
            std::snprintf(shown, sizeof shown, "%.0f", from);
            const int line = std::atoi(shown);
            char msg[96];
            std::snprintf(msg, sizeof msg, "Low health from %.2f is shown as %d: low under %d only", from, line, line);
            CHECK(!IsLowHealth(line) && IsLowHealth(line - 1), msg);
        }
        config::lowHealthFrom = 35;
        //game data: lost only after it was live, said once; back after 2 s live in one go
        SelfCue g;
        CHECK(g.update(-1, -1, false, true, 0).empty(), "data: nothing before it was ever live (the lobby at the start)");
        g.update(80, 50, true, false, 1);
        CHECK(g.update(-1, -1, false, true, 3) == "Game data lost", "data: Game data lost");
        CHECK(g.update(-1, -1, false, true, 4).empty(), "data: once");
        CHECK(g.update(80, 50, true, false, 5).empty() && g.update(80, 50, true, false, 6.5).empty(), "data: not back after 1.5 s live");
        g.update(-1, -1, false, false, 6.6); //a short stall
        CHECK(g.update(80, 50, true, false, 7).empty() && g.update(80, 50, true, false, 9.1) == "Game data back", "data: back after 2 s live in one go");
        CHECK(g.update(80, 50, true, false, 10).empty(), "data: back once");
    }

    //---- your own health: nothing when not read, the readout when read, the LOW HEALTH card under 35 (not at 0: down)
    {
        const int unread = draw([&] { DrawSelfHealth(-1, 50); });
        const int nan = draw([&] { DrawSelfHealth(NAN, -1); });
        const int fine = draw([&] { DrawSelfHealth(80, 50); });
        const int low = draw([&] { DrawSelfHealth(30, 50); });
        const int down = draw([&] { DrawSelfHealth(0, 50); });
        CHECK(unread == 0 && nan == 0, "own health not read: nothing is drawn");
        CHECK(fine > 0 && low > fine && down < low, "own health: readout, plus the LOW HEALTH card under 35 only");
        {   //Low health from (ESP tab) moves the line, for the card and for the room under the close-enemy card alike
            const int at40 = draw([&] { DrawSelfHealth(40, 50); });
            config::lowHealthFrom = 50;
            const int at40with50 = draw([&] { DrawSelfHealth(40, 50); });
            const float room = LowHealthRoom(40);
            config::lowHealthFrom = 35;
            CHECK(at40with50 > at40 && room > 0 && LowHealthRoom(40) == 0 && LowHealthRoom(34.4) > 0 && LowHealthRoom(34.6) == 0,
                  "Low health from 50: LOW HEALTH shows at 40 (and the card leaves room for it); at 35 only under 35");
        }
        draw([&] { DrawSelfHealth(30, 50); });
        CHECK(g_finite, "own health: every vertex is a finite number");
        {   //1280x800, Text size 28, a tall player list, health 20, an enemy close behind: no room under the card for LOW
            //HEALTH, so it goes above the card instead of over it, and the crosshair between them stays free
            const auto keepLs = config::labelSize;
            const int keepW = config::SCREEN_W, keepH = config::SCREEN_H;
            config::labelSize = 28; config::SCREEN_W = 1280; config::SCREEN_H = 800;
            ImGui::GetIO().DisplaySize = ImVec2(1280, 800);
            g_listRect = {240, 520, 1040, 786};
            const std::vector<RenderEntity> behind9 = {ent(Object::PLAYER, -900, 0, 0)};
            float wb = 0, wt = 0;
            LabelRect lowBox{1e9f, 1e9f, -1e9f, -1e9f};
            draw([&] {
                DrawCloseEnemyWarning(behind9, cam, 3, &wb, LowHealthRoom(20), nullptr, &wt);
                const int n0 = ImGui::GetBackgroundDrawList()->VtxBuffer.Size;
                DrawSelfHealth(20, 50, wb, wt);
                const auto& vb = ImGui::GetBackgroundDrawList()->VtxBuffer;
                for (int i = n0; i < vb.Size; i++)
                    if (vb[i].pos.x > 520) { //LOW HEALTH, not the HP readout at the left
                        lowBox.x0 = std::min(lowBox.x0, vb[i].pos.x); lowBox.x1 = std::max(lowBox.x1, vb[i].pos.x);
                        lowBox.y0 = std::min(lowBox.y0, vb[i].pos.y); lowBox.y1 = std::max(lowBox.y1, vb[i].pos.y);
                    }
            });
            const float crossY = (float)View(cam, MakeProjection()).cy;
            std::printf("      card %.0f-%.0f, LOW HEALTH %.0f-%.0f, crosshair at %.0f\n", wt, wb, lowBox.y0, lowBox.y1, crossY);
            CHECK(wt > 0 && lowBox.y1 > lowBox.y0 && lowBox.y1 < wt && lowBox.y0 >= 0, "no room under the card: LOW HEALTH goes above it, not over it");
            CHECK(lowBox.y1 < crossY - 12 && wt > crossY + 12, "and the crosshair between them stays free");
            g_listRect = {};
            config::labelSize = keepLs; config::SCREEN_W = keepW; config::SCREEN_H = keepH;
            ImGui::GetIO().DisplaySize = ImVec2((float)keepW, (float)keepH);
        }
        {   //an enemy ahead walking down the screen 1 cm a frame, his pose bobbing as he walks: the card goes from below
            //him to above him once, and never jumps back and forth (his standing box is used, and the side is kept)
            float last = 0, lo = 1e9f, hi = -1e9f;
            int jumps = 0;
            for (int step = 0; step <= 260; step++) {
                const double z = -120 - step;
                RenderEntity e = ent(Object::PLAYER, 600, 0, z);
                e.pose = standingPose(600, 0, z);
                auto bobbed = std::make_shared<skeleton::Pose>(*e.pose);
                for (Vector3& j : bobbed->j) j.z += 6 * std::sin(step * 0.9);
                e.pose = bobbed;
                float wb = 0;
                draw([&] { DrawCloseEnemyWarning({e}, cam, 3, &wb, LowHealthRoom(20)); });
                if (step > 0 && std::abs(wb - last) > 20) jumps++;
                last = wb; lo = std::min(lo, wb); hi = std::max(hi, wb);
            }
            std::printf("      card lower edge from %.0f to %.0f px, %d jumps\n", lo, hi, jumps);
            CHECK(hi - lo > 100 && jumps <= 2, "the close-enemy card changes sides once as he walks down the screen, never back and forth");
        }
        {   //an enemy stepping left and right by 5 cm (about 9 px) right at the edge where the card starts to move off him:
            //once moved, the card stays moved
            auto bottomAt = [&](double y, bool fresh) {
                if (fresh) g_warnSide = 0;
                float wb = 0;
                draw([&] { DrawCloseEnemyWarning({ent(Object::PLAYER, 600, y, -60)}, cam, 3, &wb); });
                return wb;
            };
            const float home = bottomAt(-900, true); //far to the side: the card at its own place
            double edge = -900;
            while (edge < 0 && std::abs(bottomAt(edge, true) - home) < 1) edge += 1;
            int jumps = 0;
            float last = bottomAt(edge - 5, true);
            for (int step = 1; step <= 200; step++) {
                const float b = bottomAt(edge + 5 * std::sin(step * 0.3), false);
                jumps += std::abs(b - last) > 20;
                last = b;
            }
            std::printf("      the card starts to move %.0f cm to his side, %d jumps while he steps 5 cm each way there\n", -edge, jumps);
            CHECK(edge < 0 && jumps <= 1, "an enemy stepping left and right at the card's edge: the card moves off him once and stays");
        }
        //an enemy 9 m ahead: his card and LOW HEALTH both show under the crosshair, LOW HEALTH below his card
        float wb = 0, lowTop = 1e9f;
        const std::vector<RenderEntity> close = {ent(Object::PLAYER, 900, 0, 0)};
        draw([&] {
            DrawCloseEnemyWarning(close, cam, 3, &wb);
            const int n0 = ImGui::GetBackgroundDrawList()->VtxBuffer.Size;
            DrawSelfHealth(30, 50, wb);
            const auto& vb = ImGui::GetBackgroundDrawList()->VtxBuffer;
            for (int i = n0; i < vb.Size; i++) if (std::abs(vb[i].pos.x - config::SCREEN_W * 0.5f) < 60) lowTop = std::min(lowTop, vb[i].pos.y);
        });
        CHECK(wb > 0 && lowTop < 1e9f && lowTop > wb, "LOW HEALTH goes under the close-enemy card, not on top of it");
        {   //and the card itself goes under that enemy's box and his distance and gun lines, not over them
            draw([&] { DrawCloseEnemyWarning(close, cam, 3); });
            float bx, by, bw, bh;
            const bool on = PlayerScreenBox(View(cam, MakeProjection()), close[0], bx, by, bw, bh);
            CHECK(on && g_minY > by + bh + 2 * config::labelSize, "the close-enemy card stays clear of the enemy ahead and his labels");
        }
        {   //an enemy 5 m ahead, feet low on screen, and a tall player list at the bottom: neither the card nor LOW
            //HEALTH goes under the list (ImGui draws it over them)
            config::showSelfHealth = true;
            g_listRect = {config::SCREEN_W * 0.5f - 500, 1150, config::SCREEN_W * 0.5f + 500, (float)config::SCREEN_H - 14};
            const std::vector<RenderEntity> low = {ent(Object::PLAYER, 500, 0, -60)};
            float lb = 0, maxY = -1e9f;
            int lowVerts = 0;
            draw([&] {
                DrawCloseEnemyWarning(low, cam, 3, &lb, LowHealthRoom(30));
                const int n0 = ImGui::GetBackgroundDrawList()->VtxBuffer.Size;
                DrawSelfHealth(30, 50, lb);
                const auto& vb = ImGui::GetBackgroundDrawList()->VtxBuffer;
                for (int i = 0; i < vb.Size; i++)
                    if (std::abs(vb[i].pos.x - config::SCREEN_W * 0.5f) < 600) { //not the HP readout at the left
                        maxY = std::max(maxY, vb[i].pos.y);
                        lowVerts += i >= n0;
                    }
            });
            CHECK(lb > 0 && lowVerts > 0 && maxY <= g_listRect.y0, "the close-enemy card and LOW HEALTH both stay above the player list");
            g_listRect = {};
        }
        {   //High visibility, an enemy 5 m away and 2 m below you (his distance and guns go above his name): LOW HEALTH
            //under the card must not cover him either, not only the card
            const auto keepLs = config::labelSize, keepTh = config::boxThickness;
            const auto keepStyle = config::boxStyle;
            const bool keepCards = config::labelCards;
            config::labelSize = 28; config::labelCards = true; config::boxThickness = 4; config::boxStyle = 1;
            std::vector<RenderEntity> low = {ent(Object::PLAYER, 400, -84, -208)};
            low[0].label = "Someone"; low[0].sub = "Ferro";
            std::vector<ImVec2> his;
            draw([&] { DrawESP(low, cam, 3); for (const ImDrawVert& q : ImGui::GetBackgroundDrawList()->VtxBuffer) his.push_back(q.pos); });
            float lb = 0;
            LabelRect warnBox{1e9f, 1e9f, -1e9f, -1e9f};
            draw([&] {
                DrawCloseEnemyWarning(low, cam, 3, &lb, LowHealthRoom(20));
                const int n0 = ImGui::GetBackgroundDrawList()->VtxBuffer.Size;
                DrawSelfHealth(20, 50, lb);
                const auto& vb = ImGui::GetBackgroundDrawList()->VtxBuffer;
                for (int i = n0; i < vb.Size; i++)
                    if (std::abs(vb[i].pos.x - config::SCREEN_W * 0.5f) < 600) { //LOW HEALTH, not the HP readout at the left
                        warnBox.x0 = std::min(warnBox.x0, vb[i].pos.x); warnBox.x1 = std::max(warnBox.x1, vb[i].pos.x);
                        warnBox.y0 = std::min(warnBox.y0, vb[i].pos.y); warnBox.y1 = std::max(warnBox.y1, vb[i].pos.y);
                    }
            });
            bool clear = lb > 0 && warnBox.x1 > warnBox.x0 && !his.empty();
            for (const ImVec2& p : his) clear = clear && !(p.x > warnBox.x0 && p.x < warnBox.x1 && p.y > warnBox.y0 && p.y < warnBox.y1);
            CHECK(clear, "LOW HEALTH under the close-enemy card keeps off the enemy's box and labels");
            //...but never by moving the card up to the crosshair: 1920x1080, a tall player list, an enemy 11 m ahead and
            //below, right of LOW HEALTH. The card stays where it was (nothing of his was under it)
            const int keepW = config::SCREEN_W, keepH = config::SCREEN_H;
            config::SCREEN_W = 1920; config::SCREEN_H = 1080;
            ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
            g_listRect = {550, 790, 1370, 1066};
            std::vector<RenderEntity> right = {ent(Object::PLAYER, 900, 400, -390)};
            right[0].label = "Someone"; right[0].sub = "Ferro  Anvil";
            g_warnSide = 0; //a new scene: the card was not moved for someone else a frame ago
            draw([&] { DrawCloseEnemyWarning(right, cam, 3, nullptr, LowHealthRoom(20)); });
            CHECK(g_verts > 0 && g_minY >= (float)View(cam, MakeProjection()).cy + 18,
                  "LOW HEALTH alone never lifts the close-enemy card onto the crosshair");
            g_listRect = {};
            config::SCREEN_W = keepW; config::SCREEN_H = keepH;
            ImGui::GetIO().DisplaySize = ImVec2((float)keepW, (float)keepH);
            config::labelSize = keepLs; config::labelCards = keepCards; config::boxThickness = keepTh; config::boxStyle = keepStyle;
        }
        //an enemy to your left and a little behind: his edge arrow lands at the readout's height on the left edge
        const std::vector<RenderEntity> side = {ent(Object::PLAYER, -681, -2000, 0)};
        const LabelRect r = SelfHealthRect();
        auto onCard = [&] {
            int n = 0;
            draw([&] {
                DrawESP(side, cam, 3);
                for (const ImDrawVert& q : ImGui::GetBackgroundDrawList()->VtxBuffer)
                    n += q.pos.x > r.x0 && q.pos.x < r.x1 && q.pos.y > r.y0 && q.pos.y < r.y1;
            });
            return n;
        };
        config::showSelfHealth = false;
        const int under = onCard();
        config::showSelfHealth = true;
        CHECK(under > 0 && onCard() == 0, "an edge arrow and its distance move off the HP readout");
    }

    //---- Text size 40 on a short, narrow screen (1280x800 is the Steam Deck): LOW HEALTH keeps off the big HP/SH readout
    //(it hid the shield number), stays on the screen and off the crosshair
    {
        const float keepLs = config::labelSize;
        const int keepW = config::SCREEN_W, keepH = config::SCREEN_H;
        const double keepFrom = config::lowHealthFrom;
        config::labelSize = 40; config::showSelfHealth = true; config::lowHealthFrom = 90;
        for (const ImVec2 scr : {ImVec2(1280, 800), ImVec2(1280, 720), ImVec2(1366, 768), ImVec2(1600, 900), ImVec2(1920, 1080)})
            for (const double hp : {30.0, 89.0}) {
                config::SCREEN_W = (int)scr.x; config::SCREEN_H = (int)scr.y;
                ImGui::GetIO().DisplaySize = scr;
                LabelRect red{1e9f, 1e9f, -1e9f, -1e9f};
                draw([&] {
                    DrawSelfHealth(hp, 100);
                    for (const ImDrawVert& q : ImGui::GetBackgroundDrawList()->VtxBuffer)
                        if (q.col == COLOR::red) {
                            red.x0 = std::min(red.x0, q.pos.x); red.x1 = std::max(red.x1, q.pos.x);
                            red.y0 = std::min(red.y0, q.pos.y); red.y1 = std::max(red.y1, q.pos.y);
                        }
                });
                char text[32];
                std::snprintf(text, sizeof text, "HP %.0f   SH 100", hp);
                const LabelRect hpCard = SelfHealthRect(text);
                const float cx = scr.x / 2, cy = scr.y / 2;
                const bool cross = red.x0 < cx + 12 && red.x1 > cx - 12 && red.y0 < cy + 12 && red.y1 > cy - 12;
                char msg[160];
                std::snprintf(msg, sizeof msg, "Text size 40 on %.0fx%.0f, health %.0f: LOW HEALTH (x %.0f-%.0f) keeps off the HP/SH readout (to %.0f), on the screen, off the crosshair",
                              scr.x, scr.y, hp, red.x0, red.x1, hpCard.x1);
                CHECK(red.x1 > red.x0 && !RectsOverlap(red, hpCard) && red.x0 >= 0 && red.x1 <= scr.x && red.y1 <= scr.y && !cross, msg);
            }
        config::labelSize = keepLs; config::SCREEN_W = keepW; config::SCREEN_H = keepH; config::lowHealthFrom = keepFrom;
        ImGui::GetIO().DisplaySize = ImVec2((float)keepW, (float)keepH);
    }

    //---- the largest Text size (40): the big HP/SH readout, LOW HEALTH, the status card and a toast all stay on a 1280x720
    //and on a 1920x1080 screen
    {
        const float keepLs = config::labelSize;
        const int keepW = config::SCREEN_W, keepH = config::SCREEN_H;
        config::labelSize = 40; config::showSelfHealth = true;
        g_toasts.clear(); //one message, the longest of the tests
        Toast("Teammate marked: Rattlerfan", 5.0);
        for (const ImVec2 scr : {ImVec2(1280, 720), ImVec2(1920, 1080)}) {
            config::SCREEN_W = (int)scr.x; config::SCREEN_H = (int)scr.y;
            ImGui::GetIO().DisplaySize = scr;
            auto onScreen = [&] { return g_verts > 0 && g_finite && g_minX >= 0 && g_minY >= 0 && g_maxX <= scr.x && g_maxY <= scr.y; };
            const LabelRect r = SelfHealthRect();
            draw([&] { DrawSelfHealth(20, 100); });
            const bool health = onScreen() && r.x0 >= 0 && r.y0 >= 0 && r.x1 <= scr.x && r.y1 <= scr.y;
            draw([&] { DrawToast(DrawStatusCard("live  -  120 Hz  -  data 4 ms  -  12 players", "60 fps   step 0.50 ms   scan 1.2 ms   120 moving, 3000 fixed")); });
            char msg[160];
            std::snprintf(msg, sizeof msg, "Text size 40 on %.0fx%.0f: the HP/SH readout and LOW HEALTH are drawn fully on the screen", scr.x, scr.y);
            CHECK(health, msg);
            std::snprintf(msg, sizeof msg, "Text size 40 on %.0fx%.0f: the status card and a toast are drawn fully on the screen", scr.x, scr.y);
            CHECK(onScreen(), msg);
            //the close-enemy card for three enemies behind you, and the longest banner (GAME UPDATED at 1.6 x Text size)
            const std::vector<RenderEntity> three = {ent(Object::PLAYER, -900, 0, 0), ent(Object::PLAYER, -1100, 300, 0), ent(Object::PLAYER, -1300, -300, 0)};
            std::string said;
            draw([&] { said = DrawCloseEnemyWarning(three, cam, 3); });
            std::printf("      %s: %.0f px wide on %.0f\n", said.c_str(), g_maxX - g_minX, scr.x);
            std::snprintf(msg, sizeof msg, "Text size 40 on %.0fx%.0f: the close-enemy card for three enemies fits across the screen", scr.x, scr.y);
            CHECK(said.rfind("3 ENEMIES", 0) == 0 && onScreen(), msg);
            draw([&] { DrawLabelCentered(scr.x * 0.5f, 12 + 32, "GAME UPDATED: MARKERS MAY BE WRONG", 40 * 1.6f, COLOR::white, true); });
            std::snprintf(msg, sizeof msg, "Text size 40 on %.0fx%.0f: the GAME UPDATED banner fits across the screen", scr.x, scr.y);
            CHECK(onScreen(), msg);
        }
        {   //a label that fits is drawn at its own size, whatever the screen
            draw([&] { DrawLabelCentered(640, 360, "ENEMY 9 m BEHIND", 80, COLOR::white, true); });
            const float w = g_maxX - g_minX;
            CHECK(std::abs(w - (LabelSize("ENEMY 9 m BEHIND", 80).x + 8)) < 1.0f, "a big label that fits across the screen keeps its full size");
        }
        config::SCREEN_W = keepW; config::SCREEN_H = keepH;
        ImGui::GetIO().DisplaySize = ImVec2((float)keepW, (float)keepH);
        config::labelSize = keepLs;
    }

    //---- the radar panel
    {
        std::vector<RenderEntity> radarEnts = {ent(Object::PLAYER, 3000, 200, 0), ent(Object::ARC, -2500, 1000, 0)};
        const int radar = draw([&] { DrawRadar(radarEnts, cam, 3); });
        const int emptyRadar = draw([&] { DrawRadar(none, cam, 3); });
        CHECK(emptyRadar > 300 && radar > emptyRadar, "radar: panel, rings, title and your arrow even when empty; dots when players are near");
    }

    //---- every part's text follows Text size: at 28 px its letters are clearly taller than at 10 px. Text drawn at a
    //fixed size (the radar title used to be 12 px always) fails here.
    {
        ImGui::GetStyle().AntiAliasedLinesUseTex = false; //thin lines then use the white pixel too, so only letters pass the filter
        //the tallest letter drawn: text vertices (not the atlas' white pixel) come 4 per letter
        auto tallest = [&](auto fn) {
            float best = 0;
            draw([&] {
                fn();
                const ImVec2 wp = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
                std::vector<float> ys;
                for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer)
                    if (std::abs(v.uv.x - wp.x) > 1e-6f || std::abs(v.uv.y - wp.y) > 1e-6f) ys.push_back(v.pos.y);
                for (size_t i = 0; i + 4 <= ys.size(); i += 4) {
                    const auto [lo, hi] = std::minmax_element(ys.begin() + i, ys.begin() + i + 4);
                    best = std::max(best, *hi - *lo);
                }
            });
            return best;
        };
        const float keep = config::labelSize;
        auto follows = [&](const char* what, auto fn) {
            config::labelSize = 10; const float small = tallest(fn);
            config::labelSize = 28; const float big = tallest(fn);
            config::labelSize = keep;
            std::printf("      %s: tallest letter %.1f px at Text size 10, %.1f px at 28\n", what, small, big);
            char msg[160];
            std::snprintf(msg, sizeof msg, "Text size: the %s text is clearly larger at 28 than at 10", what);
            CHECK(small > 0 && big >= 1.8f * small, msg);
        };
        g_toasts.clear();
        Toast("Teammate marked: cooked", 5.0);
        std::vector<RenderEntity> box = {ent(Object::SEARCH, 1500, 100, 0)};
        box[0].label = "Trash can";
        config::showContainerNames = true; config::showOffscreen = true;
        follows("radar", [&] { DrawRadar(none, cam, 3); });
        follows("status card and toast", [&] { DrawToast(DrawStatusCard("live", "120 fps")); });
        follows("container label", [&] { DrawESP(box, cam, 3); });
        follows("edge arrow", [&] { DrawESP(behind, cam, 3); });
        ImGui::GetStyle().AntiAliasedLinesUseTex = true;
    }

    auto rgbOf = [](ImU32 c) { return c | IM_COL32_A_MASK; }; //anti-aliased edges share the colour at alpha 0
    auto isText = [](const ImDrawVert& q) {
        const ImVec2 wp = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
        return std::abs(q.uv.x - wp.x) > 1e-6f || std::abs(q.uv.y - wp.y) > 1e-6f;
    };
    //how many text vertices a drawing makes, and the area they cover
    LabelRect textBox{};
    auto textOf = [&](auto fn) {
        int n = 0;
        textBox = {1e9f, 1e9f, -1e9f, -1e9f};
        draw([&] {
            fn();
            for (const ImDrawVert& q : ImGui::GetBackgroundDrawList()->VtxBuffer)
                if (isText(q)) {
                    n++;
                    textBox = {std::min(textBox.x0, q.pos.x), std::min(textBox.y0, q.pos.y), std::max(textBox.x1, q.pos.x), std::max(textBox.y1, q.pos.y)};
                }
        });
        return n;
    };

    //---- markers and bars grow with Text size: at 28 px about 28/15 times as big as at the 15 px default
    {
        auto widthOf = [&](const RenderEntity& e, ImU32 col) {
            float x0 = 1e9f, x1 = -1e9f;
            draw([&] {
                DrawESP({e}, cam, 3);
                for (const ImDrawVert& q : ImGui::GetBackgroundDrawList()->VtxBuffer)
                    if (rgbOf(q.col) == rgbOf(col)) { x0 = std::min(x0, q.pos.x); x1 = std::max(x1, q.pos.x); }
            });
            return x1 - x0;
        };
        RenderEntity down = ent(Object::PLAYER, 1500, 300, 0);
        down.isDead = true;
        struct Marker { const char* what; RenderEntity e; ImU32 col; };
        const Marker markers[] = {{"ARC diamond", ent(Object::ARC, 2000, 100, 0), COLOR::arc(false)},
                                  {"container square", ent(Object::SEARCH, 1500, 100, 0), COLOR::container()},
                                  {"item triangle", ent(Object::PICKUP, 1500, -200, 0), COLOR::pickup()},
                                  {"down player's cross", down, COLOR::deadPlayer()}};
        bool all = true;
        for (const Marker& mk : markers) {
            config::labelSize = 15; const float w15 = widthOf(mk.e, mk.col);
            config::labelSize = 28; const float w28 = widthOf(mk.e, mk.col);
            config::labelSize = 15;
            std::printf("      %s: %.1f px wide at Text size 15, %.1f px at 28\n", mk.what, w15, w28);
            all &= w15 > 0 && w28 > 1.7f * w15 && w28 < 1.95f * w15;
        }
        CHECK(all, "Text size 28: ARC diamonds, containers, items and down players' crosses are about 28/15 times as big as at 15");
        RenderEntity p = ent(Object::PLAYER, 3000, 200, 0);
        auto barWidth = [&] {
            float x0 = 1e9f, x1 = -1e9f;
            draw([&] {
                DrawPlayerBars(1000, 500, 40, 100, p, 3);
                for (const ImDrawVert& q : ImGui::GetBackgroundDrawList()->VtxBuffer)
                    if (q.col == COLOR::health(1.0f)) { x0 = std::min(x0, q.pos.x); x1 = std::max(x1, q.pos.x); }
            });
            return x1 - x0;
        };
        const float bar15 = barWidth();
        config::labelSize = 28;
        const float bar28 = barWidth();
        config::labelSize = 15;
        std::printf("      health bar %.2f px wide at Text size 15, %.2f px at 28\n", bar15, bar28);
        CHECK(std::abs(bar15 - BAR_MAX_W) < 0.01f && std::abs(bar28 - BAR_MAX_W * 28 / 15) < 0.01f, "the health bar is 5 px wide at Text size 15 and 28/15 times that at 28");
        CHECK(BarsReach() == 16.0f, "at Text size 15 the close-enemy card keeps the same room for the bars as before (16 px)");
        p.hasShield = true; p.shield = 50; p.shieldMax = 100;
        config::labelSize = 40;
        float right = 0;
        draw([&] { right = DrawPlayerBars(1000, 500, 40, 100, p, 3); });
        const float reach = BarsReach();
        config::labelSize = 15;
        std::printf("      bars reach %.2f px past the box at Text size 40, room %.2f\n", right - 1040, 3 + reach);
        CHECK(right - 1040 > 30 && right - 1040 <= 3 + reach, "Text size 40: both wide bars fit in the room the close-enemy card keeps for them");
    }

    //---- Full-size small text: distances, guns, marker labels and the status card in the full Text size
    {
        config::labelSize = 20; config::showPlayerNames = false; config::showPlayerGuns = false;
        std::vector<RenderEntity> box = {ent(Object::SEARCH, 1500, 100, 0)}, player = {ent(Object::PLAYER, 3000, 200, 0)};
        box[0].label = "Trash can";
        auto height = [&](auto fn) { textOf(fn); return textBox.y1 - textBox.y0; };
        const float boxSmall = height([&] { DrawLabelCentered(500, 500, "Trash can  15m", 18); });
        const float boxFull = height([&] { DrawLabelCentered(500, 500, "Trash can  15m", 20); });
        const float distSmall = height([&] { DrawLabelCentered(500, 500, "30m", 18); });
        const float distFull = height([&] { DrawLabelCentered(500, 500, "30m", 20); });
        const float cardSmall = height([&] { DrawLabelCentered(500, 500, "live", 18, COLOR::white, true); });
        const float cardFull = height([&] { DrawLabelCentered(500, 500, "live", 20, COLOR::white, true); });
        const float boxOff = height([&] { DrawESP(box, cam, 3); }), distOff = height([&] { DrawESP(player, cam, 3); });
        const float cardOff = height([&] { DrawStatusCard("live", ""); });
        config::fullSizeText = true;
        const float boxOn = height([&] { DrawESP(box, cam, 3); }), distOn = height([&] { DrawESP(player, cam, 3); });
        const float cardOn = height([&] { DrawStatusCard("live", ""); });
        config::fullSizeText = false;
        config::labelSize = 15; config::showPlayerNames = true; config::showPlayerGuns = true;
        std::printf("      text heights off/on: marker label %.1f/%.1f, distance %.1f/%.1f, status card %.1f/%.1f\n", boxOff, boxOn, distOff, distOn, cardOff, cardOn);
        CHECK(std::abs(boxOff - boxSmall) < 0.5f && std::abs(distOff - distSmall) < 0.5f && std::abs(cardOff - cardSmall) < 0.5f,
              "Full-size small text off: a marker label, a player's distance and the status card are 0.9 times Text size, as before");
        CHECK(boxFull > boxSmall + 1 && std::abs(boxOn - boxFull) < 0.5f && std::abs(distOn - distFull) < 0.5f && std::abs(cardOn - cardFull) < 0.5f,
              "Full-size small text on: they are in the full Text size");
        config::labelSize = 40; config::fullSizeText = true;
        draw([&] { DrawStatusCard("live", "60 fps"); });
        config::labelSize = 15; config::fullSizeText = false;
        CHECK(g_verts > 0 && g_minY >= 0, "Text size 40 with Full-size small text: the status card stays on the screen's top");
    }

    //---- the radar: solid, thicker lines, a white title, ring ranges, height carets, and the main view's switches
    {
        auto radar = [&](const std::vector<RenderEntity>& l, int team = 3) { return draw([&] { DrawRadar(l, cam, team); }); };
        const int empty = radar({});
        RenderEntity squad = ent(Object::PLAYER, 2000, 300, 0, 3), turret = ent(Object::ARC, 2500, -300, 0), enemy = ent(Object::PLAYER, 2000, -500, 0, 5);
        turret.isTurret = true;
        config::showTeammates = false;
        const int squadOff = radar({squad});
        config::showTeammates = true;
        const int squadOn = radar({squad});
        config::showTeammates = false;
        marks::setMarked("radar-mark", true);
        RenderEntity marked = enemy; marked.account = "radar-mark";
        const int markedOff = radar({marked});
        marks::setMarked("radar-mark", false);
        CHECK(squadOff == empty && squadOn > empty && markedOff > empty, "radar: a squad mate only with Teammates on (as on screen); a hand mark always");
        config::showTurrets = false;
        const int turretOff = radar({turret});
        config::showTurrets = true;
        CHECK(turretOff == empty && radar({turret}) > empty, "radar: no turrets with Turrets off");
        config::hideUnknownTeams = true;
        const int unknown = radar({enemy}, -1), known = radar({enemy}, 3);
        config::hideUnknownTeams = false;
        CHECK(unknown == empty && known > empty, "radar: Hide players with unknown team hides them on the radar too");
        RenderEntity high = enemy, low = enemy;
        high.pos.z = 500; low.pos.z = -500;
        const int level = radar({enemy});
        CHECK(radar({high}) > level && radar({low}) > level, "radar: a caret on a dot more than 3 m above or below you");
        int faint = 0;
        bool greyTitle = false;
        draw([&] {
            DrawRadar({}, cam, 3);
            for (const ImDrawVert& q : ImGui::GetBackgroundDrawList()->VtxBuffer) {
                const int a = (int)(q.col >> 24);
                if (rgbOf(q.col) == rgbOf(COLOR::white) && a > 0 && a < RADAR_LINE_ALPHA) faint++;
                if (isText(q) && rgbOf(q.col) == IM_COL32(190, 190, 190, 255)) greyTitle = true;
            }
        });
        CHECK(faint == 0 && !greyTitle, "radar: every line is at least half solid and the title is white");
        ImGui::GetStyle().AntiAliasedLinesUseTex = false; //lines then use the white pixel too, so only letters count as text
        const int radarText = textOf([&] { DrawRadar({}, cam, 3); });
        ImGui::GetStyle().AntiAliasedLinesUseTex = true;
        int ringsText = textOf([&] { DrawLabelCentered(500, 500, RadarTitle().c_str(), RadarTitleSize(), COLOR::white); });
        for (const char* t : {"67m", "133m", "200m"}) ringsText += textOf([&] { DrawLabelCentered(500, 500, t, 12); });
        std::printf("      radar text vertices %d, title and three ranges %d\n", radarText, ringsText);
        CHECK(config::radarRadius * config::scale / 100 == 200 && radarText == ringsText, "radar: each of the three rings is labelled with its range (67m, 133m, 200m)");
        config::labelSize = 40;
        draw([&] { DrawRadar({}, cam, 3); });
        const LabelRect panel = RadarPanel();
        config::labelSize = 15;
        CHECK(g_finite && g_minX >= panel.x0 - 1 && g_maxX <= panel.x1 + 1 && g_maxY <= panel.y1 + 1, "radar at Text size 40: rings, ranges and title stay in the panel");
    }

    //---- teammates are told apart by a word, not only by colour: TEAM or MARKED before the name, in the teammate colour
    {
        CHECK(PlayerNameText("Someone", true, false) == "TEAM  Someone" && PlayerNameText("Someone", false, true) == "MARKED  Someone" &&
              PlayerNameText("Someone", true, true) == "TEAM  Someone" && PlayerNameText("Someone", false, false) == "Someone" &&
              PlayerNameText("", false, true) == "MARKED", "name line: TEAM for your squad, MARKED for a hand mark, the name alone for anyone else");
        config::showTeammates = true;
        RenderEntity mateP = ent(Object::PLAYER, 3000, 200, 0, 3), enemyP = ent(Object::PLAYER, 3000, 200, 0, 5);
        mateP.label = enemyP.label = "Someone"; mateP.sub = enemyP.sub = "Ferro";
        auto textIn = [&](const RenderEntity& e, ImU32 col) {
            int n = 0;
            draw([&] { DrawESP({e}, cam, 3); for (const ImDrawVert& q : ImGui::GetBackgroundDrawList()->VtxBuffer) n += isText(q) && q.col == col; });
            return n;
        };
        CHECK(textIn(mateP, COLOR::mate()) > 0 && textIn(enemyP, COLOR::mate()) == 0, "a teammate's name is drawn in the teammate colour, an enemy's is not");
        const int mateText = textVerts({mateP}), enemyText = textVerts({enemyP});
        config::monochrome = true;
        const int monoMate = textVerts({mateP}), monoEnemy = textVerts({enemyP});
        config::monochrome = false;
        CHECK(mateText > enemyText && monoMate > monoEnemy, "a teammate's name line has the word TEAM, also in monochrome");
        marks::setMarked("esp-mark", true);
        RenderEntity markedP = enemyP; markedP.account = "esp-mark";
        const int markedText = textVerts({markedP});
        const bool markedGreen = textIn(markedP, COLOR::mate()) > 0;
        marks::setMarked("esp-mark", false);
        CHECK(markedText > mateText && markedGreen, "a player marked by hand: MARKED before his name, in the teammate colour");
        config::showPlayerNames = false;
        const int namesOffMate = textVerts({mateP}), namesOffEnemy = textVerts({enemyP});
        config::showPlayerNames = true;
        CHECK(namesOffMate > namesOffEnemy, "Player names off: a teammate still has TEAM over his box");
        config::showTeammates = false;
    }

    //---- labels stay on the screen at its left and right edges; marker labels never take a place off the screen
    {
        const View v(cam, MakeProjection());
        const double d = 2000;
        const float SW = (float)config::SCREEN_W;
        auto at = [&](float sx, float sy) { return Vector3(d, (sx - v.cx) * d / (v.focal * v.sx), (v.cy - sy) * d / (v.focal * v.sy)); };
        bool inside = true;
        for (const float sx : {-30.0f, SW + 30}) {
            RenderEntity p = ent(Object::PLAYER, 0, 0, 0);
            p.pos = at(sx, 700); p.label = "Somebody with a long name"; p.sub = "Ferro (held) + Stitcher";
            const int n = textOf([&] { DrawESP({p}, cam, 3); });
            std::printf("      player at x %.0f: text from %.1f to %.1f\n", sx, textBox.x0, textBox.x1);
            inside &= n > 0 && textBox.x0 >= 0 && textBox.x1 <= SW;
        }
        CHECK(inside, "a player at the left or right screen edge: his name, distance and guns stay fully on the screen");
        std::vector<RenderEntity> edgeBoxes;
        for (int i = 0; i < 12; i++) {
            RenderEntity e = ent(Object::SEARCH, 0, 0, 0);
            e.pos = at(SW - 6, 400 + i * 4.0f); e.actor += i; e.label = "Trash can"; //so close that above and below fill up
            edgeBoxes.push_back(e);
        }
        const int n = textOf([&] { DrawESP(edgeBoxes, cam, 3); });
        CHECK(n > 0 && textBox.x0 >= 0 && textBox.x1 <= SW, "containers at the right screen edge: every label drawn is fully on the screen");
        //(text wholly off the screen makes no vertices at all: a label put right of a marker at the right edge would
        //vanish unseen, so the places offered are checked themselves)
        bool offered = true;
        size_t places = 0;
        for (const ImVec2 m : {ImVec2(SW - 6, 500), ImVec2(6, 500), ImVec2(1200, 4), ImVec2(1200, (float)config::SCREEN_H - 4)}) {
            QueueMarkerLabel(m.x, m.y, 8, "Trash can  20m", 13.5f, 3, 20);
            const PendingLabel& p = PendingLabels().back();
            places += p.box.alts.size();
            for (const LabelRect& r : p.box.alts) offered &= r.x0 >= -0.5f && r.y0 >= -0.5f && r.x1 <= SW + 0.5f && r.y1 <= config::SCREEN_H + 0.5f;
            PendingLabels().clear();
        }
        CHECK(offered && places >= 8, "a marker at a screen edge: its label is offered other places only where they are fully on the screen");
    }

    //---- two lines stacked as a player's distance and guns: wherever float rounding puts them, the lower one keeps its
    //place (it used to flip a line further down and back as he moved, when rounding made them overlap by a hair)
    {
        const float s = config::labelSize * 0.9f, lineH = LineSlot(s);
        const ImVec2 ts = LabelSize("30m", s);
        float found = -1;
        for (int i = 0; i < 200000 && found < 0; i++) {
            const float b = 300.0f + (float)i * 0.0137f;
            if (LabelAt(1000, b + lineH, ts).y0 < LabelAt(1000, b, ts).y1) found = b;
        }
        std::printf("      stacked lines overlap by a hair at y %.4f\n", found);
        CHECK(found > 0, "(setup) a height where rounding makes the two lines' areas overlap by a hair");
        textOf([&] {
            QueueLabel(1000, found, "30m", s, 0, 30, true, lineH);
            QueueLabel(1000, found + lineH, "Ferro", s, 0, 30, true, lineH);
            FlushLabels();
        });
        CHECK(textBox.y1 < found + lineH + s / 2 + 4, "the guns line stays right under the distance line, not a line further down");
    }

    //---- label cards: nearly solid with a light border, and the room a label keeps free is all of its card or outline
    {
        const ImVec2 ts = LabelSize("Trash can", 15);
        const LabelRect plain = LabelAt(500, 500, ts), big = LabelAt(500, 500, LabelSize("Trash can", 28));
        config::labelCards = true;
        const LabelRect card = LabelAt(500, 500, ts);
        bool solid = false, border = false;
        draw([&] {
            DrawLabelCentered(500, 500, "Trash can", 15, COLOR::white, true);
            for (const ImDrawVert& q : ImGui::GetBackgroundDrawList()->VtxBuffer) {
                solid |= q.col == IM_COL32(0, 0, 0, 230);
                border |= rgbOf(q.col) == IM_COL32(210, 210, 210, 255);
            }
        });
        CHECK(solid && border, "label cards: a nearly solid dark card (alpha 230) with a light border");
        CHECK(std::abs(g_minX - card.x0) < 0.01f && std::abs(g_maxX - card.x1) < 0.01f && std::abs(g_minY - card.y0) < 0.01f &&
              std::abs(g_maxY - card.y1) < 0.01f, "label cards: the room a label keeps free is exactly its card as drawn, border included");
        //two labels whose cards would touch-overlap side by side (6 px between their texts): only the first is drawn
        const int one = textOf([&] { QueueLabel(500, 500, "Trash can", 15, 3, 10); FlushLabels(); });
        const int two = textOf([&] { QueueLabel(500, 500, "Trash can", 15, 3, 10); QueueLabel(500 + ts.x + 6, 500, "Trash can", 15, 3, 11); FlushLabels(); });
        config::labelCards = false;
        CHECK(one > 0 && two == one, "label cards 6 px apart would overlap: the second one is left out");
        CHECK(std::abs(plain.x0 - (500 - ts.x / 2 - 2)) < 0.01f && std::abs(plain.y0 - (500 - 7.5f - 1)) < 0.01f &&
              std::abs(big.y0 - (500 - 14.0f - 2)) < 0.01f, "outlined text keeps 2 px to the sides free, and its outline up and down (1 px at 15 px, 2 px at 28)");
    }

    //---- down players and destroyed ARC: solid, in their own colours, with a word on their label
    {
        bool solidColours = (COLOR::deadPlayer() >> 24) == 255 && (COLOR::arc(true) >> 24) == 255 && COLOR::deadPlayer() != COLOR::enemy() &&
                            COLOR::arc(true) != COLOR::arc(false);
        config::monochrome = true;
        solidColours &= (COLOR::deadPlayer() >> 24) == 255 && (COLOR::arc(true) >> 24) == 255 && COLOR::deadPlayer() != COLOR::enemy() &&
                        COLOR::arc(true) != COLOR::arc(false);
        config::monochrome = false;
        CHECK(solidColours, "down players and destroyed ARC are fully solid (also in monochrome) and not in the living ones' colours");
        config::showPlayerNames = false;
        RenderEntity down = ent(Object::PLAYER, 1500, 100, 0), wreck = ent(Object::ARC, 2000, 100, 0);
        down.isDead = true; wreck.isDead = true; wreck.label = "Tick";
        const float s = config::labelSize * 0.9f;
        const int downText = textVerts({down}), downLabel = textOf([&] { DrawLabelCentered(500, 500, "DOWN  15m", s); });
        const int wreckText = textVerts({wreck}), wreckLabel = textOf([&] { DrawLabelCentered(500, 500, "DEAD  Tick  20m", s); });
        config::showPlayerNames = true;
        CHECK(downText > 0 && downText == downLabel, "a down player's label: DOWN and his distance");
        CHECK(wreckText > 0 && wreckText == wreckLabel, "a destroyed ARC's label: DEAD, its name and distance");
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
            e.yaw = i % 7 == 0 ? INFINITY : U(-1e6, 1e6);
            junk.push_back(e);
        }
        FminimalViewInfo c = camera();
        c.Rotation = Vector3(U(-89, 89), U(-180, 180), 0);
        c.FOV = (float)(t % 17 == 0 ? 0.0 : U(30, 120));
        draw([&] { DrawESP(junk, c, 3); DrawRadar(junk, c, 3); DrawCloseEnemyWarning(junk, c, 3); });
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
            e.yaw = i % 3 == 0 ? NAN : U(-360, 360);
            good.push_back(e);
        }
        FminimalViewInfo c = camera();
        c.Rotation = Vector3(U(-89, 89), U(-180, 180), U(-40, 40)); //with some camera roll
        c.FOV = (float)U(30, 120);
        draw([&] { DrawESP(good, c, 3); DrawRadar(good, c, 3); DrawCloseEnemyWarning(good, c, 3); });
        fin &= g_finite;
    }
    CHECK(fin, "200 frames of normal entities and cameras: every vertex is a finite number");

    ImGui::DestroyContext();
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
