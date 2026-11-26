//Tests for the item sell values (utils/itemvalues.cpp): parsing, lookups and unpriced items.
#include "../utils/itemvalues.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

static int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { std::printf("FAIL  %s\n", what); fails++; } else std::printf("PASS  %s\n", what); } while (0)

int main() {
    const int ignored = itemvalues::parse(
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
    CHECK(ignored == 3, "the bad lines are counted as ignored (comments and blank lines are not)");

    CHECK(itemvalues::parse("Snap Hook | 1") == 0, "a good file ignores nothing");
    CHECK(itemvalues::valueOf("Snap Hook") == 1, "parse replaces the table");
    CHECK(itemvalues::valueOf("Light Ammo") == -1, "old entries are gone after parse");
    CHECK(itemvalues::parse("Light Ammo | 1,23\n| 5\nName |\n") == 3, "a wrong thousands group, no name, no number: each line counted");

    //names in another script (your own names from labels.txt) stay apart; a name of signs only is ignored, not a price
    //for every such name. A no-break space or a curly quote counts as little as a space or a straight quote.
    CHECK(itemvalues::parse("Бинт | 500\nАптечка | 20000\n*** | 9000\n") == 1, "a name of signs only is ignored");
    CHECK(itemvalues::valueOf("Бинт") == 500 && itemvalues::valueOf("Аптечка") == 20000 && itemvalues::valueOf("Ферро") == -1 &&
          itemvalues::valueOf("\xE2\x98\x85") == -1 && itemvalues::valueOf("") == -1,
          "Cyrillic names: each its own price; an unlisted one, a star and an empty name have none");
    CHECK(itemvalues::parse("Snap\xC2\xA0Hook | 14000\nLance\xE2\x80\x99s Mixtape | 10000\n") == 0 && itemvalues::valueOf("Snap Hook") == 14000 &&
          itemvalues::valueOf("Lance's Mixtape") == 10000, "a no-break space and a curly quote match a plain space and quote");

    //prices written with thousands separators or with k and m
    CHECK(itemvalues::parse("A | 14,000\nB | 14 000\nC | 14k\nD | 1.5k\nE | 2m\nF | 2M\nG | 1,234,567\nH | +500\nI | 1.25 K\nJ | 007\n") == 0,
          "14,000  14 000  14k  1.5k  2m  2M  1,234,567  +500  1.25 K  007: all read");
    CHECK(itemvalues::valueOf("A") == 14000 && itemvalues::valueOf("B") == 14000 && itemvalues::valueOf("C") == 14000,
          "14,000, 14 000 and 14k are 14000");
    CHECK(itemvalues::valueOf("D") == 1500 && itemvalues::valueOf("E") == 2000000 && itemvalues::valueOf("F") == 2000000,
          "1.5k is 1500, 2m and 2M are 2000000");
    CHECK(itemvalues::valueOf("G") == 1234567 && itemvalues::valueOf("H") == 500 && itemvalues::valueOf("I") == 1250 && itemvalues::valueOf("J") == 7,
          "1,234,567, +500, 1.25 K and 007 read as written");
    CHECK(itemvalues::parse("A | 1,2345\nB | 12,34\nC | ,500\nD | 500,\nE | 1.5\nF | 14.000\nG | k\nH | 1.k\nI | 1x\nJ | 1,000 000\n"
                            "K | 99999999999999\nL | 5000000m\nM | -5k\n") == 13,
          "groups not of three, a decimal without k or m, a bare k, junk, too big and negative: each line ignored");
    CHECK(itemvalues::count() == 0 && itemvalues::valueOf("F") == -1, "14.000 is not taken for fourteen thousand (nor 14)");
    CHECK(itemvalues::grouped(0) == "0" && itemvalues::grouped(999) == "999" && itemvalues::grouped(1000) == "1,000" &&
          itemvalues::grouped(145000) == "145,000" && itemvalues::grouped(1234567) == "1,234,567" && itemvalues::grouped(-1500) == "-1,500",
          "thousands separators: 0, 999, 1,000, 145,000, 1,234,567, -1,500");

    itemvalues::clear();
    CHECK(itemvalues::valueOf("Snap Hook") == -1, "clear empties the table");
    CHECK(itemvalues::count() == 0, "no prices loaded: count is 0");

    //values.txt is read again when it is saved, added or removed (no restart)
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "kwinhelper_test_itemvalues";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const fs::path file = dir / "values.txt";
        auto save = [&](const char* text, int ageSec) {
            { std::ofstream f(file); f << text; }
            fs::last_write_time(file, fs::file_time_type::clock::now() - std::chrono::seconds(ageSec)); //each save a distinct time
        };
        itemvalues::setDir(dir.string());
        itemvalues::load();
        CHECK(itemvalues::count() == 0, "no values.txt: no prices");
        save("Snap Hook | 100\n", 30);
        itemvalues::reloadIfChanged(0);
        CHECK(itemvalues::valueOf("Snap Hook") == 100, "a values.txt created while running is read");
        save("Snap Hook | 200\n", 20);
        itemvalues::reloadIfChanged(60000);
        CHECK(itemvalues::valueOf("Snap Hook") == 100, "the file is not checked again before the gap is up");
        itemvalues::reloadIfChanged(0);
        CHECK(itemvalues::valueOf("Snap Hook") == 200, "an edited price shows at the next check");
        itemvalues::setValueForTest("Snap Hook", 5);
        itemvalues::reloadIfChanged(0);
        CHECK(itemvalues::valueOf("Snap Hook") == 5, "an unchanged file is not read again");
        fs::remove(file);
        itemvalues::reloadIfChanged(0);
        CHECK(itemvalues::count() == 0, "removing values.txt clears the prices");
        fs::remove_all(dir);
    }

    std::printf(fails ? "SOME TESTS FAILED\n" : "ALL TESTS PASSED\n");
    return fails ? 1 : 0;
}
