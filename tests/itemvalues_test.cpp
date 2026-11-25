//Tests for the item sell values (utils/itemvalues.cpp): parsing, lookups and unpriced items.
#include "../utils/itemvalues.h"

#include <cstdio>

static int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { std::printf("FAIL  %s\n", what); fails++; } else std::printf("PASS  %s\n", what); } while (0)

int main() {
    itemvalues::parse(
        "# a comment line\n"
        "\n"
        "Snap Hook | 14000\n"
        "\"Leviathan's Crown\" Ship Model | 10000\n"
        "Lance's Mixtape (5th Edition) | 10000\n"
        "Light Ammo | 4\n"
        "Broken | notanumber\n"
        "Negative | -5\n"
        "No Separator\n");

    CHECK(itemvalues::valueOf("Snap Hook") == 14000, "a listed item has its value");
    CHECK(itemvalues::valueOf("snap hook") == 14000, "lookup ignores case");
    CHECK(itemvalues::valueOf("Lance's Mixtape (5th edition)") == 10000, "punctuation and case in the name do not matter");
    CHECK(itemvalues::valueOf("\"Leviathan's Crown\" Ship Model") == 10000, "quoted names parse");
    CHECK(itemvalues::valueOf("Light Ammo") == 4, "a small value parses");
    CHECK(itemvalues::valueOf("Unknown Thing") == -1, "an unlisted item is unpriced (-1)");
    CHECK(itemvalues::valueOf("Broken") == -1, "a non-number value is skipped");
    CHECK(itemvalues::valueOf("Negative") == -1, "a negative value is skipped");
    CHECK(itemvalues::valueOf("No Separator") == -1, "a line without | is skipped");
    CHECK(itemvalues::count() == 4, "count is the number of usable price lines");

    itemvalues::parse("Snap Hook | 1");
    CHECK(itemvalues::valueOf("Snap Hook") == 1, "parse replaces the table");
    CHECK(itemvalues::valueOf("Light Ammo") == -1, "old entries are gone after parse");

    itemvalues::clear();
    CHECK(itemvalues::valueOf("Snap Hook") == -1, "clear empties the table");
    CHECK(itemvalues::count() == 0, "no prices loaded: count is 0");

    std::printf(fails ? "SOME TESTS FAILED\n" : "ALL TESTS PASSED\n");
    return fails ? 1 : 0;
}
