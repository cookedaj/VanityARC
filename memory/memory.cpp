#include "memory.h"

#include <iostream>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <signal.h>
#include <strings.h>
#include <vector>

#include "utils/obfstr.h"


// Global variables, needed for easy read without a class

std::atomic<pid_t> ProcessId{0};
//The usual base of a 64-bit .exe and the range used before the image was read from the process
constexpr uintptr_t DEFAULT_IMAGE_BASE = 0x140000000ULL, DEFAULT_IMAGE_END = 0x154000000ULL;
uintptr_t ImageBase = DEFAULT_IMAGE_BASE, ImageEnd = DEFAULT_IMAGE_END;
uint32_t ImageStamp = 0;

#define debug false

void ReadMemoryBuffer(pid_t pid, long address, void* buffer, size_t size)
{

    struct iovec local[1];
    struct iovec remote[1];

    local[0].iov_base = buffer;
    local[0].iov_len = size;
    remote[0].iov_base = (void*)address;
    remote[0].iov_len = size;

    ssize_t nread = process_vm_readv(pid, local, 1, remote, 1, 0);
    if (nread != (ssize_t)size)
    {
        // If the read fails, zero out the buffer to prevent using old/garbage data.
        memset(buffer, 0, size);
#if debug
        std::cerr << "[ReadMemory (Buffer)] Failed to read " << size
                  << " bytes at address 0x" << std::hex << address
                  << " (pid " << pid << "). nread=" << nread << std::endl;
        perror("[ReadMemory (Buffer)] process_vm_readv");
#endif
    }
}

void ReadMemoryBuffer(uintptr_t address, void* buffer, size_t size)
{
    ReadMemoryBuffer(ProcessId, address, buffer, size);
}

size_t ReadMemoryPrefix(uintptr_t address, void* buffer, size_t size)
{
    //process_vm_readv copies page by page and stops at the first page it cannot read, reporting what came before it
    struct iovec local = {buffer, size}, remote = {(void*)address, size};
    ssize_t got = process_vm_readv(ProcessId, &local, 1, &remote, 1, 0);
    if (got < 0) got = 0;
    if ((size_t)got < size) std::memset((char*)buffer + got, 0, size - (size_t)got);
    return (size_t)got;
}

size_t ReadScatter(pid_t pid, ReadReq* r, size_t n)
{
    //process_vm_readv stops at the first read it cannot do and reports how many bytes came before it. So: find which
    //read that was, mark it failed, and carry on with the ones after it.
    thread_local std::vector<struct iovec> loc, rem;
    size_t ok = 0, i = 0;
    while (i < n) {
        const size_t cnt = std::min<size_t>(n - i, 1024); //IOV_MAX
        loc.resize(cnt);
        rem.resize(cnt);
        for (size_t k = 0; k < cnt; k++) {
            loc[k] = {r[i + k].dst, r[i + k].len};
            rem[k] = {(void*)r[i + k].addr, r[i + k].len};
            r[i + k].ok = false;
        }
        ssize_t got = process_vm_readv(pid, loc.data(), cnt, rem.data(), cnt, 0);
        if (got < 0) {
            if (errno != EFAULT) { //no such process / no permission: nothing in this batch or the ones after it can be read
                for (size_t k = i; k < n; k++) { std::memset(r[k].dst, 0, r[k].len); r[k].ok = false; }
                return ok;
            }
            got = 0;
        }
        size_t k = 0, acc = 0;
        while (k < cnt && acc + r[i + k].len <= (size_t)got) {
            r[i + k].ok = true;
            acc += r[i + k].len;
            ok++;
            k++;
        }
        if (k == cnt) { i += cnt; continue; } //every read in this batch worked
        std::memset(r[i + k].dst, 0, r[i + k].len); //the one that failed (may be half read)
        i += k + 1;
    }
    return ok;
}

size_t ReadScatter(ReadReq* reqs, size_t n)
{
    return ReadScatter(ProcessId, reqs, n);
}

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
namespace fs = std::filesystem;

