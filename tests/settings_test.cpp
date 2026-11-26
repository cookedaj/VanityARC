//Saved settings: every value stays inside its safe range, damaged files do no harm, Reset restores the defaults.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include "utils/settings.h"

static int fails = 0;
#define CHECK(cond, msg) do { bool ok_ = (cond); std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) fails++; } while (0)

static std::string testDir(const char* name) {
    return (std::filesystem::temp_directory_path() / (std::string("kwinhelper_test_") + name)).string();
}
static void writeFile(const std::string& dir, const std::string& text) {
    std::filesystem::create_directories(dir);
    std::ofstream(settings::path(dir), std::ios::trunc) << text;
}

int main() {
    const std::string dir = testDir("settings");
    std::filesystem::remove_all(dir);

    settings::load(dir); //no file: the defaults, and they are captured now
    CHECK(settings::table().size() > 40, "the settings table is filled");
    CHECK(!config::showItemValues, "Show item values starts off");
    CHECK(config::showLootGuns && config::showLootMeds && config::showLootOther && config::showContainerMedical && config::showContainerOther &&
          !config::colorLootKinds && !config::highlightValuable && config::valuableFrom == 10000,
          "Loot tab: every kind and group starts on, colours by kind and the valuable highlight start off, valuable from 10000");

    std::set<std::string> names;
    bool unique = true, defaultsInRange = true;
    for (size_t i = 0; i < settings::table().size(); i++) {
        const settings::Entry& e = settings::table()[i];
        unique &= names.insert(e.name).second;
        const double d = settings::defaults()[i];
        defaultsInRange &= d >= e.lo && d <= e.hi;
    }
    CHECK(unique, "every setting has its own name");
    CHECK(defaultsInRange, "every default is inside its own range");

    //a damaged / hand-edited file
    writeFile(dir, "maxPlayerDist 99999\nboxStyle -3\nboxThickness nan\noverlayOpacity 0\nlistRows 1\nbogusName 5\nshowPlayers 0\n"
                   "trackerHz abc\nlabelSize 1e300\nmenuOpacity inf\n\x01\x02 garbage line\n\n   \n# a comment\nshowArc true\r\nstreamHide 1\n");
    const int ignored = settings::load(dir);
    //nan, bogusName, abc, inf, the garbage line, "true": ignored. Clamped values still count as used; blank lines, comments
    //and the retired streamHide line are not errors.
    CHECK(ignored == 6, "ignored lines are counted");
    CHECK(config::maxPlayerDist == 800, "too big: pulled down to the largest allowed value");
    CHECK(config::boxStyle == 0, "negative: pulled up to the smallest allowed value");
    CHECK(config::overlayOpacity == 0.2f, "opacity 0 is raised to 0.2 (the overlay can never be made invisible by accident)");
    CHECK(config::listRows == 3, "list rows below the minimum");
    CHECK(config::showPlayers == false, "a normal line still works");
    CHECK(config::boxThickness == 1.5f && config::trackerHz == 120, "text that is not a number leaves the default alone");
    CHECK(config::labelSize == 40.0f, "absurdly large number: the largest allowed value (Text size 40)");
    CHECK(config::menuOpacity == 0.94f, "infinity is ignored");

    //save and load again: the same values come back
    config::maxArcDist = 222; config::boxColor = 4; config::monochrome = true; config::labelCards = true; config::overlayOpacity = 0.7f; config::trackerHz = 90;
    config::showItemValues = true;
    config::barStyle = 2; config::barNumbers = true;
    config::showLootGuns = false; config::showLootValuables = false; config::showContainerTrash = false;
    config::colorLootKinds = true; config::highlightValuable = true; config::valuableFrom = 25000;
    config::headMarker = 0; config::skeletonColor = 2; config::boxStyle = 3;
    config::menuTextSize = 27; config::labelSize = 38;
    settings::save(dir);
    config::maxArcDist = 50; config::boxColor = 0; config::monochrome = false; config::labelCards = false; config::overlayOpacity = 1.0f; config::trackerHz = 240;
    config::showItemValues = false;
    config::barStyle = 0; config::barNumbers = false;
    config::showLootGuns = true; config::showLootValuables = true; config::showContainerTrash = true;
    config::colorLootKinds = false; config::highlightValuable = false; config::valuableFrom = 10000;
    config::headMarker = 1; config::skeletonColor = 0; config::boxStyle = 0;
    config::menuTextSize = 15; config::labelSize = 15;
    CHECK(settings::load(dir) == 0, "a file the overlay saved has no ignored lines");
    CHECK(config::menuTextSize == 27.0f && config::labelSize == 38.0f, "Menu text size and a Text size above the old 28 are saved");
    CHECK(config::headMarker == 0 && config::skeletonColor == 2 && config::boxStyle == 3, "head marker, skeleton colour and the Skeleton only box style are saved");
    CHECK(config::maxArcDist == 222 && config::boxColor == 4 && config::monochrome && config::labelCards && std::abs(config::overlayOpacity - 0.7f) < 1e-6 && config::trackerHz == 90,
          "saved values come back after loading");
    CHECK(config::showItemValues, "Show item values is saved");
    CHECK(config::barStyle == 2 && config::barNumbers, "bar style and bar numbers are saved");
    CHECK(!config::showLootGuns && !config::showLootValuables && config::showLootAmmo && !config::showContainerTrash && config::showContainerHome,
          "Loot tab: the kinds and groups switched off are saved, the others stay on");
    CHECK(config::colorLootKinds && config::highlightValuable && config::valuableFrom == 25000, "colours by kind and the valuable highlight are saved");
    CHECK(!std::filesystem::exists(settings::path(dir) + ".tmp"), "no half-written file is left behind");

    //reset everything
    settings::resetAll();
    bool same = true;
    for (size_t i = 0; i < settings::table().size(); i++) same &= settings::value(settings::table()[i]) == settings::defaults()[i] || std::abs(settings::value(settings::table()[i]) - settings::defaults()[i]) < 1e-6;
    CHECK(same, "Reset restores every default");
    CHECK(!config::showItemValues, "Reset turns Show item values off");
    CHECK(config::barStyle == 0 && !config::barNumbers, "Reset: health and shield side by side, no numbers");
    CHECK(config::showLootGuns && config::showLootValuables && config::showContainerTrash && !config::colorLootKinds &&
          !config::highlightValuable && config::valuableFrom == 10000, "Reset: every kind and group on, no kind colours, no valuable highlight");
    CHECK(config::headMarker == 1 && config::skeletonColor == 0 && config::boxStyle == 0, "Reset: a filled head marker, the skeleton in the box colour, corner boxes");
    CHECK(config::menuTextSize == 15.0f && config::labelSize == 15.0f, "Reset: menu text and text size back to 15 px");

    //only some settings back to their defaults (Reset calibration): the others keep their values
    config::fovScale = 1.5f; config::centerOffsetX = -40; config::fovIsHorizontal = true; config::boxThickness = 7;
    settings::reset({&config::fovScale, &config::centerOffsetX, &config::fovIsHorizontal});
    CHECK(config::fovScale == 1.0f && config::centerOffsetX == 0.0f && !config::fovIsHorizontal && config::boxThickness == 7.0f,
          "settings::reset puts only the named settings back to their defaults");
    config::boxThickness = 1.5f;

    //a menu text size outside its range in the file is pulled inside (never below the 15 px default)
    writeFile(dir, "menuTextSize 4\n");
    settings::load(dir);
    CHECK(config::menuTextSize == 15.0f, "menu text size 4 in the file: 15 px");
    writeFile(dir, "menuTextSize 99\n");
    settings::load(dir);
    CHECK(config::menuTextSize == 30.0f, "menu text size 99 in the file: 30 px");
    config::menuTextSize = 15;

    //a value that left its range by a bug is brought back
    config::fovScale = 9.0f; config::listMode = -4;
    settings::clampAll();
    CHECK(config::fovScale == 2.0f && config::listMode == 0, "clampAll brings stray values back inside their range");

    std::filesystem::remove_all(dir);
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
