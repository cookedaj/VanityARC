//Tests for the hand-marked teammates file (utils/marks.h): save/load round trip and bad lines in teammates.txt.
#include "../utils/marks.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { std::printf("FAIL  %s\n", what); fails++; } else std::printf("PASS  %s\n", what); } while (0)

int main() {
    namespace fs = std::filesystem;
    //A throw-away folder under the system temp directory (never your real ~/.config/kwin-helper)
    const fs::path root = fs::temp_directory_path() / "kwinhelper_test_marks";
    std::error_code ec;
    fs::remove_all(root, ec);
    const std::string dir = (root / "sub").string(); //does not exist yet: save() must create it

    //Round trip
    marks::clear();
    marks::setMarked("793381ba5d093c9f", true);
    marks::setMarked("aaaa1111", true);
    marks::save(dir);
    CHECK(fs::exists(dir + "/teammates.txt"), "save writes teammates.txt (and creates the folder)");
    CHECK(!fs::exists(dir + "/teammates.txt.tmp"), "save leaves no .tmp file behind");
    marks::clear();
    CHECK(!marks::isMarked("aaaa1111"), "clear removes the marks from memory");
    marks::load(dir);
    CHECK(marks::isMarked("793381ba5d093c9f") && marks::isMarked("aaaa1111"), "load brings both marks back");
    CHECK(marks::all().size() == 2, "load brings back exactly the saved marks");

    //A file edited by hand on Windows: BOM, CRLF, blank lines, bad ids, no newline at the end
    const std::string id32(32, 'b'), id40(40, 'c');
    {
        std::ofstream f(dir + "/teammates.txt", std::ios::binary | std::ios::trunc);
        f << "\xEF\xBB\xBF" "first1\r\n"
          << "\r\n" << "\n"
          << "second2\r\n"
          << id32 << "\r\n"
          << id40 << "\n"
          << "has space\n"
          << " lead\n"
          << "tab\tid\n"
          << "last";
    }
    marks::clear();
    marks::load(dir);
    CHECK(marks::isMarked("first1"), "a byte-order mark before the first id is ignored");
    CHECK(marks::isMarked("second2"), "Windows line endings (CRLF) are read");
    CHECK(marks::isMarked(id32), "a 32-character id is kept");
    CHECK(marks::isMarked("last"), "the last id is read without a newline at the end");
    CHECK(!marks::isMarked(id40), "a 40-character id is rejected");
    CHECK(!marks::isMarked("has space") && !marks::isMarked(" lead") && !marks::isMarked("tab\tid"), "ids with spaces or tabs are rejected");
    CHECK(marks::all().size() == 4, "only the 4 valid ids are loaded (blank lines skipped)");

    //Bad ids can not be marked by hand either
    marks::setMarked("has space", true);
    marks::setMarked(id40, true);
    marks::setMarked("", true);
    CHECK(marks::all().size() == 4, "setMarked ignores empty, spaced and over-long ids");

    //No file yet: nothing loads and nothing crashes
    marks::clear();
    marks::load((root / "missing").string());
    CHECK(marks::all().empty(), "a missing teammates.txt loads no marks");

    marks::clear();
    fs::remove_all(root, ec);
    std::printf(fails ? "SOME TESTS FAILED\n" : "ALL TESTS PASSED\n");
    return fails ? 1 : 0;
}
