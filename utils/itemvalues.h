#pragma once
//Sell values for items. Read from ~/.config/kwin-helper/values.txt, one item per line: "Name | 1234".
//The name is the display name the overlay shows (for example "Snap Hook") or the internal asset name without "DA_Item_".
//Lines starting with # are ignored. Items not in the file have no value and are counted as unpriced.
#ifndef KWINHELPER_ITEMVALUES_H
#define KWINHELPER_ITEMVALUES_H

#include <cstddef>
#include <string>
#include <vector>

namespace itemvalues {

//Value of an item by its display or internal name, or -1 if the file has no price for it
long valueOf(const std::string& name);

//How many prices are loaded (0: no values.txt, or nothing usable in it)
size_t count();

void load();                             //read values.txt (call once at start)
void setDir(const std::string& dir);     //tests only; default is ~/.config/kwin-helper
void setValueForTest(const std::string& name, long value);
void clear();

//Parse the file text into the table (used by load and by the tests)
void parse(const std::string& text);

} // namespace itemvalues

#endif //KWINHELPER_ITEMVALUES_H
