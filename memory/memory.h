#pragma once

#ifndef MEMORY_H
#define MEMORY_H

#include <atomic>
#include <string>
#include <sys/types.h>
#include <sys/uio.h>
#include <stdint.h>
#include "../utils/gameUtil.h"

// Memory read function templates (this program only reads; there is deliberately no write function)
template<typename T>
T ReadMemory(pid_t pid, long address);

template<typename T>
T ReadMemory(uintptr_t address); //Overload thats to be used

void ReadMemoryBuffer(pid_t pid, long address, void* buffer, size_t size);
void ReadMemoryBuffer(uintptr_t address, void* buffer, size_t size); //Overload thats to be used

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
//The image's PE TimeDateStamp (which build of the game runs). 0 = not known (header not found or not readable).
extern uint32_t ImageStamp;


#endif //MEMORY_H