//Read-only discovery report for names and weapons (switch on with probeNames in config.h).
//Prints one report block every few seconds: which player names / object names / gun names can be read in this
//build, and what the actors around you are called. It reads memory only and draws nothing.
#include "config.h"
#include "utils/localUtil.h"
#include "utils/names.h"

#include <algorithm>
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

constexpr ptr IMAGE_BASE = 0x140000000ULL;

bool isModulePtr(ptr p) { return p >= IMAGE_BASE && p < 0x154000000ULL; }

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
//The PlayerState: pawn+0x3E8 in v1029. If that is stale, look for any pointer in the pawn that leads to a
//plausible player name.
ptr playerStateOf(ptr pawn, int* offOut = nullptr) {
    ptr ps = ReadMemory<ptr>(pawn + 0x3E8);
    if (isObject(ps)) { if (offOut) *offOut = 0x3E8; return ps; }
    for (int off = 0x380; off < 0x470; off += 8) {
        ptr p = ReadMemory<ptr>(pawn + off);
        if (isObject(p) && !names::playerName(p + 0x460).empty()) { if (offOut) *offOut = off; return p; }
    }
    return 0;
}

//The account id is held through PlayerState+0x3E0 -> object, id qword at object+0x18
ptr accountId(ptr ps) {
    ptr obj = ReadMemory<ptr>(ps + 0x3E0);
    return isValidPtr(obj) ? ReadMemory<ptr>(obj + 0x18) : 0;
}

