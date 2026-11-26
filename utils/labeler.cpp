#include "labeler.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "amp.h"
#include "labels.h"
#include "log.h"
#include "names.h"
#include "offsets.h"

namespace labeler {

std::string itemName(const std::string& key, bool gun, int slotLevel) {
    const amp::GunLevel g = amp::gunLevel(key);
    //The level the gun slot holds, when the name has none: "Stitcher 2" (your own name for the gun keeps the number)
    if (g.level == 0 && gun && slotLevel >= 1 && slotLevel <= 4)
        return amp::gunLabel(labels::display(labels::Cat::Item, key), slotLevel);
    if (g.level == 0 || !(gun || labels::isGun(key))) return labels::display(labels::Cat::Item, key);
    //"AMP Canto"; your own name for the full internal key, or for "AMP Canto" itself, wins
    return labels::display(labels::Cat::Item, key, amp::gunLabel(labels::display(labels::Cat::Item, g.base), g.level));
}

namespace {

//The game's own name for an item key: itemName without your labels.txt names. values.txt prices are also looked up by
//it, so renaming an item never loses a price listed under the game's name.
std::string builtInItemName(const std::string& key) {
    const amp::GunLevel g = amp::gunLevel(key);
    if (g.level == 0 || !labels::isGun(key)) return labels::defaultName(labels::Cat::Item, key);
    return amp::gunLabel(labels::defaultName(labels::Cat::Item, g.base), g.level);
}

//The offsets live in config.h (off::), shared with the probe report so both always read the same fields
using off::PAWN_PLAYERSTATE, off::PS_NAME, off::PS_ACCOUNT_OBJ, off::PAWN_INVENTORY, off::INV_STOWED, off::INV_SLOT_LEVEL, off::INV_HELD, off::WEAPON_LEVEL,
      off::PICKUP_ITEM_ASSET, off::ARC_TYPE_ASSET;

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
    std::string gameName; //ground items: builtInItemName of the key
    std::string account;
    uintptr_t ps = 0;   //the PlayerState the account came from
    bool turret = false;
    int kind = -1;      //ground items: labels::Kind, containers: labels::Group; worked out once per asset or class (-1: not yet)
};

std::unordered_map<uintptr_t, Info> g_cache;
bool g_idsBad = false; //two players at once gave the same account id: the id offsets are wrong for this build
int g_pickupOff = PICKUP_ITEM_ASSET; //where the fallback last found a ground item's asset (a patch moved the field)

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool isImagePtr(uintptr_t p) { return p >= ImageBase && p < ImageEnd; }
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
        in.kind = -1;
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

//The guns a player has: held first (inventory "current items" with a weapon actor), then the stowed slots. A held gun's
//level is in its weapon actor, a stowed gun's in its slot; an amplified gun says so in its name, held or not.
std::string gunsOf(uintptr_t pawn) {
    uintptr_t inv = ReadMemory<uintptr_t>(pawn + PAWN_INVENTORY);
    if (!isObj(inv)) return "";

    std::vector<std::string> held, stowed;
    uintptr_t data = ReadMemory<uintptr_t>(inv + INV_HELD);
    int num = ReadMemory<int>(inv + INV_HELD + 8);
    if (isValidPtr(data) && num > 0 && num <= 16) {
        for (int i = 0; i < num && i < 4; i++) {
            uintptr_t rec = data + (uintptr_t)i * 0x20;
            const uintptr_t weapon = ReadMemory<uintptr_t>(rec);
            if (assetName(weapon, "BP_WeaponActor_").empty()) continue; //not a gun
            std::string asset = assetName(ReadMemory<uintptr_t>(rec + 0x10), "DA_Item_");
            if (asset.empty()) asset = assetName(weapon, "BP_WeaponActor_");
            const int q = ReadMemory<uint8_t>(weapon + WEAPON_LEVEL); //0..3 = level I..IV; anything else: not shown
            if (!asset.empty()) held.push_back(itemName(labels::itemToken(asset), true, q <= 3 ? q + 1 : 0));
        }
    }
    for (int base : INV_STOWED) {
        std::string asset = assetName(ReadMemory<uintptr_t>(inv + base + 0x18), "DA_WeaponVisuals_");
        if (asset.empty()) continue;
        const int q = ReadMemory<int>(inv + base + INV_SLOT_LEVEL); //0..3 = level I..IV; anything else: not shown
        stowed.push_back(itemName(labels::itemToken(asset), true, q >= 0 && q <= 3 ? q + 1 : 0));
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
        const uintptr_t ps = ReadMemory<uintptr_t>(pawn + PAWN_PLAYERSTATE);
        std::string name;
        uint64_t id = 0;
        if (isObj(ps)) {
            name = names::playerName(ps + PS_NAME);
            uintptr_t acc = ReadMemory<uintptr_t>(ps + PS_ACCOUNT_OBJ);
            id = isValidPtr(acc) ? (uint64_t)ReadMemory<uintptr_t>(acc + 0x18) : 0;
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
        }
        //A read that fails keeps what this same PlayerState gave before, so a name or hand mark does not blink off.
        //Another PlayerState (or none) may be another player at a reused address: it starts empty.
        if (ps != in.ps) { in.label.clear(); in.account.clear(); }
        in.ps = ps;
        if (id) in.account = hex64(id);
        if (g_idsBad) in.account.clear();
        if (!name.empty()) in.label = in.account.empty() ? name : labels::display(labels::Cat::Player, in.account, name);
        if (name.empty() || (!id && !g_idsBad)) in.labelT = t - (PLAYER_TTL - RETRY_TTL); //something failed: try again in 1 s, not 3
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
            std::string k = in.key; //"DA_EnemyType_TurretEnemy" and friends; worked out once per asset
            for (char& c : k) c = (char)std::tolower((unsigned char)c);
            in.turret = k.find("turret") != std::string::npos;
        }
    }
    if (in.resolved && in.label.empty()) {
        in.label = labels::display(labels::Cat::Arc, in.key);
    }
}

