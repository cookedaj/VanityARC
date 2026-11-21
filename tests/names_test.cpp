//Round-trip test of the name decoders against a fake game image, plus a run of probe v4 over it.
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "config.h"
#include "utils/localUtil.h"
#include "utils/names.h"

void runNameProbe(const std::vector<RenderEntity>& ents, const std::vector<RenderEntity>& unknown,
                  const FminimalViewInfo& vm, ptr uworld, FILE* out);

void runLiveProbe(const std::string& mode, ptr localPawn, const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm, FILE* out);

static int fails = 0;
#define CHECK(cond, msg) do { bool ok_ = (cond); std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) fails++; } while (0)

typedef uint64_t u64; typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8;
static u32 rol32(u32 v, int n) { return (v << n) | (v >> (32 - n)); }
static u64 rol64(u64 v, int n) { return (v << n) | (v >> (64 - n)); }

//constants duplicated on purpose from names.cpp
static const u64 NAMES_RVA = 0x105726C0, TEXT_RVA = 0x1043A2A4, ANCHOR_RVA = 0x104F48B0, ANCHOR_XOR = 0x25FAB87EA2567D68ULL;
static const u32 HP = 0x01000193;
static const u64 FNV_PRIME = 0x100000001B3ULL, FNV_ADD = 0x2790CE36A33B0E0AULL;
static const u64 SLOT_KEY_ADD = 0x902552F3625ADD06ULL, SLOT_A = 0xA15809052DCA75EDULL, SLOT_B = 0xBDEF271E65265A09ULL;
static const u64 BLOCK_KEY_ADD = 0xDFD86F31B20DF944ULL, BLOCK_A = 0x1148A6092DCA75EDULL, BLOCK_B = 0xF2121ADC1DBC1135ULL;

static u64 invClmul(u64 a, u64 x) { //a must be odd
    u64 t = 0;
    for (int i = 0; i < 64; i++) {
        u64 c = 0;
        for (int j = 1; j <= i; j++) c ^= ((a >> j) & 1) & ((t >> (i - j)) & 1);
        t |= (((x >> i) & 1) ^ c) << i;
    }
    return t;
}
//make a 16-byte pair that Unfold()s to target
static void encodePair(u64 target, u64 A, u64 B, u64 out[2]) {
    u64 raw0 = 0x9E3779B97F4A7C15ULL ^ (target * 31);
    u64 t = invClmul(A, target ^ raw0);
    out[0] = raw0;
    out[1] = t ^ names::clmul64(B, raw0);
}

static uint8_t* region;
static u64 base;
static u64 bump;
static u16 textTable[64];

static u64 alloc(size_t n) { u64 p = bump; bump += (n + 15) & ~15ULL; return p; }

static u16 textKey(u32 key, int i) { return textTable[((i & 1) == 0 ? key : key * 0xFFFFD236u + 0x75EDu) & 0x3F]; }
static u32 stepKey(u32 key, int i) { return (i & 1) ? key * 0x830A364u + 0xEAE8BFEBu : key; }

static u64 anchor, keyAll, slotXor, blockKey;

