#ifndef KWINHELPER_TRACKER_H
#define KWINHELPER_TRACKER_H
//Reads the game on its own thread, so drawing never waits for memory reads. It also finds the game process,
//notices when the game closes or restarts, and publishes the newest result for the draw thread to pick up.
#include <atomic>
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

class Tracker {
public:
    ~Tracker() { stop(); }
    //Tests only: read this process (with the game image at base) instead of looking for the game
    void useProcess(pid_t pid, ptr base) { fixedPid_ = pid; fixedBase_ = base; }

    void start();
    void stop();

    //Newest result. Never null. Cheap: call once per frame.
    std::shared_ptr<const Snapshot> latest() const;

    //The camera as it is right now (one small read), so markers follow the view without waiting for the next
    //tracker step. False when there is no camera yet; use the snapshot's then.
    bool freshCamera(FminimalViewInfo& out) const;

    //The gamescope the game was started in, if any (looked up every couple of seconds)
    gamescope::Info gamescopeInfo() const;

    //Players (their actor addresses) whose inventory is wanted; read about twice a second
    void setInspected(std::vector<ptr> actors);

    struct Stats { double updatesPerSec = 0; double stepMs = 0; double scanMs = 0; size_t statics = 0, dynamics = 0; };
    Stats stats() const;

private:
    void run();
    void publish(std::shared_ptr<const Snapshot> s);
    void sleepFor(int ms); //in short slices, so stop() is never slow

    pid_t fixedPid_ = 0;
    ptr fixedBase_ = 0;
    std::thread th_;
    std::atomic<bool> stop_{false};
    mutable std::mutex mu_;
    std::shared_ptr<const Snapshot> snap_ = std::make_shared<const Snapshot>();
    std::atomic<ptr> cam_{0};
    mutable std::mutex gsMu_;
    gamescope::Info gs_{};
    mutable std::mutex statMu_;
    Stats stats_{};
    mutable std::mutex inspMu_;
    std::vector<ptr> inspected_;
};

#endif //KWINHELPER_TRACKER_H
