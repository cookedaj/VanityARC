#include "itemvalues.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace itemvalues {

namespace {

std::map<std::string, long>& table() {
    static std::map<std::string, long> t;
    return t;
}

//write time of values.txt when it was last read (min(): it was not there)
std::filesystem::file_time_type& loadedTime() {
    static std::filesystem::file_time_type t = std::filesystem::file_time_type::min();
    return t;
}

std::string& dirOverride() {
    static std::string d;
    return d;
}

std::string configDir() {
    if (!dirOverride().empty()) return dirOverride();
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/kwin-helper";
}

//Lookup key: letters and digits only, lower case, so "Lance's Mixtape (5th edition)" and "(5th Edition)" match
std::string norm(const std::string& s) {
    std::string out;
    for (unsigned char c : s) if (std::isalnum(c)) out.push_back((char)std::tolower(c));
    return out;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

} // namespace

long valueOf(const std::string& name) {
    auto it = table().find(norm(name));
    return it == table().end() ? -1 : it->second;
}

size_t count() { return table().size(); }

int parse(const std::string& text) {
    table().clear();
    std::istringstream in(text);
    std::string line;
    int lineNo = 0, ignored = 0;
    while (std::getline(in, line)) {
        lineNo++;
        if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3); //UTF-8 byte-order mark (some Windows editors add one)
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t bar = line.rfind('|');
        std::string name = bar == std::string::npos ? "" : trim(line.substr(0, bar));
        std::string num = bar == std::string::npos ? "" : trim(line.substr(bar + 1));
        char* end = nullptr;
        long v = num.empty() ? -1 : std::strtol(num.c_str(), &end, 10);
        if (name.empty() || num.empty() || end == num.c_str() || *end != '\0' || v < 0) {
            if (++ignored <= 5) //a few are enough to find them; the toast gives the total
                std::printf("[!] values.txt line %d ignored: \"%s\" (expected: name | whole number without commas, e.g. \"Snap Hook | 14000\")\n",
                            lineNo, line.c_str());
            continue;
        }
        table()[norm(name)] = v;
    }
    return ignored;
}

int load() {
    const std::string path = configDir() + "/values.txt";
    std::error_code ec;
    loadedTime() = std::filesystem::last_write_time(path, ec); //before reading: a save that lands mid-read changes it again
    std::ifstream f(path);
    if (!f) { table().clear(); return 0; }
    std::stringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
}

void reloadIfChanged(int minGapMs) {
    static std::chrono::steady_clock::time_point lastCheck{};
    const auto now = std::chrono::steady_clock::now();
    if (now - lastCheck < std::chrono::milliseconds(minGapMs)) return;
    lastCheck = now;
    std::error_code ec; //a missing file reads as file_time_type::min(): removing values.txt clears the prices
    if (std::filesystem::last_write_time(configDir() + "/values.txt", ec) != loadedTime()) load();
}

void setDir(const std::string& dir) { dirOverride() = dir; }

void setValueForTest(const std::string& name, long value) { table()[norm(name)] = value; }

void clear() { table().clear(); }

} // namespace itemvalues