//register a name in the fake pool under index ci. All entries of a chunk live in one buffer: entry = chunkBuf + 2*(u16)ci
static u64 chunkBuf = 0;
static void addPoolName(u32 ci, const std::string& text) {
    u64 chunk = base + NAMES_RVA + (u64)((ci >> 8) & 0xFFFF00);
    if (!chunkBuf) {
        chunkBuf = alloc(0x20000);
        u64 seed = chunk + 0x60; u32 lo = (u32)seed, hi = (u32)(seed >> 32);
        u32 h = HP * rol32(lo, 25) + 0x6C48F44Au; h = HP * rol32(h, 26) + hi + 0x6C48F44Au; h = HP * (h >> 7) + 0x6C48F44Au; h = HP * (h >> 6) + 0x6C48F44Au;
        u32 mixed = h ^ (h >> 16); u32 b1 = mixed & 7, b2 = (mixed + 1) & 7;
        u64 s1 = 0x1122334455667788ULL;
        u64 mix = FNV_PRIME * rol64(s1, 40) + FNV_ADD; mix = FNV_PRIME * rol64(mix, 54) + FNV_ADD;
        u64 s2 = (chunkBuf - s1) ^ mix;
        u64 p1[2], p2[2];
        encodePair(s1 ^ blockKey, BLOCK_A, BLOCK_B, p1);
        encodePair(s2 ^ blockKey, BLOCK_A, BLOCK_B, p2);
        std::memcpy((void*)(chunk + 0x70 + 32 * b1), p1, 16);
        std::memcpy((void*)(chunk + 0x70 + 32 * b2), p2, 16);
    }
    int L = (int)text.size();
    u16 header = 0;
    for (u32 h = 0; h < 0x10000; h += 2) { int len = ((h << 1) & 0x380) | (h >> 9); if (len == L) { header = (u16)h; break; } }
    u64 E = chunkBuf + 2ULL * (u16)ci;
    std::memcpy((void*)E, &header, 2);
    u32 key = (u32)L + 0x2DCA;
    for (int i = 0; i < L; i++) { u8 c = (u8)text[i] ^ (u8)(textKey(key, i) >> 3); ((u8*)E)[2 + i] = c; key = stepKey(key, i); }
}

//a UObject with a vtable and an encrypted name
static u64 makeObject(size_t size, u32 ci, u32 number) {
    u64 o = alloc(size);
    std::memset((void*)o, 0, size);
    u64 vt = 0x14d000000ULL; std::memcpy((void*)o, &vt, 8);
    u32 slot = names::pickNameSlot(o);
    u64 pair[2]; encodePair((((u64)ci << 32) | number) ^ slotXor, SLOT_A, SLOT_B, pair);
    std::memcpy((void*)(o + 0x20 + 0x20ULL * slot), pair, 16);
    return o;
}
template <typename T> static void put(u64 o, size_t off, T v) { std::memcpy((void*)(o + off), &v, sizeof v); }

static u64 makePS(const char* hexchars, int n, u64 id) {
    u64 ps = makeObject(0x800, 0x2000, 0);
    u64 buf = alloc(64);
    for (int i = 0; i < n; i++) put<u16>(buf, i * 2, (u8)hexchars[i]);
    put<u64>(ps, 0x460, buf); put<int>(ps, 0x468, n + 1); put<int>(ps, 0x46C, 16);
    u64 acc = alloc(64); put<u64>(acc, 0x18, id); put<u64>(ps, 0x3E0, acc);
    return ps;
}

