#include "itemvalues.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
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

//Lookup key: letters and digits only, lower case, so "Lance's Mixtape (5th edition)" and "(5th Edition)" match. A name
//in another script (UTF-8: bytes from 0x80) keeps its letters, so "Бинт" and "Аптечка" are two names, not both "".
//Like ASCII spaces and punctuation, a no-break space and the signs U+0080-U+00BF (bytes C2 xx) and the typographic
//spaces, dashes and quotes U+2000-U+207F (E2 80 xx, E2 81 xx) are left out. ponytail: case is ignored for A-Z only.
std::string norm(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = (unsigned char)s[i], next = i + 1 < s.size() ? (unsigned char)s[i + 1] : 0;
        if (c == 0xC2) i += 1;
        else if (c == 0xE2 && (next == 0x80 || next == 0x81)) i += 2;
        else if (c >= 0x80) out.push_back((char)c);
        else if (std::isalnum(c)) out.push_back((char)std::tolower(c));
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

//The highest price taken: no item is worth that much, and it keeps a window's total far from overflowing
constexpr long long MAX_PRICE = 1000000000000LL;

bool allDigits(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

//A price as values.txt may write it: "14000", "14,000", "14 000", "14k", "1.5k" or "2m" (k thousand, m million, either
//case). -1 for anything else.
long long priceOf(std::string s) {
    if (!s.empty() && s[0] == '+') s.erase(0, 1); //"+14000" was always read
    if (s.empty()) return -1;
    const char last = (char)std::tolower((unsigned char)s.back());
    if (last == 'k' || last == 'm') {
        const long long mult = last == 'k' ? 1000 : 1000000;
        s = trim(s.substr(0, s.size() - 1));
        const size_t dot = s.find('.');
        const std::string whole = s.substr(0, dot), frac = dot == std::string::npos ? "" : s.substr(dot + 1);
        if (whole.empty() || whole.size() > 7 || !allDigits(whole) || frac.size() > 6 || !allDigits(frac) ||
            (dot != std::string::npos && frac.empty()))
            return -1;
        const double part = frac.empty() ? 0.0 : (double)std::stoll(frac) / std::pow(10.0, (double)frac.size());
        return std::stoll(whole) * mult + std::llround(part * (double)mult);
    }
    //whole digits, or after the first one to three digits groups of three, all split the same way: "14,000" or "14 000"
    std::string digits;
    size_t run = 0; //digits since the last separator
    char sep = 0;   //the separator used (0: none yet)
    for (const char c : s) {
        if (std::isdigit((unsigned char)c)) { digits += c; run++; }
        else if ((c == ',' || c == ' ') && (!sep || c == sep) && run >= 1 && (sep ? run == 3 : run <= 3)) { sep = c; run = 0; }
        else return -1;
    }
    if (digits.empty() || (sep && run != 3) || digits.size() > 13) return -1;
    return std::stoll(digits);
}

} // namespace

std::string grouped(long v) {
    const std::string d = std::to_string(v);
    const size_t start = d[0] == '-' ? 1 : 0;
    std::string out = d.substr(0, start);
    for (size_t i = start; i < d.size(); i++) {
        if (i > start && (d.size() - i) % 3 == 0) out += ',';
        out += d[i];
    }
    return out;
}

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
        const long long v = num.empty() ? -1 : priceOf(num);
        if (norm(name).empty() || v < 0 || v > MAX_PRICE) { //a name of signs only would match every other one
            if (++ignored <= 5) //a few are enough to find them; the toast gives the total
                std::printf("[!] values.txt line %d ignored: \"%s\" (expected: name | price, e.g. \"Snap Hook | 14000\", \"14,000\" or \"14k\")\n",
                            lineNo, line.c_str());
            continue;
        }
        table()[norm(name)] = (long)v;
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
