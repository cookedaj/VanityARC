#pragma once
//Display names. The game gives raw names ("DA_EnemyType_Tick", "BP_SocketContainer_Gvt_BinTrash_01_A_C",
//"DA_Item_BattleRifle_BreachAction_01"); this turns them into what you see on screen ("Tick", "Trash can",
//"Ferro") and uses the names you typed yourself in ~/.config/kwin-helper/labels.txt (read only, never written).
#ifndef KWINHELPER_LABELS_H
#define KWINHELPER_LABELS_H

#include <map>
#include <string>
#include <vector>

namespace labels {

enum class Cat { Arc = 0, Container = 1, Item = 2, Player = 3 };
constexpr int CAT_COUNT = 4;

//Built-in name for a raw key (the table first, then a cleaned-up version of the raw name)
std::string defaultName(Cat c, const std::string& key);

//The name to show: your own name if you set one (by raw key, else by the shown name), else def, else the built-in name.
//May be called from any thread.
std::string display(Cat c, const std::string& key, const std::string& def = "");

//Item / gun key: the part of a raw name that identifies the item ("BattleRifle_BreachAction_01")
std::string itemToken(const std::string& raw);

int load();                       //read labels.txt (call once at start); returns how many lines were ignored
void setDir(const std::string& dir); //tests only; default is ~/.config/kwin-helper

const char* catName(Cat c);

} // namespace labels

#endif //KWINHELPER_LABELS_H
