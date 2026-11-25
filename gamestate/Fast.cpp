//The normal read path (see GameState::step). The old path in GameState.cpp reads every actor of the level one at a
//time on every frame (about 7 ms for 10 000 actors, on the same thread that draws). Here a full scan only runs a few
//times a second; in between, only the actors that move are read again, and with many reads per system call.
#include "GameState.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#include "utils/labeler.h"
#include "utils/log.h"
#include "utils/offsets.h"

void printStatusLine(const std::vector<RenderEntity>& others, const RenderEntity* me, int posed, int poseWanted); //GameState.cpp
#ifdef KWINHELPER_PROBES
void runLiveProbe(const std::string& mode, ptr localPawn, const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm, FILE* out = stdout); //Probe.cpp
void announceLiveProbe(const std::string& mode, FILE* out = stdout); //Probe.cpp
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
constexpr auto WORLD_CHECK_GAP = std::chrono::milliseconds(30);    //the UWorld chain is read at least this often (every 4th step at 120 updates a second)
constexpr auto FAR_ARC_PERIOD = std::chrono::milliseconds(66);     //ARC far beyond the ARC and radar ranges: read about 15 times a second
constexpr double FAR_ARC_MARGIN = 2500;                            //cm past that range, so an ARC is back to every step before it can be drawn

//printf into a std::string (the self-check's lines)
std::string say(const char* format, ...) {
    char b[320];
    va_list a;
    va_start(a, format);
    std::vsnprintf(b, sizeof b, format, a);
    va_end(a);
    return b;
}
} // namespace

