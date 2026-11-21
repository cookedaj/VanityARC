//The normal read path (see GameState::step). The old path in GameState.cpp reads every actor of the level one at a
//time on every frame (about 7 ms for 10 000 actors, on the same thread that draws). Here a full scan only runs a few
//times a second; in between, only the actors that move are read again, and with many reads per system call.
#include "GameState.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#include "utils/labeler.h"
#include "utils/log.h"
#include "utils/offsets.h"

void printStatusLine(const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm); //GameState.cpp
void runLiveProbe(const std::string& mode, ptr localPawn, const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm, FILE* out = stdout); //Probe.cpp

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
} // namespace

bool GameState::scan() {
    const auto t0 = clk::now();
    ptr uworld = getUworld();
    if (!isValidPtr(uworld)) {
        logThrottled("[-] Invalid Uworld (not in a raid yet?)");
        return false;
    }
    lastWorld_ = uworld;
    //PersistentLevel at UWorld+0x170 is encrypted; the plain pointer is in LevelCollections[0] (see config.h)
    ptr levelCollection = ReadMemory<ptr>(uworld + off::LEVEL_COLLECTIONS);
    if (!isValidPtr(levelCollection)) { logThrottled("[-] No level collection yet"); return false; }
    ptr persistentLevel = ReadMemory<ptr>(levelCollection + off::PERSISTENT_LEVEL);
    if (!isValidPtr(persistentLevel)) { logThrottled("[-] No persistent level yet"); return false; }
    ptr actorsPtr = ReadMemory<ptr>(persistentLevel + off::ACTORS_PTR);
    int count = ReadMemory<int>(persistentLevel + off::ACTORS_PTR + 0x8);
    if (!(isValidPtr(actorsPtr) && count > 0 && count < 30000)) { logThrottled("[-] Actor list not readable yet"); return false; }

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
    for (size_t i = 0; i < cand.size(); i++) {
        const ptr vt = vts[i];
        Object t;
        if (vt == vtabels::PLAYER) t = Object::PLAYER;
        else if (vt == vtabels::ARC) t = Object::ARC;
        else if (vt == vtabels::SEARCH) t = Object::SEARCH;
        else if (vt == vtabels::CAMMANAGER) { cam = cand[i]; continue; } //the camera only; the last one wins
        else continue; //everything else (including the old PICKUP vtable) is not drawn
        kept.push_back({cand[i], vt, 0, t, {}});
    }

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
    camActor_ = cam;
    for (auto it = shieldSeen_.begin(); it != shieldSeen_.end();) { //forget players that are gone (their address may be reused)
        bool present = false;
        for (const Tracked& t : dyn_) if (t.ent.actor == it->first) { present = true; break; }
        it = present ? std::next(it) : shieldSeen_.erase(it);
    }
    timing_.lastScanMs = ms(t0, clk::now());
    timing_.statics = statics_.size();
    timing_.dynamics = dyn_.size();
    return true;
}

void GameState::updateDynamic() {
    const size_t n = dyn_.size();
    if (!n) return;
    struct Row { ptr vt = 0, root = 0, hc = 0, sc = 0; int team = 0; uint8_t destroyed = 0; Vector3 pos; double hp = 0; uint64_t shRaw = 0, shMaxRaw = 0; };
    const bool shieldOn = live::shieldValue >= 0;
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
            queue(rq, e.actor + off::TEAM_ID, &r.team);
            if (shieldOn && live::shieldComponent >= 0) queue(rq, e.actor + (ptr)live::shieldComponent, &r.sc); //the shield lives in another component
        } else {
            queue(rq, e.actor + off::IS_DESTROYED, &r.destroyed);
        }
    }
    ReadScatter(rq.data(), rq.size());

    rq.clear();
    for (size_t i = 0; i < n; i++) {
        Row& r = rows[i];
        if (r.vt != dyn_[i].ent.vt || !isValidPtr(r.root)) continue;
        queue(rq, r.root + off::POS_PTR, &r.pos);
        if (dyn_[i].ent.type == Object::PLAYER && isValidPtr(r.hc)) queue(rq, r.hc + off::HEALTH_CACHED, &r.hp);
        if (dyn_[i].ent.type == Object::PLAYER && shieldOn) {
            const ptr comp = live::shieldComponent >= 0 ? r.sc : r.hc;
            if (isValidPtr(comp)) {
                queue(rq, comp + (ptr)live::shieldValue, &r.shRaw);
                if (live::shieldMax >= 0) queue(rq, comp + (ptr)live::shieldMax, &r.shMaxRaw);
            }
        }
    }
    ReadScatter(rq.data(), rq.size());

    for (size_t i = 0; i < n; i++) {
        Tracked& t = dyn_[i];
        const Row& r = rows[i];
        t.live = r.vt == t.ent.vt && isValidPtr(r.root) && plausible(r.pos);
        if (!t.live) continue;
        t.root = r.root;
        t.ent.pos = r.pos;
        if (t.ent.type == Object::PLAYER) {
            t.ent.playerHealth = isValidPtr(r.hc) ? r.hp : 100; //no health component read: treated as alive, as before
            t.ent.isDead = t.ent.playerHealth < 1;
            t.ent.teamID = r.team & 0xFF; //the byte at TeamId only; the bytes after it belong to other fields
            const ptr comp = live::shieldComponent >= 0 ? r.sc : r.hc;
            t.ent.hasShield = shieldOn && isValidPtr(comp);
            t.ent.shield = t.ent.hasShield ? shieldNumber(r.shRaw) : 0.0;
            if (t.ent.hasShield) {
                double& seen = shieldSeen_[t.ent.actor]; //the longest the bar has been
                seen = std::max(seen, t.ent.shield);
                t.ent.shieldMax = live::shieldMax >= 0 ? std::max(shieldNumber(r.shMaxRaw), t.ent.shield) : seen;
            }
        } else {
            t.ent.isDead = r.destroyed != 0;
        }
    }
}

