#ifndef KWINHELPER_GAMESTATE_H
#define KWINHELPER_GAMESTATE_H

#include "config.h"
#include "utils/localUtil.h"

#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>
#include "memory/memory.h"


struct gsRet {
    std::vector<RenderEntity> entities{};
    FminimalViewInfo vm{};
    int localPlayerTeam{};
    bool valid = false; //false: no world / no camera (main menu, loading screen)
};

class GameState {
private:
    ptr baseAddr{};
    int lastTeamID = -1; //needed because local player is based off dist from camera,
    //if you roll, you go father, outside reasonable range, so need to save to memory

public:
    GameState(ptr baseAddr = 0x140000000);

    //Reads everything from scratch. Slow (about one read per actor) but simple: used by the diagnostic modes
    //(isDebugMode / probeNames) and by the tests as the reference for step().
    gsRet tick();

    //The normal way: a full scan of the actor list a few times a second, and in between only the things that move
    //(players, ARC) are re-read, with many reads per system call. Call it about 100 times a second.
    gsRet step();

    ptr cameraActor() const { return camActor_; } //where the camera lives (0 until found)

    struct Timing { double lastScanMs = 0; double lastStepMs = 0; size_t statics = 0, dynamics = 0; };
    Timing timing() const { return timing_; }

private:
    ptr getUworld();

    struct RawInfo {
        std::vector<RenderEntity> rawEnt{};
        std::vector<RenderEntity> unknown{}; //actors of other classes; only filled while probeNames is on
        FminimalViewInfo VM{};
    };
    RawInfo getRawEnt(ptr uworld);

    struct FilterRet {
        std::vector<RenderEntity> filtEnt{};
        int localPlayerTeam = -1;
    };
    FilterRet filterEnt(const std::vector<RenderEntity>& rawEnt, const FminimalViewInfo& vm);

    //--- step() (gamestate/Fast.cpp)
    struct Tracked {
        RenderEntity ent;
        ptr root = 0;       //RootComponent when last read
        bool live = false;  //passed this update's checks, so it is drawn
    };
    std::vector<Tracked> statics_; //containers and ground items: position read at each scan
    std::vector<Tracked> dyn_;     //players and ARC: re-read every step
    ptr camActor_ = 0;
    ptr lastWorld_ = 0;   //UWorld at the last scan: a different one (or none) means a full scan right now
    ptr meActor_ = 0;     //your own pawn at the last step (for the probes)
    std::unordered_map<ptr, double> shieldSeen_; //highest shield seen per player: the bar's full length when the game keeps no maximum
    std::chrono::steady_clock::time_point lastScan_{};
    Timing timing_{};

    bool scan();            //the whole actor list; false when there is no world yet
    void updateDynamic();   //position, health, team, dead for players and ARC
    gsRet assemble(const FminimalViewInfo& vm);
    void runLiveProbeIfAsked(const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm); //KWINHELPER_PROBE=shield|inventory
};

#endif //KWINHELPER_GAMESTATE_H
