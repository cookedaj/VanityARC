#pragma once
//Console messages that could repeat every frame (lobby, loading screen) are printed at most once every 5 s.
#ifndef KWINHELPER_LOG_H
#define KWINHELPER_LOG_H

#include <chrono>
#include <iostream>
#include <map>
#include <string>

//When each message was last printed. At most LOG_KEEP are remembered: a message with a changing number in it (a garbage
//bone count, say) must not make the list grow for as long as the overlay runs.
constexpr size_t LOG_KEEP = 256;
inline std::map<std::string, std::chrono::steady_clock::time_point>& logTimes() {
    static std::map<std::string, std::chrono::steady_clock::time_point> last;
    return last;
}

//Call from one thread only (the tracker thread)
inline void logThrottled(const char* msg) {
    auto& last = logTimes();
    auto now = std::chrono::steady_clock::now();
    auto it = last.find(msg);
    if (it != last.end() && now - it->second < std::chrono::seconds(5)) return;
    if (it == last.end() && last.size() >= LOG_KEEP) last.clear(); //ponytail: forgets all, so a repeat may print once early
    last[msg] = now;
    std::cout << msg << std::endl;
}

#endif //KWINHELPER_LOG_H