bool GameState::scan(ptr uworld) {
    const auto t0 = clk::now();
    if (!isValidPtr(uworld)) {
        //The link that read 0 (see getUworld). The menu stops the chain at CHAIN_1 (no PlayerController yet); in a raid a
        //stop means the offsets in config.h are stale for this game build.
        static const char* const stop[4] = {
            "no game world: UWorld chain stops at UWORLD_RVA (reads 0: offsets stale?)",
            "no game world: UWorld chain stops at UWORLD_CHAIN_1 (menu, or offsets stale)",
            "no game world: UWorld chain stops at UWORLD_CHAIN_2 (menu, or offsets stale)",
            "no game world: UWorld chain stops at UWORLD_CHAIN_3 (menu, or offsets stale)"};
        fault_ = stop[chainStop_ >= 0 ? chainStop_ : 0];
        logThrottled("[-] Invalid Uworld (not in a raid yet?)");
        return false;
    }
    if (uworld != lastWorld_) { camActor_ = 0; shieldSeen_.clear(); lastTeamID = -1; meActor_ = 0; learned_.clear(); slots_.clear(); } //a new world: new camera, players and actors, your team and pawn not known yet, vtables learned again
    lastWorld_ = uworld;
    //PersistentLevel at UWorld+0x170 is encrypted; the plain pointer is in LevelCollections[0] (see config.h)
    ptr levelCollection = ReadMemory<ptr>(uworld + off::LEVEL_COLLECTIONS);
    if (!isValidPtr(levelCollection)) { fault_ = "no level yet: loading (or LEVEL_COLLECTIONS is stale)"; logThrottled("[-] No level collection yet"); return false; }
    ptr persistentLevel = ReadMemory<ptr>(levelCollection + off::PERSISTENT_LEVEL);
    if (!isValidPtr(persistentLevel)) { fault_ = "no level yet: loading (or PERSISTENT_LEVEL is stale)"; logThrottled("[-] No persistent level yet"); return false; }
    struct { ptr data; int num, max; } arr{}; //Level::Actors: the list and its count side by side, so one read (zero if it fails)
    ReadMemoryBuffer(persistentLevel + off::ACTORS_PTR, &arr, sizeof arr);
    const ptr actorsPtr = arr.data;
    const int count = arr.num;
    if (!(isValidPtr(actorsPtr) && count > 0 && count < 250000)) { fault_ = "actor list not readable: loading (or ACTORS_PTR is stale)"; logThrottled("[-] Actor list not readable yet"); return false; }

    std::vector<ptr> actors((size_t)count);
    ReadMemoryBuffer(actorsPtr, actors.data(), actors.size() * sizeof(ptr));

    //The first word of every actor says what kind it is. That only changes when the slot gets another actor, so it is
    //read for slots whose pointer changed, plus a rotating 1/20 of the list (a freed address reused in the same slot
    //is caught within about 2 s). Most scans read a few hundred vtables instead of every actor's.
    const size_t n = actors.size();
    slots_.resize(n);
    const size_t slice = n / 20 + 1, from = recheck_ % n;
    recheck_ = (from + slice) % n;
    std::vector<ReadReq> rq;
    for (size_t i = 0; i < n; i++) {
        Slot& s = slots_[i];
        if (!isValidPtr(actors[i])) { s = {}; continue; }
        if (actors[i] == s.actor && (i + n - from) % n >= slice) continue; //unchanged, not in this scan's re-check
        s = {actors[i], 0, 0};
        queue(rq, s.actor, &s.vt);
    }
    ReadScatter(rq.data(), rq.size());

    struct C { size_t slot; ptr actor, vt, root; Object type; Vector3 pos; };
    std::vector<C> kept;
    ptr cam = 0;
    std::unordered_map<ptr, std::pair<int, ptr>> unsure; //a vtable to ask about: how many of its actors were seen, one of them at random
    for (size_t i = 0; i < n; i++) {
        Slot& s = slots_[i];
        if (!s.vt) { s.actor = 0; continue; } //empty slot, or a read that failed: tried again at the next scan
        const ptr vt = s.vt;
        Object t;
        if (vt == vtabels::PLAYER) t = Object::PLAYER;
        else if (vt == vtabels::ARC) t = Object::ARC;
        else if (vt == vtabels::SEARCH) t = Object::SEARCH;
        else if (vt == vtabels::CAMMANAGER) { cam = s.actor; continue; } //the camera only; the last one wins
        else { //not in config.h (stale after a game patch): what the names of this vtable's actors said
            const auto it = learned_.find(vt);
            if (it != learned_.end() && it->second.decided && it->second.kind != Object::NONE) t = it->second.kind;
            else {
                if ((it == learned_.end() || t0 >= it->second.next) && vt >= ImageBase && vt < ImageEnd) { //a real vtable is in the game image
                    auto& u = unsure[vt];
                    if (std::uniform_int_distribution<int>(0, u.first++)(scanJitter) == 0) u.second = s.actor;
                }
                continue; //everything else is not drawn
            }
        }
        kept.push_back({i, s.actor, vt, s.root, t, {}});
    }
    learnKinds(unsure, t0);

    rq.clear();
    for (C& c : kept) if (!isValidPtr(c.root)) queue(rq, c.actor + off::ROOT_COMPONENT_PTR, &c.root); //known from an earlier scan otherwise
    ReadScatter(rq.data(), rq.size());
    rq.clear();
    for (C& c : kept) if (isValidPtr(c.root)) queue(rq, c.root + off::POS_PTR, &c.pos);
    ReadScatter(rq.data(), rq.size());

    statics_.clear();
    dyn_.clear();
    for (const C& c : kept) {
        slots_[c.slot].root = isValidPtr(c.root) && plausible(c.pos) ? c.root : 0; //a half-built actor: its root is read again
        if (!slots_[c.slot].root) continue;
        Tracked t;
        t.ent.actor = c.actor;
        t.ent.vt = c.vt;
        t.ent.type = c.type;
        t.ent.pos = c.pos;
        t.root = c.root;
        if (c.type == Object::PLAYER || c.type == Object::ARC) {
            dyn_.push_back(std::move(t));
        } else { //named in assemble(), and only when within the loot range
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
//  - a frozen player gets the pose of someone the game IS animating in the same stance and speed (drawn at full
//    strength like a live pose; only Dim players behind cover can fade him).
//Returns nullptr when he got a pose, otherwise why not (for the console).
const char* GameState::updatePose(Tracked& t, const std::vector<uint8_t>& bones, const skeleton::Xform& mesh, bool freshBones) {
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

    const bool haveBones = bones.size() == (size_t)skeleton::READ_BONES * skeleton::RECORD;
    bool readOk = false;
    skeleton::Pose comp;
    if (haveBones) {
        skeleton::Xform local[skeleton::BONES];
        skeleton::decodeRead(bones.data(), local);
        readOk = skeleton::componentPose(local, comp);
        if (freshBones && readOk) { //a whole list read just now: compare it with the last one, and keep it for the next steps
            if (ps.bones != bones) { //a byte compare with the list kept last time; an unchanged list is not copied again
                if (!ps.bones.empty()) ps.changed = now; //the first list has nothing to differ from
                ps.bones = bones;
            }
            ps.bonesAt = now;
        }
    }
    const bool animated = now - ps.changed < POSE_FROZEN_AFTER; //a failed read leaves the kept list alone: still animated if it was
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
    if (!readOk) return haveBones ? "the bone records do not read as bones (record layout in utils/skeleton.h, or BONE_ARRAY in config.h)"
                                  : "no bone list was read";
    auto pose = std::make_shared<skeleton::Pose>();
    if (!skeleton::placePose(comp, mesh, t.ent.pos, *pose)) return "the mesh transform is not believable (MESH_TRANSFORM in config.h)";
    t.ent.pose = std::move(pose);
    return nullptr;
}

void GameState::updateDynamic(FminimalViewInfo& vm) {
    poseWanted_ = posed_ = 0;
    poseWhy_.clear();
    const size_t n = dyn_.size(); //may be 0: the camera is still read
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
        double hpMax = 0; int teamAt = -1, hpAt = -1, maxAt = -1, shAt = -1, deadAt = -1; bool teamOk = false, hpOk = false, shOk = false, deadOk = false; //which reads worked (a failed one reads as 0)
        bool skip = false; //a far ARC not due this step: it keeps what it had
    };
    const bool shieldOn = live::shieldValue >= 0;
    const bool skeletonOn = g_skeletonOn.load(std::memory_order_relaxed);
    const double skeletonRange = g_skeletonDist.load(std::memory_order_relaxed) * 100.0; //cm
    const bool matesShown = g_showTeammates.load(std::memory_order_relaxed);
    //The last step's view as a cone: the picture's corners plus 15 degrees, for a camera that turns before the next step
    const double Pi = 3.14159265358979, yaw = lastView_.Rotation.y * Pi / 180.0, pitch = lastView_.Rotation.x * Pi / 180.0;
    const Vector3 fwd(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch));
    const double cosCone = !viewUsable(lastView_) ? -1.0 //no view yet: every direction
        : std::cos(std::atan(g_viewSpread.load(std::memory_order_relaxed) * std::tan(lastView_.FOV * Pi / 360.0)) + 15.0 * Pi / 180.0);
    //Would DrawESP draw this player's skeleton (as of the last step)? Only then are his bones read.
    auto skeletonDrawn = [&](const RenderEntity& e) {
        const Vector3 d = e.pos - lastView_.Location;
        const double dist = std::sqrt(d.Dot(d));
        if (!skeletonOn || dist > skeletonRange) return false;
        if (e.actor == meActor_) return true; //you: never drawn, but always animated, so the stance templates are learned from you
        if (e.isDead || (!matesShown && lastTeamID >= 0 && e.teamID == lastTeamID)) return false; //a cross, or a hidden squad mate
        return dist <= 500.0 || d.Dot(fwd) >= cosCone * dist; //within 5 m, or in front of the camera
    };
    const double arcNear = g_arcDrawDist.load(std::memory_order_relaxed) * 100.0 + FAR_ARC_MARGIN; //cm, flat
    const auto stepNow = clk::now();
    std::vector<Row> rows(n);
    std::vector<ReadReq> rq;
    rq.reserve(n * 5 + 1);
    if (camActor_) queue(rq, camActor_ + off::VIEW_MATRIX, &vm); //the camera's view rides along (a failed read is zeroed: not usable)
    for (size_t i = 0; i < n; i++) {
        const Tracked& t = dyn_[i];
        const RenderEntity& e = t.ent;
        Row& r = rows[i];
        if (e.type == Object::ARC && stepNow - t.readAt < FAR_ARC_PERIOD &&
            std::hypot(e.pos.x - lastView_.Location.x, e.pos.y - lastView_.Location.y) > arcNear) { r.skip = true; continue; }
        //The components do not change while the actor lives: found once per scan, and looked for again only while missing.
        //The vtable read on every step is the guard against an actor whose memory was reused.
        r.root = t.root; r.hc = t.hc; r.sc = t.sc; r.mesh = t.mesh;
        queue(rq, e.actor, &r.vt); //still the same kind of actor? (the game reuses memory)
        if (e.type == Object::PLAYER) {
            if (!isValidPtr(r.hc)) queue(rq, e.actor + off::HEALTH_COMPONENT, &r.hc);
            r.teamAt = (int)rq.size(); queue(rq, e.actor + off::TEAM_ID, &r.team);
            if (shieldOn && live::shieldComponent >= 0 && !isValidPtr(r.sc)) queue(rq, e.actor + (ptr)live::shieldComponent, &r.sc); //the shield lives in another component
            r.wantPose = skeletonDrawn(e);
            queue(rq, e.actor + off::CONTROLLER, &r.ctrl);
            if (!isValidPtr(r.mesh)) queue(rq, e.actor + off::MESH, &r.mesh); //for the pose and for when the game last drew him
        } else {
            r.deadAt = (int)rq.size(); queue(rq, e.actor + off::IS_DESTROYED, &r.destroyed);
        }
    }
    ReadScatter(rq.data(), rq.size());
    for (Row& r : rows) { //before rq is reused: a failed read is 0, not team 0 or alive
        r.teamOk = r.teamAt >= 0 && rq[(size_t)r.teamAt].ok;
        r.deadOk = r.deadAt >= 0 && rq[(size_t)r.deadAt].ok;
    }

    rq.clear();
    for (size_t i = 0; i < n; i++) {
        Row& r = rows[i];
        if (r.skip || r.vt != dyn_[i].ent.vt || !isValidPtr(r.root)) continue;
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

    //Third round: the bone list itself, only for players whose mesh and a list of exactly 97 bones were readable, and
    //only the 24 bones the stick figure uses (skeleton::READ_RANGES). A list read less than BONE_PERIOD ago is reused
    //instead: reading it on every step would cost the game and the CPU for no gain.
    rq.clear();
    const auto boneNow = clk::now();
    const size_t boneBytes = (size_t)skeleton::READ_BONES * skeleton::RECORD;
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
        uint8_t* dst = r.bones.data();
        for (const auto& rg : skeleton::READ_RANGES) {
            const size_t len = (size_t)(rg[1] - rg[0] + 1) * skeleton::RECORD;
            rq.push_back({r.boneData + (ptr)rg[0] * skeleton::RECORD, dst, len, false});
            dst += len;
        }
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
        if (r.skip) continue; //a far ARC between its reads: kept as last read
        t.live = r.vt == t.ent.vt && isValidPtr(r.root) && plausible(r.xf.pos);
        if (!t.live) continue; //not read: tried again at the next step
        t.readAt = clockNow;
        t.root = r.root; t.hc = r.hc; t.sc = r.sc; t.mesh = r.mesh; //kept until the next scan
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
                const char* why = updatePose(t, r.view ? *r.view : none, mesh, r.freshBones);
                poseWanted_++;
                if (t.ent.pose) posed_++;
                else if (poseWhy_.empty()) { //the first reason this step, said on the console when no one gets a pose
                    if (!isValidPtr(r.mesh)) poseWhy_ = "the player's mesh is not readable (MESH in config.h)";
                    else if (r.boneNum != skeleton::BONES)
                        poseWhy_ = "the game's bone list has " + std::to_string(r.boneNum) + " bones, this build expects " +
                                   std::to_string(skeleton::BONES) + " (utils/skeleton.h)";
                    else if (!isValidPtr(r.boneData)) poseWhy_ = "the bone list is not readable (BONE_ARRAY in config.h)";
                    else poseWhy_ = why ? why : "unknown";
                }
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
            if (r.deadOk) t.ent.isDead = r.destroyed == 1; //a bool is 0 or 1; any other byte means the offset moved: unknown, treated as alive. Failed: as it was
        }
    }
}

