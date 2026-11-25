#pragma once
//Spoken cues, so you do not have to read the screen: the result of a hotkey (F6, F7, F8, F10) and a warning when an
//enemy comes close are said out loud by speech-dispatcher's spd-say, with your own speech-dispatcher voice and speed.
//Nothing waits: spd-say hands the text over and exits at once, and speech-dispatcher queues what it gets, so cues
//never talk over each other. Without spd-say the overlay stays silent (the Settings tab says so).
#ifndef KWINHELPER_CUE_H
#define KWINHELPER_CUE_H

#include <signal.h>
#include <spawn.h>
#include <string>

#include "../config.h"

extern char** environ;

namespace cue {

inline bool missing = false; //the last cue could not start spd-say (not installed)

//Once at startup: the kernel cleans up every finished spd-say, so none is left behind as a zombie. SIGCHLD keeps its
//default action and the flag is not passed on to spd-say; nothing in this program waits for a child.
inline void init() {
    struct sigaction sa {};
    sa.sa_handler = SIG_DFL;
    sa.sa_flags = SA_NOCLDWAIT;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, nullptr);
}

//Says text out loud without waiting for it. posix_spawnp, not fork: the overlay's memory is not copied (no stutter),
//and it is safe in a program with several threads. Called from the draw thread only.
inline void say(std::string text) {
    if (!config::soundCues || text.empty()) return;
    char prog[] = "spd-say", end[] = "--"; //"--": the text is never read as an option
    char* argv[] = {prog, end, text.data(), nullptr};
    pid_t pid;
    missing = posix_spawnp(&pid, prog, nullptr, nullptr, argv, environ) != 0;
}

} // namespace cue

#endif //KWINHELPER_CUE_H
