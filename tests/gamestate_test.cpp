//The fast read path (GameState::step) against the original one (GameState::tick) on a fake game, then the
//tracker thread running against it while the menu thread hammers the name list.
#include <sys/mman.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include "config.h"
#include "gamestate/GameState.h"
#include "gamestate/Tracker.h"
#include "utils/labels.h"
#include "utils/names.h"

static int fails = 0;
#define CHECK(cond, msg) do { bool ok_ = (cond); std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) fails++; } while (0)

typedef uint64_t u64;
static u64 base, bump;
static u64 alloc(size_t n) { bump = (bump + 15) & ~15ULL; u64 a = bump; bump += n; return a; } //fresh pages are zero
template <typename T> static void put(u64 a, T v) { std::memcpy((void*)a, &v, sizeof v); }
template <typename T> static T get(u64 a) { T v; std::memcpy(&v, (void*)a, sizeof v); return v; }

//--- fake game: an actor is a block with a vtable word at +0, RootComponent at +0x248, position inside the root
static u64 makeRoot(double x, double y, double z) {
    u64 r = alloc(0x3D0);
    put<double>(r + off::POS_PTR, x); put<double>(r + off::POS_PTR + 8, y); put<double>(r + off::POS_PTR + 16, z);
    return r;
}
static u64 makeActor(u64 vt, double x, double y, double z) {
    u64 a = alloc(0x1240);
    put<u64>(a, vt);
    put<u64>(a + off::ROOT_COMPONENT_PTR, makeRoot(x, y, z));
    return a;
}
static u64 makePlayer(double x, double y, double z, int team, double hp) {
    u64 a = makeActor(vtabels::PLAYER, x, y, z);
    u64 hc = alloc(0x720);
    put<double>(hc + off::HEALTH_MAX, 100.0); put<double>(hc + off::HEALTH_CACHED, hp);
    put<u64>(a + off::HEALTH_COMPONENT, hc);
    put<uint8_t>(a + off::TEAM_ID, (uint8_t)team);
    return a;
}
static void setPos(u64 actor, double x, double y, double z) {
    u64 r = get<u64>(actor + off::ROOT_COMPONENT_PTR);
    put<double>(r + off::POS_PTR, x); put<double>(r + off::POS_PTR + 8, y); put<double>(r + off::POS_PTR + 16, z);
}
static void setHp(u64 player, double hp) { put<double>(get<u64>(player + off::HEALTH_COMPONENT) + off::HEALTH_CACHED, hp); }

//A character mesh: its own transform (turned 90 degrees about Z, at the given place) and a list of 97 bone transforms where
//every bone sits 10 cm above its parent, except bone 8 (a shoulder) which sits 10 cm along the parent's X
static u64 giveMesh(u64 actor, double x, double y, double z) {
    u64 mesh = alloc(0xC00);
    put<u64>(actor + off::MESH, mesh);
    const double h = 0.70710678118654752;
    double q[4] = {0, 0, h, h}, t[3] = {x, y, z}, s[3] = {1, 1, 1};
    std::memcpy((void*)(mesh + off::MESH_TRANSFORM), q, 32);
    std::memcpy((void*)(mesh + off::MESH_TRANSFORM + 0x20), t, 24);
    std::memcpy((void*)(mesh + off::MESH_TRANSFORM + 0x40), s, 24);
    u64 bones = alloc(97 * 0x60);
    for (int i = 0; i < 97; i++) {
        double bq[4] = {0, 0, 0, 1}, bt[3] = {0, 0, i == 0 ? 0.0 : 10.0}, bs[3] = {1, 1, 1};
        if (i == 8) { bt[0] = 10; bt[2] = 0; }
        std::memcpy((void*)(bones + 0x60 * (u64)i), bq, 32);
        std::memcpy((void*)(bones + 0x60 * (u64)i + 0x20), bt, 24);
        std::memcpy((void*)(bones + 0x60 * (u64)i + 0x40), bs, 24);
    }
    put<u64>(mesh + off::BONE_ARRAY, bones);
    put<int>(mesh + off::BONE_ARRAY + 8, 97);
    put<int>(mesh + off::BONE_ARRAY + 12, 97);
    return mesh;
}

