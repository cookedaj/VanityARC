#ifndef KWINHELPER_GAMESTATE_H
#define KWINHELPER_GAMESTATE_H

#include "config.h"
#include "utils/localUtil.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "memory/memory.h"

//Set by the drawing thread from the settings, read here: read players' poses only when they will be drawn
inline std::atomic<bool> g_skeletonOn{true};
inline std::atomic<int> g_skeletonDist{120}; //m (also no further than players are drawn at all)
inline std::atomic<bool> g_showTeammates{true}; //squad mates are drawn (until the drawing thread says: assumed so)
inline std::atomic<double> g_viewSpread{4.0};    //the picture's corners (with DrawESP's margin) as a tangent per unit tan(FOV/2); until known: wide
//The loot range plus a margin (m): containers and ground items further away are not named or put in the snapshot
inline std::atomic<int> g_lootRange{50};
//The larger of the ARC range and the radar range (m). ARC well beyond it are drawn nowhere, so they are read less often.
inline std::atomic<double> g_arcDrawDist{200};

struct gsRet {
    std::vector<RenderEntity> entities{};
    FminimalViewInfo vm{};
    int localPlayerTeam{};
    bool valid = false; //false: no world / no camera (main menu, loading screen)
    const char* fault = nullptr; //when !valid: why, in a few words for the screen (nullptr: not known)
    double selfHealth = -1;      //your own health, only from a read that worked (-1: not known, nothing is shown)
    double selfShield = -1;      //your own shield, the same way (-1: not known or no shield offset)
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
    //The camera through the PlayerController the UWorld chain passes (off::PC_CAMERA_MANAGER), for when no actor has the
    //CAMMANAGER vtable (that address moves with every patch); 0 when not set or its view is not usable. vtableCam: the
    //camera the vtable found (0: none), returned as it is; while it is found, the field holding it is printed once.
    ptr cameraViaController(ptr vtableCam);
    bool camFieldTold_ = false;

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
        ptr root = 0;       //RootComponent, found by the scan
        ptr hc = 0, sc = 0, mesh = 0; //a player's health, shield and mesh components: found once per scan (0: looked for again)
        std::chrono::steady_clock::time_point readAt{}; //when updateDynamic last read it (far ARC are read less often)
        bool live = false;  //passed this update's checks, so it is drawn
        bool hpRead = false;     //players: playerHealth came from a read that worked since the scan (not the stand-in 100)
        double shieldRead = -1;  //players: the shield from the last read that worked since the scan (-1: none)
        bool named = false; //statics: refine/fill done since the last scan (done when it first comes within g_lootRange)
    };
    std::vector<Tracked> statics_; //containers and ground items: position read at each scan
    std::vector<Tracked> dyn_;     //players and ARC: re-read every step (ARC far beyond any range about 15 times a second)
    //Each slot of the actor list as the last scan saw it: the actor, its vtable and its RootComponent (0: read it again).
    //A scan reads the vtable only for slots that changed plus a rotating 1/20 of the rest (all within about 2 s).
    struct Slot { ptr actor = 0, vt = 0, root = 0; };
    std::vector<Slot> slots_;
    size_t recheck_ = 0;           //first slot of the next scan's rotating re-check
    ptr camActor_ = 0;
    FminimalViewInfo goodVm_{};                          //the last usable view, and when it was read: one bad read reuses it
    std::chrono::steady_clock::time_point goodVmAt_{};
    ptr lastWorld_ = 0;   //UWorld at the last scan: a different one (or none) at a world check means a full scan right now
    std::chrono::steady_clock::time_point worldAt_{}; //when the UWorld chain was last read (see step())
    ptr meActor_ = 0;     //your own pawn at the last step (for the probes)
    FminimalViewInfo lastView_{}; //the camera at the last step: only players near it and in front of it get their pose read
    std::unordered_map<ptr, double> shieldSeen_; //highest shield seen per player: the bar's full length when the game keeps no maximum
    double shieldCap_ = 0; //highest shield any player held still on two steps in a row (under 1000), for the whole session
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
        bool speedKnown = false;               //speed measured at least once (until then it reads 0: idle)
        bool haveGood = false;
        skeleton::Pose good;                   //mesh space
        std::chrono::steady_clock::time_point goodAt{};
        std::vector<uint8_t> bones;            //the bones the figure uses (skeleton::READ_RANGES) as last read whole, reused between reads (BONE_PERIOD)
        std::chrono::steady_clock::time_point bonesAt{};
    };
    std::unordered_map<ptr, PoseState> poseSeen_;
    //Per player: his LastRenderTimeOnScreen as last read, and when it last changed (the game rewrites it every frame it draws him)
    struct DrawnState { float rt = 0; std::chrono::steady_clock::time_point changed{}; };
    std::unordered_map<ptr, DrawnState> drawnSeen_;
    //What a standing / crouched, idle / moving player looks like, learned from players the game IS animating (you
    //included). Frozen players are drawn with the one that matches their stance and speed. Index: crouch * 2 + moving.
    skeleton::Pose tmpl_[4];
    bool haveTmpl_[4] = {false, false, false, false};
    //Stick figures at the last step: players near enough for one, how many got one, and why the first one that did not
    //failed. poseOkAt_: the last step where one did (or nobody needed one); 5 s without one is said on the console.
    int poseWanted_ = 0, posed_ = 0;
    std::string poseWhy_;
    std::chrono::steady_clock::time_point poseOkAt_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastScan_{};
    Timing timing_{};
    const char* fault_ = nullptr; //why the last scan() failed (shown on screen)
    //Vtables config.h does not know (its constants go stale with every game patch): what their actors' names said.
    //A kind is kept until the world changes; NONE is asked again after a while (next).
    struct Learned { Object kind = Object::NONE; int votes = 0; bool decided = false; std::chrono::steady_clock::time_point next{}; };
    std::unordered_map<ptr, Learned> learned_;
    //KWINHELPER_SELFCHECK (Fast.cpp). stage: 0 off or done, 1 looking for you, 2 first look taken (the report comes 1 s later)
    struct SelfCheck { int stage = 0; int looks = 0; ptr me = 0; float rt = 0; std::chrono::steady_clock::time_point at{}; };
    SelfCheck self_;

    bool scan(ptr uworld);  //the whole actor list of this world (just read by step()); false when there is no world yet
    void learnKinds(const std::unordered_map<ptr, std::pair<int, ptr>>& unsure, std::chrono::steady_clock::time_point now);
    const char* updatePose(Tracked& t, const std::vector<uint8_t>& bones, const skeleton::Xform& mesh, bool freshBones); //nullptr: posed, else why not
    void updateDynamic(FminimalViewInfo& vm); //position, health, team, dead for players and ARC; also the camera's view into vm (left as is without a camera)
    gsRet assemble(const FminimalViewInfo& vm);
    void runLiveProbeIfAsked(const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm); //KWINHELPER_PROBE=shield|inventory
    void selfCheckIfAsked(const FminimalViewInfo& vm); //KWINHELPER_SELFCHECK=1: PASS / FAIL per game number, once in a raid
};

#endif //KWINHELPER_GAMESTATE_H
