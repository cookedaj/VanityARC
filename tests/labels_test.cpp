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
#include "utils/labels.h"
#include "utils/labeler.h"
#include "utils/offsets.h"
#include "utils/keytext.h"
#include "utils/settings.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>
//A throw-away folder under the system temp directory (tests never touch your real ~/.config/kwin-helper)
static std::string testDir(const char* name) {
    return (std::filesystem::temp_directory_path() / (std::string("kwinhelper_test_") + name)).string();
}


void runNameProbe(const std::vector<RenderEntity>& ents, const std::vector<RenderEntity>& unknown,
                  const FminimalViewInfo& vm, ptr uworld, FILE* out);

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
    base = (u64)region; bump = base + 0x10600000;
    for (int i = 0; i < 64; i++) textTable[i] = (u16)(0x1111 * (i + 1) + i * 7 + 1);
    std::memcpy((void*)(base + TEXT_RVA), textTable, sizeof textTable);
    anchor = base + ANCHOR_RVA; keyAll = anchor * ((anchor ^ ANCHOR_XOR) | 1);
    slotXor = keyAll + SLOT_KEY_ADD; blockKey = keyAll + BLOCK_KEY_ADD;
    names::setBase(base);

    using labels::Cat;
    //--- built-in names, using the raw names seen in the live probe
    auto D = [](Cat c, const char* k) { return labels::defaultName(c, k); };
    CHECK(D(Cat::Arc, "DA_EnemyType_Tick") == "Tick", "arc Tick");
    CHECK(D(Cat::Arc, "DA_EnemyType_RollBot_Pop") == "Pop", "arc Pop");
    CHECK(D(Cat::Arc, "DA_EnemyType_RollBot_Flamethrower") == "Fireball", "arc Fireball");
    CHECK(D(Cat::Arc, "DA_EnemyType_Runner") == "Runner", "arc fallback Runner");
    CHECK(D(Cat::Arc, "DA_EnemyType_HeavyDrone_Laser") == "Heavy Drone Laser", "arc fallback spaced (title case)");
    CHECK(D(Cat::Container, "BP_SocketContainer_Gvt_BinTrash_01_A_C") == "Trash Can", "container trash 1");
    CHECK(D(Cat::Container, "BP_SocketContainer_01_TrashBin_A_01_C") == "Trash Can", "container trash 2");
    CHECK(D(Cat::Container, "BP_SocketContainer_CUL_MuseumCrate_01_A_C") == "Museum Crate", "container museum crate (not in the loot tag list: cleaned name)");
    CHECK(D(Cat::Container, "BP_SocketContainer_Raider_AmmoBox_01_Lid_A_Dynamic_C") == "Ammo Box", "container ammo box");
    CHECK(D(Cat::Container, "BP_SocketContainer_Sct_LockerMedical_01_A_C") == "Locker", "container medical locker (not in the loot tag list: shown as Locker)");
    CHECK(D(Cat::Container, "BP_SocketContainer_Sct_Locker_01_A_C") == "Locker", "container locker (patch 2.5.0 loot tag list)");
    CHECK(D(Cat::Container, "BP_SalvageContainer_GreatMullein_C") == "Great Mullein", "container fallback (title case)");
    CHECK(D(Cat::Container, "BP_SalvageContainer_ARC_Scrap_C") == "ARC Scrap", "container fallback keeps ARC in capitals");
    CHECK(D(Cat::Item, "BattleRifle_BreachAction_01") == "Ferro", "item Ferro token");
    CHECK(D(Cat::Item, "DA_Item_BattleRifle_BreachAction_01") == "Ferro", "item Ferro full name");
    CHECK(D(Cat::Item, "DA_WeaponVisuals_SMG_LowTier_01") == "Stitcher", "gun Stitcher (visuals asset)");
    CHECK(D(Cat::Item, "BP_WeaponActor_AssaultRifle_Pneumatic_01_C") == "Kettle", "gun Kettle (weapon actor)");
    CHECK(D(Cat::Item, "DA_Item_Shotgun_PumpAction_01_MoreReload_SlugRounds") == "Il Toro", "gun variant keeps its base name");
    CHECK(D(Cat::Item, "DA_Item_SMG_Medium_01_Carbine") == "Canto", "gun Canto variant");
    CHECK(D(Cat::Item, "DA_Item_Bandage_01") == "Bandage", "item fallback");
    CHECK(D(Cat::Item, "DA_Item_ESRAnalyzer_01") == "ESR Analyzer", "item wiki spelling despite the _01 suffix");

    //--- your own names, read from labels.txt (the overlay never writes it)
    std::string dir = testDir("cfgtest");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    labels::setDir(dir);
    CHECK(labels::display(Cat::Arc, "DA_EnemyType_Runner") == "Runner", "no labels.txt: built-in name");
    CHECK(labels::display(Cat::Player, "793381ba5d093c9f", "cooked") == "cooked", "player shows its game name");
    const std::string labelsFile = dir + "/labels.txt";
    const std::string labelsText = "\xEF\xBB\xBF" "arc\tDA_EnemyType_Runner\tSpider\n"
                                   "Container \t BP_SocketContainer_CUL_MuseumCrate_01_A_C \t Art crate \r\n";
    { std::ofstream out(labelsFile, std::ios::binary); out << labelsText; }
    labels::load();
    CHECK(labels::display(Cat::Arc, "DA_EnemyType_Runner") == "Spider", "own name wins (first line after a UTF-8 mark)");
    CHECK(labels::display(Cat::Container, "BP_SocketContainer_CUL_MuseumCrate_01_A_C") == "Art crate", "category in any case, spaces around the fields dropped");

    //"shown name = your name" lines (names not used further down, so the later checks keep the built-in names)
    { std::ofstream out(labelsFile, std::ios::binary | std::ios::app);
      out << "great  MULLEIN = Plant\n" " ESR Analyzer\t=\tScanner\r\n" "# Heavy Drone Laser = Note\n" "Museum Crate = Box\n"
             "rival = Buddy\n" "AAAABBBBCCCCDDDD = Friend\n" "no separator here\n" " = nameless\n"; }
    labels::load();
    CHECK(labels::display(Cat::Container, "BP_SalvageContainer_GreatMullein_C") == "Plant", "shown name: capitals and spaces do not matter");
    CHECK(labels::display(Cat::Item, "DA_Item_ESRAnalyzer_01") == "Scanner", "shown name: TABs around '=' are fine");
    CHECK(labels::display(Cat::Arc, "DA_EnemyType_HeavyDrone_Laser") == "Heavy Drone Laser", "a line starting with # is ignored");
    CHECK(labels::display(Cat::Container, "BP_SocketContainer_CUL_MuseumCrate_01_A_C") == "Art crate", "the TAB line with the raw key wins over a shown-name line");
    CHECK(labels::display(Cat::Player, "1111222233334444", "Rival") == "Buddy", "shown name: a player's game name");
    CHECK(labels::display(Cat::Player, "aaaabbbbccccdddd", "Someone") == "Friend", "the raw key works on a '=' line too");
    CHECK(labels::display(Cat::Arc, "DA_EnemyType_Tick") == "Tick", "an unrelated name is unchanged");

    //--- settings file
    config::showArcNames = false; config::labelSize = 19.0f; config::nameMaxLootDist = 33;
    settings::save(dir);
    config::showArcNames = true; config::labelSize = 15.0f; config::nameMaxLootDist = 25;
    settings::load(dir);
    CHECK(!config::showArcNames && config::labelSize == 19.0f && config::nameMaxLootDist == 33, "settings round-trip");
    config::showArcNames = true; config::labelSize = 15.0f; config::nameMaxLootDist = 25;

    //--- typing
    CHECK(KeyToChar(KEY_A, false) == 'a' && KeyToChar(KEY_A, true) == 'A' && KeyToChar(KEY_Z, false) == 'z' && KeyToChar(KEY_M, true) == 'M', "letters");
    CHECK(KeyToChar(KEY_1, false) == '1' && KeyToChar(KEY_0, false) == '0' && KeyToChar(KEY_1, true) == '!', "digits");
    CHECK(KeyToChar(KEY_SPACE, false) == ' ' && KeyToChar(KEY_MINUS, true) == '_' && KeyToChar(KEY_DOT, false) == '.', "space and punctuation");
    CHECK(KeyToChar(KEY_ENTER, false) == 0 && KeyToChar(KEY_F1, false) == 0 && KeyToChar(KEY_LEFT, false) == 0, "non-text keys type nothing");
    std::string typed; const int seq[] = {KEY_C, KEY_O, KEY_O, KEY_K, KEY_E, KEY_D};
    for (int k : seq) typed.push_back(KeyToChar(k, false));
    CHECK(typed == "cooked", "typing c-o-o-k-e-d");

    //--- the game world: names in the pool
    addPoolName(0, "None"); //the decoder checks this before it trusts the pool
    addPoolName(0x1240, "BP_PickupBase_C");
    addPoolName(0x1280, "DA_Item_BattleRifle_BreachAction_01");
    addPoolName(0x12C0, "BP_SocketContainer_Gvt_BinTrash_01_A_C");
    addPoolName(0x2000, "BP_PioneerCharacter_C");
    addPoolName(0x2040, "C_RollBot_Pop_C");
    addPoolName(0x2080, "DA_EnemyType_RollBot_Pop");
    addPoolName(0x20C0, "BP_WeaponActor_AssaultRifle_LowTier_01_C");
    addPoolName(0x2100, "DA_Item_AssaultRifle_LowTier_01");
    addPoolName(0x2140, "DA_WeaponVisuals_SMG_LowTier_01");
    addPoolName(0x2180, "StowedWeaponActor");
    addPoolName(0x21C0, "BP_PioneerPlayerController_C");

    auto ent = [&](Object t, double m, u64 actor) { RenderEntity e; e.type = t; e.actor = actor; e.pos = Vector3(m * 100, 0, 0); e.vt = 0; return e; };

    //ground Ferro: container-class vtable, but the actor is a BP_PickupBase_C
    u64 ferro = makeObject(0x1000, 0x1240, 0);
    u64 ferroAsset = makeObject(0x400, 0x1280, 0);
    put<u64>(ferro, 0x518, ferroAsset);
    RenderEntity e1 = ent(Object::SEARCH, 3.0, ferro);
    labeler::refine(e1);
    CHECK(e1.type == Object::PICKUP, "ground item is told apart from containers");
    labeler::fill(e1);
    CHECK(e1.label == "Ferro", "ground item named Ferro");

    u64 bin = makeObject(0x1000, 0x12C0, 7);
    RenderEntity e2 = ent(Object::SEARCH, 10.0, bin);
    labeler::refine(e2); labeler::fill(e2);
    CHECK(e2.type == Object::SEARCH && e2.label == "Trash Can", "container named Trash can");

    //---- inventory lists named in offsets.txt
    addPoolName(0x2400, "DA_Item_Bandage_01");
    addPoolName(0x2440, "DA_Item_SMG_LowTier_01");
    u64 bandAsset = makeObject(0x400, 0x2400, 0), smgAsset = makeObject(0x400, 0x2440, 0);
    u64 invPawn = makeObject(0x1400, 0x2000, 3), invComp = makeObject(0x800, 0x2180, 0);
    put<u64>(invPawn, 0xCB0, invComp);
    u64 recs = alloc(0x20 * 4);
    put<u64>(recs, 0x10, bandAsset); put<u64>(recs, 0x30, smgAsset); put<u64>(recs, 0x50, bandAsset); //[3] stays empty
    put<u64>(invComp, 0x600, recs); put<int>(invComp, 0x608, 4);
    CHECK(labeler::inventoryOf(invPawn).empty(), "inventory: nothing is read until the lists are named in offsets.txt");
    live::InvArray listA; listA.off = 0x600; listA.stride = 0x20; listA.asset = 0x10; listA.category = "backpack";
    live::invArrays = {listA};
    auto rows = labeler::inventoryOf(invPawn);
    bool bandOk = false, smgOk = false;
    for (const ItemRow& r : rows) { bandOk |= r.name == "Bandage" && r.count == 2 && r.category == "backpack"; smgOk |= r.name == "Stitcher" && r.count == 1; }
    CHECK(rows.size() == 2 && bandOk && smgOk, "inventory: items are named, the same item is merged into one row with a count");
    bool keyOk = false;
    for (const ItemRow& r : rows) keyOk |= r.name == "Bandage" && r.key == "Bandage_01";
    CHECK(keyOk, "inventory: a row keeps the internal name as its key (for prices)");
    put<int>(recs, 0x18, 5); put<int>(recs, 0x58, 3); //stack sizes; [1] keeps 0, which counts as 1
    listA.qty = 0x18;
    live::invArrays = {listA};
    bool stackBand = false, stackSmg = false;
    for (const ItemRow& r : labeler::inventoryOf(invPawn)) { stackBand |= r.name == "Bandage" && r.count == 8; stackSmg |= r.name == "Stitcher" && r.count == 1; }
    CHECK(stackBand && stackSmg, "inventory: with the stack size offset known, rows count items, not slots");
    put<int>(invComp, 0x608, 100000);
    CHECK(labeler::inventoryOf(invPawn).empty(), "inventory: an absurd count is not trusted");
    put<int>(invComp, 0x608, 4); put<u64>(invPawn, 0xCB0, 0);
    CHECK(labeler::inventoryOf(invPawn).empty(), "inventory: no inventory component, no rows");
    //a list that lives in another component of the pawn (inv_array_in in offsets.txt)
    u64 otherComp = makeObject(0x800, 0x2180, 0);
    put<u64>(invPawn, 0x700, otherComp); put<u64>(otherComp, 0x40, recs); put<int>(otherComp, 0x48, 4);
    live::InvArray listB; listB.comp = 0x700; listB.off = 0x40; listB.stride = 0x20; listB.asset = 0x10; listB.category = "pockets";
    live::invArrays = {listB};
    auto rowsB = labeler::inventoryOf(invPawn);
    bool pocketsOk = false;
    for (const ItemRow& r : rowsB) pocketsOk |= r.name == "Bandage" && r.count == 2 && r.category == "pockets";
    CHECK(rowsB.size() == 2 && pocketsOk, "inventory: a list in another component is read through that component's pointer");
    live::invArrays.clear();

    u64 popAsset = makeObject(0x300, 0x2080, 0);
    u64 pop = makeObject(0x1400, 0x2040, 1); put<u64>(pop, 0x11C0, popAsset);
    RenderEntity e3 = ent(Object::ARC, 30.0, pop);
    labeler::fill(e3);
    CHECK(e3.label == "Pop", "ARC named Pop");

    //a player with PlayerState, a held Rattler and a stowed Stitcher
    u64 ps = makePS("\x76\x6e\x6b\x7e\x64\x46", 6, 0x793381ba5d093c9fULL);
    u64 pawn = makeObject(0x1400, 0x2000, 1);
    put<u64>(pawn, 0x3E8, ps);
    u64 inv = makeObject(0x800, 0x2000, 0); put<u64>(pawn, 0xCB0, inv);
    u64 stowedAsset = makeObject(0x400, 0x2140, 0);
    put<u64>(inv, 0x3B0 + 0x10, makeObject(0x400, 0x2180, 0)); put<u64>(inv, 0x3B0 + 0x18, stowedAsset);
    u64 heldActor = makeObject(0x1000, 0x20C0, 0), heldAsset = makeObject(0x400, 0x2100, 0);
    u64 rec = alloc(0x40); put<u64>(rec, 0x0, heldActor); put<u64>(rec, 0x10, heldAsset);
    put<u64>(inv, 0x530, rec); put<int>(inv, 0x538, 1);
    RenderEntity e4 = ent(Object::PLAYER, 50.0, pawn);
    labeler::fill(e4);
    CHECK(e4.label == "cooked", "player name");
    CHECK(e4.sub == "Rattler (held) + Stitcher", "player guns: held first, then stowed");

    //a nickname in labels.txt replaces the game name (a fresh actor: a known one is re-read only every few seconds)
    { std::ofstream out(labelsFile, std::ios::binary | std::ios::app); out << "player\t793381ba5d093c9f\tAlex\n"; }
    labels::load();
    u64 pawn2 = makeObject(0x1400, 0x2000, 1); put<u64>(pawn2, 0x3E8, ps); put<u64>(pawn2, 0xCB0, inv);
    RenderEntity e5 = ent(Object::PLAYER, 50.0, pawn2);
    labeler::fill(e5);
    CHECK(e5.label == "Alex", "nickname shows on screen");

    //a held non-gun record (a tool) is not listed as a gun
    u64 toolRec = alloc(0x40); put<u64>(toolRec, 0x0, makeObject(0x1000, 0x2180, 0)); put<u64>(toolRec, 0x10, heldAsset);
    put<u64>(inv, 0x530, toolRec);
    u64 pawn3 = makeObject(0x1400, 0x2000, 1); put<u64>(pawn3, 0x3E8, ps); put<u64>(pawn3, 0xCB0, inv);
    RenderEntity e6 = ent(Object::PLAYER, 50.0, pawn3);
    labeler::fill(e6);
    CHECK(e6.sub.find("held") == std::string::npos, "non-weapon record is not a held gun");

    //player with no PlayerState: no name, no crash
    u64 lone = makeObject(0x1400, 0x2000, 2);
    RenderEntity e7 = ent(Object::PLAYER, 90.0, lone);
    labeler::fill(e7);
    CHECK(e7.label.empty() && e7.sub.empty(), "player without PlayerState has no label");

    //--- what an actor is from its names alone (how vtables config.h does not know are learned)
    addPoolName(0x2380, "CharacterMesh0");
    u64 charMesh = makeObject(0x200, 0x2380, 0);
    put<u64>(pawn, off::MESH, charMesh); put<u64>(lone, off::MESH, charMesh);
    CHECK(labeler::kindByName(pawn) == Object::PLAYER, "learn: a CharacterMesh and a PlayerState is a player");
    CHECK(labeler::kindByName(lone) == Object::NONE, "learn: a character without a PlayerState is not a player");
    CHECK(labeler::kindByName(pop) == Object::ARC, "learn: a DA_EnemyType_ asset is ARC");
    CHECK(labeler::kindByName(bin) == Object::SEARCH && labeler::kindByName(ferro) == Object::SEARCH, "learn: containers and ground items are loot");
    CHECK(labeler::kindByName(makeObject(0x1400, 0x2180, 0)) == Object::NONE, "learn: anything else is nothing");
    CHECK(!labeler::kindByName(makeObject(0x1400, 0x23C0, 0)), "learn: an actor whose name does not read tells nothing");
    labeler::endTick();

    //--- the game reuses memory: new ARC / item at an old address must not keep the old name
    addPoolName(0x2240, "DA_EnemyType_Tick");
    addPoolName(0x2280, "DA_Item_SMG_LowTier_01");
    u64 tickAsset = makeObject(0x300, 0x2240, 0);
    u64 stitcherAsset = makeObject(0x400, 0x2280, 0);
    //a name that is not in the pool yet
    u64 late = makeObject(0x400, 0x2300, 0);
    CHECK(names::name(late).empty(), "a name that is not loaded reads as empty");
    addPoolName(0x2300, "DA_Late_Name");
    CHECK(names::name(late).empty(), "the failure is remembered for a moment (no read storm)");

    put<u64>(pop, 0x11C0, tickAsset);
    put<u64>(ferro, 0x518, stitcherAsset);

    //a player whose name and account then fail to read (re-read after the two sleeps below, more than 3 s together)
    u64 psK = makePS("\x76\x6e\x6b\x7e\x64\x46", 6, 0x5a5a1234abcd5678ULL);
    u64 pK = makeObject(0x1400, 0x2000, 8); put<u64>(pK, 0x3E8, psK);
    RenderEntity eK = ent(Object::PLAYER, 50.0, pK); labeler::fill(eK);
    CHECK(eK.label == "cooked" && eK.account == "5a5a1234abcd5678", "player read: name and account");
    put<int>(psK, 0x468, 0); put<u64>(psK, 0x3E0, 0); //the name and the account object now read as nothing

    std::this_thread::sleep_for(std::chrono::milliseconds(2200));
    RenderEntity e8 = ent(Object::ARC, 30.0, pop); labeler::fill(e8);
    CHECK(e8.label == "Tick", "ARC at a reused address gets its new name");
    RenderEntity e9 = ent(Object::PICKUP, 3.0, ferro); labeler::fill(e9);
    CHECK(e9.label == "Stitcher", "ground item at a reused address gets its new name");

    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    CHECK(names::name(late) == "DA_Late_Name", "a name that failed to read is tried again later");

    eK = ent(Object::PLAYER, 50.0, pK); labeler::fill(eK);
    CHECK(eK.label == "cooked" && eK.account == "5a5a1234abcd5678", "player read fails: the name and account of the same PlayerState are kept");
    put<u64>(pK, 0x3E8, makePS("\x76\x6e\x6b\x7e", 4, 0)); //another PlayerState: "cook", no account
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    eK = ent(Object::PLAYER, 50.0, pK); labeler::fill(eK);
    CHECK(eK.label == "cook" && eK.account.empty(), "a failed player read is tried again after 1 s; another PlayerState does not get the old account");

    //--- account ids are sanity-checked (the first player above was last seen more than 3 s ago)
    u64 pA = makeObject(0x1400, 0x2000, 4); put<u64>(pA, 0x3E8, makePS("\x76\x6e\x6b\x7e\x64\x46", 6, 0x793381ba5d093c9fULL));
    RenderEntity eA = ent(Object::PLAYER, 50.0, pA); labeler::fill(eA);
    CHECK(eA.account == "793381ba5d093c9f", "account: a player seen long ago with the same id is not a clash");
    u64 pP = makeObject(0x1400, 0x2000, 5); put<u64>(pP, 0x3E8, makePS("\x76\x6e\x6b\x7e\x64\x46", 6, pA));
    RenderEntity eP = ent(Object::PLAYER, 50.0, pP); labeler::fill(eP);
    CHECK(eP.account.empty() && eP.label == "cooked", "account: an id that looks like a pointer is not trusted (the name still shows)");
    u64 pB = makeObject(0x1400, 0x2000, 6); put<u64>(pB, 0x3E8, makePS("\x76\x6e\x6b\x7e\x64\x46", 6, 0x793381ba5d093c9fULL));
    RenderEntity eB = ent(Object::PLAYER, 50.0, pB); labeler::fill(eB);
    eA = ent(Object::PLAYER, 50.0, pA); labeler::fill(eA);
    CHECK(eB.account.empty() && eA.account.empty() && eB.label == "cooked", "account: two players at once with one id: nobody gets an id, so one mark cannot hide everyone");

    std::filesystem::remove_all(dir);
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
