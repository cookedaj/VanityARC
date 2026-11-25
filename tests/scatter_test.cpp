//ReadScatter: many reads in one system call, with some of them failing in the middle.
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "memory/memory.h"

static int fails = 0;
#define CHECK(cond, msg) do { bool ok_ = (cond); std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) fails++; } while (0)

int main() {
    ProcessId = getpid();
    const size_t PG = 4096;
    uint8_t* m = (uint8_t*)mmap(nullptr, 9 * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for (size_t i = 0; i < 9 * PG; i++) m[i] = (uint8_t)(i * 31 + 7);
    mprotect(m + 4 * PG, PG, PROT_NONE); //page 4 cannot be read

    auto want = [&](size_t off) { uint64_t v; std::memcpy(&v, m + off, 8); return v; };
    uint64_t out[9];
    std::memset(out, 0xAA, sizeof out);
    ReadReq r[9] = {
        {(uintptr_t)m, &out[0], 8, false},
        {(uintptr_t)m + PG + 16, &out[1], 8, false},
        {(uintptr_t)m + 4 * PG - 4, &out[2], 8, false},   //starts on page 3, ends on the unreadable page 4
        {(uintptr_t)m + 5 * PG, &out[3], 8, false},
        {(uintptr_t)m + 6 * PG + 8, &out[4], 8, false},
        {(uintptr_t)0x10, &out[5], 8, false},              //not mapped at all
        {(uintptr_t)m + 7 * PG, &out[6], 8, false},
        {(uintptr_t)m + 7 * PG, &out[7], 0, false},        //nothing to read
        {(uintptr_t)m + 8 * PG + 100, &out[8], 8, false},
    };
    size_t n = ReadScatter(r, 9);
    CHECK(n == 7, "7 of 9 reads worked");
    CHECK(r[0].ok && out[0] == want(0), "read 0");
    CHECK(r[1].ok && out[1] == want(PG + 16), "read 1");
    CHECK(!r[2].ok && out[2] == 0, "a read that runs into an unreadable page fails and is zeroed");
    CHECK(r[3].ok && out[3] == want(5 * PG), "read after the failure still works");
    CHECK(r[4].ok && out[4] == want(6 * PG + 8), "read 4");
    CHECK(!r[5].ok && out[5] == 0, "an unmapped address fails and is zeroed");
    CHECK(r[6].ok && out[6] == want(7 * PG), "read after the second failure");
    CHECK(r[7].ok, "a read of zero bytes counts as done");
    CHECK(r[8].ok && out[8] == want(8 * PG + 100), "last read");

    //more reads than one system call takes (1024), failures scattered through them
    const int N = 3000;
    std::vector<uint64_t> big(N, 0xAAAAAAAAAAAAAAAAULL);
    std::vector<ReadReq> rr(N);
    for (int i = 0; i < N; i++) {
        bool bad = i % 7 == 3;
        uintptr_t a = bad ? (uintptr_t)(m + 4 * PG + (i % 100) * 8) : (uintptr_t)(m + (size_t)(i % 2000) * 8);
        rr[i] = {a, &big[i], 8, false};
    }
    size_t okCount = ReadScatter(rr.data(), rr.size());
    bool all = true;
    int expectOk = 0;
    for (int i = 0; i < N; i++) {
        bool bad = i % 7 == 3;
        if (!bad) expectOk++;
        if (bad) all &= !rr[i].ok && big[i] == 0;
        else all &= rr[i].ok && big[i] == want((size_t)(i % 2000) * 8);
    }
    CHECK(okCount == (size_t)expectOk && all, "3000 reads with every 7th failing: each one is right");

    //a process that does not exist: everything fails, nothing hangs
    std::vector<uint64_t> z(50, 0xBBBBBBBBBBBBBBBBULL);
    std::vector<ReadReq> zr(50);
    for (int i = 0; i < 50; i++) zr[i] = {(uintptr_t)m, &z[i], 8, false};
    size_t zn = ReadScatter(999999, zr.data(), zr.size());
    bool zeroed = true;
    for (int i = 0; i < 50; i++) zeroed &= !zr[i].ok && z[i] == 0;
    CHECK(zn == 0 && zeroed, "no such process: all fail and are zeroed");

    CHECK(ReadScatter(nullptr, 0) == 0, "an empty list is fine");

    //FindGameImage: a mapped .exe with a 64-bit PE header gives its base and build stamp; an unreadable one gives none
    {
        char path[] = "/tmp/kwh_peXXXXXX.exe";
        const int fd = mkstemps(path, 4);
        uint8_t h[0x1000] = {};
        const uint32_t pe = 0x80, sig = 0x4550, stamp = 0x5F3A1B2C, size = 0x2000;
        const uint16_t magic = 0x20B;
        h[0] = 'M'; h[1] = 'Z';
        std::memcpy(h + 0x3C, &pe, 4);
        std::memcpy(h + pe, &sig, 4);
        std::memcpy(h + pe + 8, &stamp, 4);
        std::memcpy(h + pe + 0x18, &magic, 2);
        std::memcpy(h + pe + 0x50, &size, 4);
        const bool wrote = fd >= 0 && write(fd, h, sizeof h) == (ssize_t)sizeof h;
        void* img = wrote ? mmap(nullptr, sizeof h, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
        CHECK(img != MAP_FAILED && FindGameImage(getpid()) && ImageBase == (uintptr_t)img && ImageEnd == (uintptr_t)img + size &&
              ImageStamp == stamp, "a mapped .exe: its base, size and build stamp are read");
        if (img != MAP_FAILED) mprotect(img, sizeof h, PROT_NONE);
        CHECK(img != MAP_FAILED && !FindGameImage(getpid()) && ImageStamp == 0, "an unreadable header: build stamp unknown (0)");
        if (img != MAP_FAILED) munmap(img, sizeof h);
        if (fd >= 0) { close(fd); unlink(path); }
    }

    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
