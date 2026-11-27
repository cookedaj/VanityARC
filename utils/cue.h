#pragma once
//Spoken cues, so you do not have to read the screen: the result of a hotkey (F6, F7, F8, F9, F10), a warning when an
//enemy comes close, low health, a broken shield and lost game data are said out loud by speech-dispatcher's spd-say,
//with your own speech-dispatcher voice (and your own speed unless Speech speed is set).
//Nothing waits: spd-say hands the text over and exits at once. The warnings (an enemy close, low health, a broken
//shield, game data lost or back) go at priority "important": speech-dispatcher says them in turn and never cuts one off,
//and one cuts off anything else being said (a hotkey result), which is not said again. Without spd-say the overlay
//stays silent (the Settings tab says so).
#ifndef KWINHELPER_CUE_H
#define KWINHELPER_CUE_H

#include <cmath>
#include <csignal>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
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

//spd-say's command line for text. urgent: a warning (AlertArgs), said before anything else waiting. Speech speed (-r)
//only when it is not 0, so 0 keeps your own speech-dispatcher speed. "--": the text is never read as an option.
inline std::vector<std::string> Args(const std::string& text, bool urgent) {
    std::vector<std::string> a{"spd-say"};
    if (urgent && !noPriority) { a.push_back("-P"); a.push_back("important"); }
    const long rate = std::lround(config::speechRate);
    if (rate != 0) { a.push_back("-r"); a.push_back(std::to_string(rate)); }
    a.push_back("--");
    a.push_back(text);
    return a;
}

//spd-say's command line for one frame's warnings: the enemy warning first, then your own news (self: game data lost or
//back, a broken shield, low health), as one text at priority important. speech-dispatcher cuts a message at its normal
//priority (text) off for good when an important one comes, so "Low health" said at text could be lost to the next enemy
//warning. One spd-say, not two: two started together would race to speech-dispatcher.
inline std::vector<std::string> AlertArgs(const std::string& enemy, const std::string& self) {
    return Args(enemy.empty() ? self : self.empty() ? enemy : enemy + ". " + self, true);
}

//Starts spd-say (args from Args or AlertArgs) without waiting for it. The spoken text is written to spd-say's stdin
//(-e, pipe mode), never passed on its command line, so a marked teammate's name or account id never shows up in
///proc/<pid>/cmdline where a host observer could read it. posix_spawnp, not fork: the overlay's memory is not copied
//(no stutter), and it is safe in a program with several threads. Called from the draw thread only.
//Pipe mode would say a message at priority "message", not "text", so for a hotkey result (no -P in args) "-P text" is
//added to keep its old priority; it is left off when noPriority says this spd-say may not know -P (then pipe mode's own
//default stands, and the words are still spoken). The text goes in over stdin with no trailing newline, so spd-say reads
//one line at end of input and sends the exact same bytes it got on the command line before.
inline void run(std::vector<std::string> args) {
    if (!config::soundCues || args.back().empty()) return;
    const std::string text = args.back(); //args is [spd-say, options..., "--", text]
    args.resize(args.size() - 2);         //drop "--" and the text: the text goes in over stdin, not argv
    const bool urgent = args.size() > 1 && args[1] == "-P"; //the "-P important" warning, before adding any -P below
    if (!urgent && !noPriority) { args.emplace_back("-P"); args.emplace_back("text"); } //keep a hotkey result at "text"
    args.emplace_back("-e"); //pipe mode: spd-say reads the message from stdin

    int fds[2];
    if (pipe(fds) != 0) return; //out of file descriptors: skip this one cue rather than fall back to leaking the name

    std::vector<char*> argv;
    for (std::string& s : args) argv.push_back(s.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[0], STDIN_FILENO); //child reads its stdin from the pipe
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addclose(&fa, fds[1]); //the child must not hold the write end, or it never sees end of input

    pid_t pid;
    missing = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ) != 0;
    posix_spawn_file_actions_destroy(&fa);
    close(fds[0]); //the parent keeps only the write end
    if (missing) { close(fds[1]); return; }

    static const bool _ = [] { std::signal(SIGPIPE, SIG_IGN); return true; }(); //a write to a dead spd-say must not kill the overlay
    (void)_;
    for (size_t off = 0; off < text.size(); ) {
        const ssize_t n = write(fds[1], text.data() + off, text.size() - off);
        if (n <= 0) break; //spd-say gone (EPIPE) or an error: stop; it is collected by reap() either way
        off += (size_t)n;
    }
    close(fds[1]); //end of input: spd-say says the message and exits

    if (urgent) urgentPid = pid; //the -P important one, so reap() can tell if it failed
}

//Says text out loud (a hotkey result, Spoken cues on), at the normal priority
inline void say(const std::string& text) { run(Args(text, false)); }

//Says one frame's warnings out loud, never cut off (AlertArgs)
inline void alert(const std::string& enemy, const std::string& self) { run(AlertArgs(enemy, self)); }

} // namespace cue

#endif //KWINHELPER_CUE_H
