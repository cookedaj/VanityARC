#pragma once
//Skeleton of a player. The game keeps 97 bone transforms, each relative to its parent bone. To draw a stick figure
//they are chained from the root down (each bone = its own transform applied inside its parent's) and then moved
//into the world with the mesh's own transform. Everything here is plain maths on numbers already read from memory.
#ifndef KWINHELPER_SKELETON_H
#define KWINHELPER_SKELETON_H

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "gameUtil.h"

namespace skeleton {

constexpr int BONES = 97;          //the player skeleton (Avatar_Skeleton)
constexpr size_t RECORD = 0x60;    //bytes per bone transform: quaternion at +0, position at +0x20, scale at +0x40 (doubles)

//Parent of each bone (-1 = none), read from the skeleton asset. A parent always comes before its children.
constexpr int8_t PARENT[BONES] = {
    -1, 0, 1, 2, 3, 4, 5, 6, 4, 8, 9, 10, 11, 12, 13, 14, 11, 16, 17, 18, 11, 20, 21, 22, 11, 24, 25, 26, 11, 28, 29,
    4, 31, 32, 32, 34, 35, 36, 37, 38, 32, 31, 4, 42, 43, 44, 45, 46, 47, 48, 45, 50, 51, 52, 45, 54, 55, 56, 45, 58, 59,
    60, 45, 62, 63, 1, 65, 66, 67, 1, 69, 70, 71, 0, 73, 73, 0, 0, 77, 77, 32, 32, 11, 45, 73, 84, 73, 86, 0, 41, 45, 11,
    0, 0, 0, 33, 33};

//The bones that make the stick figure. The order is the order of Pose::j.
constexpr int JOINT_COUNT = 23;
constexpr int JOINT_BONE[JOINT_COUNT] = {
    1, 2, 3, 4,          //pelvis, three spine bones
    5, 6, 7,             //neck, neck, head
    8, 9, 10, 11,        //shoulder, upper arm, forearm, hand
    42, 43, 44, 45,      //the other arm
    65, 66, 67, 68,      //hip, knee, ankle, toe
    69, 70, 71, 72};     //the other leg
enum Joint { PELVIS, SPINE1, SPINE2, SPINE3, NECK1, NECK2, HEAD, SHOULDER_A, ARM_A, FOREARM_A, HAND_A,
             SHOULDER_B, ARM_B, FOREARM_B, HAND_B, HIP_A, KNEE_A, ANKLE_A, TOE_A, HIP_B, KNEE_B, ANKLE_B, TOE_B };

//Lines of the stick figure, as pairs of joints
constexpr int LINE_COUNT = 22;
constexpr int LINES[LINE_COUNT][2] = {
    {PELVIS, SPINE1}, {SPINE1, SPINE2}, {SPINE2, SPINE3}, {SPINE3, NECK1}, {NECK1, NECK2}, {NECK2, HEAD},
    {SPINE3, SHOULDER_A}, {SHOULDER_A, ARM_A}, {ARM_A, FOREARM_A}, {FOREARM_A, HAND_A},
    {SPINE3, SHOULDER_B}, {SHOULDER_B, ARM_B}, {ARM_B, FOREARM_B}, {FOREARM_B, HAND_B},
    {PELVIS, HIP_A}, {HIP_A, KNEE_A}, {KNEE_A, ANKLE_A}, {ANKLE_A, TOE_A},
    {PELVIS, HIP_B}, {HIP_B, KNEE_B}, {KNEE_B, ANKLE_B}, {ANKLE_B, TOE_B}};

struct Quat { double x = 0, y = 0, z = 0, w = 1; };
struct Xform { Quat q; Vector3 t; Vector3 s{1, 1, 1}; };

//The stick figure in world space (centimetres)
struct Pose { Vector3 j[JOINT_COUNT]; };

inline Quat mul(const Quat& a, const Quat& b) { //Hamilton product: b first, then a
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline Vector3 rotate(const Quat& q, const Vector3& v) {
    const Vector3 u(q.x, q.y, q.z);
    const Vector3 c1(u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x);
    const Vector3 t(2 * c1.x, 2 * c1.y, 2 * c1.z);
    const Vector3 c2(u.y * t.z - u.z * t.y, u.z * t.x - u.x * t.z, u.x * t.y - u.y * t.x);
    return Vector3(v.x + q.w * t.x + c2.x, v.y + q.w * t.y + c2.y, v.z + q.w * t.z + c2.z);
}

inline Vector3 scaled(const Vector3& a, const Vector3& b) { return Vector3(a.x * b.x, a.y * b.y, a.z * b.z); }

//"local" inside "parent": the same order Unreal uses (local * parent)
inline Xform compose(const Xform& local, const Xform& parent) {
    Xform r;
    r.q = mul(parent.q, local.q);
    const Vector3 p = rotate(parent.q, scaled(parent.s, local.t));
    r.t = Vector3(p.x + parent.t.x, p.y + parent.t.y, p.z + parent.t.z);
    r.s = scaled(local.s, parent.s);
    return r;
}

//The raw bytes of a bone list (BONES records) into transforms
inline void decode(const uint8_t* bytes, Xform out[BONES]) {
    for (int i = 0; i < BONES; i++) {
        const uint8_t* r = bytes + (size_t)i * RECORD;
        double q[4], t[3], s[3];
        std::memcpy(q, r, 32);
        std::memcpy(t, r + 0x20, 24);
        std::memcpy(s, r + 0x40, 24);
        out[i].q = {q[0], q[1], q[2], q[3]};
        out[i].t = Vector3(t[0], t[1], t[2]);
        out[i].s = Vector3(s[0], s[1], s[2]);
    }
}

//Every bone relative to the mesh instead of relative to its parent. False when a bone is not a believable transform
//(a half-written list): the caller then draws nothing for this player.
inline bool chain(const Xform local[BONES], Xform comp[BONES]) {
    for (int i = 0; i < BONES; i++) {
        const Xform& l = local[i];
        const double n = std::sqrt(l.q.x * l.q.x + l.q.y * l.q.y + l.q.z * l.q.z + l.q.w * l.q.w);
        if (!std::isfinite(n) || std::fabs(n - 1.0) > 0.05) return false;
        if (!std::isfinite(l.t.x + l.t.y + l.t.z) || !std::isfinite(l.s.x + l.s.y + l.s.z)) return false;
        comp[i] = PARENT[i] < 0 ? l : compose(l, comp[PARENT[i]]);
    }
    return true;
}

//The joints in the mesh's own space (centimetres, before the mesh's rotation, scale and position are applied).
//False when a bone is not a believable transform (a half-written or all-zero list).
inline bool componentPose(const Xform local[BONES], Pose& out) {
    Xform comp[BONES];
    if (!chain(local, comp)) return false;
    for (int k = 0; k < JOINT_COUNT; k++) {
        out.j[k] = comp[JOINT_BONE[k]].t;
        if (!std::isfinite(out.j[k].x + out.j[k].y + out.j[k].z)) return false;
    }
    return true;
}

//A pose in mesh space put into the world. "mesh" is the mesh's own transform (where the character stands and which way
//it faces). Rejects poses that are not near "anchor" (the player's position): old or garbage data is drawn as nothing.
inline bool placePose(const Pose& comp, const Xform& mesh, const Vector3& anchor, Pose& out) {
    const double mn = std::sqrt(mesh.q.x * mesh.q.x + mesh.q.y * mesh.q.y + mesh.q.z * mesh.q.z + mesh.q.w * mesh.q.w);
    if (!std::isfinite(mn) || std::fabs(mn - 1.0) > 0.05) return false; //the mesh transform was not read
    for (int k = 0; k < JOINT_COUNT; k++) {
        const Vector3 p = rotate(mesh.q, scaled(mesh.s, comp.j[k]));
        out.j[k] = Vector3(p.x + mesh.t.x, p.y + mesh.t.y, p.z + mesh.t.z);
        if (!std::isfinite(out.j[k].x + out.j[k].y + out.j[k].z) || out.j[k].Dist(anchor) > 300.0) return false;
    }
    return true;
}

//The stick figure in the world, straight from a bone list
inline bool worldPose(const Xform local[BONES], const Xform& mesh, const Vector3& anchor, Pose& out) {
    Pose comp;
    return componentPose(local, comp) && placePose(comp, mesh, anchor, out);
}

} // namespace skeleton

#endif //KWINHELPER_SKELETON_H
