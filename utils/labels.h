#pragma once
//Display names. The game gives raw names ("DA_EnemyType_Tick", "BP_SocketContainer_Gvt_BinTrash_01_A_C",
//"DA_Item_BattleRifle_BreachAction_01"); this turns them into what you see on screen ("Tick", "Trash can",
//"Ferro") and keeps the names you typed yourself in ~/.config/vanityarc/labels.txt.
#ifndef VANITYARC_LABELS_H
#define VANITYARC_LABELS_H

#include <map>
#include <string>
#include <vector>

namespace labels {

enum class Cat { Arc = 0, Container = 1, Item = 2, Player = 3 };
constexpr int CAT_COUNT = 4;

//Built-in name for a raw key (the table first, then a cleaned-up version of the raw name)
std::string defaultName(Cat c, const std::string& key);

//The name to show: your own name if you set one, else def, else the built-in name. Also remembers the key
//so the Rename tab can list it.
std::string display(Cat c, const std::string& key, const std::string& def = "");

//Item / gun key: the part of a raw name that identifies the item ("BattleRifle_BreachAction_01")
std::string itemToken(const std::string& raw);

//Your own names
void setOverride(Cat c, const std::string& key, const std::string& name); //empty name = back to the built-in one
std::string overrideOf(Cat c, const std::string& key);
unsigned version();               //changes whenever a name changes, so cached labels can refresh
//All of these may be called from any thread.

//Keys seen this session: key -> built-in name
std::vector<std::string> gunNames();       //every gun the built-in table knows, sorted, no repeats
std::map<std::string, std::string> seen(Cat c); //a copy, safe to use while the tracker thread adds keys

void load();                      //read the saved names (call once at start)
void save();
void setDir(const std::string& dir); //tests only; default is ~/.config/vanityarc

const char* catName(Cat c);

} // namespace labels

#endif //VANITYARC_LABELS_H