int main() {
    ProcessId = getpid();
    size_t size = 0x11000000;
    region = (uint8_t*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    base = (u64)region;
    bump = base + 0x10600000;
    names::setBase(base);

    //not ready before the table exists
    CHECK(!names::ready(), "not ready while the text key table is empty");
    for (int i = 0; i < 64; i++) textTable[i] = (u16)(0x1111 * (i + 1) + i * 7 + 1);
    std::memcpy((void*)(base + TEXT_RVA), textTable, sizeof textTable);
    anchor = base + ANCHOR_RVA; keyAll = anchor * ((anchor ^ ANCHOR_XOR) | 1);
    slotXor = keyAll + SLOT_KEY_ADD; blockKey = keyAll + BLOCK_KEY_ADD;
    names::setBase(base);
    CHECK(names::ready(), "ready once the table is there");

    //clmul self-check against a known small case: (x+1)*(x+1) = x^2+1
    CHECK(names::clmul64(3, 3) == 5, "clmul64(3,3) == 5");

    addPoolName(0x1240, "BP_LootContainer_Trash_C");
    addPoolName(0x1280, "DA_Item_BattleRifle_BreachAction_01");
    addPoolName(0x2000, "BP_PioneerCharacter_C");
    addPoolName(0x2040, "BP_Hornet_C");
    addPoolName(0x2080, "DA_Enemy_Hornet");
    addPoolName(0x3000, "BP_Pickup_Item_C");

    u64 a = makeObject(0x1400, 0x1240, 5);
    CHECK(names::name(a) == "BP_LootContainer_Trash_C", "decodes an actor name");
    CHECK(names::fullName(a) == "BP_LootContainer_Trash_C_4", "adds the instance number");
    u64 da = makeObject(0x400, 0x1280, 0);
    CHECK(names::name(da) == "DA_Item_BattleRifle_BreachAction_01", "decodes a data asset name");
    CHECK(names::name(0) == "" && names::name(base + 0x1000) == "", "garbage / empty object gives empty name");

    //player name: the real bytes seen live decode to 'cooked'
    u64 ps = makePS("\x76\x6e\x6b\x7e\x64\x46", 6, 0x793381ba5d093c9fULL);
    CHECK(names::playerName(ps + 0x460) == "cooked", "player name bytes 76 6e 6b 7e 64 46 -> cooked");

    //world for the probe
    std::vector<RenderEntity> ents, unknown;
    FminimalViewInfo vm{}; vm.Location = Vector3(0, 0, 0);
    auto ent = [&](Object t, double m, u64 actor) { RenderEntity e; e.type = t; e.actor = actor; e.pos = Vector3(m * 100, 0, 0); e.vt = 0; return e; };

    u64 mePawn = makeObject(0x1400, 0x2000, 1);
    put<u64>(mePawn, 0x3E8, ps);
    u64 inv = makeObject(0x800, 0x2000, 0);
    put<u64>(mePawn, 0xCB0, inv);
    put<u64>(inv, 0x3B0 + 0x10, makeObject(0x400, 0x3000, 0));
    put<u64>(inv, 0x3B0 + 0x18, da);
    put<int>(inv, 0x3B0 + 0x38, 2);
    u64 arr = alloc(0x40); put<u64>(arr, 0x10, da); put<u64>(inv, 0x530, arr); put<int>(inv, 0x538, 1); put<int>(inv, 0x53C, 1);
    ents.push_back(ent(Object::PLAYER, 1.5, mePawn));

    u64 type = makeObject(0x300, 0x2080, 0);
    for (int i = 0; i < 3; i++) { u64 h = makeObject(0x1400, 0x2040, i + 1); put<u64>(h, 0x11C0, type); ents.push_back(ent(Object::ARC, 20 + i, h)); }
    for (int i = 0; i < 6; i++) { u64 c = makeObject(0x1400, 0x1240, i + 1); put<u64>(c, 0x500, i % 2 ? da : type); ents.push_back(ent(Object::SEARCH, 5 + i, c)); }
    u64 dropped = makeObject(0x1000, 0x3000, 0); put<u64>(dropped, 0x4C0, da);
    unknown.push_back(ent(Object::NONE, 2.0, dropped));

    char* buf = nullptr; size_t len = 0;
    FILE* f = open_memstream(&buf, &len);
    runNameProbe(ents, unknown, vm, 0, f);
    fclose(f);
    std::string rep(buf, len);
    std::free(buf);
    std::puts("------ report ------"); std::fputs(rep.c_str(), stdout); std::puts("------ checks ------");
    auto has = [&](const char* s) { return rep.find(s) != std::string::npos; };
    CHECK(has("object-name decoder: ready"), "report: decoder ready");
    CHECK(has("name 'cooked'") && has("793381ba5d093c9f"), "report: player name and account id");
    CHECK(has("asset 0x") && has("<DA_Enemy_Hornet>"), "report: ARC type asset named");
    CHECK(has("sample actor BP_Hornet_C_"), "report: ARC actor named");
    CHECK(has("DA_Item_BattleRifle_BreachAction_01"), "report: gun asset named");
    CHECK(has("BP_Pickup_Item_C") && has("+0x4c0=DA_Item_BattleRifle_BreachAction_01"), "report: dropped gun actor + its asset field");
    CHECK(has("BP_LootContainer_Trash_C"), "report: container name in census");
    CHECK(has("CurrentItemActors") && has("+0x10=0x"), "report: held-item array printed");

    //---- live probes: VANITYARC_PROBE=shield / inventory
    {
        u64 pawn = makeObject(0x1400, 0x2000, 9);
        u64 health = makeObject(0x900, 0x2000, 0);
        put<u64>(pawn, 0xD98, health);                                   //HealthComponent
        put<double>(health, 0x380, 100.0); put<double>(health, 0x708, 100.0); put<double>(health, 0x710, 60.0); //max, health, and a "shield" next to them
        u64 other = makeObject(0x900, 0x2000, 0);
        put<u64>(pawn, 0x500, other);                                    //another component with a float
        put<float>(other, 0x100, 40.0f);

        auto run = [&](const char* mode, std::vector<RenderEntity> es = {}) {
            char* b = nullptr; size_t l = 0;
            FILE* o = open_memstream(&b, &l);
            runLiveProbe(mode, pawn, es, vm, o);
            fclose(o);
            std::string r(b, l);
            std::free(b);
            return r;
        };
        std::string first = run("shield");
        CHECK(first.find("SHIELD PROBE") != std::string::npos && first.find("nothing is printed until a number changes") != std::string::npos, "shield probe: first pass only watches");
        put<double>(health, 0x708, 90.0); put<double>(health, 0x710, 45.0); put<float>(other, 0x100, 35.0f);
        std::string second = run("shield");
        std::printf("%s", second.c_str());
        CHECK(second.find("known: health") != std::string::npos, "shield probe: the known health number is marked");
        CHECK(second.find("60.000 -> 45.000") != std::string::npos && second.find("shield_component -1  shield_value 0x710  shield_is_float 0") != std::string::npos,
              "shield probe: a changed number next to health comes with the offsets.txt line (health component = -1)");
        CHECK(second.find("shield_component 0x500  shield_value 0x100  shield_is_float 1") != std::string::npos, "shield probe: a float in another component is listed with its component offset");
        CHECK(second.find("shield_value 0x70c") == std::string::npos && second.find("shield_value 0x714") == std::string::npos,
              "shield probe: the 4-byte halves of a changed double are not listed as separate numbers");
        std::string third = run("shield");
        CHECK(third.find("shield_value 0x710") == std::string::npos, "shield probe: nothing is repeated while nothing changes");
        std::string none = run("shield", {});
        (void)none;

        //inventory: a list of item assets inside the inventory component, and another inside a different component
        u64 invc = makeObject(0xA00, 0x2000, 0);
        put<u64>(pawn, 0xCB0, invc);
        u64 recs = alloc(0x20 * 4);
        for (int i = 0; i < 3; i++) put<u64>(recs, 0x20 * i + 0x10, da);
        put<u64>(invc, 0x530, recs); put<int>(invc, 0x538, 3); put<int>(invc, 0x53C, 4);
        u64 comp2 = makeObject(0xA00, 0x2000, 0);
        put<u64>(pawn, 0x700, comp2);
        put<u64>(comp2, 0x40, recs); put<int>(comp2, 0x48, 3); put<int>(comp2, 0x4C, 4);
        std::string invReport = run("inventory");
        std::printf("%s", invReport.c_str());
        CHECK(invReport.find("INVENTORY PROBE: you") != std::string::npos && invReport.find("DA_Item_BattleRifle_BreachAction_01") != std::string::npos, "inventory probe: finds the list and names the items");
        CHECK(invReport.find("offsets.txt:  inv_array 0x530 0x20 0x10 item") != std::string::npos, "inventory probe: a list in the inventory component comes with its offsets.txt line");
        CHECK(invReport.find("offsets.txt:  inv_array_in 0x700 0x40 0x20 0x10 item") != std::string::npos, "inventory probe: a list in another component names that component");
        CHECK(run("nonsense").empty(), "an unknown probe name prints nothing");
    }

    //empty world must not crash
    buf = nullptr; len = 0; f = open_memstream(&buf, &len);
    runNameProbe({}, {}, vm, 0, f); fclose(f);
    std::free(buf);
    CHECK(true, "empty world does not crash");
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
