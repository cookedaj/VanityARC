#ifndef KWINHELPER_HOTKEY_H
#define KWINHELPER_HOTKEY_H
//Menu hotkey. The overlay never has keyboard focus (and Wayland has no global key hooks), so Insert is
//read straight from the keyboard devices in /dev/input. Only Insert, F6, F7, F8 and F10 are looked at; nothing else
//is stored or logged, EXCEPT while you are typing into a text box in the menu: then (and only then) the
//keyboard is grabbed so the game does not see the keys, and the key presses are handed to the menu.
//This needs read access to /dev/input (group "input"). Without it the menu can still
//be toggled with:   pkill -USR1 -x kwin-helper
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

//Set by the Insert key or SIGUSR1; the main loop clears it
inline std::atomic<bool> g_MenuToggle{false};
inline std::atomic<bool> g_HotkeyStop{false};
//F6 mark the player at the crosshair as a teammate, F7 clear the marks, F10 quit. Set on a key press, cleared by the main loop.
inline std::atomic<bool> g_KeyMark{false}, g_KeyClear{false}, g_KeyQuit{false};
//F8 toggles stream-hide (the overlay draws nothing). SIGUSR2 does the same, for testing: pkill -USR2 -x kwin-helper
inline std::atomic<bool> g_KeyStream{false};
inline constexpr int MENU_KEY = KEY_INSERT;

//Typing support. The menu sets g_TextGrab while a text box is active; the hotkey thread then grabs the
//keyboards and queues key presses here for the menu to read.
struct KeyEv { int code; int value; }; //value: 1 press, 0 release, 2 repeat
inline std::atomic<bool> g_TextGrab{false};
//The menu refreshes this while it wants the keyboard. If the main loop stalls (a long pause, a crash in
//the middle of a frame) the grab is dropped after 1.5 s, so the game can never be left without a keyboard.
inline std::atomic<long long> g_GrabBeatMs{0};
inline long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
inline constexpr long long GRAB_TIMEOUT_MS = 1500;
inline std::mutex g_KeyMutex;
inline std::vector<KeyEv> g_KeyQueue;

inline void MenuSignalHandler(int) { g_MenuToggle = true; }
inline void StreamSignalHandler(int) { g_KeyStream = true; }

//Modifiers held on this keyboard right now: bit 0 Ctrl or Shift, bit 1 Alt or Meta.
inline int HeldModifiers(int fd) {
    constexpr size_t BITS = 8 * sizeof(unsigned long);
    unsigned long st[KEY_MAX / BITS + 1] = {};
    if (ioctl(fd, EVIOCGKEY(sizeof st), st) < 0) return 0;
    auto down = [&](int c) { return ((st[c / BITS] >> (c % BITS)) & 1UL) != 0; };
    int m = 0;
    if (down(KEY_LEFTCTRL) || down(KEY_RIGHTCTRL) || down(KEY_LEFTSHIFT) || down(KEY_RIGHTSHIFT)) m |= 1;
    if (down(KEY_LEFTALT) || down(KEY_RIGHTALT) || down(KEY_LEFTMETA) || down(KEY_RIGHTMETA)) m |= 2;
    return m;
}

//Reads whatever input_event structs are waiting on fd and flips g_MenuToggle on an Insert key press
//(not on release or key repeat). Returns false once the device is gone.
//A hotkey pressed together with a modifier belongs to another program (Shift+F10 context menu, Shift+Insert paste...)
//and is ignored. F6 still works with Shift or Ctrl held (sprinting, crouching), just not with Alt or Meta.
inline bool HandleKeyEvents(int fd) {
    input_event ev[32];
    for (;;) {
        ssize_t n = read(fd, ev, sizeof ev);
        if (n < 0) return errno == EAGAIN || errno == EINTR; //EAGAIN: nothing left; anything else: device gone
        if (n == 0) return false;
        for (size_t i = 0; i < size_t(n) / sizeof(input_event); i++) {
            if (ev[i].type != EV_KEY) continue;
            const int c = ev[i].code;
            if (ev[i].value == 1 && (c == MENU_KEY || c == KEY_F6 || c == KEY_F7 || c == KEY_F8 || c == KEY_F10)) {
                const int mods = HeldModifiers(fd);
                if (c == KEY_F6) { if (!(mods & 2)) g_KeyMark = true; }
                else if (!mods) {
                    if (c == MENU_KEY) g_MenuToggle = true;
                    else if (c == KEY_F7) g_KeyClear = true;
                    else if (c == KEY_F10) g_KeyQuit = true;
                    else g_KeyStream = true; //F8
                }
            }
            if (g_TextGrab) {
                std::lock_guard<std::mutex> lk(g_KeyMutex);
                if (g_KeyQueue.size() < 256) g_KeyQueue.push_back({ev[i].code, ev[i].value});
            }
        }
    }
}

