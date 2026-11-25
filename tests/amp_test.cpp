//Tests for the amplification module level (utils/amp.h): MK I to IV is the level, MK V is AMP.
#include "../utils/amp.h"

#include <cstdio>

static int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { std::printf("FAIL  %s\n", what); fails++; } else std::printf("PASS  %s\n", what); } while (0)

int main() {
    CHECK(amp::moduleLevel("Amplification Module MK. I") == 1, "MK. I is level 1");
    CHECK(amp::moduleLevel("Amplification Module MK. II") == 2, "MK. II is level 2");
    CHECK(amp::moduleLevel("Amplification Module MK. III") == 3, "MK. III is level 3");
    CHECK(amp::moduleLevel("Amplification Module MK. IV") == 4, "MK. IV is level 4");
    CHECK(amp::moduleLevel("Amplification Module MK. V") == 5, "MK. V is level 5 (AMP)");
    CHECK(amp::moduleLevel("amplification module mk 3") == 3, "lower case and arabic numbers work");
    CHECK(amp::moduleLevel("Amplification Module MK VI") == 0, "an unknown level is 0");
    CHECK(amp::moduleLevel("Duct Tape") == 0, "other items are 0");
    CHECK(amp::moduleLevel("Amplified Il Toro") == 0, "an amplified gun is not a module");

    CHECK(amp::gunLabel("Il Toro", 0) == "Il Toro", "a plain gun keeps its name");
    CHECK(amp::gunLabel("Il Toro", 5) == "AMP Il Toro", "MK V shows AMP then the gun name");
    CHECK(amp::gunLabel("Il Toro", 3) == "Il Toro  MK III", "MK III shows the level after the name");
    CHECK(amp::gunLabel("Il Toro", 1) == "Il Toro  MK I", "MK I shows the level");

    CHECK(std::string(amp::levelText(3)) == "MK III", "level text for MK III");
    CHECK(std::string(amp::levelText(5)) == "AMP", "level text for MK V is AMP");
    CHECK(std::string(amp::levelText(0)) == "", "no module has no level text");

    std::printf(fails ? "SOME TESTS FAILED\n" : "ALL TESTS PASSED\n");
    return fails ? 1 : 0;
}
