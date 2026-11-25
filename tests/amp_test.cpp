//Tests for gun levels (utils/amp.h): "Stitcher 4" for level 1 to 4, "AMP Il Toro" for amplified, the plain name otherwise.
#include "../utils/amp.h"

#include <cstdio>

static int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { std::printf("FAIL  %s\n", what); fails++; } else std::printf("PASS  %s\n", what); } while (0)

static bool is(const std::string& raw, const std::string& base, int level) {
    const amp::GunLevel g = amp::gunLevel(raw);
    if (g.base == base && g.level == level) return true;
    std::printf("      %s -> base '%s' level %d (expected '%s' %d)\n", raw.c_str(), g.base.c_str(), g.level, base.c_str(), level);
    return false;
}

int main() {
    CHECK(amp::gunLabel("Il Toro", 0) == "Il Toro", "a gun of unknown level keeps its name");
    CHECK(amp::gunLabel("Stitcher", 4) == "Stitcher 4", "level 4: the name, a space, the number");
    CHECK(amp::gunLabel("Anvil", 2) == "Anvil 2", "level 2");
    CHECK(amp::gunLabel("Stitcher", 1) == "Stitcher 1", "level 1");
    CHECK(amp::gunLabel("Il Toro", 5) == "AMP Il Toro", "amplified: AMP then the name");

    //no level in the name: the plain name stays (today's internal names have none)
    CHECK(is("DA_Item_SMG_LowTier_01", "DA_Item_SMG_LowTier_01", 0), "DA_Item gun: no level, and LowTier / _01 are not levels");
    CHECK(is("BP_WeaponActor_AssaultRifle_LowTier_01_C", "BP_WeaponActor_AssaultRifle_LowTier_01_C", 0), "weapon actor: no level");
    CHECK(is("DA_WeaponVisuals_SMG_LowTier_01", "DA_WeaponVisuals_SMG_LowTier_01", 0), "stowed visuals: no level");
    CHECK(is("DA_Item_BasicMeleeWeapon", "DA_Item_BasicMeleeWeapon", 0), "a gun with no number: no level");
    CHECK(is("Il Toro", "Il Toro", 0) && is("Stitcher", "Stitcher", 0), "shown names without a level");

    //the game's own format: a Roman numeral after a space
    CHECK(is("Stitcher IV", "Stitcher", 4), "Stitcher IV is level 4");
    CHECK(is("Stitcher I", "Stitcher", 1), "Stitcher I is level 1");
    CHECK(is("Il Toro III", "Il Toro", 3), "Il Toro III is level 3 (Il is not a numeral)");
    CHECK(is("Kettle iv", "Kettle", 4), "any case");
    CHECK(is("Il Toro V", "Il Toro", 5), "V (the wiki's amplified) is AMP");
    CHECK(is("Anvil 2", "Anvil", 2), "a digit after a space");
    CHECK(is("Stitcher Lvl 4", "Stitcher", 4) && is("Il Toro Tier IV", "Il Toro", 4) && is("Kettle Level 3", "Kettle", 3), "level words");

    //level tokens in internal names
    CHECK(is("DA_Redirector_Gun_T0", "DA_Redirector_Gun", 1), "_T0 is level I");
    CHECK(is("T_UI_Icon_Gun_T3", "T_UI_Icon_Gun", 4), "_T3 is level IV");
    CHECK(is("DA_Item_SMG_LowTier_01_Level_2", "DA_Item_SMG_LowTier_01", 2), "_Level_2");
    CHECK(is("DA_Item_SMG_LowTier_01_Tier4", "DA_Item_SMG_LowTier_01", 4), "_Tier4 in one token");
    CHECK(is("DA_Item_SMG_LowTier_01_Lvl_IV", "DA_Item_SMG_LowTier_01", 4), "_Lvl_IV");

    //amplified: the game's "Amplified <Gun>", the Ascended assets and the upgraded ones
    CHECK(is("Amplified Il Toro", "Il Toro", 5), "Amplified Il Toro");
    CHECK(is("DA_Item_Shotgun_PumpAction_01_Ascended", "DA_Item_Shotgun_PumpAction_01", 5), "DA_Item_<Gun>_Ascended");
    CHECK(is("DA_WeaponVisuals_SMG_LowTier_01_Ascended_Default_A", "DA_WeaponVisuals_SMG_LowTier_01", 5), "stowed Ascended visuals");
    CHECK(is("DA_WeaponVisuals_SMG_LowTier_01_Ascended_Scoped_A_01", "DA_WeaponVisuals_SMG_LowTier_01", 5), "stowed Ascended visuals ending _A_01");
    CHECK(is("DA_Item_Shotgun_PumpAction_01_MoreReload_SlugRounds", "DA_Item_Shotgun_PumpAction_01", 5), "upgraded DA_Item (two upgrades)");
    CHECK(is("DA_Item_SMG_Medium_01_Carbine", "DA_Item_SMG_Medium_01", 5), "upgraded DA_Item (one upgrade)");
    CHECK(is("BP_WeaponActor_BattleRifle_EnergyBurst_01_ChargedBurst_C", "BP_WeaponActor_BattleRifle_EnergyBurst_01", 5), "the one amplified weapon actor");
    CHECK(is("DA_WeaponVisuals_Launcher_AntiArc_Medium_01_MagReload_A", "DA_WeaponVisuals_Launcher_AntiArc_Medium_01", 5), "Hullcracker Mag Reload (no Ascended)");
    CHECK(is("DA_WeaponVisuals_SMG_LowTier_01_Default_A", "DA_WeaponVisuals_SMG_LowTier_01_Default_A", 0), "a plain Default look is not an upgrade");

    std::printf(fails ? "SOME TESTS FAILED\n" : "ALL TESTS PASSED\n");
    return fails ? 1 : 0;
}
