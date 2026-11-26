#pragma once
//Spoken cues, so you do not have to read the screen: the result of a hotkey (F6, F7, F8, F9, F10), a warning when an
//enemy comes close, low health, a broken shield and lost game data are said out loud by speech-dispatcher's spd-say,
//with your own speech-dispatcher voice (and your own speed unless Speech speed is set).
//Nothing waits: spd-say hands the text over and exits at once, and speech-dispatcher queues what it gets, so cues
//never talk over each other; an enemy warning goes first (priority "important"). Without spd-say the overlay stays
//silent (the Settings tab says so).
#ifndef KWINHELPER_CUE_H
#define KWINHELPER_CUE_H

#include <cmath>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <vector>

#include "../config.h"

extern char** environ;

namespace cue {

inline bool missing = false; //the last cue could not start spd-say (not installed)
inline bool failed = false;  //the last finished spd-say could not reach speech-dispatcher (exit status not 0)
inline bool noPriority = false; //an "important" spd-say failed: this spd-say may not know -P, so it is left out from now on
inline pid_t urgentPid = 0;     //the last spd-say started with -P important

//Every draw frame, on the draw thread: collects each finished spd-say (none is left behind as a zombie) and keeps
//whether the last one worked. One non-blocking call; spd-say is the only child this program starts.
inline void reap() {
    int st;
    pid_t pid;
    while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {
        failed = !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
        if (failed && pid == urgentPid) noPriority = true;
    }
}

//spd-say's command line for text. urgent: an enemy warning, said before anything else waiting. Speech speed (-r) only
//when it is not 0, so 0 keeps your own speech-dispatcher speed. "--": the text is never read as an option.
inline std::vector<std::string> Args(const std::string& text, bool urgent) {
    std::vector<std::string> a{"spd-say"};
    if (urgent && !noPriority) { a.push_back("-P"); a.push_back("important"); }
    const long rate = std::lround(config::speechRate);
    if (rate != 0) { a.push_back("-r"); a.push_back(std::to_string(rate)); }
    a.push_back("--");
    a.push_back(text);
    return a;
}

//Says text out loud without waiting for it. posix_spawnp, not fork: the overlay's memory is not copied (no stutter),
//and it is safe in a program with several threads. Called from the draw thread only.
inline void say(const std::string& text, bool urgent = false) {
    if (!config::soundCues || text.empty()) return;
    std::vector<std::string> args = Args(text, urgent);
    std::vector<char*> argv;
    for (std::string& s : args) argv.push_back(s.data());
    argv.push_back(nullptr);
    pid_t pid;
    missing = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0;
    if (!missing && args.size() > 2 && args[1] == "-P") urgentPid = pid;
}

} // namespace cue

#endif //KWINHELPER_CUE_H
