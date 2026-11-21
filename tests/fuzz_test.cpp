//Throws random garbage at every decoder and the labeler to make sure nothing crashes, hangs or reads out of bounds.
#include <sys/mman.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include "config.h"
#include "utils/localUtil.h"
#include "utils/names.h"
#include "utils/labels.h"
#include "utils/labeler.h"
#include "utils/settings.h"
#include <filesystem>
//A throw-away folder under the system temp directory (tests never touch your real ~/.config/kwin-helper)
static std::string testDir(const char* name) {
    return (std::filesystem::temp_directory_path() / (std::string("kwinhelper_test_") + name)).string();
}


typedef uint64_t u64;
static std::mt19937_64 rng(0xC0FFEE);
static u64 R(u64 n) { return rng() % n; }

int main(int argc, char** argv) {
    int iters = argc > 1 ? atoi(argv[1]) : 20000;
    ProcessId = getpid();
    size_t size = 0x11000000;
    uint8_t* region = (uint8_t*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    u64 base = (u64)region;
    names::setBase(base);
    //key table with random non-zero entries
    for (int i = 0; i < 64; i++) { uint16_t k = (uint16_t)(rng() | 1); std::memcpy(region + 0x1043A2A4 + i * 2, &k, 2); }
    //name pool area: random garbage in the blocks and entries
    for (size_t i = 0x105726C0; i < 0x105726C0 + 0x20000; i += 8) { u64 v = rng(); std::memcpy(region + i, &v, 8); }

    const u64 objLo = base + 0x10600000, objHi = base + 0x10F00000;
    std::vector<u64> objs;
    auto mk = [&]() {
        u64 o = (objLo + R(objHi - objLo - 0x4000)) & ~15ULL;
        for (size_t i = 0; i < 0x2000; i += 8) { u64 v = rng(); if (R(3) == 0) v = 0; std::memcpy((void*)(o + i), &v, 8); }
        u64 vt = R(4) ? 0x14d000000ULL : rng();
        std::memcpy((void*)o, &vt, 8);
        objs.push_back(o);
        return o;
    };
    for (int i = 0; i < 200; i++) mk();
    auto anyObj = [&]() { return objs[R(objs.size())]; };
    const int offs[] = {0x3E8, 0x3E0, 0xCB0, 0x530, 0x538, 0x3B0, 0x3F0, 0x518, 0x11C0, 0x460, 0x10, 0x18, 0x400, 0x500, 0x600};
    labels::setDir(testDir("fuzzcfg"));

    long names_ok = 0, labels_nonempty = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int it = 0; it < iters; it++) {
        u64 o = anyObj();
        //plant pointers to other objects (and sometimes weird counts) in the fields the labeler reads
        for (int k = 0; k < 6; k++) { u64 p = anyObj(); std::memcpy((void*)(o + offs[R(sizeof offs / sizeof *offs)]), &p, 8); }
        if (R(2)) { int cnt = (int)(R(6) ? R(20) : rng()); std::memcpy((void*)(o + 0x538), &cnt, 4); }
        if (R(2)) { int cnt = (int)(R(80)); std::memcpy((void*)(o + 0x468), &cnt, 4); }

        names::Decoded d = names::objectName(o);
        names_ok += d.ok;
        (void)names::fullName(o); (void)names::name(o + 0x10);
        (void)names::playerName(o + 0x460);

        RenderEntity e; e.actor = o; e.pos = Vector3(1000, 1000, 0); e.vt = 0;
        const Object types[] = {Object::PLAYER, Object::ARC, Object::SEARCH, Object::PICKUP, Object::NONE};
        e.type = types[R(5)];
        labeler::refine(e);
        labeler::fill(e);
        labels_nonempty += !e.label.empty();

        //random keys into the label code
        std::string key; size_t len = R(4) ? R(40) : R(300);
        for (size_t i = 0; i < len; i++) key.push_back((char)(R(4) ? 32 + R(95) : R(256)));
        for (int c = 0; c < labels::CAT_COUNT; c++) {
            (void)labels::defaultName((labels::Cat)c, key);
            (void)labels::display((labels::Cat)c, key, R(2) ? key : "");
            if (R(50) == 0) labels::setOverride((labels::Cat)c, key, key);
        }
        if (it % 1000 == 0) labeler::endTick();
        if (it % 5000 == 0) { names::clearCache(); labeler::reset(); }
    }

    //random files for the loaders
    for (int f = 0; f < 200; f++) {
        std::string dir = testDir("fuzzcfg");
        system(("mkdir -p " + dir).c_str());
        for (const char* fn : {"labels.txt", "settings.txt"}) {
            std::ofstream out(dir + "/" + fn, std::ios::binary | std::ios::trunc);
            size_t n = R(400);
            for (size_t i = 0; i < n; i++) { char c = (char)(R(5) ? "\t\n 0123456789abcxyz.-eE"[R(22)] : R(256)); out.put(c); }
        }
        labels::load(); settings::load(dir);
        //after any garbage the values must still be usable numbers
        if (!(config::labelSize == config::labelSize) || !(config::maxArcDist == config::maxArcDist)) { std::puts("FAIL: NaN got into settings"); return 1; }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("fuzz done: %d iterations in %.1fs, %ld names decoded, %ld labels produced. No crash.\n", iters, secs, names_ok, labels_nonempty);
    return 0;
}
