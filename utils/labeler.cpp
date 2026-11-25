#include "labeler.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "labels.h"
#include "log.h"
#include "names.h"
#include "offsets.h"

namespace labeler {
namespace {

//v1029 offsets (these are not in the public dumps; found with the probe, see Probe.cpp)
constexpr int PAWN_PLAYERSTATE = 0x3E8;
constexpr int PS_NAME = 0x460;          //FString
constexpr int PS_ACCOUNT_OBJ = 0x3E0;   //-> object, 8-byte account id at +0x18
constexpr int PAWN_INVENTORY = 0xCB0;
constexpr int INV_STOWED[2] = {0x3B0, 0x3F0}; //+0x18 = DA_WeaponVisuals_<gun>
constexpr int INV_HELD = 0x530;               //TArray of 0x20-byte records: +0x0 weapon actor, +0x10 DA_Item_<gun>
constexpr int PICKUP_ITEM_ASSET = 0x518;      //BP_PickupBase_C -> DA_Item_<thing>
constexpr int ARC_TYPE_ASSET = 0x11C0;        //EnemyTypeDataAsset

constexpr double CLASS_TTL = 5.0, PLAYER_TTL = 3.0, GUNS_TTL = 1.5, RETRY_TTL = 1.0, ASSET_RECHECK = 2.0, FORGET_AFTER = 20.0;
constexpr double PICKUP_RETRY_MAX = 30.0; //a ground item whose asset cannot be read is tried after 1 s, 2 s, 4 s ... then every 30 s

struct Info {
    double seen = 0, classT = -1e9, labelT = -1e9, gunsT = -1e9, assetT = -1e9;
    uint32_t nameIndex = 0;
    uintptr_t assetPtr = 0; //the asset the key came from; a different pointer later means the actor is a new one
    int assetOff = PICKUP_ITEM_ASSET; //pickups: the field assetPtr was read from
    double retry = 0;   //pickups: wait before the next read after a failed one (0 = none failed yet)
    std::string cls;    //actor name, e.g. BP_PickupBase_C
    std::string key;    //raw key of what it is (asset name or class name), once known
    bool resolved = false;
    std::string label, sub;
    std::string account;
    uintptr_t ps = 0;   //the PlayerState the account came from
    bool turret = false;
};

std::unordered_map<uintptr_t, Info> g_cache;
bool g_idsBad = false; //two players at once gave the same account id: the id offsets are wrong for this build
int g_pickupOff = PICKUP_ITEM_ASSET; //where the fallback last found a ground item's asset (a patch moved the field)

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool isImagePtr(uintptr_t p) { return p >= 0x140000000ULL && p < 0x154000000ULL; }
bool isObj(uintptr_t o) { return isValidPtr(o) && isImagePtr(ReadMemory<uintptr_t>(o)); }
bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

Info& infoOf(uintptr_t actor, double t) {
    Info& in = g_cache[actor];
    in.seen = t;
    return in;
}

//Re-reads the actor's own name now and then; a different name means the address now holds another actor
void refreshClass(Info& in, uintptr_t actor, double t) {
    if (t - in.classT < (in.cls.empty() ? RETRY_TTL : CLASS_TTL)) return; //an unreadable name is retried each second, not each frame
    in.classT = t;
    names::Decoded d = names::objectName(actor);
    if (d.index != in.nameIndex) {
        in.resolved = false;
        in.key.clear();
        in.label.clear();
        in.sub.clear();
        in.assetPtr = 0;
        in.retry = 0;
        in.labelT = in.gunsT = in.assetT = -1e9;
    }
    in.nameIndex = d.index;
    in.cls = d.text;
}

std::string hex64(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
    return b;
}

//name of the object at p when it starts with prefix, else ""
std::string assetName(uintptr_t p, const char* prefix) {
    if (!isObj(p)) return "";
    std::string n = names::name(p);
    return startsWith(n, prefix) ? n : std::string();
}

//The guns a player has: held first (inventory "current items" with a weapon actor), then the stowed slots
std::string gunsOf(uintptr_t pawn) {
    uintptr_t inv = ReadMemory<uintptr_t>(pawn + PAWN_INVENTORY);
    if (!isObj(inv)) return "";

    std::vector<std::string> held, stowed;
    uintptr_t data = ReadMemory<uintptr_t>(inv + INV_HELD);
    int num = ReadMemory<int>(inv + INV_HELD + 8);
    if (isValidPtr(data) && num > 0 && num <= 16) {
        for (int i = 0; i < num && i < 4; i++) {
            uintptr_t rec = data + (uintptr_t)i * 0x20;
            if (assetName(ReadMemory<uintptr_t>(rec), "BP_WeaponActor_").empty()) continue; //not a gun
            std::string asset = assetName(ReadMemory<uintptr_t>(rec + 0x10), "DA_Item_");
            if (asset.empty()) asset = assetName(ReadMemory<uintptr_t>(rec), "BP_WeaponActor_");
            if (!asset.empty()) held.push_back(labels::display(labels::Cat::Item, labels::itemToken(asset)));
        }
    }
    for (int base : INV_STOWED) {
        std::string asset = assetName(ReadMemory<uintptr_t>(inv + base + 0x18), "DA_WeaponVisuals_");
        if (!asset.empty()) stowed.push_back(labels::display(labels::Cat::Item, labels::itemToken(asset)));
    }

    std::string out;
    auto add = [&](const std::string& s) { if (!out.empty()) out += " + "; out += s; };
    for (auto& s : held) add(s + " (held)");
    for (auto& s : stowed) add(s);
    return out;
}

void fillPlayer(Info& in, uintptr_t pawn, double t) {
    if (t - in.labelT >= PLAYER_TTL) {
        in.labelT = t;
        in.label.clear();
        uintptr_t ps = ReadMemory<uintptr_t>(pawn + PAWN_PLAYERSTATE);
        if (isObj(ps)) {
            std::string name = names::playerName(ps + PS_NAME);
            uintptr_t acc = ReadMemory<uintptr_t>(ps + PS_ACCOUNT_OBJ);
            uint64_t id = isValidPtr(acc) ? (uint64_t)ReadMemory<uintptr_t>(acc + 0x18) : 0;
            if (isValidPtr(id)) id = 0; //a pointer, not an account id
            //Another player on screen right now (seen within PLAYER_TTL) with a different PlayerState and the same id means
            //the id is some shared field after a patch. Marking it would hide everyone, so ids are off until the game restarts.
            //A dead body shares its owner's PlayerState, so it does not count; neither does a player from an earlier raid.
            if (id && !g_idsBad) {
                const std::string h = hex64(id);
                for (const auto& kv : g_cache) {
                    const Info& o = kv.second;
                    if (&o != &in && o.ps != ps && o.account == h && t - o.seen < PLAYER_TTL) { g_idsBad = true; break; }
                }
                if (g_idsBad) { std::printf("[!] Two players have the same account id: player marks and nicknames are off until the game restarts\n"); std::fflush(stdout); }
            }
            if (g_idsBad) id = 0;
            in.ps = ps;
            in.account = id ? hex64(id) : std::string();
            if (!name.empty()) in.label = id ? labels::display(labels::Cat::Player, hex64(id), name) : name;
        }
    }
    if (g_idsBad) in.account.clear(); //every player at once, not only at its next refresh
    if (t - in.gunsT >= GUNS_TTL) {
        in.gunsT = t;
        in.sub = gunsOf(pawn);
    }
}

//The game reuses memory: a new ARC can appear at the address a dead one had. So the type asset is looked at
//again every couple of seconds, and a different asset throws the old name away.
void fillArc(Info& in, uintptr_t actor, double t) {
    if (t - in.assetT >= (in.resolved ? ASSET_RECHECK : RETRY_TTL)) {
        in.assetT = t;
        uintptr_t a = ReadMemory<uintptr_t>(actor + ARC_TYPE_ASSET);
        if (a != in.assetPtr || !in.resolved) {
            in.assetPtr = a;
            in.key = assetName(a, "DA_");
            in.resolved = !in.key.empty();
            in.label.clear();
        }
    }
    if (in.resolved && in.label.empty()) {
        in.label = labels::display(labels::Cat::Arc, in.key);
    }
    if (in.resolved) { //"DA_EnemyType_TurretEnemy" and friends
        std::string k = in.key;
        for (char& c : k) c = (char)std::tolower((unsigned char)c);
        in.turret = k.find("turret") != std::string::npos;
    }
}

void fillContainer(Info& in) {
    if (in.cls.empty()) return;
    if (in.label.empty()) {
        in.key = in.cls;
        in.label = labels::display(labels::Cat::Container, in.key);
    }
}

//The DA_Item_ a ground item points at, its pointer (a) and field (off). Tries the usual field, then the one the
//fallback last found, then the first item asset in actor+0x400..0x700 (read in one go). "" when there is none.
std::string findItemAsset(uintptr_t actor, uintptr_t& a, int& off) {
    std::string n;
    for (int o : {PICKUP_ITEM_ASSET, g_pickupOff}) {
        a = ReadMemory<uintptr_t>(actor + o);
        n = assetName(a, "DA_Item_");
        if (!n.empty()) { off = o; return n; }
        if (o == g_pickupOff) break; //nothing learned yet: one field to try
    }
    //offset moved in a patch. ponytail: one isObj read per pointer-looking word; ReadScatter the vtables if this gets hot
    uintptr_t w[(0x700 - 0x400) / 8];
    ReadMemoryBuffer(actor + 0x400, w, sizeof w);
    for (int i = 0; i < (int)(sizeof w / sizeof *w); i++) {
        if (!isValidPtr(w[i])) continue; //nulls and small numbers cost no read
        n = assetName(w[i], "DA_Item_");
        if (n.empty()) continue;
        a = w[i];
        off = 0x400 + i * 8;
        if (off != g_pickupOff) {
            char msg[112];
            std::snprintf(msg, sizeof msg, "[!] Ground items: the item asset is at actor+0x%X, not +0x%X (game update?)", off, PICKUP_ITEM_ASSET);
            logThrottled(msg);
            g_pickupOff = off; //every pickup shares one layout: try this field before scanning next time
        }
        return n;
    }
    return "";
}

void fillPickup(Info& in, uintptr_t actor, double t) {
    if (t - in.assetT >= (in.resolved ? std::max(ASSET_RECHECK, in.retry) : in.retry)) {
        in.assetT = t;
        if (in.resolved && ReadMemory<uintptr_t>(actor + in.assetOff) == in.assetPtr) {
            in.retry = 0; //still the same item
        } else {
            uintptr_t a = 0;
            int off = PICKUP_ITEM_ASSET;
            std::string n = findItemAsset(actor, a, off);
            if (!n.empty()) {
                in.assetPtr = a;
                in.assetOff = off;
                in.key = labels::itemToken(n);
                in.resolved = true;
                in.retry = 0;
                in.label.clear();
            } else { //not readable (yet): back off; a failed recheck keeps the label it had
                in.retry = in.retry > 0 ? std::min(in.retry * 2, PICKUP_RETRY_MAX) : RETRY_TTL;
            }
        }
    }
    if (in.resolved && in.label.empty()) {
        in.label = labels::display(labels::Cat::Item, in.key);
    }
}

} // namespace

void refine(RenderEntity& e) {
    if (e.type != Object::SEARCH) return;
    double t = nowSec();
    Info& in = infoOf(e.actor, t);
    refreshClass(in, e.actor, t);
    if (startsWith(in.cls, "BP_Pickup")) e.type = Object::PICKUP;
}

void fill(RenderEntity& e) {
    double t = nowSec();
    Info& in = infoOf(e.actor, t);
    refreshClass(in, e.actor, t); //a different actor name at this address resets everything cached for it
    switch (e.type) {
        case Object::PLAYER: fillPlayer(in, e.actor, t); break;
        case Object::ARC: fillArc(in, e.actor, t); break;
        case Object::SEARCH: fillContainer(in); break;
        case Object::PICKUP: fillPickup(in, e.actor, t); break;
        default: break;
    }
    e.label = in.label;
    e.sub = in.sub;
    e.account = in.account;
    e.isTurret = in.turret;
}

std::vector<ItemRow> inventoryOf(uintptr_t pawn) {
    std::vector<ItemRow> out;
    if (live::invArrays.empty()) return out;
    const uintptr_t inv = ReadMemory<uintptr_t>(pawn + PAWN_INVENTORY);
    for (const live::InvArray& a : live::invArrays) {
        const uintptr_t comp = a.comp >= 0 ? ReadMemory<uintptr_t>(pawn + (uintptr_t)a.comp) : inv; //most lists are in the inventory component
        if (!isObj(comp)) continue;
        const uintptr_t data = ReadMemory<uintptr_t>(comp + (uintptr_t)a.off);
        const int num = ReadMemory<int>(comp + (uintptr_t)a.off + 8);
        if (!isValidPtr(data) || num <= 0 || num > 256) continue;
        for (int i = 0; i < num && out.size() < 160; i++) {
            const uintptr_t rec = data + (uintptr_t)i * (uintptr_t)a.stride;
            const std::string raw = assetName(ReadMemory<uintptr_t>(rec + (uintptr_t)a.asset), "DA_");
            if (raw.empty()) continue;
            const std::string key = labels::itemToken(raw);
            bool merged = false;
            for (ItemRow& r : out) if (r.key == key && r.category == a.category) { r.count++; merged = true; break; }
            if (!merged) { ItemRow r; r.name = labels::display(labels::Cat::Item, key); r.key = key; r.category = a.category; out.push_back(r); }
        }
    }
    return out;
}

void reset() {
    g_cache.clear();
    g_idsBad = false;
    g_pickupOff = PICKUP_ITEM_ASSET;
}

void endTick() {
    static double last = 0;
    double t = nowSec();
    if (t - last < 5.0) return;
    last = t;
    for (auto it = g_cache.begin(); it != g_cache.end();) {
        if (t - it->second.seen > FORGET_AFTER) it = g_cache.erase(it);
        else ++it;
    }
}

} // namespace labeler
