#pragma once
//Reads names out of the game: player names and the names of objects (actors, data assets).
//Everything here is read-only. The scrambling is the v1029 scheme from the offsets thread; after a game
//patch the constants at the top of names.cpp (and the RVAs) may need new values.
#ifndef KWINHELPER_NAMES_H
#define KWINHELPER_NAMES_H

#include <cstdint>
#include <string>

namespace names {

//Where the game image is loaded. Only a test changes this.
void setBase(uintptr_t base);

//The text key table could be read and looks sane. False means object names will all come back empty.
bool ready();

//PlayerNamePrivate: pass the address of the FString (PlayerState + 0x460). Empty when unreadable.
std::string playerName(uintptr_t fstringAddr);

struct Decoded {
    bool ok = false;
    uint32_t index = 0;   //position in the name table
    uint32_t number = 0;  //instance number (0 = none, otherwise the name ends in _<number-1>)
    std::string text;     //the name without the number
};

//Name of any UObject: an actor ("BP_..._C") or a data asset ("DA_...").
Decoded objectName(uintptr_t obj);

//Just the text, "" when it could not be read or does not look like a name.
std::string name(uintptr_t obj);

//Same, but with the instance number added the way the game prints it.
std::string fullName(uintptr_t obj);

void clearCache();

//Exposed for tests
uint64_t clmul64(uint64_t a, uint64_t b);
uint32_t pickNameSlot(uintptr_t actor);

} // namespace names

#endif //KWINHELPER_NAMES_H
