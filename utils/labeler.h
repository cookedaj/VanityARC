#pragma once
//Works out what each actor is called: reads its name, ground-item asset, ARC type, player name and guns,
//and turns them into the text shown next to the marker. Reads are cached per actor so this stays cheap.
#ifndef KWINHELPER_LABELER_H
#define KWINHELPER_LABELER_H

#include <optional>
#include <vector>

#include "localUtil.h"

namespace labeler {

//Ground items share the container class in memory; this turns a SEARCH actor into PICKUP when its name says so
void refine(RenderEntity& e);

//What an actor is, judged by names only (for vtables config.h does not know): ARC when its type asset is a
//DA_EnemyType_, PLAYER when its mesh is a CharacterMesh and it has a PlayerState, SEARCH when it is named like a
//container or ground item, NONE otherwise. nullopt: names could not be read, so this actor tells nothing.
std::optional<Object> kindByName(uintptr_t actor);

//Sets e.label (and e.sub for players: "Rattler (held) + Stitcher", with a level when the gun's name has one: "AMP Canto (held)")
void fill(RenderEntity& e);

//An item's shown name from its key (labels::itemToken). A gun gets its level from the key: "Stitcher 4", "AMP Il Toro".
//gun: the key is known to be a gun (a player's gun slots); otherwise only keys of the gun table get a level.
//Used for player guns, the inventory list and ground items alike.
std::string itemName(const std::string& key, bool gun = false);

//Everything we can read in a player's inventory: the built-in held list, or the lists named in offsets.txt
//(utils/offsets.h). Same name merged into one row with a count.
std::vector<ItemRow> inventoryOf(uintptr_t pawn);

//Call once per tick: forgets actors that are gone
void endTick();

//Forget everything (the game was restarted; every address is different now)
void reset();

} // namespace labeler

#endif //KWINHELPER_LABELER_H
