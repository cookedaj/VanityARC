//The normal read path (see GameState::step). The old path in GameState.cpp reads every actor of the level one at a
//time on every frame (about 7 ms for 10 000 actors, on the same thread that draws). Here a full scan only runs a few
//times a second; in between, only the actors that move are read again, and with many reads per system call.
#include "GameState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#include "utils/labeler.h"
#include "utils/log.h"
#include "utils/offsets.h"

void printStatusLine(const std::vector<RenderEntity>& others, const RenderEntity* me); //GameState.cpp
#ifdef KWINHELPER_PROBES
void runLiveProbe(const std::string& mode, ptr localPawn, const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm, FILE* out = stdout); //Probe.cpp
#endif

namespace {
using clk = std::chrono::steady_clock;
thread_local std::mt19937 scanJitter{std::random_device{}()};
//New actors show up within about this long (roughly 10 scans a second, with uneven gaps)
std::chrono::milliseconds nextScanGap() {
    return std::chrono::milliseconds(std::uniform_int_distribution<int>(80, 120)(scanJitter));
}

template <typename T>
void queue(std::vector<ReadReq>& q, ptr addr, T* dst) { q.push_back({addr, (void*)dst, sizeof(T), false}); }

bool plausible(const Vector3& p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false; //half-built actor
    if (std::abs(p.x) < 100) return false;
    if (std::abs(p.x) > 1e8 || std::abs(p.y) > 1e8 || std::abs(p.z) > 1e8) return false; //garbage, not a world position
    return true;
}

//A shield number as read (8 raw bytes): a double or a float, kept only when it is a believable amount
double shieldNumber(uint64_t raw) {
    double v;
    if (live::shieldIsFloat) { float f; std::memcpy(&f, &raw, sizeof f); v = f; }
    else std::memcpy(&v, &raw, sizeof v);
    return (std::isfinite(v) && v >= 0 && v <= 100000.0) ? v : 0.0;
}

double ms(clk::time_point a, clk::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

constexpr auto POSE_FROZEN_AFTER = std::chrono::milliseconds(300); //a drawn, animated player's bones change every game tick
constexpr auto BONE_PERIOD = std::chrono::milliseconds(16);        //a bone list is read at most about 60 times a second; between reads the last one is reused
constexpr auto CAMERA_HOLD = std::chrono::milliseconds(250);       //a view that cannot be used is replaced by the last good one for this long

uint64_t fnv1a(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
} // namespace

bool GameState::scan() {
    const auto t0 = clk::now();
    ptr uworld = getUworld();
    if (!isValidPtr(uworld)) {
        fault_ = "no game world: menu or loading screen (or the UWORLD offsets are stale)";
        logThrottled("[-] Invalid Uworld (not in a raid yet?)");
        return false;
    }
    if (uworld != lastWorld_) { camActor_ = 0; shieldSeen_.clear(); lastTeamID = -1; meActor_ = 0; learned_.clear(); } //a new world: new camera and players, your team and pawn not known yet, vtables learned again
    lastWorld_ = uworld;
    //PersistentLevel at UWorld+0x170 is encrypted; the plain pointer is in LevelCollections[0] (see config.h)
    ptr levelCollection = ReadMemory<ptr>(uworld + off::LEVEL_COLLECTIONS);
    if (!isValidPtr(levelCollection)) { fault_ = "no level yet: loading (or LEVEL_COLLECTIONS is stale)"; logThrottled("[-] No level collection yet"); return false; }
    ptr persistentLevel = ReadMemory<ptr>(levelCollection + off::PERSISTENT_LEVEL);
    if (!isValidPtr(persistentLevel)) { fault_ = "no level yet: loading (or PERSISTENT_LEVEL is stale)"; logThrottled("[-] No persistent level yet"); return false; }
    ptr actorsPtr = ReadMemory<ptr>(persistentLevel + off::ACTORS_PTR);
    int count = ReadMemory<int>(persistentLevel + off::ACTORS_PTR + 0x8);
    if (!(isValidPtr(actorsPtr) && count > 0 && count < 250000)) { fault_ = "actor list not readable: loading (or ACTORS_PTR is stale)"; logThrottled("[-] Actor list not readable yet"); return false; }

    std::vector<ptr> actors((size_t)count);
    ReadMemoryBuffer(actorsPtr, actors.data(), actors.size() * sizeof(ptr));

    //The first word of every actor says what kind it is
    std::vector<ptr> cand;
    cand.reserve(actors.size());
    for (ptr a : actors) if (isValidPtr(a)) cand.push_back(a);
    std::vector<ptr> vts(cand.size());
    std::vector<ReadReq> rq;
    rq.reserve(cand.size());
    for (size_t i = 0; i < cand.size(); i++) queue(rq, cand[i], &vts[i]);
    ReadScatter(rq.data(), rq.size());

    struct C { ptr actor, vt, root; Object type; Vector3 pos; };
    std::vector<C> kept;
    ptr cam = 0;
    std::unordered_map<ptr, std::pair<int, ptr>> unsure; //a vtable to ask about: how many of its actors were seen, one of them at random
    for (size_t i = 0; i < cand.size(); i++) {
        const ptr vt = vts[i];
        Object t;
        if (vt == vtabels::PLAYER) t = Object::PLAYER;
        else if (vt == vtabels::ARC) t = Object::ARC;
        else if (vt == vtabels::SEARCH) t = Object::SEARCH;
        else if (vt == vtabels::CAMMANAGER) { cam = cand[i]; continue; } //the camera only; the last one wins
        else { //not in config.h (stale after a game patch): what the names of this vtable's actors said
            const auto it = learned_.find(vt);
            if (it != learned_.end() && it->second.decided && it->second.kind != Object::NONE) t = it->second.kind;
            else {
                if ((it == learned_.end() || t0 >= it->second.next) && vt >= ImageBase && vt < ImageEnd) { //a real vtable is in the game image
                    auto& u = unsure[vt];
                    if (std::uniform_int_distribution<int>(0, u.first++)(scanJitter) == 0) u.second = cand[i];
                }
                continue; //everything else is not drawn
            }
        }
        kept.push_back({cand[i], vt, 0, t, {}});
    }
    learnKinds(unsure, t0);

    rq.clear();
    for (C& c : kept) queue(rq, c.actor + off::ROOT_COMPONENT_PTR, &c.root);
    ReadScatter(rq.data(), rq.size());
    rq.clear();
    for (C& c : kept) if (isValidPtr(c.root)) queue(rq, c.root + off::POS_PTR, &c.pos);
    ReadScatter(rq.data(), rq.size());

    statics_.clear();
    dyn_.clear();
    for (const C& c : kept) {
        if (!isValidPtr(c.root) || !plausible(c.pos)) continue;
        Tracked t;
        t.ent.actor = c.actor;
        t.ent.vt = c.vt;
        t.ent.type = c.type;
        t.ent.pos = c.pos;
        t.root = c.root;
        if (c.type == Object::PLAYER || c.type == Object::ARC) {
            dyn_.push_back(std::move(t));
        } else {
            labeler::refine(t.ent);  //ground items share the container vtable; the actor's name tells them apart
            labeler::fill(t.ent);    //static things get their name now, not on every step
            t.live = true;
            statics_.push_back(std::move(t));
        }
    }
    cam = cameraViaController(cam); //no CAMMANAGER-vtable actor (stale after a patch): the camera through the PlayerController
    //The camera is picked only with a FOV in the usual range (far from it: VIEW_MATRIX is not a view). A scan that did
    //not see one, or saw one zoomed in by a scope, keeps the camera it has; step() then takes any usable view from it.
    if (cam) {
        const FminimalViewInfo v = ReadMemory<FminimalViewInfo>(cam + off::VIEW_MATRIX);
        if (viewUsable(v) && std::abs(v.FOV - 70.0) <= 60.0) camActor_ = cam;
    }
    for (auto it = shieldSeen_.begin(); it != shieldSeen_.end();) { //forget players that are gone (their address may be reused)
        bool present = false;
        for (const Tracked& t : dyn_) if (t.ent.actor == it->first) { present = true; break; }
        it = present ? std::next(it) : shieldSeen_.erase(it);
    }
    for (auto it = poseSeen_.begin(); it != poseSeen_.end();) { //same for the poses
        bool present = false;
        for (const Tracked& t : dyn_) if (t.ent.actor == it->first) { present = true; break; }
        it = present ? std::next(it) : poseSeen_.erase(it);
    }
    for (auto it = drawnSeen_.begin(); it != drawnSeen_.end();) { //and the draw times
        bool present = false;
        for (const Tracked& t : dyn_) if (t.ent.actor == it->first) { present = true; break; }
        it = present ? std::next(it) : drawnSeen_.erase(it);
    }
    timing_.lastScanMs = ms(t0, clk::now());
    timing_.statics = statics_.size();
    timing_.dynamics = dyn_.size();
    return true;
}

//Asks one actor of each unsure vtable what it is, by its names (labeler::kindByName), at most LEARN_PER_SCAN a scan.
//LEARN_VOTES answers in a row that agree give the vtable its kind; answers that disagree (a vtable shared by different
//things) or NONE make it NONE, asked again after NONE_FOR. An actor whose names do not read tells nothing.
void GameState::learnKinds(const std::unordered_map<ptr, std::pair<int, ptr>>& unsure, clk::time_point now) {
    constexpr int LEARN_PER_SCAN = 32, LEARN_VOTES = 3;
    constexpr auto NONE_FOR = std::chrono::seconds(10);
    int asked = 0;
    for (const auto& [vt, pick] : unsure) {
        if (++asked > LEARN_PER_SCAN) break;
        Learned& L = learned_[vt];
        const std::optional<Object> k = labeler::kindByName(pick.second);
        if (!k) { L.next = now + std::chrono::seconds(1); continue; } //names not readable (yet): ask again in a second
        if (L.decided) { //a NONE that ran out: still NONE keeps it, anything else starts over
            if (*k == Object::NONE) { L.next = now + NONE_FOR; continue; }
            L = {};
        }
        if (L.votes && *k != L.kind) { L.kind = Object::NONE; L.votes = LEARN_VOTES; }
        else { L.kind = *k; L.votes++; }
        if (L.votes < LEARN_VOTES) continue;
        L.decided = true;
        L.next = now + NONE_FOR;
        if (L.kind == Object::NONE) continue;
        const char* kind = L.kind == Object::PLAYER ? "PLAYER" : L.kind == Object::ARC ? "ARC" : "SEARCH";
        std::printf("[+] Learned vtable 0x%llx = %s from its actors' names (put it in config.h as vtabels::%s to skip learning)\n",
                    (unsigned long long)vt, kind, kind);
        std::fflush(stdout);
    }
}

//One player's stick figure for this step. The bone list is read whole only some of the time (the game rewrites it
//every frame), the game stops animating players it is not drawing, and nobody far away is animated at all, so:
//  - a list that cannot be read is replaced by the last good one for a moment (no flicker),
//  - a frozen player gets the pose of someone the game IS animating in the same stance and speed (drawn faint).
void GameState::updatePose(Tracked& t, const std::vector<uint8_t>& bones, const skeleton::Xform& mesh, bool freshBones) {
    using namespace std::chrono;
    const auto now = clk::now();
    PoseState& ps = poseSeen_[t.ent.actor];
    if (!ps.init) { ps.init = true; ps.changed = now - POSE_FROZEN_AFTER; } //not animated until his list is seen to change

    //Ground speed, over at least 150 ms so the replication jitter averages out
    if (!ps.havePos) { ps.havePos = true; ps.lastPos = t.ent.pos; ps.lastPosAt = now; }
    else {
        const double dt = duration<double>(now - ps.lastPosAt).count();
        if (dt >= 0.15) {
            const double dx = t.ent.pos.x - ps.lastPos.x, dy = t.ent.pos.y - ps.lastPos.y;
            ps.speed = std::sqrt(dx * dx + dy * dy) / dt;
            ps.speedKnown = true;
            ps.lastPos = t.ent.pos;
            ps.lastPosAt = now;
        }
    }
    //Stance: the capsule is 92 cm above the mesh standing and 60 cm crouched (readable for every player, animated or not)
    const bool crouched = t.ent.pos.z - mesh.t.z < 76.0;
    const bool moving = ps.speed > 120.0;
    const int cls = (crouched ? 2 : 0) + (moving ? 1 : 0);

    const bool haveBones = bones.size() == (size_t)skeleton::BONES * skeleton::RECORD;
    bool readOk = false;
    skeleton::Pose comp;
    if (haveBones) {
        skeleton::Xform local[skeleton::BONES];
        skeleton::decode(bones.data(), local);
        readOk = skeleton::componentPose(local, comp);
        if (freshBones && readOk) { //a whole list read just now: compare it with the last one, and keep it for the next steps
            const uint64_t h = fnv1a(bones.data(), bones.size());
            if (ps.hash && h != ps.hash) ps.changed = now; //the first list has nothing to differ from
            ps.hash = h;
            ps.bones.assign(bones.begin(), bones.end());
            ps.bonesAt = now;
        }
    }
    const bool animated = now - ps.changed < POSE_FROZEN_AFTER; //a failed read leaves the hash alone: still animated if it was
    if (readOk) {
        ps.good = comp; ps.haveGood = true; ps.goodAt = now;
        //Learn what this stance looks like from a clean, animated, living player
        const double pelvisZ = comp.j[skeleton::PELVIS].z;
        const bool idle = ps.speed < 40.0;
        const bool plausible = crouched ? (pelvisZ > 25.0 && pelvisZ < 60.0) : pelvisZ > 70.0;
        //Not from a roll or a sprint: those poses are not a stance to copy (450 cm/s is above a walk, below a sprint)
        if (animated && ps.speedKnown && !t.ent.isDead && plausible && (idle || moving) && ps.speed < 450.0) { tmpl_[cls] = comp; haveTmpl_[cls] = true; }
    } else if (ps.haveGood && now - ps.goodAt < milliseconds(400)) {
        comp = ps.good; readOk = true;   //a half-written list: the last whole one, moved with the player
    }
    t.ent.poseLive = animated;
    if (!animated) { //frozen: his own list is old, so a pose that fits what he is doing now
        for (int c : {cls, cls ^ 1}) if (haveTmpl_[c]) { comp = tmpl_[c]; readOk = true; break; }
    }
    if (!readOk) return;
    auto pose = std::make_shared<skeleton::Pose>();
    if (skeleton::placePose(comp, mesh, t.ent.pos, *pose)) t.ent.pose = std::move(pose);
}

void GameState::updateDynamic() {
    const size_t n = dyn_.size();
    if (!n) return;
    //The mesh's own transform as it sits in memory (rotation, position, scale of the character's mesh)
    struct MeshXf { double q[4]; double t[3]; double padT; double s[3]; };
    static_assert(sizeof(MeshXf) == 0x58, "MeshXf must match the game's transform");
    //The root's own transform up to its position: rotation (which way it faces), then the position at POS_PTR
    struct RootXf { double q[4]; Vector3 pos; };
    static_assert(sizeof(RootXf) == 0x38, "RootXf must match the game's transform");
    struct Row {
        ptr vt = 0, root = 0, hc = 0, sc = 0; int team = 0; uint8_t destroyed = 0; RootXf xf{}; double hp = 0; uint64_t shRaw = 0, shMaxRaw = 0;
        bool wantPose = false; ptr ctrl = 0, mesh = 0, boneData = 0; uint64_t rtRaw = 0; int boneNum = 0; MeshXf mx{}; std::vector<uint8_t> bones;
        const std::vector<uint8_t>* view = nullptr; bool freshBones = false; //the bone list to use: this step's read, or the cached one
        double hpMax = 0; int teamAt = -1, hpAt = -1, maxAt = -1, shAt = -1; bool teamOk = false, hpOk = false, shOk = false; //which reads worked (a failed one reads as 0)
    };
    const bool shieldOn = live::shieldValue >= 0;
    const bool skeletonOn = g_skeletonOn.load(std::memory_order_relaxed);
    const double skeletonRange = g_skeletonDist.load(std::memory_order_relaxed) * 100.0; //cm
    std::vector<Row> rows(n);
    std::vector<ReadReq> rq;
    rq.reserve(n * 5);
    for (size_t i = 0; i < n; i++) {
        const RenderEntity& e = dyn_[i].ent;
        Row& r = rows[i];
        queue(rq, e.actor, &r.vt); //still the same kind of actor? (the game reuses memory)
        queue(rq, e.actor + off::ROOT_COMPONENT_PTR, &r.root);
        if (e.type == Object::PLAYER) {
            queue(rq, e.actor + off::HEALTH_COMPONENT, &r.hc);
            r.teamAt = (int)rq.size(); queue(rq, e.actor + off::TEAM_ID, &r.team);
            if (shieldOn && live::shieldComponent >= 0) queue(rq, e.actor + (ptr)live::shieldComponent, &r.sc); //the shield lives in another component
            r.wantPose = skeletonOn && e.pos.Dist(lastCamPos_) <= skeletonRange; //only near players are drawn as a skeleton
            queue(rq, e.actor + off::CONTROLLER, &r.ctrl);
            queue(rq, e.actor + off::MESH, &r.mesh); //for the pose and for when the game last drew him
        } else {
            queue(rq, e.actor + off::IS_DESTROYED, &r.destroyed);
        }
    }
    ReadScatter(rq.data(), rq.size());
    for (Row& r : rows) r.teamOk = r.teamAt >= 0 && rq[(size_t)r.teamAt].ok; //before rq is reused: a failed read is 0, not team 0

    rq.clear();
    for (size_t i = 0; i < n; i++) {
        Row& r = rows[i];
        if (r.vt != dyn_[i].ent.vt || !isValidPtr(r.root)) continue;
        queue(rq, r.root + off::POS_PTR - 0x20, &r.xf); //the rotation sits right before the position (config.h)
        if (dyn_[i].ent.type == Object::PLAYER && isValidPtr(r.mesh)) queue(rq, r.mesh + off::RENDER_TIME, &r.rtRaw);
        if (r.wantPose && isValidPtr(r.mesh)) {
            queue(rq, r.mesh + off::MESH_TRANSFORM, &r.mx);
            queue(rq, r.mesh + off::BONE_ARRAY, &r.boneData);
            queue(rq, r.mesh + off::BONE_ARRAY + 8, &r.boneNum);
        }
        if (dyn_[i].ent.type == Object::PLAYER && isValidPtr(r.hc)) {
            r.hpAt = (int)rq.size(); queue(rq, r.hc + off::HEALTH_CACHED, &r.hp);
            r.maxAt = (int)rq.size(); queue(rq, r.hc + off::HEALTH_MAX, &r.hpMax);
        }
        if (dyn_[i].ent.type == Object::PLAYER && shieldOn) {
            const ptr comp = live::shieldComponent >= 0 ? r.sc : r.hc;
            if (isValidPtr(comp)) {
                r.shAt = (int)rq.size(); queue(rq, comp + (ptr)live::shieldValue, &r.shRaw);
                if (live::shieldMax >= 0) queue(rq, comp + (ptr)live::shieldMax, &r.shMaxRaw);
            }
        }
    }
    ReadScatter(rq.data(), rq.size());
    for (Row& r : rows) { //before rq is reused. A failed read is 0 (dead): only a believable pair counts, as in printStatusLine
        r.hpOk = r.hpAt >= 0 && rq[(size_t)r.hpAt].ok && rq[(size_t)r.maxAt].ok &&
                 r.hpMax >= 50 && r.hpMax <= 500 && r.hp >= 0 && r.hp <= r.hpMax + 1;
        r.shOk = r.shAt >= 0 && rq[(size_t)r.shAt].ok;
    }

    //Third round: the bone list itself, only for players whose mesh and a list of exactly 97 bones were readable. A list
    //read less than BONE_PERIOD ago is reused instead: reading it on every step would cost the game and the CPU for no gain.
    rq.clear();
    const auto boneNow = clk::now();
    const size_t boneBytes = (size_t)skeleton::BONES * skeleton::RECORD;
    for (size_t i = 0; i < n; i++) {
        Row& r = rows[i];
        if (!r.wantPose || r.boneNum != skeleton::BONES || !isValidPtr(r.boneData)) continue;
        auto cached = poseSeen_.find(dyn_[i].ent.actor);
        if (cached != poseSeen_.end() && cached->second.bones.size() == boneBytes && boneNow - cached->second.bonesAt < BONE_PERIOD) {
            r.view = &cached->second.bones;
            continue;
        }
        r.bones.resize(boneBytes);
        r.view = &r.bones;
        r.freshBones = true;
        rq.push_back({r.boneData, r.bones.data(), boneBytes, false});
    }
    if (!rq.empty()) ReadScatter(rq.data(), rq.size());

    //When did the game last draw each player? The number is the game's clock at his last draw, written on every frame the
    //game draws him: one that has not changed for a moment means he is not drawn right now. Each player is judged by his
    //own number only, so one bad read cannot dim anyone else, and nothing carries over from one raid to the next.
    auto renderTime = [](const Row& r) { float f[2]; std::memcpy(f, &r.rtRaw, 8); return std::isfinite(f[1]) ? f[1] : -1.0f; }; //LastRenderTimeOnScreen
    const auto clockNow = clk::now();

    for (size_t i = 0; i < n; i++) {
        Tracked& t = dyn_[i];
        const Row& r = rows[i];
        t.live = r.vt == t.ent.vt && isValidPtr(r.root) && plausible(r.xf.pos);
        if (!t.live) continue;
        t.root = r.root;
        t.ent.pos = r.xf.pos;
        t.ent.yaw = yawFromQuat(r.xf.q);
        if (t.ent.type == Object::PLAYER) {
            if (r.hpOk) { t.ent.playerHealth = r.hp; t.hpRead = true; } //otherwise the last good value: 100 (alive) for a player not read yet
            t.ent.isDead = t.ent.playerHealth < 1;
            t.ent.isLocal = isValidPtr(r.ctrl);
            t.ent.isVisible = true; //no mesh read: unknown, treated as visible
            if (isValidPtr(r.mesh)) {
                const float drawn = renderTime(r);
                DrawnState& ds = drawnSeen_.try_emplace(t.ent.actor, DrawnState{drawn, clockNow}).first->second; //first read: unknown, visible for now
                if (drawn != ds.rt) { ds.rt = drawn; ds.changed = clockNow; }
                t.ent.isVisible = clockNow - ds.changed < std::chrono::milliseconds(250);
            }
            if (r.teamOk) t.ent.teamID = r.team & 0xFF; //the byte at TeamId only; the bytes after it belong to other fields. Failed: the last good one (-1 if none)
            t.ent.pose.reset();
            t.ent.poseLive = true;
            if (r.wantPose) {
                skeleton::Xform mesh;
                mesh.q = {r.mx.q[0], r.mx.q[1], r.mx.q[2], r.mx.q[3]};
                mesh.t = Vector3(r.mx.t[0], r.mx.t[1], r.mx.t[2]);
                mesh.s = Vector3(r.mx.s[0], r.mx.s[1], r.mx.s[2]);
                static const std::vector<uint8_t> none;
                updatePose(t, r.view ? *r.view : none, mesh, r.freshBones);
            }
            const ptr comp = live::shieldComponent >= 0 ? r.sc : r.hc;
            const double lastShield = t.ent.shield; //his shield at the last step (0 right after a scan)
            t.ent.hasShield = shieldOn && isValidPtr(comp);
            t.ent.shield = t.ent.hasShield ? shieldNumber(r.shRaw) : 0.0;
            if (t.ent.hasShield) {
                double& seen = shieldSeen_[t.ent.actor]; //the longest the bar has been
                seen = std::max(seen, t.ent.shield);
                //The same number on two steps in a row is a real reading, not one bad read: it may raise the session's cap,
                //so a player first seen with a damaged shield is not drawn as full
                if (t.ent.shield == lastShield && t.ent.shield < 1000.0) shieldCap_ = std::max(shieldCap_, t.ent.shield);
                t.ent.shieldMax = live::shieldMax >= 0 ? std::max(shieldNumber(r.shMaxRaw), t.ent.shield) : std::max(seen, shieldCap_);
                if (r.shOk) t.shieldRead = t.ent.shield;
            } else {
                t.shieldRead = -1;
            }
        } else {
            t.ent.isDead = r.destroyed == 1; //a bool is 0 or 1; any other byte means the offset moved: unknown, treated as alive
        }
    }
}

gsRet GameState::assemble(const FminimalViewInfo& vm) {
    lastCamPos_ = vm.Location;
    //You are the player closest to the camera (third person puts it about 2 m away). Only that one is you: another
    //player standing right next to the camera (a teammate being revived, an enemy in melee range) must still be drawn.
    //The pawn with a PlayerController is the one you control. If that is not exactly one player, fall back to proximity.
    const Tracked* me = nullptr;
    int locals = 0;
    for (const Tracked& t : dyn_) if (t.live && t.ent.type == Object::PLAYER && t.ent.isLocal) { me = &t; locals++; }
    if (locals != 1) {
        me = nullptr;
        if (locals == 0 && meActor_) { //no controller: you died or are watching someone. You are still who you were.
            for (const Tracked& t : dyn_) if (t.live && t.ent.type == Object::PLAYER && t.ent.actor == meActor_) me = &t;
        }
        if (!me) {
            double best = 2 * 100;
            for (const Tracked& t : dyn_) {
                if (!t.live || t.ent.type != Object::PLAYER) continue;
                double d = t.ent.pos.Dist(vm.Location);
                if (d < best) { best = d; me = &t; }
            }
        }
    }
    if (me && me->ent.teamID >= 0) lastTeamID = me->ent.teamID; //set only by a read that worked (a failed one is not team 0)
    meActor_ = me ? me->ent.actor : 0;

    gsRet out;
    out.vm = vm;
    out.localPlayerTeam = lastTeamID;
    out.valid = true;
    if (me && me->hpRead) { out.selfHealth = me->ent.playerHealth; out.selfShield = me->shieldRead; } //never the stand-in 100
    out.entities.reserve(statics_.size() + dyn_.size());
    for (const Tracked& t : statics_) {
        out.entities.push_back(t.ent);
        out.entities.back().dist = t.ent.pos.Dist(vm.Location) / 100;
    }
    for (Tracked& t : dyn_) {
        if (!t.live || &t == me) continue;
        labeler::fill(t.ent); //names and guns (cached; the real reads happen every second or so)
        out.entities.push_back(t.ent);
        out.entities.back().dist = t.ent.pos.Dist(vm.Location) / 100;
    }
    labeler::endTick();
    return out;
}

//KWINHELPER_PROBE=shield or inventory (set before starting): prints discovery reports every 2 s, reading memory only
void GameState::runLiveProbeIfAsked(const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm) {
#ifdef KWINHELPER_PROBES
    static const std::string mode = [] { const char* e = std::getenv("KWINHELPER_PROBE"); return std::string(e ? e : ""); }();
    if (mode.empty()) return;
    static auto last = clk::now() - std::chrono::seconds(10);
    const auto now = clk::now();
    if (now - last < std::chrono::seconds(2)) return;
    last = now;
    runLiveProbe(mode, meActor_, ents, vm);
#else
    (void)ents; (void)vm; //the release build has no probes
#endif
}

gsRet GameState::step() {
    if (isDebugMode || probeNames) return tick(); //the diagnostic modes want the unfiltered view

    const auto t0 = clk::now();
    //The world is checked on every step (4 small reads): leaving a raid or loading a map is noticed at once
    const ptr world = getUworld();
    const bool full = camActor_ == 0 || world != lastWorld_ || !isValidPtr(world) || t0 - lastScan_ >= nextScanGap();
    if (full) {
        lastScan_ = t0;
        if (!scan()) { statics_.clear(); dyn_.clear(); camActor_ = 0; return {.fault = fault_}; }
    }
    updateDynamic();

    //One bad read (a failed read, the camera being rewritten) must not blank the overlay: the last good view is used
    //for a moment. Only a camera that stays unusable is dropped and looked for again.
    FminimalViewInfo vm = ReadMemory<FminimalViewInfo>(camActor_ + off::VIEW_MATRIX);
    if (viewUsable(vm)) { goodVm_ = vm; goodVmAt_ = t0; }
    else if (camActor_ && t0 - goodVmAt_ < CAMERA_HOLD) vm = goodVm_;
    else {
        logThrottled("[-] Invalid rawEntities or no viewMatrix");
        camActor_ = 0; //find the camera again at the next step
        return {.fault = "no camera found (CAMMANAGER vtable or VIEW_MATRIX is stale?)"};
    }
    if (statics_.empty() && dyn_.empty()) logThrottled("[-] No ent found, may be in practice range or its just not working");

    gsRet out = assemble(vm);
    runLiveProbeIfAsked(out.entities, vm);

    //The same one-line status as always, every 2 s (see printStatusLine): me / players / ARC / containers
    static auto lastStatus = clk::now() - std::chrono::seconds(10);
    if (t0 - lastStatus >= std::chrono::seconds(2)) {
        lastStatus = t0;
        const RenderEntity* meEnt = nullptr; //the pawn assemble() chose as you; out.entities already holds everything else
        for (const Tracked& t : dyn_) if (t.live && t.ent.actor == meActor_) meEnt = &t.ent;
        printStatusLine(out.entities, meEnt);
    }
    timing_.lastStepMs = ms(t0, clk::now());
    return out;
}
