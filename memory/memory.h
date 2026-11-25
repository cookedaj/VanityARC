#pragma once

#ifndef MEMORY_H
#define MEMORY_H

#include <atomic>
#include <string>
#include <sys/types.h>
#include <sys/uio.h>
#include <stdint.h>
#include <type_traits>
#include "../utils/gameUtil.h"

// Memory reads (this program only reads; there is deliberately no write function).
// A read that fails is zero-filled.
void ReadMemoryBuffer(pid_t pid, long address, void* buffer, size_t size);
void ReadMemoryBuffer(uintptr_t address, void* buffer, size_t size); //Overload thats to be used

//One value of any plain type (int, uint32_t, a struct of numbers, ...); zero when it cannot be read
template<typename T>
T ReadMemory(uintptr_t address)
{
    static_assert(std::is_trivially_copyable_v<T>, "ReadMemory only reads plain values (numbers, pointers, simple structs)");
    T v;
    ReadMemoryBuffer(address, &v, sizeof v);
    return v;
}

//Many small reads in one go (one system call per up to 1024 reads instead of one each). Each request that could not
//be read is zeroed and left with ok = false; the others are filled. Returns how many were read.
struct ReadReq {
    uintptr_t addr;
    void* dst;
    size_t len;
    bool ok;
};
size_t ReadScatter(pid_t pid, ReadReq* reqs, size_t n);
size_t ReadScatter(ReadReq* reqs, size_t n); //Overload thats to be used

// Read a string from memory with maximum length
std::string ReadString(pid_t pid, long address, size_t maxLength = 128);

pid_t FindGamePID();

//Reads where the game's .exe is loaded (from /proc/<pid>/maps and its PE header) into ImageBase / ImageEnd.
//False when it cannot be found: the usual base and range are used then.
bool FindGameImage(pid_t pid);

// Global variables
//Atomic: the tracker thread finds / loses the game, the draw thread reads the camera every frame
extern std::atomic<pid_t> ProcessId;
//The game image is [ImageBase, ImageEnd). Set by FindGameImage on the tracker thread; read only there.
extern uintptr_t ImageBase, ImageEnd;


#endif //MEMORY_H