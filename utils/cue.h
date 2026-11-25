#pragma once
//Spoken cues, so you do not have to read the screen: the result of a hotkey (F6, F7, F8, F10) and a warning when an
//enemy comes close are said out loud by speech-dispatcher's spd-say, with your own speech-dispatcher voice and speed.
//Nothing waits: spd-say hands the text over and exits at once, and speech-dispatcher queues what it gets, so cues
//never talk over each other. Without spd-say the overlay stays silent (the Settings tab says so).
#ifndef KWINHELPER_CUE_H
#define KWINHELPER_CUE_H

#include <spawn.h>
#include <string>
#include <sys/wait.h>

#include "../config.h"

extern char** environ;

namespace cue {

inline bool missing = false; //the last cue could not start spd-say (not installed)
inline bool failed = false;  //the last finished spd-say could not reach speech-dispatcher (exit status not 0)

//Every draw frame, on the draw thread: collects each finished spd-say (none is left behind as a zombie) and keeps
//whether the last one worked. One non-blocking call; spd-say is the only child this program starts.
inline void reap() { int st; while (waitpid(-1, &st, WNOHANG) > 0) failed = !(WIFEXITED(st) && WEXITSTATUS(st) == 0); }

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
