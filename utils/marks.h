#pragma once
//Players you marked as teammates by hand (player list > Teammate). Saved by account id in
//~/.config/kwin-helper/teammates.txt, so a mark survives restarts and new games with the same people.
#ifndef KWINHELPER_MARKS_H
#define KWINHELPER_MARKS_H

#include "identity.h"   // ::configDir(): one source for ~/.config/kwin-helper

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace marks {

namespace detail {
inline std::mutex& mu() { static std::mutex m; return m; }
inline std::set<std::string>& set() { static std::set<std::string> s; return s; }
inline unsigned& ver() { static unsigned v = 1; return v; }
inline std::string path(const std::string& dir) {
    if (!dir.empty()) return dir + "/teammates.txt";
    return ::configDir() + "/teammates.txt";
}
inline bool valid(const std::string& a) { return !a.empty() && a.size() <= 32 && a.find_first_of(" \t\r\n") == std::string::npos; }
} // namespace detail

inline bool isMarked(const std::string& account) {
    if (account.empty()) return false;
    std::lock_guard<std::mutex> lk(detail::mu());
    return detail::set().count(account) > 0;
}

inline void setMarked(const std::string& account, bool on) {
    if (!detail::valid(account)) return;
    std::lock_guard<std::mutex> lk(detail::mu());
    if (on) detail::set().insert(account); else detail::set().erase(account);
    detail::ver()++;
}

inline void toggle(const std::string& account) { setMarked(account, !isMarked(account)); }

inline void clear() {
    std::lock_guard<std::mutex> lk(detail::mu());
    detail::set().clear();
    detail::ver()++;
}

inline std::vector<std::string> all() {
    std::lock_guard<std::mutex> lk(detail::mu());
    return std::vector<std::string>(detail::set().begin(), detail::set().end());
}

inline unsigned version() { std::lock_guard<std::mutex> lk(detail::mu()); return detail::ver(); }

inline void load(const std::string& dir = "") {
    std::ifstream f(detail::path(dir));
    std::string line;
    std::lock_guard<std::mutex> lk(detail::mu());
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3); //UTF-8 byte-order mark (some Windows editors add one)
        if (detail::valid(line)) detail::set().insert(line);
    }
    detail::ver()++;
}

inline void save(const std::string& dir = "") {
    std::lock_guard<std::mutex> lk(detail::mu());
    std::error_code ec;
    const std::string target = detail::path(dir), tmp = target + ".tmp";
    std::filesystem::create_directories(std::filesystem::path(target).parent_path(), ec);
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        for (const std::string& a : detail::set()) f << a << '\n';
        f.flush();
        if (!f) { std::filesystem::remove(tmp, ec); return; }
    }
    std::filesystem::rename(tmp, target, ec);
}

} // namespace marks

#endif //KWINHELPER_MARKS_H
