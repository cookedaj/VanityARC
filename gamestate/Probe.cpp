//Read-only discovery report for names and weapons (switch on with probeNames in config.h).
//Prints one report block every few seconds: which player names / object names / gun names can be read in this
//build, and what the actors around you are called. It reads memory only and draws nothing.
#include "config.h"
#include "utils/localUtil.h"
#include "utils/names.h"
#include "utils/skeleton.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

bool isModulePtr(ptr p) { return p >= ImageBase && p < ImageEnd; }

//A UObject starts with a vtable pointer into the game image
bool isObject(ptr o) { return isValidPtr(o) && isModulePtr(ReadMemory<ptr>(o)); }

std::vector<uint8_t> readBlock(ptr addr, size_t n) {
    std::vector<uint8_t> b(n, 0);
    ReadMemoryBuffer(addr, b.data(), n);
    return b;
}

uint64_t q64(const std::vector<uint8_t>& b, size_t off) {
    uint64_t v = 0;
    if (off + 8 <= b.size()) std::memcpy(&v, &b[off], 8);
    return v;
}

//Pointer-looking qword: a pointer differs between game launches, a plain number does not
bool ptrLike(uint64_t v) { return isValidPtr(v) && (v & 7) == 0; }

//Name of the object at p, "" if p is not an object or has no readable name
std::string nm(ptr p) { return isObject(p) ? names::fullName(p) : std::string(); }

//"0xADDR" or "0xADDR<name>"
std::string tag(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%lx", (unsigned long)v);
    std::string s = buf;
    if (ptrLike(v)) {
        std::string n = nm((ptr)v);
        if (!n.empty()) s += "<" + n + ">";
    }
    return s;
}

const char* kindName(Object t) {
    switch (t) {
        case Object::ARC: return "ARC";
        case Object::SEARCH: return "container";
        case Object::PICKUP: return "pickup";
        case Object::PLAYER: return "player";
        default: return "other";
    }
}

//----------------------------------------------------------------------------------------------
// Field scan across many instances of one kind: finds fields that hold a handful of distinct values,
// which is what a type / variant field looks like. Pointer values are shown with their object name.
//----------------------------------------------------------------------------------------------
void diffScan(FILE* out, const char* title, const std::vector<ptr>& actors, int from, int to, size_t maxRows) {
    if (actors.size() < 3) {
        std::fprintf(out, "%s: only %zu instance(s) in view, scan skipped\n", title, actors.size());
        return;
    }
    const size_t nOff = (size_t)(to - from) / 8;
    std::vector<std::map<uint64_t, int>> vals(nOff);
    for (ptr a : actors) {
        auto b = readBlock(a + from, (size_t)(to - from));
        for (size_t i = 0; i < nOff; i++) {
            uint64_t v = q64(b, i * 8);
            if (v) vals[i][v]++;
        }
    }
    struct Row { int off; size_t distinct; int covered; std::vector<std::pair<uint64_t, int>> top; };
    std::vector<Row> rows;
    for (size_t i = 0; i < nOff; i++) {
        int covered = 0;
        for (auto& kv : vals[i]) covered += kv.second;
        if (covered < (int)actors.size() / 2 || vals[i].size() < 2 || vals[i].size() > 48) continue;
        Row r{from + (int)i * 8, vals[i].size(), covered, {}};
        r.top.assign(vals[i].begin(), vals[i].end());
        std::sort(r.top.begin(), r.top.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (r.top.size() > 4) r.top.resize(4);
        rows.push_back(r);
    }
    //Rows whose values have readable object names are the interesting ones: show them first
    auto named = [](const Row& r) { return !nm((ptr)r.top[0].first).empty(); };
    std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) { return named(a) > named(b); });
    std::fprintf(out, "%s: %zu instances, fields with 2..48 distinct values (+offset, distinct, set in, top values)\n", title, actors.size());
    for (size_t i = 0; i < rows.size() && i < maxRows; i++) {
        std::fprintf(out, "  +0x%x  distinct %2zu  set %3d/%zu  top:", rows[i].off, rows[i].distinct, rows[i].covered, actors.size());
        for (auto& t : rows[i].top) std::fprintf(out, " %s x%d", tag(t.first).c_str(), t.second);
        std::fprintf(out, "\n");
    }
    if (rows.empty()) std::fprintf(out, "  (none)\n");
}

//----------------------------------------------------------------------------------------------
// Players
//----------------------------------------------------------------------------------------------
//The PlayerState: pawn+0x3E8 in v1029. The pawn holds other objects next to it (controller, class pointers), so a
//slot counts only when its object is named like a PlayerState: 0x3E8 first, then 0x380..0x470. If none is (object
//names unreadable, or the class is named differently), a slot whose +0x460 decodes to a player name is taken.
ptr playerStateOf(ptr pawn, int* offOut = nullptr) {
    auto accept = [](ptr p, bool byClass) {
        return byClass ? nm(p).find("PlayerState") != std::string::npos : isObject(p) && !names::playerName(p + 0x460).empty();
    };
    for (bool byClass : {true, false}) {
        ptr ps = ReadMemory<ptr>(pawn + 0x3E8);
        if (accept(ps, byClass)) { if (offOut) *offOut = 0x3E8; return ps; }
        for (int off = 0x380; off < 0x470; off += 8) {
            ptr p = ReadMemory<ptr>(pawn + off);
            if (accept(p, byClass)) { if (offOut) *offOut = off; return p; }
        }
    }
    return 0;
}

//The player name FString in the PlayerState: +0x460 in v1029. If that reads nothing, the first FString in
//+0x400..+0x500 that decodes to plain text.
int nameOffOf(ptr ps) {
    if (!names::playerName(ps + 0x460).empty()) return 0x460;
    for (int off = 0x400; off < 0x500; off += 8) {
        const std::string n = names::playerName(ps + off);
        if (!n.empty() && n.find('?') == std::string::npos) return off;
    }
    return 0x460;
}

//The account id is held through PlayerState+0x3E0 -> object, id qword at object+0x18
ptr accountId(ptr ps) {
    ptr obj = ReadMemory<ptr>(ps + 0x3E0);
    return isValidPtr(obj) ? ReadMemory<ptr>(obj + 0x18) : 0;
}