static u64 camActor, uworldObj, levelObj, actorArray;
static std::vector<u64> actorList;
static void publishActors() {
    put<u64>(levelObj + off::ACTORS_PTR, actorArray);
    for (size_t i = 0; i < actorList.size(); i++) put<u64>(actorArray + 8 * i, actorList[i]);
    put<int>(levelObj + off::ACTORS_PTR + 8, (int)actorList.size());
}
static void setCamera(double x, double y, double z, double pitch, double yaw, float fov) {
    u64 c = camActor + off::VIEW_MATRIX;
    put<double>(c, x); put<double>(c + 8, y); put<double>(c + 16, z);
    put<double>(c + 0x28, pitch); put<double>(c + 0x30, yaw); put<double>(c + 0x38, 0.0);
    put<float>(c + 0x50, fov);
}
static u64 g_chain0, g_chain1, g_chain2;
static void buildWorld() {
    g_chain0 = alloc(0x400); g_chain1 = alloc(0x1000); g_chain2 = alloc(0x400); //g_chain1 is the PlayerController
    uworldObj = alloc(0x400);
    u64 lc = alloc(0x40); levelObj = alloc(0x200);
    put<u64>(base + off::UWORLD_RVA, g_chain0);
    put<u64>(g_chain0 + off::UWORLD_CHAIN_1, g_chain1);
    put<u64>(g_chain1 + off::UWORLD_CHAIN_2, g_chain2);
    put<u64>(g_chain2 + off::UWORLD_CHAIN_3, uworldObj);
    put<u64>(uworldObj + off::LEVEL_COLLECTIONS, lc);
    put<u64>(lc + off::PERSISTENT_LEVEL, levelObj);
}
static void breakWorld() { put<u64>(g_chain2 + off::UWORLD_CHAIN_3, 0); }
static void fixWorld() { put<u64>(g_chain2 + off::UWORLD_CHAIN_3, uworldObj); }

//--- comparing the two read paths
static bool sameWorld(const gsRet& a, const gsRet& b, const char** why) {
    std::map<ptr, const RenderEntity*> ma, mb;
    for (const auto& e : a.entities) if (e.type != Object::NONE) ma[e.actor] = &e;
    for (const auto& e : b.entities) if (e.type != Object::NONE) mb[e.actor] = &e;
    if (ma.size() != mb.size()) { *why = "different number of entities"; return false; }
    for (auto& kv : ma) {
        auto it = mb.find(kv.first);
        if (it == mb.end()) { *why = "an actor is missing"; return false; }
        const RenderEntity &x = *kv.second, &y = *it->second;
        if (x.type != y.type) { *why = "type"; return false; }
        if (!(x.pos == y.pos)) { *why = "position"; return false; }
        if (std::abs(x.dist - y.dist) > 1e-9) { *why = "distance"; return false; }
        if (x.type == Object::PLAYER && (x.playerHealth != y.playerHealth || x.teamID != y.teamID)) { *why = "health/team"; return false; }
        if (x.isDead != y.isDead) { *why = "dead flag"; return false; }
        if (x.label != y.label || x.sub != y.sub) { *why = "label"; return false; }
    }
    if (a.localPlayerTeam != b.localPlayerTeam) { *why = "local team"; return false; }
    if (a.vm.Location == b.vm.Location && a.vm.FOV == b.vm.FOV) return true;
    *why = "camera";
    return false;
}