gsRet GameState::assemble(const FminimalViewInfo& vm) {
    lastView_ = vm;
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
    const double lootRange = g_lootRange.load(std::memory_order_relaxed) * 100.0; //cm
    for (Tracked& t : statics_) {
        const double d = t.ent.pos.Dist(vm.Location);
        if (d > lootRange) continue; //too far to be drawn: not named, not copied
        //Named the first time it is in range after a scan, before it is published: never drawn without its name or as
        //the wrong kind, also right after the loot range is raised. The labeler caches, so this is cheap.
        if (!t.named) {
            labeler::refine(t.ent); //ground items share the container vtable; the actor's name tells them apart
            labeler::fill(t.ent);
            t.named = true;
        }
        out.entities.push_back(t.ent);
        out.entities.back().dist = d / 100;
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

//KWINHELPER_PROBE=<names>, see announceLiveProbe (set before starting): prints discovery reports every 2 s, reading memory only
void GameState::runLiveProbeIfAsked(const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm) {
#ifdef KWINHELPER_PROBES
    static const std::string mode = [] { //read once; lower-case so "Shield" works too; says once what runs
        const char* e = std::getenv("KWINHELPER_PROBE");
        std::string m(e ? e : "");
        for (char& c : m) c = (char)std::tolower((unsigned char)c);
        if (!m.empty()) announceLiveProbe(m);
        return m;
    }();
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

//KWINHELPER_SELFCHECK=1 (the release build too): once in a raid, each game number in config.h is checked against what it
//must look like, and PASS / FAIL / SKIP is printed per number and saved to ~/.config/kwin-helper/selfcheck.txt.
//You are found without any vtable: the pawn whose Controller is the PlayerController the UWorld chain passes through
//(if none, the actor nearest the camera). Two looks 1 s apart, so your draw time can be seen to go up. Reads only.
void GameState::selfCheckIfAsked(const FminimalViewInfo& vm) {
    const auto now = clk::now();
    if (now - self_.at < std::chrono::seconds(1)) return; //at most one look a second (each reads the whole actor list)
    self_.at = now;
    auto isObj = [](ptr o) { if (!isValidPtr(o)) return false; const ptr v = ReadMemory<ptr>(o); return v >= ImageBase && v < ImageEnd; };

    //The UWorld chain link by link, the level and its actor list
    ptr link[4];
    link[0] = ReadMemory<ptr>(baseAddr + off::UWORLD_RVA);
    link[1] = ReadMemory<ptr>(link[0] + off::UWORLD_CHAIN_1); //the PlayerController
    link[2] = ReadMemory<ptr>(link[1] + off::UWORLD_CHAIN_2);
    link[3] = ReadMemory<ptr>(link[2] + off::UWORLD_CHAIN_3); //the UWorld
    const ptr pc = link[1];
    const ptr lc = ReadMemory<ptr>(link[3] + off::LEVEL_COLLECTIONS), level = ReadMemory<ptr>(lc + off::PERSISTENT_LEVEL);
    const ptr arr = ReadMemory<ptr>(level + off::ACTORS_PTR);
    const int count = ReadMemory<int>(level + off::ACTORS_PTR + 8);
    //A menu or lobby has far fewer actors than a raid (35 seen in the menu, about 1000 and up in raids): after a report
    //there (stage 3), wait for the raid and run again
    constexpr int RAID_ACTORS = 300;
    if (self_.stage == 3) {
        if (count < RAID_ACTORS) return;
        self_.stage = 1;
        self_.looks = 0;
    }
    std::vector<ptr> actors(isValidPtr(arr) && count > 0 && count < 250000 ? (size_t)count : 0);
    if (!actors.empty()) ReadMemoryBuffer(arr, actors.data(), actors.size() * sizeof(ptr));
    std::erase_if(actors, [](ptr a) { return !isValidPtr(a); });

    //Every actor's vtable, Controller and position
    const size_t n = actors.size();
    std::vector<ptr> vt(n), ctrl(n), root(n);
    std::vector<Vector3> pos(n);
    std::vector<ReadReq> rq;
    rq.reserve(n * 3);
    for (size_t i = 0; i < n; i++) {
        queue(rq, actors[i], &vt[i]);
        queue(rq, actors[i] + off::CONTROLLER, &ctrl[i]);
        queue(rq, actors[i] + off::ROOT_COMPONENT_PTR, &root[i]);
    }
    ReadScatter(rq.data(), rq.size());
    rq.clear();
    for (size_t i = 0; i < n; i++) if (isValidPtr(root[i])) queue(rq, root[i] + off::POS_PTR, &pos[i]);
    ReadScatter(rq.data(), rq.size());

    //You: the pawn your PlayerController controls (the nearest, should several say so); else the actor nearest the camera within 3 m
    size_t me = n;
    bool byController = false;
    double best = 1e18;
    for (size_t i = 0; i < n; i++) {
        if (actors[i] == pc || actors[i] == camActor_) continue;
        const bool controlled = isValidPtr(pc) && ctrl[i] == pc;
        const double d = plausible(pos[i]) ? pos[i].Dist(vm.Location) : 1e17;
        if (controlled && !byController) { byController = true; best = 1e18; } //a controlled pawn beats any guess by distance
        if ((controlled || (!byController && d < 300)) && d < best) { best = d; me = i; }
    }
    const ptr meA = me < n ? actors[me] : 0;
    const ptr mesh = ReadMemory<ptr>(meA + off::MESH);
    float rt[2] = {0, 0}; //LastRenderTime, LastRenderTimeOnScreen
    if (isValidPtr(mesh)) ReadMemoryBuffer(mesh + off::RENDER_TIME, rt, sizeof rt);
    if (self_.stage == 1) {
        if (!byController && ++self_.looks < 10) return; //not spawned yet: wait up to 10 s for the pawn your PlayerController controls
        if (meA) { self_.stage = 2; self_.me = meA; self_.rt = rt[1]; return; } //the report comes at the next look, 1 s later
    }
    const bool raid = count >= RAID_ACTORS;
    //final only for a raid with you in your pawn; a menu report, or one before you spawned, is run again
    self_.stage = raid && byController ? 0 : 3;

    constexpr int FAIL = 0, PASS = 1, SKIP = 2;
    static const char* const word[3] = {"FAIL", "PASS", "SKIP"};
    int total[3] = {0, 0, 0};
    std::string rep = "\n=== SELF-CHECK: the game numbers in config.h, checked live (read only) ===\n";
    if (!raid) rep += say("NOT A RAID: only %d actors (menu, lobby or hideout). Don't put these numbers in config.h: it runs again in a raid. (In a raid now? Then ACTORS_PTR is stale.)\n", count);
    //need: what this check needs (else SKIP, saying skipWhy); ok: the number looks right
    auto line = [&](bool need, bool ok, const std::string& name, const std::string& why, const char* skipWhy) {
        const int st = !need ? SKIP : ok ? PASS : FAIL;
        total[st]++;
        rep += say("%s  %-26s %s\n", word[st], name.c_str(), need ? why.c_str() : skipWhy);
    };
    auto hex = [](const char* name, ptr v) { return say("%s 0x%lx", name, (unsigned long)v); };

    static const char* const linkName[4] = {"UWORLD_RVA", "UWORLD_CHAIN_1", "UWORLD_CHAIN_2", "UWORLD_CHAIN_3"};
    const ptr linkOff[4] = {off::UWORLD_RVA, off::UWORLD_CHAIN_1, off::UWORLD_CHAIN_2, off::UWORLD_CHAIN_3};
    bool chainOk = true;
    for (int k = 0; k < 4; k++) { //the last link must be an object (its first word a vtable inside the game image), the others pointers
        const bool ok = k == 3 ? isObj(link[k]) : isValidPtr(link[k]);
        line(chainOk, ok, hex(linkName[k], linkOff[k]),
             say("-> 0x%lx%s", (unsigned long)link[k], k == 3 ? (ok ? " (an object: the UWorld)" : " is not an object, so not the UWorld") : (ok ? "" : " is not a pointer")),
             "an earlier link failed");
        chainOk = chainOk && ok;
    }
    const bool levelOk = isObj(level);
    line(chainOk, isValidPtr(lc), hex("LEVEL_COLLECTIONS", off::LEVEL_COLLECTIONS), say("-> 0x%lx", (unsigned long)lc), "the UWorld chain failed");
    line(isValidPtr(lc), levelOk, hex("PERSISTENT_LEVEL", off::PERSISTENT_LEVEL),
         say("-> 0x%lx%s", (unsigned long)level, levelOk ? " (an object)" : " is not an object"), "LEVEL_COLLECTIONS failed");
    line(levelOk, isValidPtr(arr) && raid && count <= 30000, hex("ACTORS_PTR", off::ACTORS_PTR),
         say("%d actors (a raid has %d to 30000)", count, RAID_ACTORS), "PERSISTENT_LEVEL failed");
    //Numbers from a menu or lobby must never be copied into config.h
    const char* copyIt = raid ? ": put that in config.h" : " (not a raid: not for config.h)";
    //The camera in use came from the CAMMANAGER vtable, or (that vtable stale) through the PlayerController fallback
    const ptr camVt = ReadMemory<ptr>(camActor_);
    const bool camByVt = camVt == vtabels::CAMMANAGER;
    line(true, camByVt, hex("CAMMANAGER vtable", vtabels::CAMMANAGER),
         camByVt ? std::string("the camera has it")
                 : say("stale: the camera was found only through the PlayerController; its vtable is 0x%lx%s", (unsigned long)camVt, copyIt), "");
    line(true, true, hex("VIEW_MATRIX", off::VIEW_MATRIX),
         say("camera found (%s), FOV %.0f", camByVt ? "by the CAMMANAGER vtable" : "through off::PC_CAMERA_MANAGER", (double)vm.FOV), "");

    const char* noMe = "you were not found";
    line(true, byController, hex("CONTROLLER", off::CONTROLLER),
         byController ? say("your pawn 0x%lx is controlled by the PlayerController 0x%lx", (unsigned long)meA, (unsigned long)pc)
         : meA ? say("no pawn points to the PlayerController 0x%lx; 'you' below is a guess: the actor nearest the camera", (unsigned long)pc)
               : say("no pawn points to the PlayerController 0x%lx, and nothing is within 3 m of the camera", (unsigned long)pc), "");
    const ptr meVt = me < n ? vt[me] : 0, meRoot = me < n ? root[me] : 0;
    const Vector3 mePos = me < n ? pos[me] : Vector3();
    line(meA != 0, meVt == vtabels::PLAYER, hex("PLAYER vtable", vtabels::PLAYER),
         meVt == vtabels::PLAYER ? std::string("your pawn has it")
         : say("your pawn's vtable is 0x%lx%s", (unsigned long)meVt, byController ? copyIt : " (a guess: CONTROLLER failed)"), noMe);
    const bool rootOk = isObj(meRoot);
    line(meA != 0, rootOk, hex("ROOT_COMPONENT_PTR", off::ROOT_COMPONENT_PTR),
         say("your root component 0x%lx%s", (unsigned long)meRoot, rootOk ? " (an object)" : " is not an object"), noMe);
    const double camDist = mePos.Dist(vm.Location) / 100.0;
    line(byController && rootOk, plausible(mePos) && camDist <= 3.0, hex("POS_PTR", off::POS_PTR),
         say("you are %.1f m from the camera (must be under 3)", camDist),
         byController ? "ROOT_COMPONENT_PTR failed" : "CONTROLLER failed, and the guess for you already used this number");

    const ptr hc = ReadMemory<ptr>(meA + off::HEALTH_COMPONENT);
    const bool hcOk = isObj(hc);
    const double hpMax = ReadMemory<double>(hc + off::HEALTH_MAX), hp = ReadMemory<double>(hc + off::HEALTH_CACHED);
    const bool maxOk = hpMax >= 50 && hpMax <= 500;
    line(meA != 0, hcOk, hex("HEALTH_COMPONENT", off::HEALTH_COMPONENT), say("-> 0x%lx%s", (unsigned long)hc, hcOk ? " (an object)" : " is not an object"), noMe);
    line(hcOk, maxOk, hex("HEALTH_MAX", off::HEALTH_MAX), say("max health %.1f (must be 50 to 500)", hpMax), "HEALTH_COMPONENT failed");
    line(hcOk, hp >= 0 && hp <= (maxOk ? hpMax + 1 : 500), hex("HEALTH_CACHED", off::HEALTH_CACHED),
         say("health %.1f (must be 0 to the max health)", hp), "HEALTH_COMPONENT failed");

    const int team = ReadMemory<uint8_t>(meA + off::TEAM_ID);
    int others = 0, same = 0; //the other actors of your class (players), and how many share your team byte
    for (size_t i = 0; i < n && meA && meVt; i++)
        if (i != me && vt[i] == meVt) { others++; same += ReadMemory<uint8_t>(actors[i] + off::TEAM_ID) == team; }
    line(others > 0, same <= 2, hex("TEAM_ID", off::TEAM_ID),
         say("your team %d is shared by %d of %d other players (a squad is at most 3)", team, same, others),
         meA ? "no other players found (found by your pawn's vtable)" : noMe);

    const bool meshObj = isObj(mesh);
    const Vector3 meshPos = ReadMemory<Vector3>(mesh + off::POS_PTR);
    const double meshDist = meshPos.Dist(mePos) / 100.0;
    line(meA != 0, meshObj && mesh != meRoot && plausible(meshPos) && meshDist < 3.0, hex("MESH", off::MESH),
         say("-> 0x%lx, %.1f m from your root (must be an object under 3 m away, not the root)", (unsigned long)mesh, meshDist), noMe);
    const ptr boneData = ReadMemory<ptr>(mesh + off::BONE_ARRAY);
    const int boneNum = ReadMemory<int>(mesh + off::BONE_ARRAY + 8);
    line(meshObj, isValidPtr(boneData) && boneNum == skeleton::BONES, hex("BONE_ARRAY", off::BONE_ARRAY),
         say("%d bones (the player skeleton has %d)", boneNum, skeleton::BONES), meA ? "MESH failed" : noMe);
    line(meshObj && self_.me == meA, std::isfinite(rt[1]) && std::isfinite(self_.rt) && rt[1] > self_.rt && rt[1] - self_.rt < 60, hex("RENDER_TIME", off::RENDER_TIME),
         say("your last draw time went %.2f -> %.2f in about 1 s (must go up)", (double)self_.rt, (double)rt[1]),
         !meA ? noMe : !meshObj ? "MESH failed" : "you changed between the two looks: run it again");

    int arcs = 0, badFlag = 0;
    for (size_t i = 0; i < n; i++)
        if (vt[i] == vtabels::ARC) { arcs++; badFlag += ReadMemory<uint8_t>(actors[i] + off::IS_DESTROYED) > 1; }
    line(arcs > 0, badFlag == 0, hex("IS_DESTROYED", off::IS_DESTROYED), say("%d of %d ARC read a byte other than 0 or 1", badFlag, arcs),
         "no ARC found (the ARC vtable may be stale, or this map has none)");

    const char* home = std::getenv("HOME");
    const std::string path = std::string(home ? home : ".") + "/.config/kwin-helper/selfcheck.txt";
    rep += say("=== %d PASS, %d FAIL, %d SKIP. %s Saved to %s ===\n", total[PASS], total[FAIL], total[SKIP],
               raid ? "Fix the first FAIL first: the ones after it may follow from it." : "NOT A RAID: the report in a raid replaces this one.",
               path.c_str());
    std::fputs(rep.c_str(), stdout);
    std::fflush(stdout);
    if (FILE* f = std::fopen(path.c_str(), "w")) { std::fputs(rep.c_str(), f); std::fclose(f); }
    else std::printf("[!] Self-check: could not save %s\n", path.c_str());
}

//No game world yet (menu, loading screen, or stale offsets): where the UWorld chain stops, link by link, saved to
//~/.config/kwin-helper/selfcheck.txt about once a second. It needs no raid, so it is the check that works before one.
void GameState::chainReportIfAsked() {
    //Not over a full report that is waiting for the raid (stage 3): a loading screen would leave this one in the file
    if (chainStop_ < 0 || self_.stage == 3) return; //a reached UWorld: the full self-check (selfCheckIfAsked) takes over
    const auto now = clk::now();
    if (now - self_.chainAt < std::chrono::seconds(1)) return;
    self_.chainAt = now;
    static const char* const linkName[4] = {"UWORLD_RVA", "UWORLD_CHAIN_1", "UWORLD_CHAIN_2", "UWORLD_CHAIN_3"};
    const ptr linkOff[4] = {off::UWORLD_RVA, off::UWORLD_CHAIN_1, off::UWORLD_CHAIN_2, off::UWORLD_CHAIN_3};
    std::string rep = "\n=== SELF-CHECK, no game world yet: the UWorld chain, link by link (read only) ===\n";
    rep += say("game base 0x%lx\n", (unsigned long)baseAddr);
    for (int k = 0; k < 4; k++) {
        if (k < chainStop_) rep += say("PASS  %-14s +0x%lx -> 0x%lx\n", linkName[k], (unsigned long)linkOff[k], (unsigned long)chain_[k]);
        else if (k == chainStop_) rep += say("FAIL  %-14s +0x%lx -> 0x0 (the chain stops here)\n", linkName[k], (unsigned long)linkOff[k]);
        else rep += say("SKIP  %-14s not read: the chain stopped before it\n", linkName[k]);
    }
    rep += chainStop_ == 0
        ? std::string("The first read is 0: UWORLD_RVA is stale for this game build, or the game base is wrong.\n")
        : say("The chain stops at %s. In the main menu that is normal. In a raid it means the offsets in config.h are stale for this game build.\n", linkName[chainStop_]);
    const char* home = std::getenv("HOME");
    const std::string path = std::string(home ? home : ".") + "/.config/kwin-helper/selfcheck.txt";
    rep += say("=== Saved to %s, refreshed every second while there is no game world ===\n", path.c_str());
    if (FILE* f = std::fopen(path.c_str(), "w")) { std::fputs(rep.c_str(), f); std::fclose(f); }
    else std::printf("[!] Self-check: could not save %s\n", path.c_str());
}

gsRet GameState::step() {
    if (isDebugMode || probeNames) return tick(); //the diagnostic modes want the unfiltered view

    const auto t0 = clk::now();
    //No world or camera: no full self-check report (it needs them). Without a world the UWorld chain report is written
    //instead, so a stale offset shows up before a raid too. While one is asked for, the console says so.
    auto failed = [&](const char* why) -> gsRet {
        if (self_.stage) {
            chainReportIfAsked();
            logThrottled((std::string("[-] Self-check waiting, no full report until a world and camera are found: ") + why).c_str());
        }
        return {.fault = why};
    };
    //The world takes 4 reads one after another, so it is checked before every scan and about 30 times a second, not on
    //every step: leaving a raid or loading a map is still noticed within a few hundredths of a second
    const bool scanDue = camActor_ == 0 || t0 - lastScan_ >= nextScanGap();
    if (scanDue || t0 - worldAt_ >= WORLD_CHECK_GAP) {
        worldAt_ = t0;
        const ptr world = getUworld();
        if (scanDue || world != lastWorld_ || !isValidPtr(world)) {
            lastScan_ = t0;
            if (!scan(world)) { statics_.clear(); dyn_.clear(); slots_.clear(); camActor_ = 0; poseOkAt_ = t0; return failed(fault_); }
        }
    }
    FminimalViewInfo vm{}; //no camera: stays zero, which is not a usable view
    updateDynamic(vm);     //the camera's view is read in the same go as the players
    if (posed_ || !poseWanted_) poseOkAt_ = t0; //stick figures work, or nobody near needs one

    //One bad read (a failed read, the camera being rewritten) must not blank the overlay: the last good view is used
    //for a moment. Only a camera that stays unusable is dropped and looked for again.
    if (viewUsable(vm)) { goodVm_ = vm; goodVmAt_ = t0; }
    else if (camActor_ && t0 - goodVmAt_ < CAMERA_HOLD) vm = goodVm_;
    else {
        logThrottled("[-] Invalid rawEntities or no viewMatrix");
        camActor_ = 0; //find the camera again at the next step
        return failed("no camera found (CAMMANAGER vtable or VIEW_MATRIX is stale?)");
    }
    if (statics_.empty() && dyn_.empty()) logThrottled("[-] No ent found, may be in practice range or its just not working");

    gsRet out = assemble(vm);
    runLiveProbeIfAsked(out.entities, vm);
    if (self_.stage) selfCheckIfAsked(vm);

    //The same one-line status as always, every 2 s (see printStatusLine): me / players / ARC / containers / stick figures
    static auto lastStatus = clk::now() - std::chrono::seconds(10);
    if (t0 - lastStatus >= std::chrono::seconds(2)) {
        lastStatus = t0;
        const RenderEntity* meEnt = nullptr; //the pawn assemble() chose as you; out.entities already holds everything else
        for (const Tracked& t : dyn_) if (t.live && t.ent.actor == meActor_) meEnt = &t.ent;
        printStatusLine(out.entities, meEnt, posed_, poseWanted_);
        //Players near enough for a stick figure, and not one of them has had a pose for 5 s: say why (a patch that
        //changed the skeleton or moved an offset would otherwise turn every stick figure off without a word)
        if (poseWanted_ && !posed_ && t0 - poseOkAt_ >= std::chrono::seconds(5))
            logThrottled(("[-] Stick figures: none drawn for 5 s: " + poseWhy_ +
                          ". Run the bones probe: build the probe version (README, probes section), then "
                          "KWINHELPER_BIN=./cmake-build-probe/kwin-helper KWINHELPER_PROBE=bones bash start.sh, "
                          "and copy the === BONES PROBE lines").c_str());
    }
    timing_.lastStepMs = ms(t0, clk::now());
    return out;
}
