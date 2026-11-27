#pragma once
// PID-namespace self-check: refuse to attach when the game shares our namespace
// (anti-cheat in the same namespace can see us). Ambiguous = continue: never blind the aid.
#ifndef KWINHELPER_SANDBOX_CHECK_H
#define KWINHELPER_SANDBOX_CHECK_H

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/types.h>

// Returns true when it is safe to proceed (namespaces differ or check skipped/ambiguous).
// Returns false when namespaces match (game is NOT sandboxed from us).
inline bool sandboxCheckPidNs(pid_t gamePid) {
    if (std::getenv("KWINHELPER_SKIP_SANDBOX_CHECK")) {
        std::cerr << "[sandbox] KWINHELPER_SKIP_SANDBOX_CHECK set, skipping namespace check\n";
        return true;
    }

    auto readNsLink = [](const std::string& path, std::string& out) -> bool {
        char buf[128];
        ssize_t n = readlink(path.c_str(), buf, sizeof(buf) - 1);
        if (n < 0) return false;
        buf[n] = '\0';
        out.assign(buf, static_cast<size_t>(n));
        return true;
    };

    std::string gameNs, selfNs;
    std::string gamePath = "/proc/" + std::to_string(gamePid) + "/ns/pid";

    bool gotGame = readNsLink(gamePath, gameNs);
    bool gotSelf = readNsLink("/proc/self/ns/pid", selfNs);

    if (!gotGame || !gotSelf) {
        // ponytail: ambiguous = proceed, log why
        std::cerr << "[sandbox] cannot read PID namespace ("
                  << (!gotGame ? gamePath : "/proc/self/ns/pid")
                  << ": " << std::strerror(errno) << "), continuing\n";
        return true;
    }

    if (gameNs == selfNs) {
        std::cerr << "[sandbox] FAIL: game PID " << gamePid
                  << " shares our PID namespace (" << gameNs
                  << "). Not sandboxed as expected; refusing to attach.\n";
        return false;
    }

    // Different namespaces: game is sandboxed, safe to proceed.
    return true;
}

#endif // KWINHELPER_SANDBOX_CHECK_H