gsRet GameState::assemble(const FminimalViewInfo& vm) {
    //You are the player closest to the camera (third person puts it about 2 m away). Only that one is you: another
    //player standing right next to the camera (a teammate being revived, an enemy in melee range) must still be drawn.
    const Tracked* me = nullptr;
    double best = 2 * 100;
    for (const Tracked& t : dyn_) {
        if (!t.live || t.ent.type != Object::PLAYER) continue;
        double d = t.ent.pos.Dist(vm.Location);
        if (d < best) { best = d; me = &t; }
    }
    if (me) lastTeamID = me->ent.teamID;
    meActor_ = me ? me->ent.actor : 0;

    gsRet out;
    out.vm = vm;
    out.localPlayerTeam = lastTeamID;
    out.valid = true;
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
    static const std::string mode = [] { const char* e = std::getenv("KWINHELPER_PROBE"); return std::string(e ? e : ""); }();
    if (mode.empty()) return;
    static auto last = clk::now() - std::chrono::seconds(10);
    const auto now = clk::now();
    if (now - last < std::chrono::seconds(2)) return;
    last = now;
    runLiveProbe(mode, meActor_, ents, vm);
}

gsRet GameState::step() {
    if (isDebugMode || probeNames) return tick(); //the diagnostic modes want the unfiltered view

    const auto t0 = clk::now();
    //The world is checked on every step (4 small reads): leaving a raid or loading a map is noticed at once
    const ptr world = getUworld();
    const bool full = camActor_ == 0 || world != lastWorld_ || !isValidPtr(world) || t0 - lastScan_ >= nextScanGap();
    if (full) {
        lastScan_ = t0;
        if (!scan()) { statics_.clear(); dyn_.clear(); camActor_ = 0; return {}; }
    }
    updateDynamic();

    //Same sanity check as the old path: a FOV far from the usual range means this is not a camera
    FminimalViewInfo vm = ReadMemory<FminimalViewInfo>(camActor_ + off::VIEW_MATRIX);
    if (!std::isfinite(vm.FOV) || std::abs(vm.FOV - 70.0) > 60.0) {
        logThrottled("[-] Invalid rawEntities or no viewMatrix");
        camActor_ = 0; //find the camera again at the next step
        return {};
    }
    if (statics_.empty() && dyn_.empty()) logThrottled("[-] No ent found, may be in practice range or its just not working");

    gsRet out = assemble(vm);
    runLiveProbeIfAsked(out.entities, vm);

    //The same one-line status as always, every 2 s (see printStatusLine): me / players / ARC / containers
    static auto lastStatus = clk::now() - std::chrono::seconds(10);
    if (t0 - lastStatus >= std::chrono::seconds(2)) {
        lastStatus = t0;
        std::vector<RenderEntity> all;
        all.reserve(statics_.size() + dyn_.size());
        for (const Tracked& t : statics_) all.push_back(t.ent);
        for (const Tracked& t : dyn_) if (t.live) all.push_back(t.ent);
        printStatusLine(all, vm);
    }
    timing_.lastStepMs = ms(t0, clk::now());
    return out;
}