static double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int main() {
    ProcessId = getpid();
    const size_t size = 0x30000000;
    uint8_t* region = (uint8_t*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    base = (u64)region;
    bump = base + 0x10A00000;
    names::setBase(base);
    labels::setDir("/tmp/kwinhelper_gamestate_test_unused"); //never writes: the test does not call save()

    buildWorld();
    camActor = alloc(0x1000);
    put<u64>(camActor, vtabels::CAMMANAGER);
    put<u64>(camActor + off::ROOT_COMPONENT_PTR, makeRoot(0, 0, 0));
    setCamera(100000, 200000, 30000, -5, 90, 70.0f);
    const double cx = 100000, cy = 200000, cz = 30000;

    u64 me = makePlayer(cx + 80, cy + 120, cz - 90, 3, 100);       //about 1.4 m from the camera: that is you
    u64 enemy1 = makePlayer(cx + 4000, cy + 100, cz - 90, 7, 80);
    u64 enemy2 = makePlayer(cx - 9000, cy + 3000, cz - 90, 9, 42.5);
    u64 corpse = makePlayer(cx + 1500, cy - 2500, cz - 90, 7, 0);
    u64 mate = makePlayer(cx + 2500, cy + 2500, cz - 90, 3, 100);
    u64 arc1 = makeActor(vtabels::ARC, cx + 6000, cy - 1000, cz - 50);
    u64 arc2 = makeActor(vtabels::ARC, cx - 3000, cy - 4000, cz - 50);
    put<uint8_t>(arc2 + off::IS_DESTROYED, 1);
    u64 noRoot = makeActor(vtabels::PLAYER, cx + 100, cy + 5000, cz); put<u64>(noRoot + off::ROOT_COMPONENT_PTR, 0);
    u64 nanPos = makeActor(vtabels::SEARCH, 1, 1, 1); setPos(nanPos, NAN, 0, 0);
    u64 tooNear = makeActor(vtabels::SEARCH, 50, 400, 0);       //x < 100: dropped by the original too
    u64 pickupVt = makeActor(vtabels::PICKUP, cx + 300, cy + 300, cz - 90); //old guess for ground items: never drawn
    std::vector<u64> containers;
    for (int i = 0; i < 25; i++) containers.push_back(makeActor(vtabels::SEARCH, cx + 500 + 700 * i, cy - 3000 + 90 * i, cz - 100));

    actorList = {me, enemy1, enemy2, corpse, mate, arc1, arc2, noRoot, nanPos, tooNear, pickupVt, camActor, 0, 0x1234, 0xFFFF000000000000ULL};
    for (u64 c : containers) actorList.push_back(c);
    for (int i = 0; i < 30; i++) actorList.push_back(makeActor(0x14d000000ULL + 0x10000ULL * i, cx + 100 * i, cy, cz)); //other kinds of actor
    actorArray = alloc(8 * 20000);
    publishActors();

    GameState ref(base), fast(base);
    const char* why = "";
    auto compare = [&](const char* what) {
        gsRet a = ref.tick(), b = fast.step();
        char msg[160];
        bool same = sameWorld(a, b, &why);
        std::snprintf(msg, sizeof msg, "%s%s%s", what, same ? "" : " -> ", same ? "" : why);
        CHECK(same && a.valid && b.valid, msg);
        return b;
    };

    //--- same world through both paths
    gsRet r0 = compare("start: step() shows what tick() shows");
    int players = 0, arcs = 0, statics = 0;
    for (const auto& e : r0.entities) { players += e.type == Object::PLAYER; arcs += e.type == Object::ARC; statics += e.type == Object::SEARCH; }
    CHECK(players == 4 && arcs == 2 && statics == 25, "4 other players (you are left out), 2 ARC, 25 containers");
    CHECK(r0.localPlayerTeam == 3, "your team is the team of the player next to the camera");
    CHECK(r0.valid && std::abs(r0.vm.FOV - 70.0) < 1e-6 && r0.vm.Location.x == cx, "camera read");

    //--- things that move, between scans (no new scan yet)
    setPos(enemy1, cx + 4100, cy + 150, cz - 90); setHp(enemy1, 20);
    put<uint8_t>(arc1 + off::IS_DESTROYED, 1);
    setHp(enemy2, 0);
    setCamera(cx + 10, cy, cz, -3, 91, 75.0f);
    gsRet r1 = compare("after players and ARC moved / died / the camera turned");
    bool found = false;
    for (const auto& e : r1.entities) if (e.actor == enemy1) found = e.pos.x == cx + 4100 && e.playerHealth == 20;
    CHECK(found, "the moved player is at his new place with his new health");

    //--- which way a player faces: his root's rotation, read together with the position (all zero is not a rotation)
    {
        const double h = 0.70710678118654752, q[4] = {0, 0, h, h}; //turned 90 degrees about Z: facing +Y
        std::memcpy((void*)(get<u64>(enemy1 + off::ROOT_COMPONENT_PTR) + off::POS_PTR - 0x20), q, 32);
        gsRet rf = fast.step();
        double y1 = 0, y2 = 0;
        for (const auto& e : rf.entities) { if (e.actor == enemy1) y1 = e.yaw; if (e.actor == mate) y2 = e.yaw; }
        CHECK(std::abs(y1 - 90.0) < 1e-6, "a player's facing comes from his root's rotation (90 degrees: along +Y)");
        CHECK(std::isnan(y2), "no rotation in memory: the facing is unknown, not 0");
    }

    //--- skeleton: a player with a mesh and a 97-bone list gets a stick figure; one without a mesh keeps the box only
    {
        giveMesh(enemy1, cx + 4100, cy + 150, cz - 182);
        auto findPlayer = [&](const gsRet& r, u64 actor) -> const RenderEntity* {
            for (const auto& e : r.entities) if (e.actor == actor) return &e;
            return nullptr;
        };
        fast.step(); //the first step after the camera moved only learns where the camera is
        gsRet rs = fast.step();
        const RenderEntity* e1 = findPlayer(rs, enemy1);
        const RenderEntity* e2 = findPlayer(rs, mate);
        CHECK(e1 && e1->pose, "a player with a mesh and 97 bones gets a pose");
        if (e1 && e1->pose) {
            const Vector3& head = e1->pose->j[skeleton::HEAD];
            const Vector3& sh = e1->pose->j[skeleton::SHOULDER_A];
            CHECK(std::abs(head.x - (cx + 4100)) < 1e-6 && std::abs(head.y - (cy + 150)) < 1e-6 && std::abs(head.z - (cz - 112)) < 1e-6, "head is 70 cm above the mesh");
            CHECK(std::abs(sh.x - (cx + 4100)) < 1e-6 && std::abs(sh.y - (cy + 160)) < 1e-6 && std::abs(sh.z - (cz - 142)) < 1e-6, "the shoulder follows its parent and the mesh's turn");
        }
        CHECK(e2 && !e2->pose, "a player without a mesh has no pose");
        g_skeletonOn = false;
        fast.step();
        gsRet roff = fast.step();
        const RenderEntity* e1off = findPlayer(roff, enemy1);
        CHECK(e1off && !e1off->pose, "with the setting off no pose is read");
        g_skeletonOn = true;
        put<int>(get<u64>(enemy1 + off::MESH) + off::BONE_ARRAY + 8, 50); //a list of another length is not the skeleton
        fast.step();
        gsRet rlen = fast.step();
        const RenderEntity* e1len = findPlayer(rlen, enemy1);
        CHECK(e1len && e1len->pose, "a bone list that cannot be read keeps the last whole pose for a moment");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        fast.step();
        gsRet rlen2 = fast.step();
        const RenderEntity* e1len2 = findPlayer(rlen2, enemy1);
        CHECK(e1len2 && !e1len2->pose, "but not for long: then there is no pose");
        put<int>(get<u64>(enemy1 + off::MESH) + off::BONE_ARRAY + 8, 97);

        //The game stops animating a player it is not drawing: bones that stop changing are not drawn as a live skeleton
        fast.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        fast.step();
        gsRet rfrozen = fast.step();
        const RenderEntity* e1frozen = findPlayer(rfrozen, enemy1);
        CHECK(e1frozen && e1frozen->pose && !e1frozen->poseLive, "bones unchanged for a while: the skeleton is kept but marked as not live");
        const u64 bones = get<u64>(get<u64>(enemy1 + off::MESH) + off::BONE_ARRAY);
        put<double>(bones + 0x60 * 1 + 0x20 + 8, 3.0); //the pelvis moves: the animation is running again
        std::this_thread::sleep_for(std::chrono::milliseconds(20)); //past BONE_PERIOD: the list is read again
        fast.step();
        gsRet rmoving = fast.step();
        const RenderEntity* e1moving = findPlayer(rmoving, enemy1);
        CHECK(e1moving && e1moving->pose && e1moving->poseLive, "and it is live again as soon as the bones change");
    }

    //--- a frozen player is drawn like the animated players in his stance
    {
        auto findPlayer = [&](const gsRet& r, u64 actor) -> const RenderEntity* {
            for (const auto& e : r.entities) if (e.actor == actor) return &e;
            return nullptr;
        };
        const u64 bones1 = get<u64>(get<u64>(enemy1 + off::MESH) + off::BONE_ARRAY);
        put<double>(bones1 + 0x60 * 1 + 0x20 + 16, 85.0); //an animated, standing player: the pelvis is high, so this is a standing pose to learn
        fast.step();
        fast.step();
        giveMesh(mate, cx + 2500, cy + 2500, cz - 182);   //a frozen player of the same stance: his own list never changes
        fast.step();                                      //its list is seen for the first time
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        fast.step();
        gsRet rm = fast.step();
        const RenderEntity* m = findPlayer(rm, mate);
        const RenderEntity* e = findPlayer(rm, enemy1);
        CHECK(m && m->pose && !m->poseLive, "the frozen player has a pose, drawn as not live");
        if (m && m->pose && e && e->pose) {
            const double mh = m->pose->j[skeleton::HEAD].z - m->pose->j[skeleton::PELVIS].z;
            const double eh = e->pose->j[skeleton::HEAD].z - e->pose->j[skeleton::PELVIS].z;
            CHECK(std::abs(mh - eh) < 1e-6, "and it is the shape the game animates for that stance, not his own old list");
            CHECK(std::abs(m->pose->j[skeleton::PELVIS].z - (cz - 182 + 85)) < 1e-6, "placed on him, at his own place");
        }
    }

    //--- visibility: the game rewrites the time of its last draw on every frame it draws him; a time that stopped changing means not drawn right now
    {
        auto findPlayer = [&](const gsRet& r, u64 actor) -> const RenderEntity* {
            for (const auto& e : r.entities) if (e.actor == actor) return &e;
            return nullptr;
        };
        const u64 m1 = get<u64>(enemy1 + off::MESH), m2 = get<u64>(mate + off::MESH);
        auto drawAt = [](u64 m, float t) { put<float>(m + off::RENDER_TIME, t); put<float>(m + off::RENDER_TIME + 4, t); };
        drawAt(m1, 100.0f); drawAt(m2, 90.0f);
        fast.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(300)); //the game draws enemy1 again, not the mate
        drawAt(m1, 100.3f);
        fast.step();
        gsRet rv = fast.step();
        const RenderEntity* a = findPlayer(rv, enemy1);
        const RenderEntity* b = findPlayer(rv, mate);
        CHECK(a && a->isVisible, "a player the game keeps drawing is visible");
        CHECK(b && !b->isVisible, "a player whose draw time stopped changing is not");
        drawAt(m2, 100.3f);
        fast.step();
        gsRet rv2 = fast.step();
        const RenderEntity* b2 = findPlayer(rv2, mate);
        CHECK(b2 && b2->isVisible, "and visible again once he is drawn");
        drawAt(m2, 1e9f); drawAt(m1, 100.6f); //one garbage time (a reused mesh, say)
        fast.step();
        gsRet rv3 = fast.step();
        const RenderEntity* a3 = findPlayer(rv3, enemy1);
        CHECK(a3 && a3->isVisible, "one player's garbage draw time does not dim the others");
    }

    //--- the pawn with a PlayerController is you, wherever the camera is
    {
        auto findPlayer = [&](const gsRet& r, u64 actor) -> const RenderEntity* {
            for (const auto& e : r.entities) if (e.actor == actor) return &e;
            return nullptr;
        };
        put<u64>(mate + off::CONTROLLER, enemy1);   //any valid pointer: this pawn has a controller
        setHp(mate, 30);
        fast.step();
        gsRet rl = fast.step();
        CHECK(!findPlayer(rl, mate), "the pawn with a controller is you and is not drawn");
        CHECK(findPlayer(rl, enemy1), "a player next to nobody else's camera stays drawn");
        CHECK(rl.selfHealth == 30 && rl.selfShield < 0, "your own health is passed on (no shield offset known: no shield)");
        put<u64>(mate + off::CONTROLLER, 0);       //you died: the controller lets go of the pawn, but you are still you
        fast.step();
        gsRet rd = fast.step();
        CHECK(!findPlayer(rd, mate), "without a controller you are still the pawn you were");
        const u64 hc = get<u64>(mate + off::HEALTH_COMPONENT);
        put<u64>(mate + off::HEALTH_COMPONENT, 0); //your health cannot be read from the next scan on
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        gsRet ru = fast.step();
        CHECK(ru.valid && ru.selfHealth < 0, "your health unreadable: no number at all, never the stand-in 100");
        put<u64>(mate + off::HEALTH_COMPONENT, hc);
        setHp(mate, 100);
    }

    //--- a destroyed actor's memory reused for something else: dropped at once
    put<u64>(arc2, 0x14d123450ULL);
    put<u64>(mate, 0x14d555000ULL);
    gsRet r2 = compare("actors whose memory was reused are dropped without waiting for a scan");
    (void)r2;
    put<u64>(arc2, vtabels::ARC);
    put<u64>(mate, vtabels::PLAYER);
    compare("and come back when they are what they were");

    //--- new things appear: seen at the next scan
    u64 late = makeActor(vtabels::SEARCH, cx + 900, cy + 900, cz - 100);
    actorList.push_back(late);
    publishActors();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    gsRet r3 = compare("a container that appeared is picked up by the next scan");
    int st2 = 0; for (const auto& e : r3.entities) st2 += e.type == Object::SEARCH;
    CHECK(st2 == 26, "26 containers now");

    //--- the world goes away (loading screen) and comes back
    breakWorld();
    gsRet a = ref.tick(), b = fast.step(); //right after a scan: leaving the world is noticed at once anyway
    CHECK(!a.valid && !b.valid && b.entities.empty(), "no world: both paths report nothing");
    CHECK(b.fault != nullptr, "no world: step() says why (shown on screen)");
    fixWorld();
    compare("world back: both are back");

    //--- a camera read that is not a view: the fast path keeps the last good view for a moment, then gives up
    setCamera(cx, cy, cz, 0, 0, 0.0f);
    gsRet badA = ref.tick(), badB = fast.step();
    CHECK(!badA.valid, "FOV 0: the reference path says there is no view");
    CHECK(badB.valid && !badB.entities.empty() && badB.vm.FOV == 75.0f, "FOV 0 once: step() keeps drawing with the last good view");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    gsRet badC = fast.step();
    CHECK(!badC.valid, "a camera that stays unusable: step() says there is no view");
    CHECK(badC.fault != nullptr, "a camera that stays unusable: step() says why (shown on screen)");
    setCamera(cx, cy, cz, 0, 0, 70.0f);
    compare("camera fixed: both are back");
    setCamera(cx, cy, cz, 0, 0, 5.0f);
    compare("FOV 5 (a high-zoom scope): both paths still draw");
    setCamera(cx, cy, cz, 0, 0, 70.0f);

    //--- a patch moved the CAMMANAGER vtable: the camera is found through the PlayerController (once config.h has the offset)
    if constexpr (off::PC_CAMERA_MANAGER != 0) {
        put<u64>(g_chain1 + off::PC_CAMERA_MANAGER, camActor);
        put<u64>(camActor, 0x14c0de000ULL); //no actor has the CAMMANAGER vtable now
        GameState patched(base);            //no camera known yet
        gsRet pa = ref.tick(), pb = patched.step();
        CHECK(pa.valid && pb.valid && pb.vm.FOV == 70.0f && pb.vm.Location.x == cx,
              "stale CAMMANAGER vtable: both paths find the camera through the PlayerController");
        put<u64>(camActor, vtabels::CAMMANAGER);
    }

    //--- KWINHELPER_SELFCHECK: PASS / FAIL per game number; you are found through the PlayerController, not a vtable
    {
        const char* oldHome = std::getenv("HOME");
        const std::string keepHome = oldHome ? oldHome : "";
        const std::string home = "/tmp/kwinhelper_selfcheck_test"; //never your real ~/.config/kwin-helper
        std::filesystem::create_directories(home + "/.config/kwin-helper");
        std::filesystem::remove(home + "/.config/kwin-helper/selfcheck.txt");
        setenv("HOME", home.c_str(), 1);
        setenv("KWINHELPER_SELFCHECK", "1", 1);
        GameState sc(base);
        unsetenv("KWINHELPER_SELFCHECK");
        const u64 objVt = 0x14d000100ULL; //a first word inside the game image makes a block an object
        for (u64 o : {g_chain0, g_chain1, g_chain2, uworldObj, levelObj}) put<u64>(o, objVt);
        put<u64>(get<u64>(me + off::ROOT_COMPONENT_PTR), objVt);
        put<u64>(get<u64>(me + off::HEALTH_COMPONENT), objVt);
        const u64 meMesh = giveMesh(me, cx + 80, cy + 120, cz - 182);
        put<u64>(meMesh, objVt);
        put<u64>(me + off::CONTROLLER, g_chain1);           //g_chain1 is the PlayerController the UWorld chain passes
        put<float>(meMesh + off::RENDER_TIME + 4, 100.0f);
        sc.step();                                          //first look: you are found
        put<float>(meMesh + off::RENDER_TIME + 4, 101.0f);  //the game drew you again
        put<u64>(me, 0x14d777000ULL);                       //a stale PLAYER vtable: you are still found
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        sc.step();                                          //second look: the report
        put<u64>(me, vtabels::PLAYER);
        put<u64>(me + off::CONTROLLER, 0);
        std::ifstream f(home + "/.config/kwin-helper/selfcheck.txt");
        const std::string rep((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto has = [&](const char* s) { return rep.find(s) != std::string::npos; };
        CHECK(has("PASS  UWORLD_CHAIN_3") && has("PASS  PERSISTENT_LEVEL"), "self-check: the UWorld chain and the level pass (report saved)");
        CHECK(has("FAIL  ACTORS_PTR"), "self-check: this small test level fails the actor count (a raid has 1000 or more)");
        CHECK(has("PASS  CONTROLLER") && has("FAIL  PLAYER vtable") && has("0x14d777000"), "self-check: you are found without the PLAYER vtable, and your real vtable is named");
        CHECK(has("PASS  POS_PTR") && has("PASS  HEALTH_MAX") && has("PASS  HEALTH_CACHED") && has("PASS  MESH") && has("PASS  BONE_ARRAY") &&
              has("PASS  RENDER_TIME") && has("PASS  IS_DESTROYED"), "self-check: your position, health, mesh, bones, draw time and the ARC destroyed flag pass");
        if (keepHome.empty()) unsetenv("HOME"); else setenv("HOME", keepHome.c_str(), 1);
    }

    //--- speed (printed; the check is only that the fast path is clearly faster)
    {
        std::vector<u64> saved = actorList;
        for (int i = 0; i < 9000; i++) { u64 a2 = alloc(0x40); put<u64>(a2, 0x14c000000ULL + 0x1000ULL * (i % 50)); actorList.push_back(a2); }
        publishActors();
        fast.step(); ref.tick();
        const int N = 30;
        double t0 = nowMs();
        for (int i = 0; i < N; i++) ref.tick();
        double tTick = (nowMs() - t0) / N;
        //step() at the pace the tracker uses (every 8 ms for a second), so the scans every 250 ms are included
        double inStep = 0, worst = 0;
        int calls = 0;
        for (double begin = nowMs(); nowMs() - begin < 1000; calls++) {
            double s0 = nowMs();
            fast.step();
            double d = nowMs() - s0;
            inStep += d;
            worst = std::max(worst, d);
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        double tStep = inStep / calls;
        std::printf("      %zu actors: tick() %.2f ms per call; step() %.3f ms per call on average (slowest, a full scan: %.2f ms)\n",
                    actorList.size(), tTick, tStep, worst);
        CHECK(tStep < tTick * 0.5, "step() costs less than half of tick() on a big level, scans included");
        actorList = saved;
        publishActors();
    }

    //--- the tracker thread
    {
        Tracker tr;
        tr.useProcess(getpid(), base);
        auto t0 = std::chrono::steady_clock::now();
        tr.start();
        while (!tr.latest()->valid && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3)) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        auto s0 = tr.latest();
        CHECK(s0->valid && s0->gameFound && s0->entities.size() > 30, "tracker: first snapshot arrives and has entities");

        //the camera turns: the fresh read sees it at once, the snapshot a few ms later
        setCamera(cx, cy, cz, 0, 123, 70.0f);
        FminimalViewInfo f;
        CHECK(tr.freshCamera(f) && f.Rotation.y == 123, "freshCamera() sees the new view immediately");
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        CHECK(tr.latest()->vm.Rotation.y == 123, "the snapshot has the new view shortly after");

        setPos(enemy1, cx + 5000, cy, cz - 90);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        bool moved = false;
        auto snapNow = tr.latest(); //keep the snapshot alive while looking at it (a temporary would be freed at once)
        for (const auto& e : snapNow->entities) if (e.actor == enemy1) moved = e.pos.x == cx + 5000;
        CHECK(moved, "a moving player is followed within about 100 ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(300)); //the rate is measured over 250 ms windows
        Tracker::Stats st = tr.stats();
        std::printf("      tracker: %.0f updates/s, step %.2f ms, scan %.2f ms, %zu moving, %zu fixed\n", st.updatesPerSec, st.stepMs, st.scanMs, st.dynamics, st.statics);
        CHECK(st.updatesPerSec > 40, "tracker: more than 40 updates a second");
        {
            auto sNow = tr.latest();
            const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            CHECK(sNow->capturedNs > 0 && nowNs - sNow->capturedNs < 200000000LL, "tracker: every snapshot says when it was read (the draw loop skips old ones)");
            CHECK(sNow->inventories != nullptr, "tracker: the inventory map is always there");
        }
        tr.setInspected({enemy1});
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        { auto sInv = tr.latest(); CHECK(sInv->inventories && sInv->inventories->count(enemy1) == 1, "tracker: an inspected player gets an inventory entry (empty here: no lists named)"); }
        tr.setInspected({});
        g_trackerHz = 30;
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        Tracker::Stats slow = tr.stats();
        std::printf("      at 30 Hz: %.0f updates/s\n", slow.updatesPerSec);
        CHECK(slow.updatesPerSec > 15 && slow.updatesPerSec < 45, "tracker: the update-rate setting is followed (30 Hz)");
        g_trackerHz = 120;

        //another thread asks for names while the tracker thread names what it reads
        std::atomic<bool> go{true};
        std::thread menu([&] {
            int i = 0;
            while (go) {
                (void)labels::display(labels::Cat::Container, "BP_SocketContainer_Test_" + std::to_string(i % 20));
                i++;
            }
        });
        uint64_t last = 0; int frames = 0, moves = 0;
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);
        while (std::chrono::steady_clock::now() < until) {
            setPos(enemy2, cx - 9000 + frames, cy + 3000, cz - 90);
            auto s = tr.latest();
            if (s->seq != last) { moves++; last = s->seq; }
            frames++;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        go = false;
        menu.join();
        CHECK(moves > 20, "while the menu thread works on the names, the tracker keeps publishing");

        auto tStop = std::chrono::steady_clock::now();
        tr.stop();
        CHECK(std::chrono::steady_clock::now() - tStop < std::chrono::milliseconds(500), "stop() returns quickly");
        CHECK(ProcessId == getpid(), "stop() leaves the test's process id alone");
    }

    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
