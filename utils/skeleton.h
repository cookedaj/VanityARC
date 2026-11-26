#pragma once
//Skeleton of a player. The game keeps 97 bone transforms, each relative to its parent bone. To draw a stick figure
//they are chained from the root down (each bone = its own transform applied inside its parent's) and then moved
//into the world with the mesh's own transform. Everything here is plain maths on numbers already read from memory.
#ifndef KWINHELPER_SKELETON_H
#define KWINHELPER_SKELETON_H

#include <algorithm>
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

//The only bones the stick figure needs: its joints and every bone above them, as [first, last] ranges. Only these are
//read from the game (24 of the 97), packed one range after another; decodeRead() puts them back in their places.
constexpr int READ_RANGES[3][2] = {{0, 11}, {42, 45}, {65, 72}};
constexpr int READ_BONES = 24;

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

//The stick figure in world space (centimetres). A joint that could not be read is NaN and drawn as nothing.
struct Pose { Vector3 j[JOINT_COUNT]; };

inline bool jointOk(const Vector3& p) { return std::isfinite(p.x + p.y + p.z); }
inline Vector3 badJoint() { return Vector3(NAN, NAN, NAN); }
inline bool whole(const Pose& p) { for (const Vector3& j : p.j) if (!jointOk(j)) return false; return true; }
inline Vector3 lerp(const Vector3& a, const Vector3& b, double f) {
    return Vector3(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f);
}

constexpr double HEAD_UP = 12.0;     //the skull's centre is about 12 cm beyond the head bone, along the neck
constexpr double HEAD_RADIUS = 9.0;  //a head is about 9 cm in radius

//Where the skull's centre is: a little further up the neck's direction than the head bone. The drawing puts the head
//marker there and the box reaches over it. NaN when the head is not known.
inline Vector3 headCentre(const Pose& p) {
    const Vector3& head = p.j[HEAD];
    const Vector3& neck = p.j[NECK2];
    if (!jointOk(head)) return badJoint();
    Vector3 d = jointOk(neck) ? head - neck : Vector3(0, 0, 1);
    double len = std::sqrt(d.Dot(d));
    if (!(len > 1e-3) || !std::isfinite(len)) { d = Vector3(0, 0, 1); len = 1; }
    return lerp(head, Vector3(head.x + d.x, head.y + d.y, head.z + d.z), HEAD_UP / len);
}

//How thick the stick figure's lines are, in px. boxH: how tall his box would be standing at his distance, so the lines
//grow as he comes closer (about 7 px at 5 m on a 1440p screen). Never thinner than the box's own lines (th), never
//thinner when he is closer, and at most MAX_LINE px (or th, if that is more).
constexpr float LINE_PER_PX = 1.0f / 50.0f;
constexpr float MAX_LINE = 12.0f;
inline float thickness(float th, float boxH) {
    const float t = std::isfinite(boxH) ? boxH * LINE_PER_PX : th;
    return std::clamp(t, th, std::max(th, MAX_LINE));
}

//The simple figure for far players: a straight spine, two arms from the top of the spine and two legs from the pelvis
//(the head is drawn on its own). Fewer, thicker lines than the full figure, so a small figure does not turn into a blob.
constexpr int SIMPLE_COUNT = 6;
constexpr int SIMPLE_LINES[SIMPLE_COUNT][2] = {
    {PELVIS, NECK2}, {NECK2, HEAD}, {SPINE3, HAND_A}, {SPINE3, HAND_B}, {PELVIS, ANKLE_A}, {PELVIS, ANKLE_B}};
//The simple line each joint lies on, as its two ends (-1: an end itself, it stays where it is)
constexpr int8_t SIMPLE_ON[JOINT_COUNT][2] = {
    {-1, -1}, {PELVIS, NECK2}, {PELVIS, NECK2}, {PELVIS, NECK2}, {PELVIS, NECK2}, {-1, -1}, {-1, -1},
    {SPINE3, HAND_A}, {SPINE3, HAND_A}, {SPINE3, HAND_A}, {-1, -1},
    {SPINE3, HAND_B}, {SPINE3, HAND_B}, {SPINE3, HAND_B}, {-1, -1},
    {PELVIS, ANKLE_A}, {PELVIS, ANKLE_A}, {-1, -1}, {ANKLE_A, ANKLE_A},
    {PELVIS, ANKLE_B}, {PELVIS, ANKLE_B}, {-1, -1}, {ANKLE_B, ANKLE_B}};

