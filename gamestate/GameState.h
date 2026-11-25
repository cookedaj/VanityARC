#ifndef KWINHELPER_GAMESTATE_H
#define KWINHELPER_GAMESTATE_H

#include "config.h"
#include "utils/localUtil.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>
#include "memory/memory.h"

//Set by the drawing thread from the settings, read here: read players' poses only when they will be drawn
inline std::atomic<bool> g_skeletonOn{true};
inline std::atomic<int> g_skeletonDist{120}; //m

struct gsRet {
    std::vector<RenderEntity> entities{};
    FminimalViewInfo vm{};
    int localPlayerTeam{};
    bool valid = false; //false: no world / no camera (main menu, loading screen)
    const char* fault = nullptr; //when !valid: why, in a few words for the screen (nullptr: not known)
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
    FminimalViewInfo goodVm_{};                          //the last usable view, and when it was read: one bad read reuses it
    std::chrono::steady_clock::time_point goodVmAt_{};
    ptr lastWorld_ = 0;   //UWorld at the last scan: a different one (or none) means a full scan right now
    ptr meActor_ = 0;     //your own pawn at the last step (for the probes)
    Vector3 lastCamPos_;  //where the camera was at the last step: only players near it get their pose read
    std::unordered_map<ptr, double> shieldSeen_; //highest shield seen per player: the bar's full length when the game keeps no maximum
    //The game stops animating a player it is not drawing: his bone list then stops changing. Per player: a hash of the
    //list as last read and when it last differed (a list that has not changed for a while is "frozen"), how fast he
    //moves, and the last pose that could be read whole (a half-written list is replaced by it for a moment).
    struct PoseState {
        bool init = false;
        uint64_t hash = 0;
        std::chrono::steady_clock::time_point changed{};
        bool havePos = false;
        Vector3 lastPos;
        std::chrono::steady_clock::time_point lastPosAt{};
        double speed = 0;                      //cm/s on the ground
        bool haveGood = false;
        skeleton::Pose good;                   //mesh space
        std::chrono::steady_clock::time_point goodAt{};
        std::vector<uint8_t> bones;            //the bone list as last read whole, reused between reads (BONE_PERIOD)
        std::chrono::steady_clock::time_point bonesAt{};
    };
    std::unordered_map<ptr, PoseState> poseSeen_;
    //What a standing / crouched, idle / moving player looks like, learned from players the game IS animating (you
    //included). Frozen players are drawn with the one that matches their stance and speed. Index: crouch * 2 + moving.
    float clockVal_ = -1;                              //the game's clock as the newest render time showed it, and when
    std::chrono::steady_clock::time_point clockAt_{};
    skeleton::Pose tmpl_[4];
    bool haveTmpl_[4] = {false, false, false, false};
    std::chrono::steady_clock::time_point lastScan_{};
    Timing timing_{};
    const char* fault_ = nullptr; //why the last scan() failed (shown on screen)

    bool scan();            //the whole actor list; false when there is no world yet
    void updatePose(Tracked& t, const std::vector<uint8_t>& bones, const skeleton::Xform& mesh, bool freshBones);
    void updateDynamic();   //position, health, team, dead for players and ARC
    gsRet assemble(const FminimalViewInfo& vm);
    void runLiveProbeIfAsked(const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm); //KWINHELPER_PROBE=shield|inventory
};

#endif //KWINHELPER_GAMESTATE_H
