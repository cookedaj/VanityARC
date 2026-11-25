#include "Tracker.h"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>

#include "utils/labeler.h"
#include "utils/names.h"
#include "utils/obfstr.h"

namespace {
using clk = std::chrono::steady_clock;
constexpr int IDLE_MS = 250;      //main menu / loading: look again a few times a second
constexpr int KEEP_MS = 250;      //a failed step this soon after a good one is a hiccup: the good snapshot stays up
constexpr int NO_GAME_MS = 200;
thread_local std::mt19937 jitterRng{std::random_device{}()}; //uneven step times: a fixed rhythm is easy to spot

bool processAlive(pid_t pid) { return pid > 0 && access(("/proc/" + std::to_string(pid)).c_str(), F_OK) == 0; }

//Finding the game needs no permission, reading it does. The kernel checks permission before it looks at the address,
//so one byte from any address tells: EPERM = the ptrace capability is missing (a rebuild not started with start.sh)
bool readDenied(pid_t pid) {
    char b;
    iovec l{&b, 1}, r{(void*)0x10000, 1};
    return process_vm_readv(pid, &l, 1, &r, 1, 0) < 0 && errno == EPERM;
}
} // namespace

void Tracker::start() {
    if (th_.joinable()) return;
    stop_ = false;
    th_ = std::thread([this] { run(); });
}

void Tracker::stop() {
    stop_ = true;
    if (th_.joinable()) th_.join();
}

std::shared_ptr<const Snapshot> Tracker::latest() const {
    std::lock_guard<std::mutex> lk(mu_);
    return snap_;
}

void Tracker::publish(std::shared_ptr<const Snapshot> s) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_ = std::move(s);
}

void Tracker::setInspected(std::vector<ptr> actors) {
    std::lock_guard<std::mutex> lk(inspMu_);
    inspected_ = std::move(actors);
}

gamescope::Info Tracker::gamescopeInfo() const {
    std::lock_guard<std::mutex> lk(gsMu_);
    return gs_;
}

Tracker::Stats Tracker::stats() const {
    std::lock_guard<std::mutex> lk(statMu_);
    return stats_;
}

bool Tracker::freshCamera(FminimalViewInfo& out) const {
    const ptr cam = cam_.load(std::memory_order_relaxed);
    if (!cam || ProcessId == 0) return false;
    FminimalViewInfo v = ReadMemory<FminimalViewInfo>(cam + off::VIEW_MATRIX);
    const bool ok = viewUsable(v);
    if (ok) out = v;
    return ok;
}

void Tracker::sleepFor(int ms) {
    while (ms > 0 && !stop_) {
        int slice = ms < 25 ? ms : 25;
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
        ms -= slice;
    }
}

