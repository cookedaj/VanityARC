//Stick-figure maths: the parent table, chaining bones, and putting the figure into the world
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "utils/skeleton.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); failures++; } } while (0)

static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

static void put(std::vector<uint8_t>& buf, int bone, const skeleton::Xform& x) {
    double q[4] = {x.q.x, x.q.y, x.q.z, x.q.w}, t[3] = {x.t.x, x.t.y, x.t.z}, s[3] = {x.s.x, x.s.y, x.s.z};
    uint8_t* r = buf.data() + (size_t)bone * skeleton::RECORD;
    std::memcpy(r, q, 32);
    std::memcpy(r + 0x20, t, 24);
    std::memcpy(r + 0x40, s, 24);
}

static skeleton::Quat aboutZ(double degrees) {
    const double h = degrees * 3.14159265358979 / 360.0;
    return {0, 0, std::sin(h), std::cos(h)};
}

int main() {
    using namespace skeleton;

    //The parent table agrees with what the probe printed from the game, and parents come before children
    const char* probe =
        "0:-1 1:0 2:1 3:2 4:3 5:4 6:5 7:6 8:4 9:8 10:9 11:10 12:11 13:12 14:13 15:14 16:11 17:16 18:17 19:18 20:11 21:20 22:21 23:22 "
        "24:11 25:24 26:25 27:26 28:11 29:28 30:29 31:4 32:31 33:32 34:32 35:34 36:35 37:36 38:37 39:38 40:32 41:31 42:4 43:42 44:43 "
        "45:44 46:45 47:46 48:47 49:48 50:45 51:50 52:51 53:52 54:45 55:54 56:55 57:56 58:45 59:58 60:59 61:60 62:45 63:62 64:63 65:1 "
        "66:65 67:66 68:67 69:1 70:69 71:70 72:71 73:0 74:73 75:73 76:0 77:0 78:77 79:77 80:32 81:32 82:11 83:45 84:73 85:84 86:73 "
        "87:86 88:0 89:41 90:45 91:11 92:0 93:0 94:0 95:33 96:33";
    std::istringstream in(probe);
    std::string tok;
    int count = 0;
    while (in >> tok) {
        const size_t c = tok.find(':');
        const int bone = std::atoi(tok.substr(0, c).c_str()), parent = std::atoi(tok.substr(c + 1).c_str());
        CHECK(bone == count, "probe list is in order");
        CHECK(bone >= 0 && bone < BONES && PARENT[bone] == parent, "parent table matches the probe output");
        count++;
    }
    CHECK(count == BONES, "probe list has 97 bones");
    for (int i = 0; i < BONES; i++) CHECK(PARENT[i] < i, "a parent comes before its child");
    for (int k = 0; k < JOINT_COUNT; k++) CHECK(JOINT_BONE[k] >= 0 && JOINT_BONE[k] < BONES, "joint bone in range");
    for (int l = 0; l < LINE_COUNT; l++) CHECK(LINES[l][0] >= 0 && LINES[l][1] < JOINT_COUNT && LINES[l][0] != LINES[l][1], "line joints in range");

    //Only READ_RANGES are read from the game: they must hold every joint and every bone above it
    {
        auto isRead = [](int b) { for (const auto& rg : READ_RANGES) if (b >= rg[0] && b <= rg[1]) return true; return false; };
        int n = 0;
        for (const auto& rg : READ_RANGES) n += rg[1] - rg[0] + 1;
        CHECK(n == READ_BONES, "READ_BONES is the size of the ranges");
        for (int k = 0; k < JOINT_COUNT; k++)
            for (int b = JOINT_BONE[k]; b >= 0; b = PARENT[b]) CHECK(isRead(b), "a joint and every bone above it are read");
    }

    //Rotation: a quarter turn about Z sends +X to +Y
    {
        const Vector3 r = rotate(aboutZ(90), Vector3(1, 0, 0));
        CHECK(near(r.x, 0, 1e-9) && near(r.y, 1, 1e-9) && near(r.z, 0, 1e-9), "quarter turn about Z");
    }
    //Chaining: a child 10 cm along X of a parent that is turned 90 degrees and sits at (0,0,100) ends up at (0,10,100)
    {
        Xform parent, local;
        parent.q = aboutZ(90);
        parent.t = Vector3(0, 0, 100);
        local.t = Vector3(10, 0, 0);
        const Xform c = compose(local, parent);
        CHECK(near(c.t.x, 0, 1e-9) && near(c.t.y, 10, 1e-9) && near(c.t.z, 100, 1e-9), "child inside a turned parent");
        //and the turn is inherited
        const Vector3 dir = rotate(c.q, Vector3(1, 0, 0));
        CHECK(near(dir.y, 1, 1e-9), "rotation is inherited");
    }
    //Scale of the parent stretches the child's offset
    {
        Xform parent, local;
        parent.s = Vector3(2, 2, 2);
        local.t = Vector3(5, 0, 0);
        CHECK(near(compose(local, parent).t.x, 10), "parent scale stretches the offset");
    }

    //A whole figure: every bone offset 10 cm up from its parent, root at the origin. Bone i is at 10 * depth(i).
    {
        std::vector<uint8_t> buf((size_t)BONES * RECORD, 0);
        for (int i = 0; i < BONES; i++) {
            Xform x;
            x.t = i == 0 ? Vector3(0, 0, 0) : Vector3(0, 0, 10);
            put(buf, i, x);
        }
        Xform local[BONES], comp[BONES];
        decode(buf.data(), local);
        CHECK(chain(local, comp), "identity chain is accepted");
        CHECK(near(comp[1].t.z, 10) && near(comp[4].t.z, 40) && near(comp[7].t.z, 70), "heights add up along the spine");

        //into the world: the mesh stands at (1000, 2000, 300) turned 90 degrees
        Xform mesh;
        mesh.q = aboutZ(90);
        mesh.t = Vector3(1000, 2000, 300);
        Pose pose;
        CHECK(worldPose(local, mesh, Vector3(1000, 2000, 300), pose), "pose near the player is accepted");
        CHECK(near(pose.j[HEAD].x, 1000) && near(pose.j[HEAD].y, 2000) && near(pose.j[HEAD].z, 370), "head position in the world");
        CHECK(!worldPose(local, mesh, Vector3(0, 0, 0), pose), "pose far from the player is rejected");

        //the packed READ_RANGES records give the same figure as the whole list
        {
            std::vector<uint8_t> packed;
            for (const auto& rg : READ_RANGES)
                packed.insert(packed.end(), buf.begin() + (long)(rg[0] * RECORD), buf.begin() + (long)((rg[1] + 1) * RECORD));
            CHECK(packed.size() == (size_t)READ_BONES * RECORD, "packed list size");
            Xform readOnly[BONES];
            decodeRead(packed.data(), readOnly);
            Pose a, b;
            CHECK(componentPose(local, a) && componentPose(readOnly, b), "both lists give a figure");
            bool same = true;
            for (int k = 0; k < JOINT_COUNT; k++) same = same && near(a.j[k].x, b.j[k].x) && near(a.j[k].y, b.j[k].y) && near(a.j[k].z, b.j[k].z);
            CHECK(same, "the bones read are all the figure needs");
        }

        //a bone that is not a unit rotation (half-written list) makes the whole pose invalid
        Xform bad;
        bad.q = {0, 0, 0, 0};
        put(buf, 20, bad);
        decode(buf.data(), local);
        CHECK(!chain(local, comp), "a zero rotation is rejected");
        bad.q = {0, 0, 0, 1};
        bad.t = Vector3(std::nan(""), 0, 0);
        put(buf, 20, bad);
        decode(buf.data(), local);
        CHECK(!chain(local, comp), "a missing number is rejected");
    }

    //The head marker's centre: 12 cm beyond the head bone along the neck; straight up without a neck; none without a head
    {
        Pose p;
        for (int k = 0; k < JOINT_COUNT; k++) p.j[k] = Vector3(0, 0, 10.0 * k);
        p.j[NECK2] = Vector3(0, 0, 150);
        p.j[HEAD] = Vector3(0, 10, 150); //the neck points along +y here
        Vector3 c = headCentre(p);
        CHECK(near(c.x, 0) && near(c.y, 22) && near(c.z, 150), "head centre: 12 cm beyond the head bone, along the neck");
        p.j[NECK2] = badJoint();
        c = headCentre(p);
        CHECK(near(c.y, 10) && near(c.z, 162), "no neck: 12 cm straight up");
        p.j[HEAD] = badJoint();
        CHECK(!jointOk(headCentre(p)), "no head: no head centre");
    }

    //One bad bone leaves out only its own joints; the trunk, or more than one limb, and it is no figure at all
    {
        std::vector<uint8_t> buf((size_t)BONES * RECORD, 0);
        auto fill = [&] {
            for (int i = 0; i < BONES; i++) { Xform x; x.t = i == 0 ? Vector3(0, 0, 0) : Vector3(0, 0, 10); put(buf, i, x); }
        };
        fill();
        Xform local[BONES];
        decode(buf.data(), local);
        BoneLengths L;
        bool bad[BONES];
        checkLengths(local, L, bad);
        int marked = 0;
        for (bool b : bad) marked += b;
        CHECK(marked == 0 && L.known[10] && near(L.len[10], 10) && !L.known[1], "the first read teaches the limb lengths (not the pelvis, which moves) and marks nothing");
        Xform stretched;
        stretched.t = Vector3(0, 0, 30); //a forearm three times as long: a torn record
        put(buf, 10, stretched);
        decode(buf.data(), local);
        checkLengths(local, L, bad);
        marked = 0;
        for (bool b : bad) marked += b;
        CHECK(bad[10] && marked == 1, "a stretched forearm is the only bone marked");
        Pose p;
        CHECK(componentPose(local, p, bad), "one bad forearm: still a figure");
        CHECK(!jointOk(p.j[FOREARM_A]) && !jointOk(p.j[HAND_A]), "its forearm and hand are left out");
        bool rest = true;
        for (int k = 0; k < JOINT_COUNT; k++) if (k != FOREARM_A && k != HAND_A) rest = rest && jointOk(p.j[k]);
        CHECK(rest && near(p.j[HEAD].z, 70) && near(p.j[ARM_A].z, 60), "every other joint is where it was");
        Xform mesh;
        mesh.t = Vector3(500, 0, 0);
        Pose w;
        CHECK(placePose(p, mesh, Vector3(500, 0, 0), w) && !jointOk(w.j[HAND_A]) && near(w.j[HEAD].x, 500), "placed in the world, the missing joints stay missing");
        for (int i = 1; i < RELEARN_AFTER; i++) checkLengths(local, L, bad);
        checkLengths(local, L, bad);
        CHECK(!bad[10] && near(L.len[10], 30), "a new length read again and again replaces the learned one (the first read was the torn one)");

        fill();
        Xform zero;
        zero.q = {0, 0, 0, 0}; //half-written: no rotation
        put(buf, 44, zero);
        decode(buf.data(), local);
        CHECK(componentPose(local, p) && !jointOk(p.j[FOREARM_B]) && !jointOk(p.j[HAND_B]) && jointOk(p.j[ARM_B]),
              "a half-written forearm: only that forearm and hand are left out, not the whole figure");
        put(buf, 65, zero);
        decode(buf.data(), local);
        CHECK(!componentPose(local, p), "a forearm and a whole leg missing: no figure");
        fill();
        put(buf, 3, zero);
        decode(buf.data(), local);
        CHECK(!componentPose(local, p), "a bad spine bone: no figure");
    }

    //Line thickness: never thinner than the box, never thinner as the player comes closer (his box grows), capped
    {
        CHECK(thickness(1.5f, 10) == 1.5f && thickness(4, 0) == 4, "far away: the box's own thickness");
        CHECK(std::fabs(thickness(1.5f, 250) - 5.0f) < 1e-3f && thickness(1.5f, 370) == MAX_LINE,
              "a player about 7 m away (box 250 px): 5 px; 5 m away (box 370 px): already at the cap");
        CHECK(thickness(1.5f, 1e5f) == MAX_LINE && MAX_LINE == 6 && thickness(10, 1e5f) == 10 && thickness(1.5f, NAN) == 1.5f,
              "capped at 6 px, or at the box's thickness if that is more; an unknown box size gives the box's thickness");
        bool rising = true, inside = true;
        for (float th : {1.0f, 1.5f, 4.0f, 10.0f}) {
            float last = 0;
            for (float h = 0; h < 3000; h += 0.5f) {
                const float t = thickness(th, h);
                rising = rising && t >= last;
                inside = inside && t >= th && t <= std::max(th, MAX_LINE);
                last = t;
            }
        }
        CHECK(rising && inside, "thickness never drops as the box grows, never under the box's, never over the cap");
    }

    //The simple far figure: every joint pulled onto one of a few straight lines, smoothly
    {
        Pose p;
        for (int k = 0; k < JOINT_COUNT; k++) p.j[k] = Vector3(std::sin(k * 1.7) * 30, std::cos(k * 2.3) * 20, k * 7.0);
        Pose s0, s1, half;
        simplify(p, 0, s0);
        simplify(p, 1, s1);
        simplify(p, 0.5, half);
        bool same = true;
        for (int k = 0; k < JOINT_COUNT; k++) same = same && s0.j[k] == p.j[k];
        CHECK(same, "simple 0: the full figure as it is");
        auto offLine = [](const Vector3& q, const Vector3& a, const Vector3& b) {
            const Vector3 ab = b - a, aq = q - a;
            const double l2 = ab.Dot(ab), f = l2 > 0 ? std::fmax(0.0, std::fmin(1.0, aq.Dot(ab) / l2)) : 0.0;
            return q.Dist(Vector3(a.x + ab.x * f, a.y + ab.y * f, a.z + ab.z * f));
        };
        bool onLines = true, endsStay = true;
        for (int k = 0; k < JOINT_COUNT; k++) {
            const int a = SIMPLE_ON[k][0], b = SIMPLE_ON[k][1];
            if (a < 0) endsStay = endsStay && s1.j[k] == p.j[k];
            else onLines = onLines && offLine(s1.j[k], s1.j[a], s1.j[b]) < 1e-9;
        }
        CHECK(endsStay && onLines, "simple 1: the ends stay, every other joint lies on its simple line");
        bool fullOnSimple = true; //so drawing only SIMPLE_LINES at 1 shows the same as the full figure
        for (int l = 0; l < LINE_COUNT; l++) {
            bool found = false;
            for (int s = 0; s < SIMPLE_COUNT && !found; s++) {
                const Vector3 &a = s1.j[SIMPLE_LINES[s][0]], &b = s1.j[SIMPLE_LINES[s][1]];
                found = offLine(s1.j[LINES[l][0]], a, b) < 1e-9 && offLine(s1.j[LINES[l][1]], a, b) < 1e-9;
            }
            fullOnSimple = fullOnSimple && found;
        }
        CHECK(fullOnSimple, "simple 1: every line of the full figure lies on a line of the simple one");
        bool halfway = true;
        for (int k : {SPINE1, SPINE2, SPINE3, NECK1, HIP_A, KNEE_A, TOE_A, HIP_B, KNEE_B, TOE_B})
            halfway = halfway && half.j[k].Dist(lerp(p.j[k], s1.j[k], 0.5)) < 1e-9;
        CHECK(halfway, "simple 0.5: half way there");
    }

    //Easing: how far a step goes, missing joints, and settling within about 150 ms
    {
        CHECK(near(easeStep(0.025, 0.025), 1 - std::exp(-1.0)) && easeStep(0, 0.025) == 0 && easeStep(0.01, 0) == 1 && easeStep(1, 0.025) > 0.999999,
              "ease step: 63% after one time constant, nothing without time, all the way without smoothing");
        Pose shown, target;
        for (int k = 0; k < JOINT_COUNT; k++) { shown.j[k] = Vector3(0, 0, 0); target.j[k] = Vector3(10, 20, 30); }
        target.j[HAND_A] = badJoint();
        shown.j[HAND_B] = badJoint();
        Pose settle = shown;
        ease(shown, target, 0.5);
        CHECK(near(shown.j[PELVIS].x, 5) && near(shown.j[PELVIS].z, 15), "half a step: half way");
        CHECK(!jointOk(shown.j[HAND_A]), "a joint the new pose does not have is not drawn");
        CHECK(near(shown.j[HAND_B].y, 20), "a joint that was missing jumps straight to the new pose");
        for (int i = 0; i < 19; i++) ease(settle, target, easeStep(0.008, 0.025)); //150 ms of 120 Hz steps
        CHECK(std::fabs(settle.j[PELVIS].z - 30) < 0.1, "after 150 ms the figure is where the bones are");
    }

    if (failures) { std::printf("%d failures\n", failures); return 1; }
    std::printf("skeleton_test: all passed\n");
    return 0;
}
