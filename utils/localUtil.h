#pragma once

#ifndef KWINHELPER_LOCALUTIL_H
#define KWINHELPER_LOCALUTIL_H
#include <cmath>
#include <cstdint>
#include <deque>
#include <fstream>
#include <memory>
#include <string>

#include "config.h"
#include "../memory/memory.h"
#include "gameUtil.h"
#include "skeleton.h"

inline bool isValidPtr(ptr addr) {
    if (addr < 0x10000 || addr > 0x800000000000) return false;
    return true;
}

//The yaw in degrees (0 along +X, 90 along +Y) of a rotation quaternion as the game stores it (x, y, z, w). NaN when
//the four numbers are not a rotation (a failed read, a half-built actor), so nothing is drawn from them.
inline double yawFromQuat(const double q[4]) {
    const double n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (!std::isfinite(n) || std::abs(n - 1.0) > 0.01) return NAN;
    return std::atan2(2 * (q[3] * q[2] + q[0] * q[1]), 1 - 2 * (q[1] * q[1] + q[2] * q[2])) * 180.0 / 3.14159265358979;
}

enum Object {
    ARC,
    SEARCH,
    PICKUP,
    PLAYER,
    NONE
};

//One kind of item in a player's inventory (the inventory window)
struct ItemRow {
    std::string name;      //"Ferro", "Bandage"
    std::string key;       //internal name ("Bandage_01"): rows merge on it, prices look it up before the name
    std::string category;  //which list it came from (offsets.txt)
    int count = 1;
};

struct RenderEntity {
    ptr actor;
    Vector3 pos;         //Position
    double yaw = NAN;    //players and ARC: which way it faces, the game's yaw in degrees (see yawFromQuat); NaN when unknown
    double dist{};       //Dist, may not be accurate until "filterEntities(...)"
    double playerHealth = 100; //Only for players
    double shield = 0;         //Only for players, and only when the shield offset is known (utils/offsets.h)
    double shieldMax = 0;
    bool hasShield = false;    //false: the shield could not be read, so no shield bar
    ptr vt;
    enum Object type = Object::NONE; //vt enum
    bool isDead = false; //Is dead, may not be accurate until "filterEntities(...)"
    bool isVisible = true; //Only for players: the game drew him a moment ago (on screen, not behind cover)
    bool isLocal = false;  //Only for players: the pawn you control (it has a PlayerController)
    int teamID = -1; //some random value
    std::string label;   //name shown on screen ("Tick", "Trash can", "Ferro", a player name); filled by labeler::fill
    std::string sub;     //second line: the guns a player carries
    std::string account; //players: the account id as hex text (stable between games), "" when unknown
    bool isTurret = false; //ARC: a turret
    std::shared_ptr<const skeleton::Pose> pose; //players: the stick figure in world space, null when it could not be read
    bool poseLive = true; //false: the game has stopped animating this player (not drawn / out of range), so the pose is old
};

#endif //KWINHELPER_LOCALUTIL_H