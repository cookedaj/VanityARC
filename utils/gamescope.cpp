#include "gamescope.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace gamescope {
namespace {

int toSize(const std::string& s) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != 0 || v < 1 || v > 16384) return 0;
    return (int)v;
}

bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

//The command line of process pid, split, when it is gamescope (not its gamescopereaper helper); else empty.
//argv[0] is checked before the rest is split.
std::vector<std::string> gamescopeArgv(const std::string& pid) {
    const std::string all = readFile("/proc/" + pid + "/cmdline");
    const std::string prog = std::filesystem::path(all.substr(0, all.find('\0'))).filename().string();
    if (!startsWith(prog, "gamescope") || startsWith(prog, "gamescopereaper")) return {};
    std::vector<std::string> argv;
    size_t pos = 0;
    while (pos < all.size()) {
        size_t z = all.find('\0', pos);
        if (z == std::string::npos) z = all.size();
        argv.push_back(all.substr(pos, z - pos));
        pos = z + 1;
    }
    return argv;
}

//Field 4 of /proc/<pid>/stat, or 0. Field 2 (the name) can hold spaces and ')', so it is read after the last ')'.
pid_t parentOf(pid_t pid) {
    const std::string st = readFile("/proc/" + std::to_string(pid) + "/stat");
    const size_t r = st.rfind(')');
    if (r == std::string::npos) return 0;
    char state = 0;
    int ppid = 0;
    if (std::sscanf(st.c_str() + r + 1, " %c %d", &state, &ppid) != 2) return 0;
    return (pid_t)ppid;
}

} // namespace

Info parseArgs(const std::vector<std::string>& argv) {
    Info in;
    in.found = true;
    for (size_t i = 1; i < argv.size(); i++) {
        const std::string& a = argv[i];
        if (a == "--") break;

        //value of an option: "-w 1440", "-w1440", "--nested-width 1440" or "--nested-width=1440"
        auto value = [&](const char* shortOpt, const char* longOpt, std::string& out) -> bool {
            const std::string s(shortOpt), l(longOpt);
            if (a == s || a == l) {
                if (i + 1 < argv.size()) { out = argv[++i]; return true; }
                return false;
            }
            if (!s.empty() && a.size() > s.size() && startsWith(a, shortOpt) && a[1] != '-') { out = a.substr(s.size()); return true; }
            if (startsWith(a, (l + "=").c_str())) { out = a.substr(l.size() + 1); return true; }
            return false;
        };
        std::string v;
        if (value("-w", "--nested-width", v)) in.nestedW = toSize(v);
        else if (value("-h", "--nested-height", v)) in.nestedH = toSize(v);
        else if (value("-W", "--output-width", v)) in.outW = toSize(v);
        else if (value("-H", "--output-height", v)) in.outH = toSize(v);
        else if (value("-S", "--scaler", v)) in.scaler = v;
        else if (a == "-f" || a == "--fullscreen") in.fullscreen = true;
        else if (a == "-i" || a == "--integer-scale") in.scaler = "integer"; //old spelling of -S integer
    }
    //gamescope's defaults: -W -H are 1280x720, -H alone means a 16:9 width; -w -h follow -W -H, -h alone means 16:9
    if (!in.outW && !in.outH) { in.outW = 1280; in.outH = 720; }
    else if (!in.outW) in.outW = in.outH * 16 / 9;
    if (!in.nestedW && !in.nestedH) { in.nestedW = in.outW; in.nestedH = in.outH; }
    else if (!in.nestedW) in.nestedW = in.nestedH * 16 / 9;
    return in;
}

Info find(pid_t gamePid) {
    if (gamePid <= 0) return {};
    //Cache the result per game PID: the environ and gamescope parent don't change while the PID lives.
    static pid_t cachedPid = 0;
    static Info   cachedInfo;
    if (gamePid == cachedPid) return cachedInfo;
    cachedPid  = gamePid;
    cachedInfo = {};

    //gamescope sets GAMESCOPE_WAYLAND_DISPLAY for everything it starts: without it the game is not in a gamescope,
    //whatever other gamescope is running
    static const char key[] = "\0GAMESCOPE_WAYLAND_DISPLAY="; //entries are NUL-separated: match a whole name
    const std::string env = '\0' + readFile("/proc/" + std::to_string(gamePid) + "/environ");
    if (env.find(key, 0, sizeof key - 1) == std::string::npos) return {};

    //The gamescope the game runs in is one of its parents (gamescope -> gamescopereaper -> ... -> the game)
    pid_t p = gamePid;
    for (int depth = 0; depth < 64; depth++) {
        p = parentOf(p);
        if (p <= 0) break;
        std::vector<std::string> argv = gamescopeArgv(std::to_string(p));
        if (argv.empty()) continue;
        Info in = parseArgs(argv);
        in.pid = p;
        return cachedInfo = in;
    }

    //None of its parents (it was moved to another parent): the gamescope with the highest pid
    Info best;
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::directory_iterator it("/proc", ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || !std::isdigit((unsigned char)name[0])) continue;
        const pid_t pid = (pid_t)std::strtol(name.c_str(), nullptr, 10);
        if (pid <= best.pid) continue;
        std::vector<std::string> argv = gamescopeArgv(name);
        if (argv.empty()) continue;
        best = parseArgs(argv);
        best.pid = pid;
    }
    return cachedInfo = best;
}

} // namespace gamescope