pid_t FindGamePID() {
    //Every Unreal game under Proton has a GameThread. Prefer the one whose command line names ARC Raiders; the first
    //other one is only used when none does (so a wrong guess at the name never stops the overlay from attaching).
    pid_t other = 0;
    std::string otherLine;
    std::error_code ec; //error_code overloads: a process that exits while we scan must not throw
    for (fs::directory_iterator it("/proc", ec), end; !ec && it != end; it.increment(ec)) {
        const std::string pid_str = it->path().filename().string();

        // Check if directory name is a number (PID)
        if (pid_str.empty() || !isdigit((unsigned char)pid_str[0])) continue;

        // Check process name first (cheap), then count threads in /proc/PID/task
        std::ifstream comm_file("/proc/" + pid_str + "/comm");
        std::string comm_name;
        if (!(comm_file >> comm_name) || comm_name.find(OBF("GameThread")) == std::string::npos) continue;

        int thread_count = 0;
        std::error_code tec;
        for (fs::directory_iterator t("/proc/" + pid_str + "/task", tec), tend; !tec && t != tend; t.increment(tec))
            thread_count++;

        // Unreal Engine games run well over 100 threads
        if (thread_count > 100) {
            pid_t pid = (pid_t)std::strtol(pid_str.c_str(), nullptr, 10);
            std::ifstream cmd_file("/proc/" + pid_str + "/cmdline", std::ios::binary);
            std::string cmdline((std::istreambuf_iterator<char>(cmd_file)), std::istreambuf_iterator<char>());
            std::replace(cmdline.begin(), cmdline.end(), '\0', ' ');
            std::string lower = cmdline;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            const bool arc = lower.find(OBF("pioneer")) != std::string::npos || lower.find(OBF("raiders")) != std::string::npos;
            const std::string line = "[+] Found game process: PID " + std::to_string(pid) + " (" + comm_name + ") with " +
                                     std::to_string(thread_count) + " threads: " + cmdline;
            if (arc) {
                std::cout << line << std::endl;
                return pid;
            }
            if (other == 0) {
                other = pid;
                otherLine = line + OBF(" (does not look like ARC Raiders)");
            }
        }
    }

    if (other != 0) std::cout << otherLine << std::endl;
    return other; //0: not running (yet); the caller decides whether to say so and retry
}

//The PE header at base: true for a readable 64-bit image, giving its size and build stamp
static bool peHeader(pid_t pid, uintptr_t base, uint32_t& size, uint32_t& stamp) {
    unsigned char h[0x1000];
    ReadMemoryBuffer(pid, (long)base, h, sizeof h); //all zero when unreadable
    uint32_t pe = 0, sig = 0;
    uint16_t magic = 0;
    std::memcpy(&pe, h + 0x3C, 4); //e_lfanew
    if (h[0] != 'M' || h[1] != 'Z' || pe > sizeof h - 0x54) return false;
    std::memcpy(&sig, h + pe, 4);
    std::memcpy(&stamp, h + pe + 8, 4);     //FileHeader.TimeDateStamp: changes with every build of the game
    std::memcpy(&magic, h + pe + 0x18, 2);  //OptionalHeader.Magic: 0x20B = 64-bit
    std::memcpy(&size, h + pe + 0x50, 4);   //OptionalHeader.SizeOfImage
    return sig == 0x4550 && magic == 0x20B && size != 0;
}

bool FindGameImage(pid_t pid) {
    ImageBase = DEFAULT_IMAGE_BASE;
    ImageEnd = DEFAULT_IMAGE_END;
    ImageStamp = 0;
    uint32_t size = 0, stamp = 0;
    auto take = [&](uintptr_t base) {
        ImageBase = base;
        ImageEnd = base + size;
        ImageStamp = stamp;
        return true;
    };
    //The usual base first, whatever /proc/<pid>/maps calls it. Proton can run the game there from "/memfd:wine-mapping"
    //and keep only a read-only view of the .exe file somewhere else (proton-cachyos 11): choosing by the .exe name
    //picked that view, and every read missed.
    if (peHeader(pid, DEFAULT_IMAGE_BASE, size, stamp)) return take(DEFAULT_IMAGE_BASE);
    const std::string mapsPath = "/proc/" + std::to_string(pid) + "/maps";
    //Something sits at the usual base but its header cannot be read: that is still the game, so keep the usual address
    //rather than take a .exe view somewhere else
    std::ifstream first(mapsPath);
    for (std::string line; std::getline(first, line);)
        if (line.starts_with("140000000-")) return false;
    std::ifstream maps(mapsPath);
    for (std::string line; std::getline(maps, line);) {
        //"start-end perms offset dev inode path": the first page of a mapped .exe is its PE header
        if (line.size() < 4 || strncasecmp(line.c_str() + line.size() - 4, ".exe", 4) != 0) continue;
        const uintptr_t base = (uintptr_t)std::strtoull(line.c_str(), nullptr, 16);
        if (peHeader(pid, base, size, stamp)) return take(base);
    }
    return false;
}