void probePlayers(FILE* out, const std::vector<std::pair<double, const RenderEntity*>>& players, ptr localPawn) {
    std::fprintf(out, "PLAYERS (PS = PlayerState @pawn+0x3E8, name = PS+0x460 decoded, account id = PS+0x3E0 -> +0x18)\n");
    int shown = 0;
    for (const auto& pl : players) {
        ptr pawn = pl.second->actor;
        int off = 0;
        ptr ps = playerStateOf(pawn, &off);
        std::fprintf(out, "  %6.1f m%s", pl.first, pawn == localPawn ? " (you)" : "      ");
        if (!ps) { std::fprintf(out, "  no PlayerState\n"); continue; }
        const int nameOff = nameOffOf(ps);
        std::string n = names::playerName(ps + nameOff);
        std::fprintf(out, "  PS @pawn+0x%x%s  name '%s' (PS+0x%x%s)  id %016lx  pawn: %s  team %d\n", off,
                     off != 0x3E8 ? " MOVED: set PAWN_PLAYERSTATE" : "", n.c_str(), nameOff, nameOff != 0x460 ? " MOVED: set PS_NAME" : "",
                     (unsigned long)accountId(ps), nm(pawn).c_str(), ReadMemory<int>(pawn + 0x802) & 0xFF);
        if (++shown >= 16) break;
    }
}

//----------------------------------------------------------------------------------------------
// Guns
//----------------------------------------------------------------------------------------------
//One stowed weapon slot: +0x10 weapon actor, +0x18 gun asset, +0x38 quality
void printSlot(FILE* out, const char* label, ptr inv, int base) {
    ptr actor = ReadMemory<ptr>(inv + base + 0x10);
    ptr asset = ReadMemory<ptr>(inv + base + 0x18);
    int q = ReadMemory<int>(inv + base + 0x38);
    if (!asset) { std::fprintf(out, " %s empty |", label); return; }
    std::fprintf(out, " %s asset %s actor %s q %d |", label, tag(asset).c_str(), tag(actor).c_str(), q);
}

//A TArray of 0x20-byte item records (CurrentItemActors): show every qword with its object name
void printItemArray(FILE* out, const char* label, ptr inv, int off) {
    ptr data = ReadMemory<ptr>(inv + off);
    int num = ReadMemory<int>(inv + off + 8);
    std::fprintf(out, "      %s (inv+0x%x): ", label, off);
    if (!isValidPtr(data) || num <= 0 || num > 32) { std::fprintf(out, "empty/invalid (num %d)\n", num); return; }
    std::fprintf(out, "num %d\n", num);
    for (int i = 0; i < num && i < 6; i++) {
        std::fprintf(out, "        [%d]", i);
        for (int k = 0; k < 4; k++) {
            uint64_t v = (uint64_t)ReadMemory<ptr>(data + i * 0x20 + k * 8);
            if (v) std::fprintf(out, " +0x%x=%s", k * 8, tag(v).c_str());
        }
        std::fprintf(out, "\n");
    }
}

void probeGuns(FILE* out, const std::vector<std::pair<double, const RenderEntity*>>& players, ptr localPawn) {
    constexpr int INV = 0xCB0, SLOT0 = 0x3B0, SLOT1 = 0x3F0, CUR = 0x530, CUR2 = 0x550;
    std::fprintf(out, "GUNS (inventory @pawn+0x%x; stowed slots +0x%x / +0x%x; held-item arrays +0x%x / +0x%x)\n", INV, SLOT0, SLOT1, CUR, CUR2);
    int shown = 0;
    for (const auto& pl : players) {
        ptr pawn = pl.second->actor;
        ptr inv = ReadMemory<ptr>(pawn + INV);
        std::fprintf(out, "  %6.1f m%s", pl.first, pawn == localPawn ? " (you)" : "      ");
        if (!isObject(inv)) { std::fprintf(out, " no inventory\n"); continue; }
        printSlot(out, "slot0", inv, SLOT0);
        printSlot(out, "slot1", inv, SLOT1);
        std::fprintf(out, "\n");
        printItemArray(out, "CurrentItemActors", inv, CUR);
        printItemArray(out, "LocalCurrentItemActors", inv, CUR2);
        if (++shown >= 6) break;
    }
}

//----------------------------------------------------------------------------------------------
// Everything else: what actors exist and what they are called
//----------------------------------------------------------------------------------------------
struct Census { int count = 0; double nearest = 1e18; Object kind = Object::NONE; std::string text; };

void probeCensus(FILE* out, const std::vector<RenderEntity>& ents, const std::vector<RenderEntity>& unknown, size_t unknownTotal, const FminimalViewInfo& vm) {
    std::map<uint32_t, Census> census;
    int unreadable = 0, total = 0;
    auto add = [&](const RenderEntity& e) {
        total++;
        names::Decoded d = names::objectName(e.actor);
        if (!d.ok) { unreadable++; return; }
        Census& c = census[d.index];
        c.count++;
        c.nearest = std::min(c.nearest, e.pos.Dist(vm.Location) / 100.0);
        if (c.text.empty()) { c.text = d.text; c.kind = e.type; }
    };
    for (const auto& e : ents) add(e);
    for (const auto& e : unknown) add(e);

    std::vector<Census> rows;
    for (auto& kv : census) rows.push_back(kv.second);
    std::sort(rows.begin(), rows.end(), [](const Census& a, const Census& b) { return a.count > b.count; });
    const size_t dropped = unknownTotal - unknown.size(); //other-class actors too far away to be named
    std::fprintf(out, "ACTOR NAMES in the level: %zu actors", (size_t)total + dropped);
    if (dropped) std::fprintf(out, " (other classes: names read for the nearest %zu of %zu)", unknown.size(), unknownTotal);
    std::fprintf(out, ", %d names unreadable, %zu distinct names (top 45 by count)\n", unreadable, rows.size());
    for (size_t i = 0; i < rows.size() && i < 45; i++)
        std::fprintf(out, "  %4d  %-9s  nearest %6.1f m  %s\n", rows[i].count, kindName(rows[i].kind), rows[i].nearest, rows[i].text.c_str());
}

//Data assets an actor points at (names starting DA_ ...): this is how an item / enemy / container type is spelled
void printAssetFields(FILE* out, ptr actor, size_t span, int maxShown) {
    auto b = readBlock(actor, span);
    int shown = 0;
    for (size_t off = 8; off + 8 <= span && shown < maxShown; off += 8) {
        uint64_t v = q64(b, off);
        if (!ptrLike(v) || !isObject((ptr)v)) continue;
        std::string n = names::name((ptr)v);
        if (n.size() > 3 && n.compare(0, 3, "DA_") == 0) {
            std::fprintf(out, " +0x%zx=%s", off, n.c_str());
            shown++;
        }
    }
    if (!shown) std::fprintf(out, " (no DA_ pointers)");
}

