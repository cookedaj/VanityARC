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

    if (failures) { std::printf("%d failures\n", failures); return 1; }
    std::printf("skeleton_test: all passed\n");
    return 0;
}