void Tracker::run() {
    //The overlay can be started before the game, and the game can be restarted while it runs: the process is
    //looked up again whenever it is missing or has gone away.
    GameState gs = fixedPid_ ? GameState(fixedBase_) : GameState();
    ProcessId = fixedPid_;
    auto lastFind = clk::now() - std::chrono::seconds(10);
    auto lastData = clk::now();
    auto lastValid = clk::now() - std::chrono::seconds(10);
    auto lastWorld = clk::now(); //last valid world (or attach / re-lookup): a minute without one looks again
    bool announced = false;
    uint64_t seq = 0;
    auto lastGamescope = clk::now() - std::chrono::seconds(10);
    auto lastInventory = clk::now() - std::chrono::seconds(10);
    std::shared_ptr<const InventoryMap> inventories = std::make_shared<InventoryMap>();
    std::unordered_map<ptr, int> emptyReads; //actors whose last reads came back empty while their old rows are still shown
    auto rateStart = clk::now();
    int rateCount = 0;
    double hz = 0;
    auto next = clk::now(); //when the next step is due, in full clock precision so the set rate is the rate that runs

    while (!stop_) {
        const auto t0 = clk::now();

        if (!fixedPid_ && ProcessId != 0 && t0 - lastData > std::chrono::seconds(10) && !processAlive(ProcessId)) {
            std::cout << "[-] The game closed. Waiting for it to start again..." << std::endl;
            ProcessId = 0;
            cam_ = 0;
            announced = true;
        }
        if (!fixedPid_ && ProcessId == 0 && t0 - lastFind >= std::chrono::seconds(2)) {
            lastFind = t0;
            ProcessId = FindGamePID();
            if (ProcessId != 0) {
                announced = false;
                if (!FindGameImage(ProcessId)) std::cout << "[-] The game's .exe was not found in its memory map: using the usual address" << std::endl;
                gs = GameState(ImageBase); //new process: nothing from the old one is valid any more
                names::setBase(ImageBase); //also empties the name cache
                labeler::reset();
                lastData = t0;
                lastWorld = t0;
            } else if (!announced) {
                std::cout << OBF("[-] ARC Raiders is not running. Waiting for it...") << std::endl;
                announced = true;
            }
        }
        //A minute attached without a valid world: another Unreal game may have been picked. Look again and switch
        //if the lookup now gives a different process.
        if (!fixedPid_ && ProcessId != 0 && t0 - lastWorld >= std::chrono::seconds(60)) {
            lastWorld = t0;
            const pid_t p = FindGamePID();
            if (p != 0 && p != ProcessId) {
                std::cout << "[*] Switching from PID " << ProcessId.load() << " to PID " << p << std::endl;
                cam_ = 0;
                ProcessId = p;
                if (!FindGameImage(ProcessId)) std::cout << "[-] The game's .exe was not found in its memory map: using the usual address" << std::endl;
                gs = GameState(ImageBase);
                names::setBase(ImageBase); //also empties the name cache
                labeler::reset();
                lastData = t0;
            }
        }
        if (ProcessId == 0) {
            { std::lock_guard<std::mutex> lk(gsMu_); gs_ = gamescope::Info{}; }
            auto s = std::make_shared<Snapshot>();
            s->seq = ++seq;
            publish(std::move(s));
            sleepFor(NO_GAME_MS);
            continue;
        }

        if (t0 - lastGamescope >= std::chrono::seconds(2)) {
            lastGamescope = t0;
            gamescope::Info g = gamescope::find(ProcessId);
            std::lock_guard<std::mutex> lk(gsMu_);
            gs_ = g;
        }

        //Inventories of the players whose window is open, read here (the thread that owns the name cache)
        if (t0 - lastInventory >= std::chrono::milliseconds(500)) {
            lastInventory = t0;
            std::vector<ptr> want;
            { std::lock_guard<std::mutex> lk(inspMu_); want = inspected_; }
            auto m = std::make_shared<InventoryMap>();
            std::unordered_map<ptr, int> held;
            for (ptr a : want) {
                std::vector<ItemRow> rows = labeler::inventoryOf(a);
                //One failed read does not wipe the list: the previous rows stay for up to 3 empty reads (about 1.5 s)
                const auto prev = inventories->find(a);
                const auto miss = emptyReads.find(a);
                const int n = miss != emptyReads.end() ? miss->second : 0;
                if (rows.empty() && prev != inventories->end() && !prev->second.empty() && n < 3) { rows = prev->second; held[a] = n + 1; }
                (*m)[a] = std::move(rows);
            }
            emptyReads = std::move(held);
            inventories = m;
        }

        gsRet r = gs.step();
        if (!r.entities.empty()) lastData = t0;
        //A failed step soon after a good one is a hiccup (a page being remapped, a partial read): the good snapshot and
        //the camera stay (main.cpp ages them out) and the next step comes at the normal rate. Only a failure that lasts
        //is the main menu or a loading screen.
        const bool hiccup = !r.valid && t0 - lastValid < std::chrono::milliseconds(KEEP_MS);
        if (r.valid) lastValid = lastWorld = t0;
        if (!hiccup) cam_ = r.valid ? gs.cameraActor() : 0;

        auto s = std::make_shared<Snapshot>();
        s->entities = std::move(r.entities);
        s->vm = r.vm;
        s->localPlayerTeam = r.localPlayerTeam;
        s->selfHealth = r.selfHealth;
        s->selfShield = r.selfShield;
        s->valid = r.valid;
        if (!r.valid) s->fault = readDenied(ProcessId) ? "no permission to read the game: start the overlay with start.sh" : r.fault;
        s->gameFound = true;
        s->seq = ++seq;
        s->capturedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(clk::now().time_since_epoch()).count();
        s->inventories = inventories;
        if (!hiccup) publish(std::move(s));

        const auto t1 = clk::now();
        rateCount++;
        if (t1 - rateStart >= std::chrono::milliseconds(250)) {
            hz = rateCount / std::chrono::duration<double>(t1 - rateStart).count();
            rateCount = 0;
            rateStart = t1;
        }
        {
            GameState::Timing tm = gs.timing();
            std::lock_guard<std::mutex> lk(statMu_);
            stats_ = {hz, tm.lastStepMs, tm.lastScanMs, tm.statics, tm.dynamics};
        }

        if (!r.valid && !hiccup) sleepFor(IDLE_MS);
        else {
            const auto step = std::chrono::microseconds(1000000 / std::max(30, std::min(240, g_trackerHz.load(std::memory_order_relaxed))));
            next += step;
            if (next < t1) next = t1; //behind (a slow step, or back from the menu): go on at once, no burst to catch up
            else {
                //up to 15% of a step early or late, around an even grid, so the average rate stays the set one
                const auto jitter = std::chrono::duration_cast<clk::duration>(step * std::uniform_real_distribution<double>(-0.15, 0.15)(jitterRng));
                std::this_thread::sleep_until(next + jitter);
            }
        }
    }
    if (!fixedPid_) ProcessId = 0;
}