void fillContainer(Info& in) {
    if (in.cls.empty()) return;
    if (in.label.empty()) {
        in.key = in.cls;
        in.label = labels::display(labels::Cat::Container, in.key);
        in.kind = (int)labels::containerKind(in.cls);
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
                in.kind = (int)labels::kindOf(in.key);
                in.resolved = true;
                in.retry = 0;
                in.label.clear();
            } else { //not readable (yet): back off; a failed recheck keeps the label it had
                in.retry = in.retry > 0 ? std::min(in.retry * 2, PICKUP_RETRY_MAX) : RETRY_TTL;
            }
        }
    }
    if (in.resolved && in.label.empty()) {
        in.label = itemName(in.key);
        in.gameName = builtInItemName(in.key);
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

std::optional<Object> kindByName(uintptr_t actor) {
    if (!names::ready()) return std::nullopt;
    const std::string cls = names::name(actor);
    if (cls.empty()) return std::nullopt; //its own name did not read: ask another actor
    if (!assetName(ReadMemory<uintptr_t>(actor + ARC_TYPE_ASSET), "DA_EnemyType_").empty()) return Object::ARC;
    if (!assetName(ReadMemory<uintptr_t>(actor + off::MESH), "CharacterMesh").empty()) //a character: a player has a PlayerState, an AI one does not
        return isObj(ReadMemory<uintptr_t>(actor + PAWN_PLAYERSTATE)) ? Object::PLAYER : Object::NONE;
    for (const char* p : {"BP_SocketContainer_", "BP_SalvageContainer_", "BP_Pickup"})
        if (startsWith(cls, p)) return Object::SEARCH; //refine() then tells ground items apart, as for the config.h vtable
    return Object::NONE;
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
    e.itemKind = in.kind;
    if (e.type == Object::PICKUP) { e.itemKey = in.key; e.gameName = in.gameName; }
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
            //The held list also has the gun in their hands, which EQUIPPED already shows
            if (a.comp < 0 && a.off == INV_HELD && labels::isGun(key)) continue;
            const int q = a.qty >= 0 ? std::clamp(ReadMemory<int>(rec + (uintptr_t)a.qty), 1, 9999) : 1; //stack size; not known: one per slot
            bool merged = false;
            for (ItemRow& r : out) if (r.key == key && r.category == a.category) { r.count += q; merged = true; break; }
            if (!merged) { ItemRow r; r.name = itemName(key); r.key = key; r.gameName = builtInItemName(key); r.category = a.category; r.count = q; out.push_back(r); }
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
