//offsets.txt: what the probes find is read back safely.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "utils/offsets.h"

static int fails = 0;
#define CHECK(cond, msg) do { bool ok_ = (cond); std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) fails++; } while (0)

static void writeFile(const std::string& dir, const std::string& text) {
    std::filesystem::create_directories(dir);
    std::ofstream(live::path(dir), std::ios::trunc) << text;
}

int main() {
    const std::string dir = (std::filesystem::temp_directory_path() / "kwinhelper_test_offsets").string();
    std::filesystem::remove_all(dir);

    live::load(dir); //no file
    CHECK(live::shieldComponent == -1 && live::shieldValue == 0x1D0 && live::shieldMax == 0x1D8 && !live::shieldIsFloat &&
          live::invArrays.size() == 1 && live::invArrays[0].off == 0x530 && live::invArrays[0].stride == 0x20 && live::invArrays[0].asset == 0x10,
          "no file: the built-in shield and held-item offsets");

    writeFile(dir, "# found with the probe\nshield_component -1\nshield_value 0x710   # next to health\nshield_max 0x718\nshield_is_float 0\n"
                   "inv_array 0x530 0x20 0x10 item\ninv_array_in 0x700 0x40 0x20 0x10 pockets\n");
    live::load(dir);
    CHECK(live::shieldComponent == -1 && live::shieldValue == 0x710 && live::shieldMax == 0x718 && live::shieldIsFloat == 0, "shield numbers are read (hex, comments allowed)");
    CHECK(live::invArrays.size() == 2 && live::invArrays[0].off == 0x530 && live::invArrays[0].comp == -1 && live::invArrays[0].category == "item", "an inventory list is read");
    CHECK(live::invArrays[1].comp == 0x700 && live::invArrays[1].off == 0x40 && live::invArrays[1].stride == 0x20 && live::invArrays[1].asset == 0x10 && live::invArrays[1].category == "pockets",
          "a list in another component is read with its component");
    CHECK(live::invArrays[0].qty == -1 && live::invArrays[1].qty == -1, "no stack size offset: not known");

    live::reset();
    writeFile(dir, "inv_array 0x530 0x20 0x10 0x18 pockets\ninv_array_in 0x700 0x40 0x20 0x10 0x1E\ninv_array 0x540 0x20 0x10 0x18\n");
    live::load(dir);
    CHECK(live::invArrays.size() == 3 && live::invArrays[0].qty == 0x18 && live::invArrays[0].category == "pockets" &&
          live::invArrays[1].qty == -1 && live::invArrays[1].category == "item" && live::invArrays[2].qty == 0x18 && live::invArrays[2].category == "item",
          "a stack size offset may follow the asset offset; one outside the record is ignored, the list is kept");

    live::reset();
    writeFile(dir, "inv_array 0x530 0x4000 0x10\ninv_array 0x530 0x20 0x40\ninv_array_in 0x10 0x40 0x20 0x10\ninv_array_in 0x5000 0x40 0x20 0x10\n"
                   "inv_array -8 0x20 0x10\nshield_value banana\nbogus 5\ninv_array 0x530\n\\x01\\x02 junk\n");
    live::load(dir);
    CHECK(live::invArrays.size() == 1 && live::invArrays[0].category == "held", "absurd strides, asset outside the record, components outside the pawn, negative offsets and short lines are all refused: the built-in list stays");
    CHECK(live::shieldValue == 0x1D0 && live::shieldMax == 0x1D8, "a shield value that is not a number is ignored");

    live::reset();
    writeFile(dir, "      if this is the shield:  shield_component 0x500  shield_value 0x100  shield_is_float 1\n"
                   "      offsets.txt:  inv_array 0x530 0x20 0x10 item\n"); //the probes' ready lines, pasted as they are
    live::load(dir);
    CHECK(live::shieldComponent == 0x500 && live::shieldValue == 0x100 && live::shieldIsFloat == 1, "the shield probe's ready line is read as it is printed");
    CHECK(live::invArrays.size() == 1 && live::invArrays[0].off == 0x530 && live::invArrays[0].stride == 0x20 && live::invArrays[0].asset == 0x10,
          "the inventory probe's ready line is read as it is printed");

    live::reset();
    writeFile(dir, "shield_value 0710\nshield_component 08\nshield_max 0x718zz\ninv_array 0x530 0x20 16q item\n");
    live::load(dir);
    CHECK(live::shieldValue == 710 && live::shieldComponent == 8, "a leading 0 is decimal, never octal");
    CHECK(live::shieldMax == -1 && live::invArrays.size() == 1 && live::invArrays[0].category == "held",
          "a number with junk after it is refused, and a shield set without a good maximum gets none");

    live::reset();
    writeFile(dir, "shield_value -1\n");
    live::load(dir);
    CHECK(live::shieldValue == -1 && live::shieldMax == -1, "shield_value -1 turns the shield bar off");

    live::reset();
    std::string many;
    for (int i = 0; i < 40; i++) many += "inv_array " + std::to_string(0x530 + i * 0x10) + " 0x20 0x10 item\n";
    writeFile(dir, many);
    live::load(dir);
    CHECK(live::invArrays.size() == 16, "no more than 16 lists are accepted");

    live::reset();
    writeFile(dir, "inv_array 0x530 0x20 0x10 item\ninv_array 0x530 0x20 0x10 item\ninv_array_in 0xCB0 0x530 0x20 0x10 item\n"
                   "inv_array_in 0x700 0x40 0x20 0x10 pockets\ninv_array_in 0x700 0x40 0x20 0x10 pockets\n");
    live::load(dir);
    CHECK(live::invArrays.size() == 2 && live::invArrays[0].comp == -1 && live::invArrays[0].off == 0x530 && live::invArrays[1].comp == 0x700,
          "the same list given twice is read once, so items are not counted twice");

    std::filesystem::remove_all(dir);
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
