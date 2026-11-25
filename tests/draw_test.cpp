//Projection (against the original formula) and label decluttering. No window needed.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include "draw/Declutter.h"
#include "draw/Marking.h"
#include "draw/W2S.h"

static int fails = 0;
#define CHECK(cond, msg) do { bool ok_ = (cond); std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) fails++; } while (0)

//The projection exactly as it was before the rewrite
static Vector2 oldW2S(Vector3 WorldLocation, FminimalViewInfo CameraInfo, int Width, int Height, bool FovIsHorizontal) {
    Vector3 Delta = WorldLocation - CameraInfo.Location;
    double Pi = 3.14159265358979;
    double Yaw = CameraInfo.Rotation.y * Pi / 180.0, Pitch = CameraInfo.Rotation.x * Pi / 180.0;
    Vector3 Forward(cos(Pitch) * cos(Yaw), cos(Pitch) * sin(Yaw), sin(Pitch));
    Vector3 Right(-sin(Yaw), cos(Yaw), 0.0);
    Vector3 Up(-sin(Pitch) * cos(Yaw), -sin(Pitch) * sin(Yaw), cos(Pitch));
    double CamX = Delta.Dot(Right), CamY = Delta.Dot(Up), CamZ = Delta.Dot(Forward);
    if (CamZ < 1.0) return Vector2(-9999.0, -9999.0);
    double TanHalfFov = tan((CameraInfo.FOV * Pi / 180.0) / 2.0);
    double Focal = FovIsHorizontal ? (Width / 2.0) / TanHalfFov : (Height / 2.0) / TanHalfFov;
    return Vector2((Width / 2.0) + (CamX / CamZ) * Focal, (Height / 2.0) - (CamY / CamZ) * Focal);
}

static FminimalViewInfo cam(double x, double y, double z, double pitch, double yaw, float fov) {
    FminimalViewInfo c;
    c.Location = Vector3(x, y, z);
    c.Rotation = Vector3(pitch, yaw, 0);
    c.FOV = fov;
    return c;
}

