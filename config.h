#ifndef KWINHELPER_CONFIG_H
#define KWINHELPER_CONFIG_H
#include <cstdint>
#include <string>
#include <vector>

//-------------------Config-------------------// btw m stands of meters
namespace config {
    //ESP Preferences
    inline bool makeLootESPTranslucent = false;       //true halves the alpha of loot and teammates (shared green)
    inline bool makeDeadBotESPTranslucent = false;
    inline bool makeDeadPlayerESPTranslucent = false;

    //What to draw. These are also the checkboxes in the Insert menu (changes there last until kwin-helper restarts)
    inline bool showPlayers = true;
    inline bool showArc = true;
    inline bool showPickups = true;     //items on the ground
    inline bool showContainers = true;  //lootable containers
    inline bool showDeadPlayers = true;
    inline bool showDeadArc = true;

    //Names next to the markers. Also in the Insert menu (Names).
    inline bool showPlayerNames = true;
    inline bool showPlayerGuns = true;     //the guns a player carries, under the box
    inline bool showArcNames = true;
    inline bool showContainerNames = true;
    inline bool showItemNames = true;
    inline double nameMaxLootDist = 25 /*<-Dist in m. Loot names further away stay hidden so the screen is not cluttered*/;
    inline float labelSize = 15.0f;        //text size in px
    inline bool showDebugInfo = false;     //small stats block in the top-left corner (fps, update rate, counts)

    //How things look (Insert menu: Look)
    inline double boxStyle = 0;            //player boxes: 0 corner brackets, 1 full rectangle, 2 sides only
    inline double boxColor = 0;            //enemy colour: 0 red, 1 white, 2 yellow, 3 blue, 4 green, 5 violet
    inline float boxThickness = 1.5f;      //px
    inline bool showTurrets = true;        //ARC turrets (separate from the other ARC)
    inline bool dimHidden = true;          //players the game is not drawing (behind walls, off screen) get a fainter box
    inline double hiddenAlpha = 120;       //how solid that fainter box is (40 faint to 255 full)
    inline bool showBars = true;           //health and shield bars beside a player's box
    inline bool showSkeleton = true;       //a stick figure on players (the box stays; players without a readable pose get the box only)
    inline double skeletonMaxDist = 120;   //m, further players only get the box: the pose is read for nearer ones only
    inline float overlayOpacity = 1.0f;    //everything the overlay draws (menu excluded) is multiplied by this
    inline double trackerHz = 120;         //how often players and ARC are read (30 to 240)
    inline double listMode = 1;            //player list at the bottom: 0 off, 1 always, 2 only while the menu is open
    inline double listRows = 8;            //most players shown in the list
    inline bool showOffscreen = true;      //arrows at the screen edge for enemy players you cannot see on screen
    inline bool closeWarning = true;       //a big "ENEMY 9 m BEHIND" card under the middle of the screen
    inline double warnDist = 25;           //m, enemies closer than this trigger that card
    inline bool showTeammates = false;     //draw your squad mates too (green); off = hidden like before
    inline bool espEnabled = true;         //master switch for boxes, labels and arrows (the radar has its own switch)
    inline bool labelCards = false;        //labels on a dark card instead of outlined text
    inline bool monochrome = false;        //draw everything in white and grey (shapes tell the kinds apart)
    inline bool useCameraRoll = true;      //take the camera's roll into account (always 0 in normal play)
    inline bool hideUnknownTeams = false;  //hide players whose team cannot be told (your own team is not known)
    inline bool hotkeysEnabled = true;     //F6 mark teammate, F7 clear marks, F10 quit
    inline bool menuKeyboard = false;      //"Keyboard controls the menu": while the menu is open its keys go to the menu, not the game
    inline bool streamHide = false;        //F8: draw nothing, so screen recordings and streams show no overlay (no startup banner)
    inline float menuOpacity = 0.94f;      //the Insert menu's background
    inline bool soundCues = true;          //say F6/F7/F8/F10 results and near enemies out loud (needs spd-say, utils/cue.h)
    inline double cueRange = 40;           //m, an enemy player first coming this close is said (at most one every 2 s)

