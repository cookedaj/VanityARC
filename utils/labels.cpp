#include "labels.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <utility>
#include <vector>

namespace labels {
namespace {

std::map<std::string, std::string> g_override[CAT_COUNT];
std::map<std::string, std::string> g_seen[CAT_COUNT];
unsigned g_version = 1;
std::string g_dir;
//The tracker thread asks for names while the menu (draw thread) edits them
std::mutex g_mu;

std::string configDir() {
    if (!g_dir.empty()) return g_dir;
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/kwin-helper";
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

//lower case, letters and digits only: "BattleRifle_BreachAction_01" -> "battleriflebreachaction01"
std::string norm(const std::string& s) {
    std::string o;
    for (unsigned char c : s) if (std::isalnum(c)) o.push_back((char)std::tolower(c));
    return o;
}

bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

std::string stripPrefix(std::string s) {
    static const char* prefixes[] = {"DA_EnemyType_", "DA_Item_", "DA_WeaponVisuals_", "BP_WeaponActor_", "BP_SocketContainer_",
                                     "BP_SalvageContainer_", "BP_PickupBase_", "DA_"};
    for (const char* p : prefixes)
        if (startsWith(s, p)) { s.erase(0, std::char_traits<char>::length(p)); break; }
    if (s.size() > 2 && s.compare(s.size() - 2, 2, "_C") == 0) s.erase(s.size() - 2);
    return s;
}

bool isVariantToken(const std::string& t) {
    if (t.empty()) return true;
    if (std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c); })) return true;
    return t.size() == 1 && std::isalpha((unsigned char)t[0]);
}

//"Gvt_BinTrash_01_A" -> "Gvt bin trash"
std::string cleanName(const std::string& raw) {
    std::string s = stripPrefix(raw);
    std::vector<std::string> tok;
    std::string cur;
    for (char c : s) {
        if (c == '_') { tok.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    tok.push_back(cur);
    while (!tok.empty() && isVariantToken(tok.back())) tok.pop_back();

    std::string out;
    for (const std::string& t : tok) {
        if (isVariantToken(t) && !std::isalpha((unsigned char)(t.empty() ? '0' : t[0]))) continue; //drop stray numbers
        std::string word;
        for (size_t i = 0; i < t.size(); i++) {
            unsigned char c = (unsigned char)t[i];
            bool prevLowerOrDigit = i > 0 && (std::islower((unsigned char)t[i - 1]) || std::isdigit((unsigned char)t[i - 1]));
            bool acronymEnd = i > 0 && i + 1 < t.size() && std::isupper((unsigned char)t[i - 1]) && std::isupper(c) && std::islower((unsigned char)t[i + 1]);
            if (i > 0 && std::isupper(c) && (prevLowerOrDigit || acronymEnd)) word.push_back(' ');
            word.push_back((char)c);
        }
        if (!out.empty()) out.push_back(' ');
        out += word;
    }
    out = lower(out);
    if (!out.empty()) out[0] = (char)std::toupper((unsigned char)out[0]);
    return out.empty() ? raw : out;
}

struct Rule { const char* key; const char* name; };

//ARC machines: exact match on the name after "DA_EnemyType_" (letters/digits only)
const Rule ARC_NAMES[] = {
    {"tick", "Tick"}, {"rollbotpop", "Pop"}, {"rollbotflamethrower", "Fireball"}, {"lightdrone02", "Wasp"},
    {"lightdroneelite", "Hornet"}, {"turretenemy", "Turret"},
};

//Containers: the first rule whose key appears inside the name wins, so specific ones come first
const Rule CONTAINER_RULES[] = {
    {"lockermedical", "Medical locker"}, {"medicalbag", "Medical bag"}, {"locker", "Locker"}, {"trash", "Trash can"},
    {"suitcase", "Suitcase"}, {"museumcrate", "Museum crate"}, {"ammobox", "Ammo box"}, {"weaponcase", "Weapon case"},
    {"grenadecontainer", "Grenade case"}, {"backpack", "Backpack"}, {"fusebox", "Fuse box"}, {"seedbox", "Seed box"},
    {"seedconsole", "Seed console"}, {"workbench", "Workbench"}, {"crate", "Crate"}, {"cupboard", "Cupboard"},
    {"drawer", "Drawer"}, {"cartshelf", "Shelf"}, {"trolley", "Trolley"}, {"computer", "Computer"}, {"server", "Server"},
    {"console", "Console"}, {"desk", "Desk"}, {"streetsweeper", "Street sweeper"}, {"benchkitchen", "Kitchen bench"},
};

//Guns and items: the longest key that appears inside the name wins (letters/digits only, lower case)
const Rule ITEM_NAMES[] = {
    {"assaultriflebullpup01", "Tempest"}, {"assaultrifleburst01", "Arpeggio"}, {"assaultrifleheavy01", "Bettina"},
    {"assaultriflelowtier01", "Rattler"}, {"assaultriflepneumatic01", "Kettle"},
    {"battleriflebreachaction01", "Ferro"}, {"battleriflenergyburst01", "Aphelion"}, {"battlerifleleveraction01", "Renegade"},
    {"battleriflesemiautolight01", "Stiletto"}, {"battleriflelight01", "Stiletto"},
    {"beam01", "Equalizer"}, {"specialbeamrifle01", "Equalizer"},
    {"handcannonrevolver01", "Bantam"}, {"handcannondoubleaction01", "Bantam"}, {"handcannondoubleactionsnubnose01", "Bantam"},
    {"handcannonsingleaction01", "Anvil"},
    {"launcherantiarcmedium01", "Hullcracker"}, {"launcherantiarcsingleshot01", "Rascal"}, {"launcherantiarclight01", "Rascal"},
    {"launchergrenadecompact01", "Rascal"},
    {"lmgmedium01", "Torrente"}, {"lmgstandard01", "Torrente"},
    {"pistol01", "Burletta"}, {"pistolhighpower01", "Venator"}, {"pistolsemiauto02", "Venator"}, {"pistolsilenced01", "Hairpin"},
    {"pumpaction01", "Il Toro"}, {"shotgunpumpaction01", "Il Toro"}, {"shotgunenergyadjustable01", "Dolabra"},
    {"shotgunsemiauto01", "Vulcano"},
    {"smg01", "Bobcat"}, {"smg02", "Bobcat"}, {"smglowtier01", "Stitcher"}, {"smgmedium01", "Canto"},
    {"sniperboltaction01", "Osprey"}, {"sniperrifleboltactionmedium01", "Osprey"}, {"sniperenergy01", "Jupiter"},
    {"specialrailgun01", "Jupiter"},
    {"basicmeleeweapon", "Pickaxe"},
    //short forms, in case a name arrives without its family prefix
    {"breachaction01", "Ferro"}, {"leveraction01", "Renegade"}, {"pneumatic01", "Kettle"}, {"singleaction01", "Anvil"},
};

} // namespace

const char* catName(Cat c) {
    static const char* n[CAT_COUNT] = {"arc", "container", "item", "player"};
    return n[(int)c];
}

std::string itemToken(const std::string& raw) {
    std::string s = raw;
    static const char* prefixes[] = {"DA_Item_", "DA_WeaponVisuals_", "BP_WeaponActor_"};
    for (const char* p : prefixes)
        if (startsWith(s, p)) { s.erase(0, std::char_traits<char>::length(p)); break; }
    if (s.size() > 2 && s.compare(s.size() - 2, 2, "_C") == 0) s.erase(s.size() - 2);
    return s;
}

std::string defaultName(Cat c, const std::string& key) {
    std::string n = norm(stripPrefix(key));
    switch (c) {
        case Cat::Arc:
            for (const Rule& r : ARC_NAMES) if (n == r.key) return r.name;
            break;
        case Cat::Container:
            for (const Rule& r : CONTAINER_RULES) if (n.find(r.key) != std::string::npos) return r.name;
            break;
        case Cat::Item: {
            const Rule* best = nullptr;
            size_t bestLen = 0;
            std::string t = norm(itemToken(key));
            for (const Rule& r : ITEM_NAMES) {
                size_t len = std::char_traits<char>::length(r.key);
                if (len > bestLen && t.find(r.key) != std::string::npos) { best = &r; bestLen = len; }
            }
            if (best) return best->name;
            break;
        }
        case Cat::Player: return key;
    }
    return cleanName(itemToken(key));
}

std::string display(Cat c, const std::string& key, const std::string& def) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto& seenMap = g_seen[(int)c];
    std::string base = def.empty() ? defaultName(c, key) : def;
    seenMap[key] = base;
    auto it = g_override[(int)c].find(key);
    return it != g_override[(int)c].end() ? it->second : base;
}