int main() {
    std::mt19937_64 rng(42);
    auto U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };

    //--- same answers as the original formula
    int mismatches = 0, compared = 0, behindAgree = 0, behind = 0;
    for (int i = 0; i < 20000; i++) {
        FminimalViewInfo c = cam(U(-1e5, 1e5), U(-1e5, 1e5), U(0, 5e3), U(-80, 80), U(-180, 180), (float)U(40, 110));
        Vector3 p(c.Location.x + U(-8e3, 8e3), c.Location.y + U(-8e3, 8e3), c.Location.z + U(-2e3, 2e3));
        bool horiz = i & 1;
        Vector2 a = oldW2S(p, c, 2560, 1440, horiz), b = WorldToScreen(p, c, 2560, 1440, horiz);
        if (a.x == -9999.0) { behind++; if (b.x == -9999.0) behindAgree++; continue; }
        compared++;
        if (std::abs(a.x - b.x) > 1e-6 || std::abs(a.y - b.y) > 1e-6) mismatches++;
    }
    CHECK(mismatches == 0 && compared > 5000, "20000 random points: same screen position as the original formula");
    CHECK(behind > 1000 && behindAgree == behind, "points behind the camera are rejected the same way");

    //--- calibration
    FminimalViewInfo c = cam(0, 0, 0, 0, 0, 70);
    Projection p; p.width = 2560; p.height = 1440;
    View v(c, p);
    Vector2 mid, side;
    CHECK(v.ok && v.project(Vector3(1000, 0, 0), mid) && std::abs(mid.x - 1280) < 1e-9 && std::abs(mid.y - 720) < 1e-9, "straight ahead is the middle of the screen");
    v.project(Vector3(1000, 100, 0), side);
    double d1 = side.x - 1280; //camera right is +y at yaw 0 in this convention
    p.scale = 1.5; View v2(c, p); Vector2 side2; v2.project(Vector3(1000, 100, 0), side2);
    CHECK(std::abs((side2.x - 1280) - 1.5 * d1) < 1e-9, "Zoom 1.5 moves a marker 1.5x as far from the middle");
    p.scale = 1.0; p.offX = 40; p.offY = -25; View v3(c, p); Vector2 mid3; v3.project(Vector3(1000, 0, 0), mid3);
    CHECK(std::abs(mid3.x - 1320) < 1e-9 && std::abs(mid3.y - 695) < 1e-9, "Shift X / Y moves the middle");
    p.offX = p.offY = 0; p.fovHorizontal = true; View v4(c, p); Vector2 side4; v4.project(Vector3(1000, 100, 0), side4);
    CHECK(std::abs((side4.x - 1280) - d1 * (2560.0 / 1440.0)) < 1e-9, "horizontal FOV basis: the focal length is width based");
    CHECK(!View(cam(0, 0, 0, 0, 0, 0.0f), Projection{}).ok, "a FOV of 0 gives an unusable camera, not a crash");
    Vector2 none;
    CHECK(!View(cam(0, 0, 0, 0, 0, 0.0f), Projection{}).project(Vector3(1000, 0, 0), none), "and projects nothing");
    CHECK(!v.project(Vector3(-1000, 0, 0), none), "behind the camera is rejected");

    //--- a stretched picture: the game draws 1440x1080, the overlay is 2560x1440
    {
        FminimalViewInfo c2 = cam(0, 0, 0, 0, 0, 70);
        Projection pg; pg.width = 1440; pg.height = 1080; pg.dx = 0; pg.dy = 0; pg.dw = 2560; pg.dh = 1440;
        View vg(c2, pg);
        Vector3 pt(1000, 100, 50);
        Vector2 inGame, onScreen;
        Projection plain; plain.width = 1440; plain.height = 1080; //the same picture, not stretched
        View vp(c2, plain);
        vp.project(pt, inGame);
        vg.project(pt, onScreen);
        CHECK(std::abs(onScreen.x - inGame.x * 2560.0 / 1440.0) < 1e-9 && std::abs(onScreen.y - inGame.y * 1440.0 / 1080.0) < 1e-9, "stretched: a point lands where the game draws it, then stretched like the picture");
        Vector2 mid2;
        vg.project(Vector3(1000, 0, 0), mid2);
        CHECK(std::abs(mid2.x - 1280) < 1e-9 && std::abs(mid2.y - 720) < 1e-9, "stretched: straight ahead is still the middle");
        Projection pb = pg; pb.dx = 320; pb.dw = 1920; pb.dy = 0; pb.dh = 1440; //kept in shape, bars at the sides
        View vb(c2, pb);
        vb.project(Vector3(1000, 0, 0), mid2);
        CHECK(std::abs(mid2.x - 1280) < 1e-9 && std::abs(vb.sx - vb.sy) < 1e-9, "kept in shape: same stretch both ways, centered");
    }


    //--- label places: a label that finds its first place taken tries the others
    {
        std::vector<LabelReq> r(3);
        r[0] = {0, 0, 100, 20, 1, 5.0, false, {}};
        r[1] = {10, 0, 110, 20, 2, 6.0, false, {{0, 30, 100, 50}, {0, 60, 100, 80}}};   //taken, the first other place is free
        r[2] = {20, 0, 120, 20, 3, 7.0, false, {{0, 30, 100, 50}, {200, 0, 300, 20}}}; //taken, the first other place is now taken too
        auto pl = ChoosePlaces(r);
        CHECK(pl.size() == 3 && pl[1].index == 1 && pl[1].alt == 0 && pl[2].index == 2 && pl[2].alt == 1, "taken first place: the next free place is used");
        r[2].alts = {{0, 30, 100, 50}};
        CHECK(ChoosePlaces(r).size() == 2, "no free place at all: dropped");
        r[2].force = true;
        auto forced = ChoosePlaces(r);
        CHECK(forced.size() == 3 && forced[0].alt == -1, "forced labels keep their first place");
    }

    //--- camera roll: Unreal's rotation matrix
    {
        bool orthonormal = true;
        for (int i = 0; i < 2000; i++) {
            FminimalViewInfo c3 = cam(0, 0, 0, U(-89, 89), U(-180, 180), 70);
            c3.Rotation.z = U(-180, 180);
            View vv(c3, Projection{});
            auto dot = [](const Vector3& a, const Vector3& b) { return a.Dot(b); };
            orthonormal &= std::abs(dot(vv.fwd, vv.right)) < 1e-9 && std::abs(dot(vv.fwd, vv.up)) < 1e-9 && std::abs(dot(vv.right, vv.up)) < 1e-9 &&
                           std::abs(dot(vv.fwd, vv.fwd) - 1) < 1e-9 && std::abs(dot(vv.right, vv.right) - 1) < 1e-9 && std::abs(dot(vv.up, vv.up) - 1) < 1e-9;
        }
        CHECK(orthonormal, "2000 random cameras with roll: forward, right and up stay a proper set of axes");
        FminimalViewInfo banked = cam(0, 0, 0, 0, 0, 70);
        banked.Rotation.z = 90;
        Vector2 p0, p1;
        View vb(banked, Projection{});
        vb.project(Vector3(1000, 100, 0), p0);
        CHECK(std::abs(p0.x - 1280) < 1e-6 && p0.y < 720 - 10, "rolled 90 degrees: what was to the right is now above the middle");
        Projection noRoll; noRoll.useRoll = false;
        View vn(banked, noRoll), vz(cam(0, 0, 0, 0, 0, 70), Projection{});
        Vector2 a2, b2;
        vn.project(Vector3(1000, 100, 0), a2); vz.project(Vector3(1000, 100, 0), b2);
        CHECK(std::abs(a2.x - b2.x) < 1e-9 && std::abs(a2.y - b2.y) < 1e-9, "'use camera roll' off: the roll is ignored");
        FminimalViewInfo junkRoll = cam(0, 0, 0, 0, 0, 70);
        junkRoll.Rotation.z = 1e9;
        View vj(junkRoll, Projection{});
        Vector2 j2;
        vj.project(Vector3(1000, 100, 0), j2);
        CHECK(std::abs(j2.x - b2.x) < 1e-9 && std::abs(j2.y - b2.y) < 1e-9, "a garbage roll counts as no roll");
    }

    //--- F6: mark the player at the crosshair
    {
        marks::clear();
        FminimalViewInfo c4 = cam(0, 0, 0, 0, 0, 70);
        View v4(c4, Projection{});
        auto player = [](double x, double y, const char* account, int team = 3) {
            RenderEntity e; e.type = Object::PLAYER; e.pos = Vector3(x, y, 0); e.account = account; e.label = std::string("p_") + account; e.teamID = team; return e;
        };
        std::vector<RenderEntity> es = {player(2000, 40, "aaaa"), player(2000, 900, "bbbb")};
        auto r1 = MarkNearestAtCrosshair(es, v4, 7);
        CHECK(r1.kind == MarkResult::Marked && marks::isMarked("aaaa") && !marks::isMarked("bbbb"), "F6 marks the player closest to the crosshair");
        auto r2 = MarkNearestAtCrosshair(es, v4, 7);
        CHECK(r2.kind == MarkResult::Unmarked && !marks::isMarked("aaaa"), "F6 again removes the mark");
        std::vector<RenderEntity> two = {player(2000, 40, "cccc"), player(2000, -50, "dddd")};
        CHECK(MarkNearestAtCrosshair(two, v4, 7).kind == MarkResult::Ambiguous && !marks::isMarked("cccc") && !marks::isMarked("dddd"), "two players near the crosshair: nothing is marked");
        std::vector<RenderEntity> far2 = {player(2000, 2500, "eeee")};
        CHECK(MarkNearestAtCrosshair(far2, v4, 7).kind == MarkResult::NoneNearby, "nobody near the crosshair: nothing is marked");
        std::vector<RenderEntity> noId = {player(2000, 40, "")};
        CHECK(MarkNearestAtCrosshair(noId, v4, 7).kind == MarkResult::NoId, "a player without an account id cannot be marked");
        std::vector<RenderEntity> squad = {player(2000, 40, "ffff", 7), player(2000, 300, "gggg")};
        auto r3 = MarkNearestAtCrosshair(squad, v4, 7);
        CHECK(r3.kind == MarkResult::NotMarkable && !r3.down && !marks::isMarked("gggg") && !marks::isMarked("ffff"), "aiming at your squad mate marks nobody (not the player behind them)");
        std::vector<RenderEntity> dead = {player(2000, 40, "hhhh")};
        dead[0].isDead = true;
        auto r4 = MarkNearestAtCrosshair(dead, v4, 7);
        CHECK(r4.kind == MarkResult::NotMarkable && r4.down && !marks::isMarked("hhhh"), "a down player is not marked");
        std::vector<RenderEntity> behind = {player(-2000, 0, "iiii")};
        CHECK(MarkNearestAtCrosshair(behind, v4, 7).kind == MarkResult::NoneNearby, "someone behind you is not at the crosshair");
        marks::clear();
    }

    //--- decluttering
    {
        std::vector<LabelReq> r(3);
        r[0] = {0, 0, 100, 20, 3, 30.0, false};
        r[1] = {50, 0, 150, 20, 3, 10.0, false}; //overlaps 0, closer: it wins
        r[2] = {300, 0, 400, 20, 3, 50.0, false};
        auto ch = ChooseLabels(r);
        CHECK(ch.size() == 2 && ch[0] == 1 && ch[1] == 2, "overlapping labels: the closer one is drawn, the other dropped");
    }
    {
        std::vector<LabelReq> r(2);
        r[0] = {0, 0, 100, 20, 3, 1.0, false};   //loot, very close
        r[1] = {10, 0, 110, 20, 1, 90.0, false}; //ARC, far, but more important
        auto ch = ChooseLabels(r);
        CHECK(ch.size() == 1 && ch[0] == 1, "a more important kind of label beats a closer, less important one");
    }
    {
        std::vector<LabelReq> r(2);
        r[0] = {0, 0, 100, 20, 0, 5.0, true};
        r[1] = {10, 0, 110, 20, 0, 6.0, true};
        CHECK(ChooseLabels(r).size() == 2, "forced labels (players) are always drawn, even over each other");
        r.push_back({20, 0, 120, 20, 3, 1.0, false});
        CHECK(ChooseLabels(r).size() == 2, "and nothing else is drawn on top of them");
    }
    CHECK(ChooseLabels({}).empty(), "no labels: nothing to do");

    bool inv = true;
    for (int t = 0; t < 300; t++) {
        std::vector<LabelReq> r(60);
        for (auto& l : r) {
            l.x0 = (float)U(0, 400); l.y0 = (float)U(0, 200); l.x1 = l.x0 + (float)U(10, 90); l.y1 = l.y0 + (float)U(8, 20);
            l.prio = (int)U(0, 4); l.dist = U(0, 100); l.force = U(0, 1) < 0.1;
        }
        auto ch = ChooseLabels(r);
        std::vector<bool> in(r.size(), false);
        for (size_t i : ch) in[i] = true;
        for (size_t a = 0; a < ch.size(); a++)
            for (size_t b = 0; b < a; b++) //a came after b
                if (!r[ch[a]].force && LabelsOverlap(r[ch[a]], r[ch[b]])) inv = false;
        for (size_t i = 0; i < r.size(); i++) {
            if (in[i] || r[i].force) continue;
            bool blocked = false;
            for (size_t j : ch) if (LabelsOverlap(r[i], r[j])) blocked = true;
            if (!blocked) inv = false; //dropped for no reason
        }
    }
    CHECK(inv, "300 random sets: nothing drawn overlaps (except forced), nothing is dropped without a reason");

    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