    //Stretched resolution / gamescope (Insert menu: Game view). Auto reads gamescope's command line.
    inline bool gameViewAuto = true;
    inline double gameRenderW = 1440;      //only used when Auto is off: the game's own picture size
    inline double gameRenderH = 1080;
    inline bool gameStretch = true;        //Auto off: stretch that picture over the whole screen (else keep its shape)

    //Calibration (Insert menu: Calibration). The defaults are the old behaviour.
    //If markers drift away from their targets towards the screen edges, flip the FOV side first, then use Zoom.
    inline bool fovIsHorizontal = false;   //false: the game's FOV number is the vertical one
    inline float fovScale = 1.0f;          //multiplies the focal length: above 1 spreads the markers out from the middle
    inline float centerOffsetX = 0.0f;     //px, moves the middle of the projection
    inline float centerOffsetY = 0.0f;     //px

    //ESP Distance Config
    inline double maxPlayerDist = 400/*<-Dist in m*/;
    inline double maxArcDist = 150  /*<-Dist in m*/ ;
    inline double maxLootDist = 40 /*<-Dist in m*/  ;

    //If You want Radar
    inline bool enableRadar = true;
    inline float radarCenterX = 150; //coords px
    inline float radarCenterY = 150; //coords px
    inline float radarRadius = 100;  //size px
    inline float scale = 200.0f; //meters
    inline float selfDot = false;

    //Set this to your monitor resolution
    inline int SCREEN_H = 1440 /*<-Make you monitor height*/ * 0.94/*just trust me, this helps with distortion around screen edge*/;
    inline int SCREEN_W = 2560 /*<-Make you monitor width*/;

    inline const auto OVERLAY_NAME = "KWin Helper"; //Name of the overlay

}

//For updating the program
inline constexpr bool isDebugMode = false;
//true: scan every actor for the camera and print off::VIEW_MATRIX + vtabels::CAMMANAGER (with the
//      full per-frame debug output).
//false (with isDebugMode): print a short vtable list every 2s instead, to find the other vtables.
inline constexpr bool scanForCamera = false;
//true (with isDebugMode false): every 4 s print a "PROBE" report that checks the name / weapon / ARC-type
//offsets and finds which fields tell types apart. Read-only; replaces the one-line status while on.
inline constexpr bool probeNames = false;
//----------------------------------------------


using ptr = uintptr_t;
#define DBG if(!(isDebugMode && scanForCamera)) {} else

namespace off {
    // UWorld is now a 4-step pointer chain (v1029):
    //   p = [BASE + UWORLD_RVA]      (ULocalPlayer*)
    //   p = [p + UWORLD_CHAIN_1]     (PlayerController)
    //   p = [p + UWORLD_CHAIN_2]     (RootComponent)
    //   UWorld = [p + UWORLD_CHAIN_3]
    // After an update, grab the new {RVA, off1, off2, off3} from the
    // "Global" namespace in the UC Structs & Offsets thread.
    inline constexpr uintptr_t UWORLD_RVA     = 0x109D3010;
    inline constexpr uintptr_t UWORLD_CHAIN_1 = 0xA0;
    inline constexpr uintptr_t UWORLD_CHAIN_2 = 0x248;
    inline constexpr uintptr_t UWORLD_CHAIN_3 = 0x1B8;

    // PersistentLevel at UWorld+0x170 is now ENCRYPTED. Instead we read the
    // plain pointer out of LevelCollections[0]:
    //   levelCollection = [UWorld + LEVEL_COLLECTIONS]   (TArray data ptr)
    //   persistentLevel = [levelCollection + PERSISTENT_LEVEL]
    inline constexpr uintptr_t LEVEL_COLLECTIONS = 0x328; // UWorld::LevelCollections (TArray<FLevelCollection>)
    inline constexpr uintptr_t PERSISTENT_LEVEL  = 0x20;  // FLevelCollection::PersistentLevel

    // From PersistentLevel (ULevel): actor array base. Count is read at +0x8.
    inline constexpr uintptr_t ACTORS_PTR = 0x110; // Level::Actors (Level::ActorCount @ 0x118)

