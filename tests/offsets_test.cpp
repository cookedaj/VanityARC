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
    const std::string dir = (std::filesystem::temp_directory_path() / "vanityarc_test_offsets").string();
    std::filesystem::remove_all(dir);

    live::load(dir); //no file
    CHECK(live::shieldValue == -1 && live::invArrays.empty(), "no file: nothing is known, the features stay off");

    writeFile(dir, "# found with the probe\nshield_component -1\nshield_value 0x710   # next to health\nshield_max 0x718\nshield_is_float 0\n"
                   "inv_array 0x530 0x20 0x10 item\ninv_array_in 0x700 0x40 0x20 0x10 pockets\n");
    live::load(dir);
    CHECK(live::shieldComponent == -1 && live::shieldValue == 0x710 && live::shieldMax == 0x718 && live::shieldIsFloat == 0, "shield numbers are read (hex, comments allowed)");
    CHECK(live::invArrays.size() == 2 && live::invArrays[0].off == 0x530 && live::invArrays[0].comp == -1 && live::invArrays[0].category == "item", "an inventory list is read");
    CHECK(live::invArrays[1].comp == 0x700 && live::invArrays[1].off == 0x40 && live::invArrays[1].stride == 0x20 && live::invArrays[1].asset == 0x10 && live::invArrays[1].category == "pockets",
          "a list in another component is read with its component");

    live::reset();
    writeFile(dir, "inv_array 0x530 0x4000 0x10\ninv_array 0x530 0x20 0x40\ninv_array_in 0x10 0x40 0x20 0x10\ninv_array_in 0x5000 0x40 0x20 0x10\n"
                   "inv_array -8 0x20 0x10\nshield_value banana\nbogus 5\ninv_array 0x530\n\\x01\\x02 junk\n");
    live::load(dir);
    CHECK(live::invArrays.empty(), "absurd strides, asset outside the record, components outside the pawn, negative offsets and short lines are all refused");
    CHECK(live::shieldValue == -1, "a shield value that is not a number is ignored");

    live::reset();
    std::string many;
    for (int i = 0; i < 40; i++) many += "inv_array 0x530 0x20 0x10 item\n";
    writeFile(dir, many);
    live::load(dir);
    CHECK(live::invArrays.size() == 16, "no more than 16 lists are accepted");

    std::filesystem::remove_all(dir);
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
