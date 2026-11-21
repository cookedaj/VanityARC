#pragma once
//Console messages that could repeat every frame (lobby, loading screen) are printed at most once every 5 s.
#ifndef KWINHELPER_LOG_H
#define KWINHELPER_LOG_H

#include <chrono>
#include <iostream>
#include <map>
#include <string>

//Call from one thread only (the tracker thread)
inline void logThrottled(const char* msg) {
    static std::map<std::string, std::chrono::steady_clock::time_point> last;
    auto now = std::chrono::steady_clock::now();
    auto it = last.find(msg);
    if (it != last.end() && now - it->second < std::chrono::seconds(5)) return;
    last[msg] = now;
    std::cout << msg << std::endl;
}

#endif //KWINHELPER_LOG_H