//Keyboards the hotkey thread has open right now; -1 until the first scan. main.cpp shows a message when it changes.
inline std::atomic<int> g_KeyboardCount{-1};

//Opens every event device that has an Insert key (= keyboards) and is not in paths yet, and appends it to
//fds and paths (same order). denied counts devices we may not read.
inline void OpenKeyboards(std::vector<int>& fds, std::vector<std::string>& paths, int* denied) {
    *denied = 0;
    DIR* dir = opendir("/dev/input");
    if (!dir) return;
    constexpr size_t BITS = 8 * sizeof(unsigned long);
    while (dirent* e = readdir(dir)) {
        if (std::strncmp(e->d_name, "event", 5) != 0) continue;
        std::string path = std::string("/dev/input/") + e->d_name;
        if (std::find(paths.begin(), paths.end(), path) != paths.end()) continue; //already open
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            if (errno == EACCES) (*denied)++;
            continue;
        }
        unsigned long keys[KEY_MAX / BITS + 1] = {};
        bool hasKey = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 &&
                      ((keys[MENU_KEY / BITS] >> (MENU_KEY % BITS)) & 1UL);
        if (hasKey) { fds.push_back(fd); paths.push_back(path); } else close(fd);
    }
    closedir(dir);
}

inline void HotkeyThread() {
    int denied = 0;
    std::vector<int> fds;
    std::vector<std::string> paths; //same order as fds
    OpenKeyboards(fds, paths, &denied);
    g_KeyboardCount = (int)fds.size();
    if (fds.empty()) {
        if (denied) {
            std::printf("[!] Insert hotkey is off: no permission to read the keyboard (/dev/input).\n"
                        "    Fix once:  sudo usermod -aG input $USER   then log out and back in.\n");
        } else {
            std::printf("[!] Insert hotkey is off: no keyboard found.\n");
        }
        std::printf("    Meanwhile you can toggle the menu with:  pkill -USR1 -x kwin-helper\n");
    } else {
        std::printf("[+] Press Insert to open the menu (%zu keyboard device%s)\n", fds.size(), fds.size() == 1 ? "" : "s");
    }
    std::fflush(stdout);

    bool grabbed = false;
    long long lastScan = NowMs();
    while (!g_HotkeyStop) { //keeps running with no keyboard, so one that comes back is found again
        bool wantGrab = g_TextGrab;
        if (wantGrab && NowMs() - g_GrabBeatMs > GRAB_TIMEOUT_MS) { //the menu stopped asking: let go
            g_TextGrab = false;
            wantGrab = false;
        }
        if (wantGrab != grabbed) { //typing into a menu text box: keep the keys away from the game
            for (int fd : fds) ioctl(fd, EVIOCGRAB, wantGrab ? 1 : 0);
            grabbed = wantGrab;
            if (!grabbed) { std::lock_guard<std::mutex> lk(g_KeyMutex); g_KeyQueue.clear(); }
        }
        //A keyboard that reconnects (Bluetooth wake, replug, resume) comes back as a new /dev/input/eventN: look every 2 s
        if (NowMs() - lastScan >= 2000) {
            lastScan = NowMs();
            const size_t before = fds.size();
            OpenKeyboards(fds, paths, &denied);
            if (grabbed) for (size_t i = before; i < fds.size(); i++) ioctl(fds[i], EVIOCGRAB, 1);
        }
        std::vector<pollfd> p;
        for (int fd : fds) p.push_back({fd, POLLIN, 0});
        poll(p.data(), p.size(), 50); //with no keyboard this just waits 50 ms
        for (size_t i = p.size(); i-- > 0;) { //backwards so erasing keeps the earlier indexes valid
            bool gone = (p[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            if (!gone && (p[i].revents & POLLIN)) gone = !HandleKeyEvents(p[i].fd);
            if (gone) {
                close(p[i].fd);
                fds.erase(fds.begin() + i);
                paths.erase(paths.begin() + i);
            }
        }
        g_KeyboardCount = (int)fds.size();
    }
    for (int fd : fds) close(fd);
}

inline std::thread StartHotkey() { //SIGUSR1/SIGUSR2 handlers are installed in main() before InitOverlay
    return std::thread(HotkeyThread);
}

#endif //KWINHELPER_HOTKEY_H