//The full figure pulled towards the simple one: each joint moves "amount" (0 to 1) of the way onto its simple line. At 1
//every full line lies on a simple line, so drawing SIMPLE_LINES instead changes nothing on screen: the figure changes
//smoothly with distance, never with a jump.
inline void simplify(const Pose& in, double amount, Pose& out) {
    out = in;
    if (!(amount > 0)) return;
    amount = std::min(1.0, amount);
    for (int k = 0; k < JOINT_COUNT; k++) { //in joint order: the top of the spine moves before the arms that hang off it
        const int a = SIMPLE_ON[k][0], b = SIMPLE_ON[k][1];
        if (a < 0 || !jointOk(in.j[k]) || !jointOk(out.j[a]) || !jointOk(out.j[b])) continue;
        const Vector3 A = out.j[a], ab = out.j[b] - A, ap = in.j[k] - A;
        const double l2 = ab.Dot(ab), f = l2 > 1e-9 ? std::clamp(ap.Dot(ab) / l2, 0.0, 1.0) : 0.0;
        out.j[k] = lerp(in.j[k], Vector3(A.x + ab.x * f, A.y + ab.y * f, A.z + ab.z * f), amount);
    }
}

//How far an eased value moves towards its target in dt seconds with time constant tau: 0 not at all, 1 all the way
inline double easeStep(double dt, double tau) { return !(dt > 0) ? 0.0 : !(tau > 0) ? 1.0 : 1.0 - std::exp(-dt / tau); }

//"shown" moved the part f of the way to "target", joint by joint. A joint the target does not know is not known
//(NaN); one that "shown" did not know jumps straight to the target.
inline void ease(Pose& shown, const Pose& target, double f) {
    for (int k = 0; k < JOINT_COUNT; k++)
        shown.j[k] = !jointOk(target.j[k]) ? badJoint() : !jointOk(shown.j[k]) ? target.j[k] : lerp(shown.j[k], target.j[k], f);
}

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

//One bone record's raw bytes into a transform
inline Xform decodeOne(const uint8_t* r) {
    double q[4], t[3], s[3];
    std::memcpy(q, r, 32);
    std::memcpy(t, r + 0x20, 24);
    std::memcpy(s, r + 0x40, 24);
    Xform x;
    x.q = {q[0], q[1], q[2], q[3]};
    x.t = Vector3(t[0], t[1], t[2]);
    x.s = Vector3(s[0], s[1], s[2]);
    return x;
}

//The raw bytes of a bone list (BONES records) into transforms
inline void decode(const uint8_t* bytes, Xform out[BONES]) {
    for (int i = 0; i < BONES; i++) out[i] = decodeOne(bytes + (size_t)i * RECORD);
}

//The READ_RANGES records, packed one after another (READ_BONES records), into transforms. Every bone not read stays
//unmoved (identity): chain() accepts it, and the stick figure never uses it.
inline void decodeRead(const uint8_t* bytes, Xform out[BONES]) {
    for (int i = 0; i < BONES; i++) out[i] = Xform{};
    for (const auto& rg : READ_RANGES)
        for (int b = rg[0]; b <= rg[1]; b++, bytes += RECORD) out[b] = decodeOne(bytes);
}

//A bone record that reads as a transform: a rotation of length 1 and finite numbers (not half-written or all zero)
inline bool believable(const Xform& l) {
    const double n = std::sqrt(l.q.x * l.q.x + l.q.y * l.q.y + l.q.z * l.q.z + l.q.w * l.q.w);
    return std::isfinite(n) && std::fabs(n - 1.0) <= 0.05 && std::isfinite(l.t.x + l.t.y + l.t.z) && std::isfinite(l.s.x + l.s.y + l.s.z);
}

//Every bone relative to the mesh instead of relative to its parent. ok (if given) gets, per bone, whether it and every
//bone above it are believable and not marked in bad (if given). True when every bone is.
inline bool chain(const Xform local[BONES], Xform comp[BONES], bool ok[BONES] = nullptr, const bool bad[BONES] = nullptr) {
    bool mine[BONES];
    if (!ok) ok = mine;
    bool all = true;
    for (int i = 0; i < BONES; i++) {
        const Xform& l = local[i];
        const int p = PARENT[i];
        ok[i] = believable(l) && !(bad && bad[i]) && (p < 0 || ok[p]);
        all = all && ok[i];
        comp[i] = p < 0 ? l : compose(l, comp[p]);
    }
    return all;
}

