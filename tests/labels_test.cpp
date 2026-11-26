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

//this test's own fake layout (names::setRvasForTest), not the game's RVAs in names.cpp: a patch update there does not
//touch it. They differ from the real ones on purpose, so the test also proves the override works.
static const u64 NAMES_RVA = 0x02000000, TEXT_RVA = 0x01000000, ANCHOR_RVA = 0x01800000;
//constants duplicated on purpose from names.cpp
static const u64 ANCHOR_XOR = 0x25FAB87EA2567D68ULL;
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
    names::setRvasForTest(NAMES_RVA, TEXT_RVA, ANCHOR_RVA);
    names::setBase(base);

    using labels::Cat;
    //--- built-in names, using the raw names seen in the live probe
    auto D = [](Cat c, const char* k) { return labels::defaultName(c, k); };
    CHECK(D(Cat::Arc, "DA_EnemyType_Tick") == "Tick", "arc Tick");
    CHECK(D(Cat::Arc, "DA_EnemyType_RollBot_Pop") == "Pop", "arc Pop");
    CHECK(D(Cat::Arc, "DA_EnemyType_RollBot_Flamethrower") == "Fireball", "arc Fireball");
    CHECK(D(Cat::Arc, "DA_EnemyType_Runner") == "Shredder", "arc Shredder (type Runner)");
    CHECK(D(Cat::Arc, "DA_EnemyType_HeavyDrone_Laser") == "Vaporizer", "arc Vaporizer (type HeavyDrone_Laser)");
    CHECK(D(Cat::Arc, "DA_EnemyType_Courier") == "ARC Reclaimer", "arc ARC Reclaimer (type Courier)");
    CHECK(D(Cat::Arc, "DA_EnemyType_RollBot") == "ARC Surveyor", "arc ARC Surveyor (type RollBot, exact match: not Pop or Fireball)");
    CHECK(D(Cat::Arc, "DA_EnemyType_SnitchDrone_Onboarding") == "Snitch", "arc Snitch (tutorial type)");
    CHECK(D(Cat::Arc, "DA_EnemyType_Chonk") == "Bastion" && D(Cat::Arc, "DA_EnemyType_ChonkPlatform_Mortar") == "Bombardier", "arc Bastion and Bombardier told apart");
    CHECK(D(Cat::Arc, "DA_EnemyType_Peppermint") == "Queen", "arc Queen (type Peppermint)");
    CHECK(D(Cat::Arc, "DA_EnemyType_TestOnly_Walker") == "Test Only Walker", "arc fallback spaced (title case)");
    CHECK(D(Cat::Container, "BP_SocketContainer_Gvt_BinTrash_01_A_C") == "Trash Can", "container trash 1");
    CHECK(D(Cat::Container, "BP_SocketContainer_01_TrashBin_A_01_C") == "Trash Can", "container trash 2");
    CHECK(D(Cat::Container, "BP_SocketContainer_CUL_MuseumCrate_01_A_C") == "Museum Crate", "container museum crate");
    CHECK(D(Cat::Container, "BP_SocketContainer_Raider_AmmoBox_01_Lid_A_Dynamic_C") == "Ammo Box", "container ammo box");
    CHECK(D(Cat::Container, "BP_SocketContainer_Sct_LockerMedical_01_A_C") == "Locker", "container medical locker (shown as Locker)");
    CHECK(D(Cat::Container, "BP_SocketContainer_Sct_Locker_01_A_C") == "Locker", "container locker");
    CHECK(D(Cat::Container, "BP_SocketContainer_Raider_FruitBasket_01_Dynamic_C") == "Basket of Fruit", "container basket of fruit (type FruitBasket)");
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
    CHECK(D(Cat::Item, "DA_Item_BattleRifle_EnergyBurst_01") == "Aphelion", "gun Aphelion");
    CHECK(D(Cat::Item, "DA_Item_HealingHoT_Small") == "Bandage", "item Bandage (real asset name)");
    CHECK(D(Cat::Item, "DA_Item_Salvage_Chonk_Part") == "Bastion Cell", "item Bastion Cell");
    CHECK(D(Cat::Item, "DA_Item_Salvage_Trinket_RubberDuck_Standard") == "Rubber Duck", "item Rubber Duck");
    CHECK(D(Cat::Item, "DA_Item_Salvage_Trinket_BoatModel_C") == "\"Leviathan's Crown\" Ship Model", "item whose name ends in _C");
    CHECK(D(Cat::Item, "DA_Item_WeaponMod_Muzzle_Silencer_02") == "Silencer II", "weapon mod keeps its level");
    CHECK(D(Cat::Item, "DA_Item_HEGrenade") == "Heavy Fuze Grenade", "grenade Heavy Fuze Grenade");
    CHECK(D(Cat::Item, "DA_Item_CrashPad") == "Crash Mat" && D(Cat::Item, "DA_Item_Recipe_CrashPadRecipe") == "Crash Mat Blueprint", "item and its blueprint");
    CHECK(D(Cat::Item, "DA_Item_Recipe_LauncherAntiARCSingleShot01Recipe") == "Rascal Blueprint", "a gun blueprint is not the gun");
    CHECK(D(Cat::Item, "DA_Item_Recipe_TestOnly_SMG_LowTier_01Recipe") != "Stitcher", "an unknown blueprint is not shown as its gun");
    CHECK(labels::isGun("SMG_Medium_01_Carbine") && !labels::isGun("Recipe_TestOnly_SMG_LowTier_01Recipe") && !labels::isGun("Augment_Combat_T1"), "isGun: a gun, not a blueprint or an augment");
    CHECK(D(Cat::Item, "DA_Item_Salvage_Planks") == "Planks", "item name after Salvage_");
    CHECK(D(Cat::Item, "DA_Item_Salvage_Trinket_BurntoutCandles") == "Burnt-out Candles", "item name after Salvage_Trinket_, game spelling");
    CHECK(D(Cat::Item, "DA_Item_IntegratedDefibrillator") == "Integrated Defibrillator", "item Integrated Defibrillator");

    //--- what kind a ground item is (the Loot tab's switches), from its key alone
    {
        using K = labels::Kind;
        auto kind = [](const char* k) { return labels::kindOf(k); };
        CHECK(kind("BattleRifle_BreachAction_01") == K::Gun && kind("DA_Item_Shotgun_PumpAction_01_Ascended") == K::Gun &&
              kind("BasicMeleeWeapon") == K::Gun, "kind: Ferro, an amplified Il Toro and the Pickaxe are guns");
        CHECK(kind("AssaultRifle_TestOnly_09") == K::Gun, "kind: a gun family not in the gun table is still a gun");
        CHECK(kind("Recipe_BattlerifleLight01Recipe") == K::Blueprint && kind("Recipe_LauncherAntiARCSingleShot01Recipe") == K::Blueprint,
              "kind: a gun's blueprint is a blueprint, not a gun");
        CHECK(kind("EnergyAmmo") == K::Ammo && kind("LightAmmo") == K::Ammo, "kind: ammo");
        CHECK(kind("WeaponMod_Muzzle_Silencer_02") == K::Mod && kind("WeaponMod_Tech_ProjectileSplitter") == K::Mod, "kind: gun mods");
        CHECK(kind("Augment_Combat_T1") == K::Augment, "kind: augment");
        CHECK(kind("ArmorHeavyBody") == K::Shield && kind("ArmorPack") == K::Meds, "kind: a body shield is a shield, the shield recharger is meds");
        CHECK(kind("HealingHoT_Small") == K::Meds && kind("Bandage_01") == K::Meds && kind("AdrenalinShot") == K::Meds &&
              kind("IntegratedShieldRepair") == K::Meds, "kind: bandages, adrenaline and shield repair are meds");
        CHECK(kind("HEGrenade") == K::Grenade && kind("Throwable_TetherGrenade") == K::Grenade && kind("LaserTrap_DotGrenade") == K::Grenade,
              "kind: grenades and traps");
        CHECK(kind("Deployable_JumpMine_Explosive") == K::Grenade && kind("TimedMine_Explosive") == K::Grenade,
              "kind: mines are grenades and mines, not gadgets");
        CHECK(kind("TetherGun") == K::Gadget && kind("MetalDetector_02") == K::Gadget && kind("CrashPad") == K::Gadget &&
              kind("Deployable_Barricade") == K::Gadget, "kind: gadgets");
        CHECK(kind("Salvage_FrozenTrailObservatoryKey_01") == K::Key && kind("Salvage_Quest_CollectorCircuitry") == K::Key &&
              kind("KeyItem_RaiderHatchKey") == K::Key && kind("Salvage_SecurityCode") == K::Key, "kind: keys and quest items");
        CHECK(kind("Salvage_CollectorKey_01") == K::Material && kind("Salvage_CollectorKeyComponent_02") == K::Material,
              "kind: the Collector Key and its parts are materials, not keys");
        CHECK(kind("Salvage_Chonk_Part") == K::Material && kind("Salvage_Mat_ARCMetal") == K::Material, "kind: materials");
        CHECK(kind("Salvage_Trinket_RubberDuck_Standard") == K::Valuable && kind("Salvage_RubberDuck_Winner01") == K::Valuable &&
              kind("Salvage_Mat_Junk3") == K::Valuable, "kind: trinkets are valuables");
        CHECK(kind("ESRAnalyzer_01") == K::Other && kind("") == K::Other, "kind: anything else is Other");
        CHECK(kind("DA_Item_BattleRifle_BreachAction_01") == K::Gun, "kind: the full asset name works too");
        CHECK(std::string(labels::kindName(K::Meds)) == "Meds" && std::string(labels::kindName(K::Grenade)) == "Grenades and mines" &&
              std::string(labels::kindName((K)-1)) == "Other", "kind names as the Loot tab shows them");

        using G = labels::Group;
        auto group = [](const char* c) { return labels::containerKind(c); };
        CHECK(group("BP_SocketContainer_Sct_LockerMedical_01_A_C") == G::Medical && group("BP_SocketContainer_Sct_Locker_01_A_C") == G::Lockers,
              "container group: a medical locker is Medical (checked before Locker), a plain locker is a locker");
        CHECK(group("BP_SocketContainer_Raider_AmmoBox_01_Lid_A_Dynamic_C") == G::Weapons && group("BP_SocketContainer_Gvt_BinTrash_01_A_C") == G::Trash,
              "container group: ammo box, trash can");
        CHECK(group("DA_LootContainer_InformationTerminal_01_Lid_01_A") == G::Tech && group("BP_SocketContainer_Res_Refrigerator_01_C") == G::Home &&
              group("BP_SocketContainer_Raider_FruitBasket_01_Dynamic_C") == G::Home, "container group: tech and home");
        CHECK(group("BP_SocketContainer_WaspHusk_01_C") == G::Arc && group("BP_SalvageContainer_GreatMullein_C") == G::Nature &&
              group("BP_SocketContainer_CUL_MuseumCrate_01_A_C") == G::Lockers, "container group: ARC husk, a plant, a museum crate");
        CHECK(group("BP_SocketContainer_Raider_RaiderCache_01_C") == G::Caches, "container group: a raider cache");
        CHECK(group("BP_SocketContainer_TestOnly_Statue_C") == G::Other && group("") == G::Other, "container group: anything else is Other");
        CHECK(std::string(labels::groupName(G::Arc)) == "ARC" && std::string(labels::groupName(G::Weapons)) == "Weapons and ammo",
              "group names as the Loot tab shows them");
    }

    //the owner's names (November 2026), by the game's real internal names
    const struct { Cat c; const char* key; const char* name; } owner[] = {
        {Cat::Arc, "DA_EnemyType_Rose", "Frigate Cannon"},
        {Cat::Container, "BP_SocketContainer_Raider_Backpack_Dynamic_C", "Worn Backpack"},
        {Cat::Container, "BP_SocketContainer_CandleBerries_C", "Candleberry Bush"},
        {Cat::Container, "DA_LootContainer_Res_Closet_01_A_Lid_01_A", "Residential Closet"},
        {Cat::Container, "DA_LootContainer_InformationTerminal_01_Lid_01_A", "Terminal"},
        {Cat::Container, "DA_LootContainer_Res_DrawerMedical_01_A", "Medical Drawer"},
        {Cat::Container, "BP_SocketContainer_Res_WhiteMedicalDrawer_01_C", "White Medical Drawer"}, //made-up name: the rule still wins
        {Cat::Container, "DA_LootContainer_Res_Refrigerator_01_Lid_01_A", "Refrigerator"},
        {Cat::Container, "BP_SalvageContainer_VolcanicRock_Temp_C", "Volcanic Rock"},
        {Cat::Container, "BP_SocketContainer_CrashedARCProbe_01_C", "Crashed ARC Probe"}, //wiki-only keys stay (made-up names)
        {Cat::Container, "BP_SocketContainer_WaspHusk_01_C", "Wasp Husk"},
        {Cat::Item, "DA_Item_BasicMeleeWeapon", "Pickaxe"},
        {Cat::Item, "DA_Item_WeaponMod_ShotgunMuzzle_Silencer_01", "Shotgun Silencer"},
        {Cat::Item, "DA_Item_WeaponMod_Tech_ProjectileSplitter", "Anvil Splitter"},
        {Cat::Item, "DA_Item_Salvage_FootballShoes_01_Green", "Colorful Shoes"},
        {Cat::Item, "DA_Item_Salvage_FootballShoes_01_Red", "Colorful Shoes"},
        {Cat::Item, "DA_Item_Salvage_FootballShoes_01_Silver", "Colorful Shoes"},
        {Cat::Item, "DA_Item_Salvage_DeadFlowerBouquet_01", "Dead Flowers"},
        {Cat::Item, "DA_Item_Salvage_BronzeMaskFragment_01", "Mask Fragment"},
        {Cat::Item, "DA_Item_Salvage_MajolicaTazza_01", "Fancy Plate"},
        {Cat::Item, "DA_Item_Salvage_Tarragon_Part", "Bully Fragmenter"},
        {Cat::Item, "DA_Item_Salvage_Meerkat_Part", "Salvage Meerkat Part"},  //not sure which Skulker part: cleaned name
        {Cat::Item, "DA_Item_Salvage_Meerkat_Part_Turret", "Skulker Targeter"},
        {Cat::Item, "DA_Item_Salvage_Trinket_BentClothesHanger", "Bent Clothes Hanger"},
        {Cat::Item, "DA_Item_Salvage_Trinket_BlownFuses", "Blown Fuses"},
        {Cat::Item, "DA_Item_Salvage_Trinket_FoodWrapper", "Food Wrapper"},
        {Cat::Item, "DA_Item_Salvage_Trinket_HuskLily", "Husk Lily"},
        {Cat::Item, "DA_Item_Salvage_Community_Event_TT1_Bunker_Key", "Hidden Bunker Key"},
        {Cat::Item, "DA_Item_Salvage_Quest_LibraryBook", "Library Book"},
        {Cat::Item, "DA_Item_Salvage_PurificationSystemBlueprint", "Purification System Blueprint"},
        {Cat::Item, "DA_Item_Salvage_PlasticTrash", "Plastic Trash"},
        {Cat::Item, "DA_Item_Salvage_RubberBand_01", "Rubber Bands"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailMat1", "Steel Cable"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailMat2", "Epoxy Bucket"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailMat3", "Insulation Roll"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailMechanical1", "Cable Stripper"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailMechanical2", "Hand Drill"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailMechanical3", "Laser Level"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailMechanical4", "Radial Press"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailTech1", "Mini Pump"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailTech2", "Calculator"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailTech3", "Differential Microphone"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailTech4", "Thermal Shaker"},
        {Cat::Item, "DA_Item_Salvage_AscensionMaterial1", "Propellant Valve Assembly"},
        {Cat::Item, "DA_Item_Salvage_AscensionMaterial2", "Pressure Regulator"},
        {Cat::Item, "DA_Item_Salvage_AscensionMaterial3", "Fluted Rotor Core"},
        {Cat::Item, "DA_Item_Salvage_AscensionMaterial4", "Pneumatic Actuator"},
        {Cat::Item, "DA_Item_Salvage_AscensionFragments", "Amplified Fragments"},
        {Cat::Item, "DA_Item_Salvage_CommonAscensionModule", "Amplification Module MK. I"},
        {Cat::Item, "DA_Item_Salvage_UncommonAscensionModule", "Amplification Module MK. II"},
        {Cat::Item, "DA_Item_Salvage_RareAscensionModule", "Amplification Module MK. III"},
        {Cat::Item, "DA_Item_Salvage_EpicAscensionModule", "Amplification Module MK. IV"},
        {Cat::Item, "DA_Item_Salvage_LegendaryAscensionModule", "Amplification Module MK. V"},
        {Cat::Item, "DA_Item_Salvage_Collector1", "Emperor Modulator"},
        {Cat::Item, "DA_Item_Salvage_CollectorKey_01", "Emperor Gateway Conduit"},
        {Cat::Item, "DA_Item_Salvage_CollectorKeyComponent_01", "ARC Plated Coupler"},
        {Cat::Item, "DA_Item_Salvage_CollectorKeyComponent_02", "ARC Insulated Coupler"},
        {Cat::Item, "DA_Item_Salvage_CollectorKeyComponent_03", "ARC Conductive Coupler"},
        {Cat::Item, "DA_Item_Salvage_Frigate1", "Frigate Inductor"},
        {Cat::Item, "DA_Item_Salvage_BrokenTransmitter", "Ruined Emperor Beacon"},
        {Cat::Item, "DA_Item_Transmitter", "Emperor Beacon"},
        {Cat::Item, "DA_Item_Salvage_ARCMemoryCard", "ARC Data Module"},
        {Cat::Item, "DA_Item_Salvage_ExodusBook", "Exodus Technical Handbook"},
        {Cat::Item, "DA_Item_Salvage_FrigateMemoryModule", "Frigate Diagnostic Node"},
        {Cat::Item, "DA_Item_Salvage_RaiderJournal", "Raider Logbook"},
        {Cat::Item, "DA_Item_Salvage_OldBook", "Battered Paperback"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailObservatoryKey_01", "Pendola Pass Observatory Key"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailRailYardKey_01", "Pendola Pass Railyard Key"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailStationKey_01", "Pendola Pass Train Station Key"},
        {Cat::Item, "DA_Item_Salvage_FrozenTrailVillageKey", "Pendola Pass Old Town Key"},
        {Cat::Item, "DA_Item_TetherGun", "Tether Launcher"},
        {Cat::Item, "DA_Item_MetalDetector_02", "Mountaineer's Detector"},
        {Cat::Item, "DA_Item_Throwable_TetherGrenade", "Yank Grenade"},
        {Cat::Item, "DA_Item_BasicCamera", "Basic Camera"},
        {Cat::Item, "DA_Item_Camera", "Camera"},  //not sure which camera: cleaned name
        {Cat::Item, "DA_Item_Salvage_Corrupted_ARC_Part_01", "Glitched ARC Power Converter"},
        {Cat::Item, "DA_Item_Salvage_Corrupted_ARC_Part_02", "Glitched ARC Transmitter"},
        {Cat::Item, "DA_Item_Salvage_Corrupted_ARC_Part_03", "Glitched ARC Phased Array"},
        {Cat::Item, "DA_Item_Salvage_Corrupted_ARC_Part_04", "Glitched ARC Light Ring"},
        {Cat::Item, "DA_Item_Salvage_Corrupted_ARC_Part_05", "Glitched ARC Circuitry"},
        {Cat::Item, "DA_Item_Salvage_RubberDuck_Winner01", "MRI Duck"},
        {Cat::Item, "DA_Item_Salvage_RubberDuck_Winner02", "Arcade Duck"},
        {Cat::Item, "DA_Item_Salvage_RubberDuck_Winner03", "Frosty Duck"},
        {Cat::Item, "DA_Item_IntegratedShieldRepair", "Integrated Shield Recharger"},
        {Cat::Item, "DA_Item_Salvage_Quest_CollectorCircuitry", "Unknown ARC Circuitry"},
        {Cat::Item, "DA_Item_Recipe_BattlerifleLight01Recipe", "Stiletto Blueprint"},
        {Cat::Item, "DA_Item_Recipe_Snubnose01Recipe", "Bantam Blueprint"},
        {Cat::Item, "DA_Item_Recipe_AdvancedCameraRecipe", "Advanced Camera Blueprint"},
        {Cat::Item, "DA_Item_Recipe_Banjo", "Banjo Blueprint"},
        {Cat::Item, "DA_Item_Recipe_CollectorKey", "Emperor Gateway Conduit Blueprint"},
        {Cat::Item, "DA_Item_Recipe_GrapplingHookRecipe", "Grappling Hook Blueprint"},
        {Cat::Item, "DA_Item_Recipe_TetherGunRecipe", "Tether Launcher Blueprint"},
        {Cat::Item, "DA_Item_Recipe_TetherGrenadeRecipe", "Yank Grenade Blueprint"},
        {Cat::Item, "DA_Item_Recipe_Recipe_Anvil", "Anvil Blueprint"}, //not caught by the stowed Anvil's key
        {Cat::Item, "DA_Item_HandCannon_DoubleAction_Snubnose_01", "Bantam"},
        {Cat::Item, "DA_Item_OutpostFurnitureRecipe_Res_DrawerMedical_01_A", "Outpost Furniture Recipe Res Drawer Medical"}, //Designs stay as they are
    };
    for (const auto& o : owner) CHECK(D(o.c, o.key) == o.name, o.key);
    //stowed guns (DA_WeaponVisuals_), and amplification modules are items, not amplified guns
    CHECK(labeler::itemName(labels::itemToken("DA_WeaponVisuals_HandCannon_01"), true) == "Anvil", "stowed Anvil (HandCannon_01)");
    CHECK(labeler::itemName(labels::itemToken("DA_WeaponVisuals_Special_SniperRifle_Railgun_01_Ascended_Default_A"), true) == "AMP Jupiter" &&
          labeler::itemName(labels::itemToken("DA_WeaponVisuals_Special_SniperRifle_Railgun_01_Ascended_Scoped_A"), true) == "AMP Jupiter", "stowed amplified Jupiter");
    CHECK(labeler::itemName("Salvage_CommonAscensionModule") == "Amplification Module MK. I" && labeler::itemName("Salvage_AscensionFragments") == "Amplified Fragments", "amplification items keep their names (no AMP)");

    //--- your own names, read from labels.txt (the overlay never writes it)
    std::string dir = testDir("cfgtest");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    labels::setDir(dir);
    CHECK(labels::display(Cat::Arc, "DA_EnemyType_TestOnly_Walker") == "Test Only Walker", "no labels.txt: built-in name");
    CHECK(labels::display(Cat::Player, "793381ba5d093c9f", "cooked") == "cooked", "player shows its game name");
    const std::string labelsFile = dir + "/labels.txt";
    const std::string labelsText = "\xEF\xBB\xBF" "arc\tDA_EnemyType_TestOnly_Walker\tSpider\n"
                                   "Container \t BP_SocketContainer_CUL_MuseumCrate_01_A_C \t Art crate \r\n"
                                   "# my own names\n\n"
                                   "arc DA_EnemyType_Tick Bug\n"          //spaces instead of TABs
                                   "monster\tDA_EnemyType_Tick\tBug\n";   //no such category
    { std::ofstream out(labelsFile, std::ios::binary); out << labelsText; }
    CHECK(labels::load() == 2, "labels.txt: the two bad lines are counted (a comment and a blank line are not)");
    CHECK(labels::display(Cat::Arc, "DA_EnemyType_Tick") == "Tick", "labels.txt: a bad line changes nothing");
    CHECK(labels::display(Cat::Arc, "DA_EnemyType_TestOnly_Walker") == "Spider", "own name wins (first line after a UTF-8 mark)");
    CHECK(labels::display(Cat::Container, "BP_SocketContainer_CUL_MuseumCrate_01_A_C") == "Art crate", "category in any case, spaces around the fields dropped");

    //"shown name = your name" lines (names not used further down, so the later checks keep the built-in names)
    { std::ofstream out(labelsFile, std::ios::binary | std::ios::app);
      out << "great  MULLEIN = Plant\n" " ESR Analyzer\t=\tScanner\r\n" "# Vaporizer = Note\n" "Museum Crate = Box\n"
             "rival = Buddy\n" "AAAABBBBCCCCDDDD = Friend\n" "no separator here\n" " = nameless\n"; }
    labels::load();
    CHECK(labels::display(Cat::Container, "BP_SalvageContainer_GreatMullein_C") == "Plant", "shown name: capitals and spaces do not matter");
    CHECK(labels::display(Cat::Item, "DA_Item_ESRAnalyzer_01") == "Scanner", "shown name: TABs around '=' are fine");
    CHECK(labels::display(Cat::Arc, "DA_EnemyType_HeavyDrone_Laser") == "Vaporizer", "a line starting with # is ignored");
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
    CHECK(e1.itemKind == (int)labels::Kind::Gun, "ground item Ferro is a gun (Loot tab)");
    CHECK(e1.itemKey == "BattleRifle_BreachAction_01" && e1.gameName == "Ferro", "ground item: its internal name and the game's name, for prices");
    //renamed in labels.txt (ESR Analyzer = Scanner): shown as Scanner, but the game's own name stays for prices
    addPoolName(0x2500, "DA_Item_ESRAnalyzer_01");
    u64 scanner = makeObject(0x1000, 0x1240, 0);
    put<u64>(scanner, 0x518, makeObject(0x400, 0x2500, 0));
    RenderEntity eScan = ent(Object::SEARCH, 5.0, scanner);
    labeler::refine(eScan); labeler::fill(eScan);
    CHECK(eScan.label == "Scanner" && eScan.gameName == "ESR Analyzer" && eScan.itemKey == "ESRAnalyzer_01",
          "a ground item renamed in labels.txt keeps the game's own name for prices");

    u64 bin = makeObject(0x1000, 0x12C0, 7);
    RenderEntity e2 = ent(Object::SEARCH, 10.0, bin);
    labeler::refine(e2); labeler::fill(e2);
    CHECK(e2.type == Object::SEARCH && e2.label == "Trash Can", "container named Trash can");
    CHECK(e2.itemKind == (int)labels::Group::Trash, "container Trash can is in the Trash group (Loot tab)");

    //a ground item whose asset cannot be read (yet): no name, and its kind is not known (-1, drawn as Other)
    RenderEntity eBare = ent(Object::SEARCH, 4.0, makeObject(0x1000, 0x1240, 0));
    labeler::refine(eBare); labeler::fill(eBare);
    CHECK(eBare.type == Object::PICKUP && eBare.label.empty() && eBare.itemKind == -1, "ground item not read yet: kind -1");

    //---- inventory lists named in offsets.txt
    addPoolName(0x2400, "DA_Item_Bandage_01");
    addPoolName(0x2440, "DA_Item_SMG_LowTier_01");
    u64 bandAsset = makeObject(0x400, 0x2400, 0), smgAsset = makeObject(0x400, 0x2440, 0);
    u64 invPawn = makeObject(0x1400, 0x2000, 3), invComp = makeObject(0x800, 0x2180, 0);
    put<u64>(invPawn, 0xCB0, invComp);
    u64 recs = alloc(0x20 * 4);
    put<u64>(recs, 0x10, bandAsset); put<u64>(recs, 0x30, smgAsset); put<u64>(recs, 0x50, bandAsset); //[3] stays empty
    put<u64>(invComp, 0x600, recs); put<int>(invComp, 0x608, 4);
    live::invArrays.clear();
    CHECK(labeler::inventoryOf(invPawn).empty(), "inventory: nothing is read when no list is named");
    live::InvArray listA; listA.off = 0x600; listA.stride = 0x20; listA.asset = 0x10; listA.category = "backpack";
    live::invArrays = {listA};
    auto rows = labeler::inventoryOf(invPawn);
    bool bandOk = false, smgOk = false;
    for (const ItemRow& r : rows) { bandOk |= r.name == "Bandage" && r.count == 2 && r.category == "backpack"; smgOk |= r.name == "Stitcher" && r.count == 1; }
    CHECK(rows.size() == 2 && bandOk && smgOk, "inventory: items are named, the same item is merged into one row with a count");
    bool keyOk = false;
    for (const ItemRow& r : rows) keyOk |= r.name == "Bandage" && r.key == "Bandage_01";
    CHECK(keyOk, "inventory: a row keeps the internal name as its key (for prices)");
    bool gameOk = false;
    for (const ItemRow& r : rows) gameOk |= r.key == "Bandage_01" && r.gameName == "Bandage";
    CHECK(gameOk, "inventory: a row keeps the game's own name too (for prices)");
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
    //the built-in held list (offsets.h): what is in their hands, without the gun EQUIPPED already shows
    live::reset();
    put<u64>(invPawn, 0xCB0, invComp);
    u64 held = alloc(0x40);
    put<u64>(held, 0x10, smgAsset); put<u64>(held, 0x30, bandAsset);
    put<u64>(invComp, 0x530, held); put<int>(invComp, 0x538, 2);
    const auto inHand = labeler::inventoryOf(invPawn);
    CHECK(inHand.size() == 1 && inHand[0].name == "Bandage" && inHand[0].category == "held", "inventory: the built-in list shows what they hold besides their gun");
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
    CHECK(e4.sub == "Rattler (held) + Stitcher 1", "player guns: held first, then stowed with its slot's level (0: level 1)");
    put<int>(inv, 0x3B0 + 0x38, 3);
    RenderEntity e4b = ent(Object::PLAYER, 50.0, makeObject(0x1400, 0x2000, 1));
    put<u64>(e4b.actor, 0x3E8, ps); put<u64>(e4b.actor, 0xCB0, inv);
    labeler::fill(e4b);
    put<int>(inv, 0x3B0 + 0x38, 7);
    RenderEntity e4c = ent(Object::PLAYER, 50.0, makeObject(0x1400, 0x2000, 1));
    put<u64>(e4c.actor, 0x3E8, ps); put<u64>(e4c.actor, 0xCB0, inv);
    labeler::fill(e4c);
    put<int>(inv, 0x3B0 + 0x38, 0);
    CHECK(e4b.sub == "Rattler (held) + Stitcher 4" && e4c.sub == "Rattler (held) + Stitcher",
          "player guns: slot level 3 is level 4; a number outside 0..3 shows no level");

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

    //an amplified gun: held upgraded Canto and a stowed Ascended Stitcher get AMP, and the name tables still find them
    addPoolName(0x2600, "DA_Item_SMG_Medium_01_Carbine");
    addPoolName(0x2640, "DA_WeaponVisuals_SMG_LowTier_01_Ascended_Default_A");
    u64 ampInv = makeObject(0x800, 0x2000, 0);
    put<u64>(ampInv, 0x3B0 + 0x18, makeObject(0x400, 0x2640, 0));
    put<int>(ampInv, 0x3B0 + 0x38, 2); //an amplified gun's slot number does not turn it into "Stitcher 3"
    u64 ampRec = alloc(0x40); put<u64>(ampRec, 0x0, heldActor); put<u64>(ampRec, 0x10, makeObject(0x400, 0x2600, 0));
    put<u64>(ampInv, 0x530, ampRec); put<int>(ampInv, 0x538, 1);
    u64 ampPawn = makeObject(0x1400, 0x2000, 1); put<u64>(ampPawn, 0x3E8, ps); put<u64>(ampPawn, 0xCB0, ampInv);
    RenderEntity eAmp = ent(Object::PLAYER, 50.0, ampPawn);
    labeler::fill(eAmp);
    CHECK(eAmp.sub == "AMP Canto (held) + AMP Stitcher", "player guns: amplified guns show AMP before the name");
    //the inventory list and ground items use the same names, but only a gun gets a level
    CHECK(labeler::itemName("SMG_Medium_01_Carbine") == "AMP Canto" && labeler::itemName("Shotgun_PumpAction_01_Ascended") == "AMP Il Toro", "inventory and ground: an amplified gun shows AMP");
    CHECK(labeler::itemName("Augment_Combat_T1") == "Combat Mk. 1", "an augment's _T1 is not a gun level");
    CHECK(labeler::itemName(labels::itemToken("DA_WeaponVisuals_Pistol_Silenced_01"), true, 2) == "Hairpin 2" &&
          labeler::itemName(labels::itemToken("DA_WeaponVisuals_SMG_LowTier_01_Ascended_Default_A"), true, 2) == "AMP Stitcher",
          "a stowed gun's slot level: Hairpin 2; an amplified one stays AMP");
    { std::ofstream out(labelsFile, std::ios::binary | std::ios::app); out << "item\tSMG_Medium_01_Carbine\tMy Canto\n" "AMP Il Toro = Bull\n"; }
    labels::load();
    CHECK(labeler::itemName("SMG_Medium_01_Carbine") == "My Canto", "labels.txt: a TAB line for the full internal key of an amplified gun still works");
    CHECK(labeler::itemName("Shotgun_PumpAction_01_Ascended") == "Bull", "labels.txt: 'AMP Il Toro = ...' works");
    CHECK(labels::kindOf("SMG_Medium_01_Carbine") == labels::Kind::Gun && labels::kindOf("Shotgun_PumpAction_01_Ascended") == labels::Kind::Gun,
          "labels.txt: a renamed gun is still a gun (the Loot tab's switches)");

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
    CHECK(e9.itemKind == (int)labels::Kind::Gun, "and its kind from the new asset");

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
