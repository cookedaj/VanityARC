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
static float g_minY = 1e9f; //the highest point on screen that was drawn (smallest y)
template <typename F> static int draw(F fn) {
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    fn();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    g_verts = dl->VtxBuffer.Size;
    g_finite = true;
    g_minY = 1e9f;
    for (const ImDrawVert& v : dl->VtxBuffer) {
        if (!std::isfinite(v.pos.x) || !std::isfinite(v.pos.y)) g_finite = false;
        else g_minY = std::min(g_minY, v.pos.y);
    }
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
        config::overlayOpacity = 1.0f;
        CHECK(worst > 0 && worst <= 129, "overlay opacity 0.5: nothing is more than half opaque");
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

    //---- the spoken warning when an enemy comes close (camera at 0,0,0 looking along +x; your team is 3)
    {
        config::cueRange = 40;
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

    //---- your own health: nothing when not read, the readout when read, the LOW HEALTH card under 35 (not at 0: down)
    {
        const int unread = draw([&] { DrawSelfHealth(-1, 50); });
        const int nan = draw([&] { DrawSelfHealth(NAN, -1); });
        const int fine = draw([&] { DrawSelfHealth(80, 50); });
        const int low = draw([&] { DrawSelfHealth(30, 50); });
        const int down = draw([&] { DrawSelfHealth(0, 50); });
        CHECK(unread == 0 && nan == 0, "own health not read: nothing is drawn");
        CHECK(fine > 0 && low > fine && down < low, "own health: readout, plus the LOW HEALTH card under 35 only");
        draw([&] { DrawSelfHealth(30, 50); });
        CHECK(g_finite, "own health: every vertex is a finite number");
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