void probePlayers(FILE* out, const std::vector<std::pair<double, const RenderEntity*>>& players, ptr localPawn) {
    std::fprintf(out, "PLAYERS (name = PlayerState+0x460 decoded, account id = PlayerState+0x3E0 -> +0x18)\n");
    int shown = 0;
    for (const auto& pl : players) {
        ptr pawn = pl.second->actor;
        int off = 0;
        ptr ps = playerStateOf(pawn, &off);
        std::fprintf(out, "  %6.1f m%s", pl.first, pawn == localPawn ? " (you)" : "      ");
        if (!ps) { std::fprintf(out, "  no PlayerState\n"); continue; }
        std::string n = names::playerName(ps + 0x460);
        std::fprintf(out, "  name '%s'  id %016lx  pawn: %s  team %d\n", n.c_str(), (unsigned long)accountId(ps),
                     nm(pawn).c_str(), ReadMemory<int>(pawn + 0x802) & 0xFF);
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

void probeCensus(FILE* out, const std::vector<RenderEntity>& ents, const std::vector<RenderEntity>& unknown, const FminimalViewInfo& vm) {
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
    std::fprintf(out, "ACTOR NAMES in the level: %d actors, %d names unreadable, %zu distinct names (top 45 by count)\n", total, unreadable, rows.size());
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
    std::fprintf(out, "object-name decoder: %s\n", names::ready() ? "ready" : "NOT READY (text key table unreadable: RVA stale or game not in a raid)");

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
    if (players.size() > 16) players.resize(16);
    const RenderEntity* me = (!players.empty() && players[0].first < 2.0) ? players[0].second : nullptr;
    std::fprintf(out, "players seen %zu, you: %s, ground items (PICKUP class) in view: %d\n", players.size(),
                 me ? "found" : "NOT FOUND (no PLAYER within 2 m)", pickups);

    probePlayers(out, players, me ? me->actor : 0);

    std::fprintf(out, "ARC TYPES (EnemyTypeDataAsset @actor+0x11C0; sample = nearest actor of that type)\n");
    for (auto& kv : arc) {
        std::fprintf(out, "  count %3d  nearest %6.1f m  asset %s  sample actor %s  AITemplate %s\n", kv.second.count, kv.second.nearest,
                     tag(kv.first).c_str(), nm(kv.second.sample).c_str(), tag(ReadMemory<ptr>(kv.second.sample + 0x11B0)).c_str());
    }
    if (arc.empty()) std::fprintf(out, "  (no ARC in view)\n");

    probeGuns(out, players, me ? me->actor : 0);
    probeNear(out, ents, unknown, vm);
    probeCensus(out, ents, unknown, vm);

    std::fprintf(out, "CONTAINERS\n");
    diffScan(out, "  container fields", containers, 0x300, 0x1400, 14);
    std::fflush(out);
}


//==============================================================================================
// Live probes: KWINHELPER_PROBE=shield or KWINHELPER_PROBE=inventory, set before starting (see the README). Read only.
//==============================================================================================
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

struct FieldState { double last = 0; int changes = 0; };

//Shield: every number in every component of your pawn is remembered; the ones that CHANGE (a few times, not every
//moment like a clock) are printed. Take a hit, let the shield drain or recharge, and the shield number shows up
//next to the known health number.
void shieldProbe(ptr pawn, FILE* out) {
    static ptr lastPawn = 0;
    static std::unordered_map<uint64_t, FieldState> st;
    static int cycle = 0;
    if (pawn != lastPawn) { st.clear(); lastPawn = pawn; cycle = 0; }
    cycle++;
    if (cycle == 1) std::fprintf(out, "\n=== SHIELD PROBE: watching your pawn 0x%lx. Take a hit (or let the shield drain / recharge), then copy the lines below ===\n", (unsigned long)pawn);

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
                    f.changes++;
                    const uint64_t doubleKey = ((uint64_t)c.pawnOff << 32) | (isFloat ? (o & ~(size_t)7) : o);
                    if (!isFloat) doubleChanged.insert(doubleKey);
                    const bool halfOfDouble = isFloat && doubleChanged.count(doubleKey) > 0; //the same bytes as a double above: not a new finding
                    if (f.changes <= 3 && printed < 50 && !halfOfDouble) {
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
    std::fflush(out);
}

//Inventory: find the lists (TArrays) of item data assets (DA_*) inside the pawn's components. For each candidate the
//record size (stride) and where the asset pointer sits in a record are worked out from the names found.
void inventoryProbeOne(ptr pawn, const char* who, FILE* out) {
    std::fprintf(out, "\n=== INVENTORY PROBE: %s (pawn 0x%lx %s) ===\n", who, (unsigned long)pawn, nm(pawn).c_str());
    int found = 0;
    std::vector<Comp> comps = componentsOf(pawn);
    comps.insert(comps.begin(), {0xCB0, ReadMemory<ptr>(pawn + 0xCB0)}); //the inventory component first
    for (const Comp& c : comps) {
        if (!isObject(c.obj)) continue;
        auto blk = readBlock(c.obj, 0xA00);
        for (size_t o = 0; o + 16 <= blk.size(); o += 8) {
            uint64_t data = q64(blk, o);
            uint32_t num = 0, mx = 0;
            std::memcpy(&num, &blk[o + 8], 4);
            std::memcpy(&mx, &blk[o + 12], 4);
            if (!ptrLike(data) || num < 1 || num > 200 || mx < num || mx > 2000) continue;
            int bestStride = 0, bestHits = 0, bestAsset = 0;
            for (int stride : {8, 0x10, 0x18, 0x20, 0x28, 0x30, 0x40}) {
                int hits = 0, assetOff = -1;
                const int n = (int)std::min<uint32_t>(num, 8);
                auto elems = readBlock((ptr)data, (size_t)stride * n);
                for (int i = 0; i < n; i++)
                    for (int k = 0; k * 8 < stride; k++) {
                        uint64_t v = q64(elems, (size_t)i * stride + k * 8);
                        if (!ptrLike(v) || !isObject((ptr)v)) continue;
                        std::string name = names::name((ptr)v);
                        if (name.compare(0, 3, "DA_") == 0) { hits++; if (assetOff < 0) assetOff = k * 8; }
                    }
                if (hits > bestHits) { bestHits = hits; bestStride = stride; bestAsset = assetOff; }
            }
            if (bestHits == 0) continue;
            std::fprintf(out, "  pawn+0x%x (%s)  +0x%zx  TArray num %u  stride 0x%x  asset at +0x%x  ->", c.pawnOff, nm(c.obj).c_str(), o, num, bestStride, bestAsset);
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

void runLiveProbe(const std::string& mode, ptr localPawn, const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm, FILE* out) {
    //mode: "shield", "inventory" or both, e.g. "shield,inventory": one raid can find everything
    if (mode.find("shield") != std::string::npos) {
        if (!localPawn) { std::fprintf(out, "[probe shield] your own player was not found (stand still, third person, in a raid)\n"); std::fflush(out); }
        else shieldProbe(localPawn, out);
    }
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