constexpr int CORE_JOINTS = HEAD + 1; //pelvis, spine, neck and head: without all of them it is no figure
constexpr int MAX_BAD_JOINTS = 4;     //at most one whole limb may be missing

//The joints in the mesh's own space (centimetres, before the mesh's rotation, scale and position are applied). A joint
//whose bone, or a bone above it, is not believable or is marked in bad is NaN: only that arm or leg is missing. False
//when it is no figure at all (a half-written or all-zero list): the trunk or head is bad, or more than one limb.
inline bool componentPose(const Xform local[BONES], Pose& out, const bool bad[BONES] = nullptr) {
    Xform comp[BONES];
    bool ok[BONES];
    chain(local, comp, ok, bad);
    int badJoints = 0;
    for (int k = 0; k < JOINT_COUNT; k++) {
        const Vector3& t = comp[JOINT_BONE[k]].t;
        const bool good = ok[JOINT_BONE[k]] && jointOk(t);
        out.j[k] = good ? t : badJoint();
        if (!good && (k < CORE_JOINTS || ++badJoints > MAX_BAD_JOINTS)) return false;
    }
    return true;
}

//Bones whose offset from their parent never changes while animating: the spine, neck, arms and legs (not the pelvis,
//which moves up and down with the stance). One of them with another length is a torn or garbage record.
inline bool fixedLength(int b) { return (b >= 2 && b <= 11) || (b >= 42 && b <= 45) || (b >= 65 && b <= 72); }
constexpr double LENGTH_SLACK = 0.15; //more than 15% (and 2 cm) off the learned length: that bone is bad
constexpr int RELEARN_AFTER = 4;      //reads in a row that disagree with the learned length: the learned one was wrong

//One player's fixed-length bones as learned from his reads
struct BoneLengths { double len[BONES] = {}; bool known[BONES] = {}; uint8_t off[BONES] = {}; };

//bad gets the fixed-length bones of this read whose length is not the learned one. The first believable record of a
//bone sets its length; a different length read RELEARN_AFTER times in a row replaces it (that first record was torn).
//Learned per player rather than fixed numbers: fixed ones need a live run of the bones probe first (Probe.cpp).
inline void checkLengths(const Xform local[BONES], BoneLengths& L, bool bad[BONES]) {
    for (int b = 0; b < BONES; b++) {
        bad[b] = false;
        if (!fixedLength(b)) continue;
        const double n = std::sqrt(local[b].t.Dot(local[b].t));
        if (!std::isfinite(n)) { bad[b] = true; continue; }
        if (!L.known[b]) {
            if (believable(local[b])) { L.len[b] = n; L.known[b] = true; }
            continue;
        }
        if (std::fabs(n - L.len[b]) <= std::max(2.0, LENGTH_SLACK * L.len[b])) { L.off[b] = 0; continue; }
        bad[b] = true;
        if (++L.off[b] >= RELEARN_AFTER) { L.len[b] = n; L.off[b] = 0; }
    }
}

//A pose in mesh space put into the world. "mesh" is the mesh's own transform (where the character stands and which way
//it faces). A NaN joint stays NaN. Rejects poses that are not near "anchor" (the player's position): old or garbage
//data is drawn as nothing.
inline bool placePose(const Pose& comp, const Xform& mesh, const Vector3& anchor, Pose& out) {
    const double mn = std::sqrt(mesh.q.x * mesh.q.x + mesh.q.y * mesh.q.y + mesh.q.z * mesh.q.z + mesh.q.w * mesh.q.w);
    if (!std::isfinite(mn) || std::fabs(mn - 1.0) > 0.05) return false; //the mesh transform was not read
    for (int k = 0; k < JOINT_COUNT; k++) {
        if (!jointOk(comp.j[k])) { out.j[k] = badJoint(); continue; }
        const Vector3 p = rotate(mesh.q, scaled(mesh.s, comp.j[k]));
        out.j[k] = Vector3(p.x + mesh.t.x, p.y + mesh.t.y, p.z + mesh.t.z);
        if (!jointOk(out.j[k]) || out.j[k].Dist(anchor) > 300.0) return false;
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