void probeNear(FILE* out, const std::vector<RenderEntity>& ents, const std::vector<RenderEntity>& unknown, const FminimalViewInfo& vm) {
    struct Near { double d; const RenderEntity* e; };
    std::vector<Near> v;
    for (const auto& e : ents) if (e.type != Object::PLAYER) { double d = e.pos.Dist(vm.Location) / 100.0; if (d < 6.0) v.push_back({d, &e}); }
    for (const auto& e : unknown) { double d = e.pos.Dist(vm.Location) / 100.0; if (d < 6.0) v.push_back({d, &e}); }
    std::sort(v.begin(), v.end(), [](const Near& a, const Near& b) { return a.d < b.d; });
    std::fprintf(out, "NEAR YOU (within 6 m, nearest first; DA_ = data assets the actor points at)\n");
    if (v.empty()) std::fprintf(out, "  (nothing)\n");
    for (size_t i = 0; i < v.size() && i < 14; i++) {
        std::fprintf(out, "  %5.1f m  %-9s %s |", v[i].d, kindName(v[i].e->type), nm(v[i].e->actor).c_str());
        printAssetFields(out, v[i].e->actor, 0x1000, 4);
        std::fprintf(out, "\n");
    }
}

} // namespace

void runNameProbe(const std::vector<RenderEntity>& ents, const std::vector<RenderEntity>& unknown,
                  const FminimalViewInfo& vm, ptr uworld, FILE* out) {
    (void)uworld;
    std::fprintf(out, "\n=== PROBE v4 (paste from here to the next === PROBE line) ===\n");
    const names::Status ns = names::status();
    std::fprintf(out, "object-name decoder: %s\n", ns == names::Status::Ok ? "ready"
                 : ns == names::Status::PoolStale ? "NOT READY (key table reads, but name 0 is not \"None\": NAMES_RVA / ANCHOR_RVA / ANCHOR_XOR stale, or the pool is not loaded yet)"
                 : "NOT READY (text key table unreadable: RVA stale or game not in a raid)");

    std::vector<ptr> containers;
    std::vector<std::pair<double, const RenderEntity*>> players;
    struct ArcType { int count = 0; double nearest = 1e18; ptr sample = 0; };
    std::map<ptr, ArcType> arc;
    int pickups = 0;

    for (const auto& e : ents) {
        double d = e.pos.Dist(vm.Location) / 100.0;
        switch (e.type) {
            case Object::ARC: {
                ArcType& t = arc[ReadMemory<ptr>(e.actor + 0x11C0)]; //EnemyTypeDataAsset
                t.count++;
                if (d < t.nearest) { t.nearest = d; t.sample = e.actor; }
                break;
            }
            case Object::SEARCH: if (containers.size() < 600) containers.push_back(e.actor); break;
            case Object::PICKUP: pickups++; break;
            case Object::PLAYER: players.push_back({d, &e}); break;
            default: break;
        }
    }
    std::sort(players.begin(), players.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const size_t seen = players.size(); //before the cut to 16, so a stale PLAYER vtable matching many actors shows
    if (players.size() > 16) players.resize(16);
    const RenderEntity* me = (!players.empty() && players[0].first < 2.0) ? players[0].second : nullptr;
    std::fprintf(out, "players seen %zu, you: %s, ground items (PICKUP class) in view: %d\n", seen,
                 me ? "found" : "NOT FOUND (no PLAYER within 2 m)", pickups);

    probePlayers(out, players, me ? me->actor : 0);

    std::fprintf(out, "ARC TYPES (EnemyTypeDataAsset @actor+0x11C0; sample = nearest actor of that type)\n");
    for (auto& kv : arc) {
        std::fprintf(out, "  count %3d  nearest %6.1f m  asset %s  sample actor %s  AITemplate %s\n", kv.second.count, kv.second.nearest,
                     tag(kv.first).c_str(), nm(kv.second.sample).c_str(), tag(ReadMemory<ptr>(kv.second.sample + 0x11B0)).c_str());
    }
    if (arc.empty()) std::fprintf(out, "  (no ARC in view)\n");

    probeGuns(out, players, me ? me->actor : 0);
    //A level holds about 10,000 other-class actors and reading every name is slow: only the 1500 nearest the camera
    std::vector<RenderEntity> nearest(unknown);
    if (nearest.size() > 1500) {
        std::nth_element(nearest.begin(), nearest.begin() + 1500, nearest.end(), [&](const RenderEntity& a, const RenderEntity& b) {
            return a.pos.Dist(vm.Location) < b.pos.Dist(vm.Location);
        });
        nearest.resize(1500);
    }
    probeNear(out, ents, nearest, vm);
    probeCensus(out, ents, nearest, unknown.size(), vm);

    std::fprintf(out, "CONTAINERS\n");
    diffScan(out, "  container fields", containers, 0x300, 0x1400, 14);
    std::fflush(out);
}


//==============================================================================================
// Live probes: KWINHELPER_PROBE=shield or KWINHELPER_PROBE=inventory, set before starting (see the README). Read only.
//==============================================================================================
#ifdef KWINHELPER_PROBES //the offset-finding probes: not in the release build (cmake -DKWINHELPER_PROBES=ON)
namespace {

//Every object the pawn points to between +0x300 and +0x1400 (its components), with where the pointer sits
struct Comp { int pawnOff; ptr obj; };
std::vector<Comp> componentsOf(ptr pawn) {
    std::vector<Comp> comps;
    auto pb = readBlock(pawn + 0x300, 0x1100);
    for (size_t i = 0; i + 8 <= pb.size() && comps.size() < 120; i += 8) {
        uint64_t v = q64(pb, i);
        if (ptrLike(v) && isObject((ptr)v)) comps.push_back({0x300 + (int)i, (ptr)v});
    }
    return comps;
}

struct FieldState { double last = 0; int changes = 0; bool noisy = false; };

//Shield: every number in every component of your pawn is remembered; the ones that CHANGE (a few times, not every
//moment like a clock) are printed. Take a hit, let the shield drain or recharge, and the shield number shows up
//next to the known health number.
void shieldProbe(ptr pawn, FILE* out) {
    static ptr lastPawn = 0;
    static std::unordered_map<uint64_t, FieldState> st;
    static int cycle = 0;
    if (pawn != lastPawn) { st.clear(); lastPawn = pawn; cycle = 0; }
    cycle++;
    const bool warmUp = cycle <= 4; //pass 1 remembers, passes 2-4 (about 6 s) mark every number that changes by itself (clocks, positions) as noise
    if (cycle == 1) std::fprintf(out, "\n=== SHIELD PROBE: watching your pawn 0x%lx. Move around, but keep your shield full and take NO damage until '=== NOW take a hit ===' appears (about 6 s) ===\n", (unsigned long)pawn);

    int printed = 0;
    std::unordered_set<uint64_t> doubleChanged; //doubles that changed in this pass: their two 4-byte halves are not separate numbers
    for (const Comp& c : componentsOf(pawn)) {
        auto blk = readBlock(c.obj, 0x900);
        for (size_t o = 0; o + 4 <= blk.size(); o += 4) {
            for (int isFloat = 0; isFloat < 2; isFloat++) {
                if (!isFloat && (o & 7)) continue;
                double v;
                if (isFloat) { float f; std::memcpy(&f, &blk[o], 4); v = f; }
                else { std::memcpy(&v, &blk[o], 8); }
                if (!std::isfinite(v) || v < 0 || v > 100000.0) v = -1; //not a believable amount
                const uint64_t key = ((uint64_t)c.pawnOff << 32) | ((uint64_t)o << 1) | (uint64_t)isFloat;
                auto it = st.find(key);
                if (it == st.end()) { st[key] = {v, 0}; continue; }
                FieldState& f = it->second;
                if (v >= 0 && f.last >= 0 && std::fabs(v - f.last) > 1e-6) {
                    const uint64_t doubleKey = ((uint64_t)c.pawnOff << 32) | (isFloat ? (o & ~(size_t)7) : o);
                    if (!isFloat) doubleChanged.insert(doubleKey);
                    const bool halfOfDouble = isFloat && doubleChanged.count(doubleKey) > 0; //the same bytes as a double above: not a new finding
                    if (warmUp) f.noisy = true; //changed by itself before the hit: never printed
                    else if (!f.noisy && !halfOfDouble && printed < 50 && ++f.changes <= 3) { //counted only when printed: the 50-line cap cannot use up a number's 3 lines
                        const bool isHealth = c.pawnOff == (int)off::HEALTH_COMPONENT && o == (size_t)off::HEALTH_CACHED;
                        std::fprintf(out, "  pawn+0x%x (%s)  +0x%zx  %s  %.3f -> %.3f%s\n", c.pawnOff, nm(c.obj).c_str(), o, isFloat ? "float " : "double", f.last, v,
                                    isHealth ? "   <- known: health" : "");
                        if (!isHealth) //what to put in offsets.txt if THIS is the shield
                        {
                            char comp[16];
                            if (c.pawnOff == (int)off::HEALTH_COMPONENT) std::snprintf(comp, sizeof comp, "-1"); else std::snprintf(comp, sizeof comp, "0x%x", c.pawnOff);
                            std::fprintf(out, "      if this is the shield:  shield_component %s  shield_value 0x%zx  shield_is_float %d\n", comp, o, isFloat);
                        }
                        printed++;
                    }
                }
                f.last = v;
            }
        }
    }
    if (cycle == 1) std::fprintf(out, "  (watching %zu components; nothing is printed until a number changes)\n", st.size());
    if (cycle == 4) std::fprintf(out, "\n=== NOW take a hit === (or let the shield drain / recharge), then copy the lines below\n");
    std::fflush(out);
}

//Mesh: checks that off::MESH really points at the skeletal mesh of your pawn. Its position should sit next to the
//pawn's root position (within about 2 m); a wrong pointer shows up as a far-off or missing position.
void meshProbe(ptr pawn, FILE* out) {
    static ptr lastPawn = 0;
    static int cycle = 0;
    if (pawn != lastPawn) { lastPawn = pawn; cycle = 0; }
    if (cycle++ % 10) return; //about every 20 s while the same pawn is alive

    const ptr mesh = ReadMemory<ptr>(pawn + off::MESH);
    std::fprintf(out, "\n=== MESH PROBE: your pawn 0x%lx (%s) ===\n", (unsigned long)pawn, nm(pawn).c_str());
    std::fprintf(out, "  pawn+0x%lx (mesh pointer) = 0x%lx\n", (unsigned long)off::MESH, (unsigned long)mesh);
    if (!isValidPtr(mesh)) { std::fprintf(out, "  -> not a pointer: off::MESH is wrong for this build\n"); std::fflush(out); return; }
    std::fprintf(out, "  mesh has a vtable inside the game image: %s\n", isObject(mesh) ? "yes" : "NO");
    const std::string meshName = nm(mesh);
    std::fprintf(out, "  mesh name: %s\n", meshName.c_str());

    const ptr root = ReadMemory<ptr>(pawn + off::ROOT_COMPONENT_PTR);
    auto meshPos = readBlock(mesh + off::POS_PTR, 24);
    double m[3] = {0, 0, 0};
    std::memcpy(m, meshPos.data(), 24);
    std::fprintf(out, "  mesh position (mesh+0x%lx): %.1f %.1f %.1f\n", (unsigned long)off::POS_PTR, m[0], m[1], m[2]);
    if (!isValidPtr(root)) { std::fprintf(out, "  root component not readable, cannot compare\n"); std::fflush(out); return; }
    auto rootBlk = readBlock(root + off::POS_PTR, 24);
    double r[3] = {0, 0, 0};
    std::memcpy(r, rootBlk.data(), 24);
    const double d = std::sqrt((m[0] - r[0]) * (m[0] - r[0]) + (m[1] - r[1]) * (m[1] - r[1]) + (m[2] - r[2]) * (m[2] - r[2]));
    std::fprintf(out, "  root position: %.1f %.1f %.1f   distance mesh-root: %.1f units (%.2f m)\n", r[0], r[1], r[2], d, d / 100.0);
    //Every component attached to the pawn sits within 300 units, the capsule (root) at 0: distance alone proves nothing
    const int boneNum = ReadMemory<int>(mesh + off::BONE_ARRAY + 8);
    std::fprintf(out, "  bone list count (mesh+0x%lx): %d (the player skeleton has %d)\n", (unsigned long)(off::BONE_ARRAY + 8), boneNum, skeleton::BONES);
    const char* fail = mesh == root ? "the mesh pointer is the root component (capsule) itself"
                     : boneNum != skeleton::BONES ? "the bone list is not the player skeleton"
                     : meshName.rfind("CharacterMesh", 0) != 0 ? "the name does not start with CharacterMesh"
                     : d >= 300.0 ? "the mesh is far from the pawn" : nullptr;
    if (fail) std::fprintf(out, "  -> SUSPECT: %s, off::MESH is probably wrong\n", fail);
    else std::fprintf(out, "  -> PLAUSIBLE: a CharacterMesh with the player skeleton, next to the pawn\n");
    std::fflush(out);
}

//Bones: looks inside the skeletal mesh for lists (TArrays) of transforms: a unit quaternion at the start of each
//record, then a position. The record size is tried as 0x30 (float), 0x50 and 0x60 (double). The height (z) of
//every bone is printed too: the head is the highest, the feet the lowest, which labels the bone numbers.
void bonesProbeOne(ptr pawn, const char* who, FILE* out) {
    const ptr mesh = ReadMemory<ptr>(pawn + off::MESH);
    std::fprintf(out, "\n=== BONES PROBE: %s (pawn 0x%lx, mesh 0x%lx) ===\n", who, (unsigned long)pawn, (unsigned long)mesh);
    if (!isObject(mesh)) { std::fprintf(out, "  mesh pointer is not an object\n"); std::fflush(out); return; }
    auto blk = readBlock(mesh, 0x1800);
    int found = 0;
    uint32_t boneCount = 0;
    for (size_t o = 0x300; o + 16 <= blk.size() && found < 10; o += 8) {
        const uint64_t data = q64(blk, o);
        uint32_t num = 0, mx = 0;
        std::memcpy(&num, &blk[o + 8], 4);
        std::memcpy(&mx, &blk[o + 12], 4);
        if (!ptrLike(data) || num < 10 || num > 300 || mx < num || mx > 1000) continue;
        bool matched = false;
        for (int stride : {0x60, 0x50, 0x30}) {
            const bool isDouble = stride != 0x30;
            auto el = readBlock((ptr)data, (size_t)stride * num);
            auto quatNorm = [&](uint32_t i) {
                double q[4];
                if (isDouble) std::memcpy(q, &el[(size_t)i * stride], 32);
                else { float f[4]; std::memcpy(f, &el[(size_t)i * stride], 16); for (int k = 0; k < 4; k++) q[k] = f[k]; }
                return std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            };
            auto pos = [&](uint32_t i, double t[3]) {
                if (isDouble) std::memcpy(t, &el[(size_t)i * stride + 0x20], 24);
                else { float f[3]; std::memcpy(f, &el[(size_t)i * stride + 0x10], 12); for (int k = 0; k < 3; k++) t[k] = f[k]; }
            };
            uint32_t good = 0; //a few unused bones may hold zeros: 9 out of 10 good is enough
            for (uint32_t i = 0; i < num; i++) {
                const double n = quatNorm(i);
                double t[3];
                pos(i, t);
                if (std::isfinite(n) && std::fabs(n - 1.0) < 0.02 && std::isfinite(t[2]) && std::fabs(t[0]) < 1e4 && std::fabs(t[1]) < 1e4 && std::fabs(t[2]) < 1e4) good++;
            }
            if (good * 10 < num * 9) continue;
            std::fprintf(out, "  mesh+0x%zx  TArray num %u max %u  record 0x%x (%s)  data 0x%lx\n", o, num, mx, stride, isDouble ? "double" : "float", (unsigned long)data);
            for (uint32_t i = 0; i < std::min<uint32_t>(num, 3); i++) { double t[3]; pos(i, t); std::fprintf(out, "    bone[%u] position %.1f %.1f %.1f\n", i, t[0], t[1], t[2]); }
            std::fprintf(out, "    height of each bone (index:z):");
            for (uint32_t i = 0; i < num; i++) { double t[3]; pos(i, t); std::fprintf(out, " %u:%.0f", i, t[2]); }
            std::fprintf(out, "\n");
            if (stride == 0x60 && num == (uint32_t)skeleton::BONES) { //the stick figure the overlay would draw, to check the bone numbers
                skeleton::Xform local[skeleton::BONES], comp[skeleton::BONES];
                skeleton::decode(el.data(), local);
                if (skeleton::chain(local, comp)) {
                    static const struct { const char* name; int bone; } show[] = {{"pelvis", 1}, {"head", 7}, {"upper arm A", 9}, {"hand A", 11}, {"upper arm B", 43}, {"hand B", 45}, {"ankle A", 67}, {"ankle B", 71}};
                    std::fprintf(out, "    stick figure, cm from the mesh origin (x y z):");
                    for (const auto& s : show) std::fprintf(out, "  %s(%.0f %.0f %.0f)", s.name, comp[s.bone].t.x, comp[s.bone].t.y, comp[s.bone].t.z);
                    std::fprintf(out, "\n");
                }
            }
            found++;
            matched = true;
            if (!boneCount) boneCount = num;
            break;
        }
        if (!matched && num >= 90 && num <= 110) { //same length as the bone list but not a transform list: a sibling worth a look
            auto raw = readBlock((ptr)data, 64);
            std::fprintf(out, "  mesh+0x%zx  TArray num %u max %u  data 0x%lx  (not a transform list) first 8 doubles:", o, num, mx, (unsigned long)data);
            for (int k = 0; k < 8; k++) { double d; std::memcpy(&d, &raw[(size_t)k * 8], 8); std::fprintf(out, " %.3g", d); }
            std::fprintf(out, "\n");
        }
    }
    //The transforms above are relative to each bone's parent. The skeleton asset keeps {name, parent index} records of
    //12 bytes; the first parent is -1 and every other parent comes before its child. Looking for that pattern in every
    //object the mesh points to finds the parent of each bone, which is what turns these into mesh-relative positions.
    if (boneCount) {
        std::set<uint64_t> seen;
        int parentLists = 0;
        //Records of 12 to 32 bytes with an int32 parent somewhere inside: first parent -1, every other parent before its child
        auto searchObject = [&](ptr obj) {
            auto ab = readBlock(obj, 0x1000);
            for (size_t ao = 0x28; ao + 16 <= ab.size() && parentLists < 2; ao += 8) {
                const uint64_t d = q64(ab, ao);
                uint32_t n2 = 0, m2 = 0;
                std::memcpy(&n2, &ab[ao + 8], 4);
                std::memcpy(&m2, &ab[ao + 12], 4);
                if (!ptrLike(d) || n2 != boneCount || m2 < n2 || m2 > 1000) continue;
                for (int stride : {12, 16, 20, 24, 32}) {
                    auto rec = readBlock((ptr)d, (size_t)stride * n2);
                    for (int poff = 0; poff + 4 <= stride; poff += 4) {
                        int32_t p = 0;
                        std::memcpy(&p, &rec[(size_t)poff], 4);
                        bool okp = p == -1;
                        for (uint32_t i = 1; i < n2 && okp; i++) { std::memcpy(&p, &rec[(size_t)i * stride + poff], 4); okp = p >= 0 && p < (int32_t)i; }
                        if (!okp) continue;
                        std::fprintf(out, "  PARENTS: %s (0x%lx) +0x%zx  %u records of %d bytes, parent at +%d\n    parent of each bone (bone:parent):", nm(obj).c_str(), (unsigned long)obj, ao, n2, stride, poff);
                        for (uint32_t i = 0; i < n2; i++) { std::memcpy(&p, &rec[(size_t)i * stride + poff], 4); std::fprintf(out, " %u:%d", i, (int)p); }
                        std::fprintf(out, "\n");
                        parentLists++;
                        return;
                    }
                }
            }
        };
        std::vector<std::pair<size_t, ptr>> level1; //objects the mesh points to
        for (size_t po = 0x300; po + 8 <= blk.size(); po += 8) {
            const uint64_t ap = q64(blk, po);
            if (ptrLike(ap) && (ptr)ap != mesh && isObject((ptr)ap) && seen.insert(ap).second) level1.push_back({po, (ptr)ap});
        }
        std::fprintf(out, "  the mesh points to %zu objects; first ones:", level1.size());
        for (size_t i = 0; i < level1.size() && i < 24; i++) std::fprintf(out, " +0x%zx=%s", level1[i].first, nm(level1[i].second).c_str());
        std::fprintf(out, "\n");
        for (const auto& l : level1) { if (parentLists >= 2) break; searchObject(l.second); }
        int scanned = 0;
        for (const auto& l : level1) { //one level deeper: the skeleton asset may sit behind another object
            if (parentLists >= 2 || scanned >= 800) break;
            auto ob = readBlock(l.second, 0x800);
            for (size_t k = 0x28; k + 8 <= ob.size() && parentLists < 2 && scanned < 800; k += 8) {
                const uint64_t v = q64(ob, k);
                if (!ptrLike(v) || !isObject((ptr)v) || !seen.insert(v).second) continue;
                scanned++;
                searchObject((ptr)v);
            }
        }
        if (!parentLists) std::fprintf(out, "  no parent list found (searched %zu objects and %d behind them)\n", level1.size(), scanned);
    }
    if (!found) std::fprintf(out, "  no list of bone transforms found in the first 0x1800 bytes of the mesh\n");
    std::fflush(out);
}

//Stance: for you and the nearest players, once per call: how far the mesh sits below the capsule (changes when a player
//crouches or goes prone), how fast he moves, the pelvis height in his bone list, and whether the game is still
//animating him (his bone list changed since the last call). Stand, crouch, go prone, walk, run: a few seconds each.
void stanceProbeOne(ptr pawn, const char* who, FILE* out) {
    struct Last { Vector3 pos; std::chrono::steady_clock::time_point t; uint64_t hash = 0; bool have = false; };
    static std::unordered_map<ptr, Last> last;
    const ptr root = ReadMemory<ptr>(pawn + off::ROOT_COMPONENT_PTR), mesh = ReadMemory<ptr>(pawn + off::MESH);
    if (!isValidPtr(root) || !isValidPtr(mesh)) { std::fprintf(out, "  %-14s root or mesh not readable\n", who); return; }
    const Vector3 rp(ReadMemory<double>(root + off::POS_PTR), ReadMemory<double>(root + off::POS_PTR + 8), ReadMemory<double>(root + off::POS_PTR + 16));
    const Vector3 mp(ReadMemory<double>(mesh + off::POS_PTR), ReadMemory<double>(mesh + off::POS_PTR + 8), ReadMemory<double>(mesh + off::POS_PTR + 16));
    double q[4];
    auto qb = readBlock(mesh + off::MESH_TRANSFORM, 32);
    std::memcpy(q, qb.data(), 32);
    const double yaw = std::atan2(2 * (q[3] * q[2] + q[0] * q[1]), 1 - 2 * (q[1] * q[1] + q[2] * q[2])) * 180.0 / 3.14159265358979;
    const double pitchSin = 2 * (q[3] * q[1] - q[2] * q[0]); //how far the mesh is tipped forward or back (prone / diving)

    const ptr bonesPtr = ReadMemory<ptr>(mesh + off::BONE_ARRAY);
    uint64_t hash = 0;
    double pelvisZ = 0;
    if (isValidPtr(bonesPtr)) {
        auto b = readBlock(bonesPtr, (size_t)skeleton::BONES * skeleton::RECORD);
        hash = 1469598103934665603ULL;
        for (uint8_t c : b) { hash ^= c; hash *= 1099511628211ULL; }
        std::memcpy(&pelvisZ, &b[(size_t)skeleton::RECORD + 0x20 + 16], 8);
    }
    const auto now = std::chrono::steady_clock::now();
    Last& l = last[pawn];
    double speed = 0;
    if (l.have) { const double dt = std::chrono::duration<double>(now - l.t).count(); if (dt > 0.2) speed = std::sqrt((rp.x - l.pos.x) * (rp.x - l.pos.x) + (rp.y - l.pos.y) * (rp.y - l.pos.y)) / dt; }
    const bool animated = l.have && l.hash != hash;
    std::fprintf(out, "  %-14s mesh below capsule %6.1f cm | speed %5.0f cm/s | pelvis height %5.1f | mesh tilt %5.2f | bones %s\n", who, rp.z - mp.z, speed, pelvisZ, pitchSin,
                 !l.have ? "(first look)" : animated ? "ANIMATED" : "FROZEN");
    (void)yaw;
    l.pos = rp; l.t = now; l.hash = hash; l.have = true;
}

void stanceProbe(ptr localPawn, const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm, FILE* out) {
    std::fprintf(out, "\n=== STANCE PROBE (stand, crouch, go prone, walk, run: a few seconds each) ===\n");
    if (localPawn) stanceProbeOne(localPawn, "you", out);
    std::vector<std::pair<double, ptr>> near;
    for (const RenderEntity& e : ents) if (e.type == Object::PLAYER && !e.isDead) near.push_back({e.pos.Dist(vm.Location), e.actor});
    std::sort(near.begin(), near.end());
    for (size_t i = 0; i < near.size() && i < 5; i++) { char who[32]; std::snprintf(who, sizeof who, "%.0f m away", near[i].first / 100.0); stanceProbeOne(near[i].second, who, out); }
    std::fflush(out);
}

//Flags: every byte of your pawn, its components, your mesh and your animation objects is watched; one that flips between
//0 and 1 (crouching, going prone, sprinting, aiming) is printed with the time. Do those things a few seconds apart.
void flagProbe(ptr pawn, FILE* out) {
    struct B { uint8_t last; int flips; };
    static std::unordered_map<uint64_t, B> st;
    static ptr lastPawn = 0;
    static std::chrono::steady_clock::time_point t0;
    const auto now = std::chrono::steady_clock::now();
    if (pawn != lastPawn) { st.clear(); lastPawn = pawn; t0 = now; std::fprintf(out, "\n=== FLAG PROBE: watching 0/1 bytes of your pawn 0x%lx. Crouch, go prone, sprint, aim: a few seconds apart ===\n", (unsigned long)pawn); }
    const double t = std::chrono::duration<double>(now - t0).count();
    auto scan = [&](uint64_t tag, const char* label, ptr obj, size_t n) {
        if (!isValidPtr(obj)) return;
        auto blk = readBlock(obj, n);
        for (size_t o = 0x28; o < n; o++) {
            const uint64_t key = (tag << 32) | o;
            auto it = st.find(key);
            if (it == st.end()) { st[key] = {blk[o], 0}; continue; }
            B& b = it->second;
            if (blk[o] == b.last) continue;
            const uint8_t from = b.last;
            b.last = blk[o];
            b.flips++;
            if (from <= 1 && blk[o] <= 1 && b.flips <= 4) std::fprintf(out, "  [%6.1f s] %s+0x%zx  %u -> %u\n", t, label, o, from, blk[o]);
        }
    };
    scan(1, "pawn", pawn, 0x1400);
    char lab[40];
    for (const Comp& c : componentsOf(pawn)) { std::snprintf(lab, sizeof lab, "pawn+0x%x", c.pawnOff); scan(0x100 + (uint64_t)c.pawnOff, lab, c.obj, 0x600); }
    const ptr mesh = ReadMemory<ptr>(pawn + off::MESH);
    if (isValidPtr(mesh)) {
        scan(2, "mesh", mesh, 0x1000);
        scan(0x10BB0, "anim(mesh+0xbb0)", ReadMemory<ptr>(mesh + 0xBB0), 0x800);
        scan(0x10BB8, "anim(mesh+0xbb8)", ReadMemory<ptr>(mesh + 0xBB8), 0x800);
    }
    std::fflush(out);
}

//Render time: a number in your mesh that grows with the game clock is probably "when this mesh was last drawn". The same
//place in other players' meshes then shows how long ago they were drawn: near 0 means on screen, growing means hidden.
void renderTimeProbe(ptr localPawn, const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm, FILE* out) {
    static std::unordered_map<uint32_t, double> prev;
    static std::set<uint32_t> good;
    static ptr lastPawn = 0;
    if (localPawn != lastPawn) { prev.clear(); good.clear(); lastPawn = localPawn; std::fprintf(out, "\n=== RENDER TIME PROBE: looking for numbers in your mesh that follow the game clock ===\n"); }
    const ptr mesh = ReadMemory<ptr>(localPawn + off::MESH);
    if (!isValidPtr(mesh)) return;
    auto valueAt = [](const std::vector<uint8_t>& b, uint32_t key) {
        const size_t o = key >> 1;
        double v;
        if (key & 1) { float f; std::memcpy(&f, &b[o], 4); v = f; } else std::memcpy(&v, &b[o], 8);
        return v;
    };
    auto mine = readBlock(mesh, 0x1000);
    for (size_t o = 0x300; o + 8 <= mine.size(); o += 4)
        for (int isFloat = 0; isFloat < 2; isFloat++) {
            if (!isFloat && (o & 7)) continue;
            const uint32_t key = (uint32_t)(o << 1) | (uint32_t)isFloat;
            const double v = valueAt(mine, key);
            if (!std::isfinite(v) || v < 1.0 || v > 1e7) continue;
            auto it = prev.find(key);
            if (it != prev.end() && v - it->second > 1.0 && v - it->second < 4.0) good.insert(key); //grew by about the 2 s between calls
            prev[key] = v;
        }
    if (good.empty()) { std::fprintf(out, "  (warming up: needs two looks)\n"); std::fflush(out); return; }

    struct Other { double dist; std::vector<uint8_t> blk; };
    std::vector<std::pair<double, ptr>> near;
    for (const RenderEntity& e : ents) if (e.type == Object::PLAYER && !e.isDead) near.push_back({e.pos.Dist(vm.Location), e.actor});
    std::sort(near.begin(), near.end());
    std::vector<Other> others;
    for (size_t i = 0; i < near.size() && i < 4; i++) {
        const ptr m = ReadMemory<ptr>(near[i].second + off::MESH);
        if (isValidPtr(m)) others.push_back({near[i].first / 100.0, readBlock(m, 0x1000)});
    }
    int shown = 0;
    for (uint32_t key : good) {
        if (shown++ >= 8) break;
        std::fprintf(out, "  mesh+0x%x %s  you %.2f", key >> 1, (key & 1) ? "float " : "double", valueAt(mine, key));
        for (const Other& ot : others) std::fprintf(out, " | %.0f m: %.2f (%.1f s behind)", ot.dist, valueAt(ot.blk, key), valueAt(mine, key) - valueAt(ot.blk, key));
        std::fprintf(out, "\n");
    }
    std::fflush(out);
}

//Inventory: find the lists (TArrays) of item data assets (DA_*) inside the pawn's components. For each candidate the
//record size (stride) and where the asset pointer sits in a record are worked out from the names found.
void inventoryProbeOne(ptr pawn, const char* who, FILE* out) {
    std::fprintf(out, "\n=== INVENTORY PROBE: %s (pawn 0x%lx %s) ===\n", who, (unsigned long)pawn, nm(pawn).c_str());
    int found = 0;
    std::vector<Comp> comps = componentsOf(pawn);
    comps.insert(comps.begin(), {0xCB0, ReadMemory<ptr>(pawn + 0xCB0)}); //the inventory component first
    std::set<ptr> listed; //componentsOf finds the inventory component again (and a component may sit at two pawn offsets)
    for (const Comp& c : comps) {
        if (!isObject(c.obj) || !listed.insert(c.obj).second) continue;
        auto blk = readBlock(c.obj, 0xA00);
        for (size_t o = 0; o + 16 <= blk.size(); o += 8) {
            uint64_t data = q64(blk, o);
            uint32_t num = 0, mx = 0;
            std::memcpy(&num, &blk[o + 8], 4);
            std::memcpy(&mx, &blk[o + 12], 4);
            if (!ptrLike(data) || num < 1 || num > 200 || mx < num || mx > 2000) continue;
            int bestStride = 0, bestHits = 0, bestAsset = 0;
            for (int stride : {8, 0x10, 0x18, 0x20, 0x28, 0x30, 0x40}) {
                //score = records with a DA_ in the best single slot; a multiple of the true stride only ties, and loses on '>'
                int hits = 0, assetOff = -1, slotHits[8] = {};
                const int n = (int)std::min<uint32_t>(num, 8);
                auto elems = readBlock((ptr)data, (size_t)stride * n);
                for (int i = 0; i < n; i++)
                    for (int k = 0; k * 8 < stride; k++) {
                        uint64_t v = q64(elems, (size_t)i * stride + k * 8);
                        if (!ptrLike(v) || !isObject((ptr)v)) continue;
                        std::string name = names::name((ptr)v);
                        if (name.compare(0, 3, "DA_") == 0) slotHits[k]++;
                    }
                for (int k = 0; k * 8 < stride; k++) if (slotHits[k] > hits) { hits = slotHits[k]; assetOff = k * 8; }
                if (hits > bestHits) { bestHits = hits; bestStride = stride; bestAsset = assetOff; }
            }
            if (bestHits == 0) continue;
            std::fprintf(out, "  pawn+0x%x (%s)  +0x%zx  TArray num %u  stride 0x%x  item at +0x%x in %d of %d records  ->", c.pawnOff, nm(c.obj).c_str(), o, num, bestStride, bestAsset, bestHits, (int)std::min<uint32_t>(num, 8));
            auto elems = readBlock((ptr)data, (size_t)bestStride * std::min<uint32_t>(num, 6));
            for (uint32_t i = 0; i < std::min<uint32_t>(num, 6); i++) {
                uint64_t v = q64(elems, (size_t)i * bestStride + bestAsset);
                std::string name = ptrLike(v) ? names::name((ptr)v) : std::string();
                std::fprintf(out, " [%u] %s", i, name.empty() ? "?" : name.c_str());
            }
            std::fprintf(out, "\n");
            if (c.pawnOff == 0xCB0) std::fprintf(out, "      offsets.txt:  inv_array 0x%zx 0x%x 0x%x item\n", o, bestStride, bestAsset);
            else std::fprintf(out, "      offsets.txt:  inv_array_in 0x%x 0x%zx 0x%x 0x%x item\n", c.pawnOff, o, bestStride, bestAsset);
            if (++found >= 24) { std::fprintf(out, "  (more lists omitted)\n"); std::fflush(out); return; }
        }
    }
    if (!found) std::fprintf(out, "  no list of item assets found in this pawn's components\n");
    std::fflush(out);
}

} // namespace

//Team and local player: for you and the nearest players, the team byte, the pointers that should only be set on the pawn you
//control (class names), and the PlayerState with the offsets in it that hold the same number as the team byte.
void teamProbeOne(ptr pawn, const char* who, FILE* out) {
    const int team = ReadMemory<uint8_t>(pawn + off::TEAM_ID);
    auto nameAt = [&](int o) {
        const ptr p = ReadMemory<ptr>(pawn + o);
        if (!p) return std::string("null");
        if (!isValidPtr(p)) return std::string("junk");
        const std::string n = names::name(p);
        return n.empty() ? std::string("?") : n;
    };
    std::fprintf(out, "  %-14s team %3d | +3f8 %-28s +400 %-28s +878 %s\n", who, team, nameAt(0x3F8).c_str(), nameAt(0x400).c_str(), nameAt(0x878).c_str());
    const ptr state = ReadMemory<ptr>(pawn + 0x3E8);
    if (!isValidPtr(state)) return;
    auto b = readBlock(state + 0x2C0, 0x400);
    std::string hits;
    int n = 0;
    for (size_t i = 0; i < b.size() && n < 10; i++) if (b[i] == team && team != 0) { char t[32]; std::snprintf(t, sizeof t, " +%zx", (size_t)0x2C0 + i); hits += t; n++; }
    std::fprintf(out, "      PlayerState %s: bytes equal to the team at:%s\n", names::name(state).c_str(), hits.empty() ? " (none)" : hits.c_str());
}

void teamProbe(ptr localPawn, const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm, FILE* out) {
    std::fprintf(out, "\n=== TEAM PROBE (stand near a teammate or another player if you can) ===\n");
    if (localPawn) teamProbeOne(localPawn, "you (guess)", out);
    std::vector<std::pair<double, ptr>> near;
    for (const RenderEntity& e : ents) if (e.type == Object::PLAYER) near.push_back({e.pos.Dist(vm.Location), e.actor});
    std::sort(near.begin(), near.end());
    for (size_t i = 0; i < near.size() && i < 6; i++) { char who[32]; std::snprintf(who, sizeof who, "%.0f m away", near[i].first / 100.0); teamProbeOne(near[i].second, who, out); }
    std::fflush(out);
}

void runLiveProbe(const std::string& mode, ptr localPawn, const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm, FILE* out) {
    //mode: "shield", "inventory", "mesh" or several, e.g. "shield,inventory": one raid can find everything
    if (mode.find("mesh") != std::string::npos && localPawn) meshProbe(localPawn, out);
    if (mode.find("shield") != std::string::npos) {
        if (!localPawn) { std::fprintf(out, "[probe shield] your own player was not found (stand still, third person, in a raid)\n"); std::fflush(out); }
        else shieldProbe(localPawn, out);
    }
    if (mode.find("bones") != std::string::npos && localPawn) {
        static int boneRound = 0;
        if (boneRound++ % 5 == 0) { //every 10 s; yours first, then the nearest other player to confirm the same offset
            bonesProbeOne(localPawn, "you", out);
            std::vector<std::pair<double, ptr>> near;
            for (const RenderEntity& e : ents) if (e.type == Object::PLAYER && !e.isDead) near.push_back({e.pos.Dist(vm.Location), e.actor});
            std::sort(near.begin(), near.end());
            if (!near.empty()) { char who[48]; std::snprintf(who, sizeof who, "player %.0f m away", near[0].first / 100.0); bonesProbeOne(near[0].second, who, out); }
        }
    }
    if (mode.find("flags") != std::string::npos && localPawn) flagProbe(localPawn, out);
    if (mode.find("rendertime") != std::string::npos && localPawn) renderTimeProbe(localPawn, ents, vm, out);
    if (mode.find("stance") != std::string::npos) stanceProbe(localPawn, ents, vm, out);
    if (mode.find("team") != std::string::npos) teamProbe(localPawn, ents, vm, out);
    if (mode.find("inventory") != std::string::npos) {
        static int round = 0;
        if (round++ % 4) return; //every 8 s: the report is long
        if (localPawn) inventoryProbeOne(localPawn, "you", out);
        std::vector<std::pair<double, ptr>> near;
        for (const RenderEntity& e : ents) if (e.type == Object::PLAYER && !e.isDead) near.push_back({e.pos.Dist(vm.Location), e.actor});
        std::sort(near.begin(), near.end());
        for (size_t i = 0; i < near.size() && i < 2; i++) { char who[48]; std::snprintf(who, sizeof who, "player %.0f m away", near[i].first / 100.0); inventoryProbeOne(near[i].second, who, out); }
    }
}
#endif //KWINHELPER_PROBES
