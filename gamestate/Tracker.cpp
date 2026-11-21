#include "Tracker.h"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

#include "utils/labeler.h"
#include "utils/names.h"

namespace {
using clk = std::chrono::steady_clock;
constexpr int IDLE_MS = 250;      //main menu / loading: look again a few times a second
constexpr int NO_GAME_MS = 200;

bool processAlive(pid_t pid) { return pid > 0 && access(("/proc/" + std::to_string(pid)).c_str(), F_OK) == 0; }
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
    const bool ok = std::isfinite(v.FOV) && std::abs(v.FOV - 70.0) <= 60.0 &&
                    std::isfinite(v.Location.x) && std::isfinite(v.Location.y) && std::isfinite(v.Location.z) &&
                    std::isfinite(v.Rotation.x) && std::isfinite(v.Rotation.y) && std::isfinite(v.Rotation.z);
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
    bool announced = false;
    uint64_t seq = 0;
    auto lastGamescope = clk::now() - std::chrono::seconds(10);
    auto lastInventory = clk::now() - std::chrono::seconds(10);
    std::shared_ptr<const InventoryMap> inventories = std::make_shared<InventoryMap>();
    auto rateStart = clk::now();
    int rateCount = 0;
    double hz = 0;

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
                gs = GameState(); //new process: nothing from the old one is valid any more
                names::clearCache();
                labeler::reset();
                lastData = t0;
            } else if (!announced) {
                std::cout << "[-] ARC Raiders is not running. Waiting for it..." << std::endl;
                announced = true;
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
            gamescope::Info g = gamescope::find();
            std::lock_guard<std::mutex> lk(gsMu_);
            gs_ = g;
        }

        //Inventories of the players whose window is open, read here (the thread that owns the name cache)
        if (t0 - lastInventory >= std::chrono::milliseconds(500)) {
            lastInventory = t0;
            std::vector<ptr> want;
            { std::lock_guard<std::mutex> lk(inspMu_); want = inspected_; }
            auto m = std::make_shared<InventoryMap>();
            for (ptr a : want) (*m)[a] = labeler::inventoryOf(a);
            inventories = m;
        }

        gsRet r = gs.step();
        if (!r.entities.empty()) lastData = t0;
        cam_ = r.valid ? gs.cameraActor() : 0;

        auto s = std::make_shared<Snapshot>();
        s->entities = std::move(r.entities);
        s->vm = r.vm;
        s->localPlayerTeam = r.localPlayerTeam;
        s->valid = r.valid;
        s->gameFound = true;
        s->seq = ++seq;
        s->capturedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(clk::now().time_since_epoch()).count();
        s->inventories = inventories;
        publish(std::move(s));

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

        const int spent = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        if (!r.valid) sleepFor(IDLE_MS);
        else {
            const int stepMs = std::max(4, 1000 / std::max(30, std::min(240, g_trackerHz.load(std::memory_order_relaxed))));
            if (spent < stepMs) sleepFor(stepMs - spent);
        }
    }
    if (!fixedPid_) ProcessId = 0;
}
