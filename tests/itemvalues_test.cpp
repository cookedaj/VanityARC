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
    CHECK(itemvalues::parse("Light Ammo | 1,234\n| 5\nName |\n") == 3, "thousands commas, no name, no number: each line counted");

    CHECK(itemvalues::parse("Snap Hook | 1") == 0, "a good file ignores nothing");
    CHECK(itemvalues::valueOf("Snap Hook") == 1, "parse replaces the table");
    CHECK(itemvalues::valueOf("Light Ammo") == -1, "old entries are gone after parse");

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