    inline constexpr uintptr_t ROOT_COMPONENT_PTR = 0x248; // AActor::RootComponent

    //Find by setting scanForCamera = true (below) and reading the console.
    // PCM::ViewTarget(0xDF0) + TViewTarget::POV(0x10) + MinimalViewInfo::Location(0x10)
    inline constexpr uintptr_t VIEW_MATRIX = 0xE10;
    inline constexpr uintptr_t POS_PTR = 0x3B0; // SceneComponent::ComponentToWorld(0x390) + translation(0x20)

    // "namespace Character" -> TeamId
    inline constexpr uintptr_t TEAM_ID = 0x802; //TeamId

    //find under // Class /Script/Engine.Character, "namespace Character"
    inline constexpr uintptr_t MESH = 0x448; //verified live (v1029): points at "CharacterMesh0"
    inline constexpr uintptr_t MESH_TRANSFORM = 0x390; //mesh ComponentToWorld: rotation (4 doubles), position at +0x20, scale at +0x40
    inline constexpr uintptr_t RENDER_TIME = 0x500;    //mesh: two floats, LastRenderTime and LastRenderTimeOnScreen (the game's clock when it last drew him; seen live)
    inline constexpr uintptr_t CONTROLLER = 0x3F8;     //pawn -> its PlayerController: set only on the pawn you control
    inline constexpr uintptr_t BONE_ARRAY = 0xBE8;     //mesh -> TArray of 97 bone transforms (0x60 bytes each), each relative to its parent bone

    //im just guessing for this one
    // find under "namespace EmbarkCharacterMovementComponentBase" OR "namespace CharacterMovementComponent"
    inline constexpr uintptr_t LAST_SUBMIT_TIME = 0x4CC; //NetworkMinTimeBetweenClientAckGoodMoves

    inline constexpr uintptr_t IS_DESTROYED = 0x1230; //PioneerConstructablePawn::bIsDestroyed

    //Not in the public dumps (found with the vtable probe): player actor -> HealthComponent -> health numbers
    inline constexpr uintptr_t HEALTH_COMPONENT = 0xD98; //(v1029)
    inline constexpr uintptr_t HEALTH_MAX = 0x380;       //MaxHealth (double)
    inline constexpr uintptr_t HEALTH_CACHED = 0x708;    //CachedHealth (double)
}

// !!! ARC / SEARCH / PICKUP / PLAYER are STALE for v1029 (CAMMANAGER is current).
// These are RUNTIME addresses, not in any offset dump.
// They change every game update and MUST be rediscovered in debug mode:
//   1) isDebugMode = true and scanForCamera = true above, rebuild, run in a raid (not scoped)
//      and copy the printed "vtabels::CAMMANAGER" value in here
//   2) set scanForCamera = false, rebuild: the console now lists every vtable nearest-first every 2s
//      (and the overlay labels objects with theirs). Your own player is the ~0 m row = PLAYER;
//      stand next to a loot container / ground item / ARC robot to find SEARCH / PICKUP / ARC
namespace vtabels {

    // v1029 best guesses from the live vtable lists (2026-11-20). SEARCH is confirmed (nearest 2.0 m while
    // looting a trash can). The others are inferred from how each row behaved:
    inline constexpr uintptr_t ARC = 0x14e12b1c0;     // count 40-55, moves while you stand still, closed to 3 m when a Shredder killed me
    inline constexpr uintptr_t SEARCH = 0x14e0e5de0;  // count ~650-740, static
    inline constexpr uintptr_t PICKUP = 0x14dc5a3c0;  // count 4-9, static, 2.9 m while standing by gun parts on the ground
    inline constexpr uintptr_t PLAYER = 0x14e337000;  // count 14, always 1.5-1.9 m from the camera (third person). Alt: 0x14d3b03e0
    inline constexpr uintptr_t CAMMANAGER = 0x14d8af9d0; //v1029, found by the camera scan (VIEW_MATRIX 0xE10 confirmed live)
}

#endif //KWINHELPER_CONFIG_H