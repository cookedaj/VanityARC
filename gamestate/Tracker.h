#ifndef KWINHELPER_TRACKER_H
#define KWINHELPER_TRACKER_H
//Reads the game on its own thread, so drawing never waits for memory reads. It also finds the game process,
//notices when the game closes or restarts, and publishes the newest result for the draw thread to pick up.
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "GameState.h"
#include "utils/gamescope.h"

using InventoryMap = std::unordered_map<ptr, std::vector<ItemRow>>;

//How often players and ARC are read (the main loop copies the "Update rate" setting here)
inline std::atomic<int> g_trackerHz{120};

struct Snapshot {
    std::vector<RenderEntity> entities;
    FminimalViewInfo vm{};
    int localPlayerTeam = -1;
    bool valid = false;      //in a raid: world and camera were read
    bool gameFound = false;  //the game process exists
    bool gameUpdated = false; //the game's build stamp is known and differs from GAME_PE_STAMP: the offsets may be stale
    const char* fault = nullptr; //when !valid: why there is no data, in a few words for the screen (nullptr: not known)
    uint64_t seq = 0;
    int64_t capturedNs = 0;  //steady_clock time when this was read: the draw loop ignores data that is too old
    std::shared_ptr<const InventoryMap> inventories; //players whose inventory window is open (see setInspected)
    double selfHealth = -1, selfShield = -1; //your own health and shield (-1: not read, nothing is shown)
};

//Data older than this is not drawn: after a stall the markers would be where things were, not where they are
constexpr double STALE_MS = 250.0;
//Without the live camera only this new: through the snapshot's own (old) camera the markers would slide off what they mark
constexpr double NO_CAMERA_MS = 50.0;
//Gate for the draw-thread camera read: skip the extra ReadMemory when the snapshot is younger than this.
//At 120 Hz tracker rate (~8 ms period) a 4 ms gate skips roughly half the frames; the snapshot's own camera
//is close enough when it was just taken. ponytail: fixed 4 ms, tie to g_trackerHz if jitter shows.
constexpr double FRESH_CAMERA_GATE_MS = 4.0;

//How old a snapshot is at nowNs (steady_clock ns), in ms. One never read (capturedNs 0) counts as very old.
inline double snapshotAgeMs(const Snapshot& s, int64_t nowNs) { return s.capturedNs ? (nowNs - s.capturedNs) / 1e6 : 1e9; }
//In a raid and new enough to draw (the draw loop shows no entities otherwise)
inline bool snapshotIsLive(const Snapshot& s, double ageMs, bool freshCam) {
    return s.valid && ageMs <= (freshCam ? STALE_MS : NO_CAMERA_MS);
}

bool processAlive(pid_t pid); //the process still exists (/proc/<pid> is there)

//How the tracker finds the game and notices that it closed. Tests put stand-ins here (Tracker::useLookup).
struct GameLookup {
    std::function<pid_t()> find = FindGamePID;        //the game's pid; 0: not running
    std::function<bool(pid_t)> image = FindGameImage; //points ImageBase / ImageEnd at the game's .exe
    std::function<bool(pid_t)> alive = processAlive;
    int deadAfterMs = 10000; //a game that gives no data this long is checked for having closed
};

class Tracker {
public:
    ~Tracker() { stop(); }
    //Tests only: read this process (with the game image at base) instead of looking for the game
    void useProcess(pid_t pid, ptr base) { fixedPid_ = pid; fixedBase_ = base; }
    //Tests only: find the game, and notice it closed, with these instead of /proc. Call before start().
    void useLookup(GameLookup l) { lookup_ = std::move(l); }

    void start();
    void stop();

    //Newest result. Never null. Cheap: call once per frame.
    std::shared_ptr<const Snapshot> latest() const;

    //The camera as it is right now (one small read), so markers follow the view without waiting for the next
    //tracker step. False when there is no camera yet; use the snapshot's then.
    bool freshCamera(FminimalViewInfo& out) const;

    //Gated: only does the read when the snapshot is older than FRESH_CAMERA_GATE_MS.
    //Pass snapshotAgeMs(*snap, now) as snapAgeMs. When the snapshot is very fresh its own camera is accurate
    //enough and the per-frame ReadMemory is skipped.
    bool freshCameraGated(FminimalViewInfo& out, double snapAgeMs) const {
        if (snapAgeMs < FRESH_CAMERA_GATE_MS) return false;
        return freshCamera(out);
    }

    //The gamescope the game was started in, if any (looked up every couple of seconds)
    gamescope::Info gamescopeInfo() const;

    //Players (their actor addresses) whose inventory is wanted; read about twice a second
    void setInspected(std::vector<ptr> actors);

    struct Stats { double updatesPerSec = 0; double stepMs = 0; double scanMs = 0; size_t statics = 0, dynamics = 0; };
    Stats stats() const;

private:
    void run();
    void publish(std::shared_ptr<const Snapshot> s);
    void fail(const char* what, const char* fault) noexcept; //logs what, publishes an invalid snapshot with fault (a literal) for the screen
    void sleepFor(int ms); //in short slices, so stop() is never slow

    pid_t fixedPid_ = 0;
    ptr fixedBase_ = 0;
    GameLookup lookup_;
    std::thread th_;
    std::atomic<bool> stop_{false};
    mutable std::mutex mu_;
    std::shared_ptr<const Snapshot> snap_ = std::make_shared<const Snapshot>();
    std::atomic<ptr> cam_{0};
    std::atomic<bool> updated_{false}; //the running build differs from the one config.h is for (kept across run() restarts, so fail() shows it too)
    mutable std::mutex gsMu_;
    gamescope::Info gs_{};
    mutable std::mutex statMu_;
    Stats stats_{};
    mutable std::mutex inspMu_;
    std::vector<ptr> inspected_;
};

#endif //KWINHELPER_TRACKER_H