void setOverride(Cat c, const std::string& key, const std::string& name) {
    std::lock_guard<std::mutex> lk(g_mu);
    std::string n;
    for (char ch : name) if (ch != '\t' && ch != '\n' && ch != '\r') n.push_back(ch);
    while (!n.empty() && n.back() == ' ') n.pop_back();
    auto& m = g_override[(int)c];
    auto it = m.find(key);
    if (n.empty()) { if (it == m.end()) return; m.erase(it); }
    else { if (it != m.end() && it->second == n) return; m[key] = n; }
    g_version++;
}

std::string overrideOf(Cat c, const std::string& key) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_override[(int)c].find(key);
    return it == g_override[(int)c].end() ? std::string() : it->second;
}

std::vector<std::string> gunNames() {
    std::vector<std::string> v;
    for (const Rule& r : ITEM_NAMES) v.push_back(r.name);
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

unsigned version() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_version;
}

std::map<std::string, std::string> seen(Cat c) {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_seen[(int)c]; //a copy: the other thread may add keys while the menu draws the list
}

void setDir(const std::string& dir) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_dir = dir;
}

void load() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::ifstream f(configDir() + "/labels.txt");
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back(); //file edited on another system
        size_t a = line.find('\t');
        size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos) continue;
        std::string cat = line.substr(0, a), key = line.substr(a + 1, b - a - 1), name = line.substr(b + 1);
        for (int i = 0; i < CAT_COUNT; i++)
            if (cat == catName((Cat)i) && !key.empty() && !name.empty()) g_override[i][key] = name;
    }
    g_version++;
}

void save() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::error_code ec;
    std::filesystem::create_directories(configDir(), ec);
    //Write to a temporary file and rename it over the real one: a crash or a full disk in the middle of
    //saving can never leave you with a half-written (or empty) names file.
    const std::string path = configDir() + "/labels.txt", tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        for (int i = 0; i < CAT_COUNT; i++)
            for (auto& kv : g_override[i]) f << catName((Cat)i) << '\t' << kv.first << '\t' << kv.second << '\n';
        f.flush();
        if (!f) { std::filesystem::remove(tmp, ec); return; }
    }
    std::filesystem::rename(tmp, path, ec);
}

} // namespace labels
