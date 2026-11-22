#include "itemvalues.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace itemvalues {

namespace {

std::map<std::string, long>& table() {
    static std::map<std::string, long> t;
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

void parse(const std::string& text) {
    table().clear();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t bar = line.rfind('|');
        if (bar == std::string::npos) continue;
        std::string name = trim(line.substr(0, bar));
        std::string num = trim(line.substr(bar + 1));
        if (name.empty() || num.empty()) continue;
        char* end = nullptr;
        long v = std::strtol(num.c_str(), &end, 10);
        if (end == num.c_str() || *end != '\0' || v < 0) continue;
        table()[norm(name)] = v;
    }
}

void load() {
    std::ifstream f(configDir() + "/values.txt");
    if (!f) { table().clear(); return; }
    std::stringstream ss;
    ss << f.rdbuf();
    parse(ss.str());
}

void setDir(const std::string& dir) { dirOverride() = dir; }

void setValueForTest(const std::string& name, long value) { table()[norm(name)] = value; }

void clear() { table().clear(); }

} // namespace itemvalues
