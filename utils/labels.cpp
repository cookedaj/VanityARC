#include "labels.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <utility>
#include <unordered_map>
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
    for (size_t at = out.find("arc"); at != std::string::npos; at = out.find("arc", at + 1)) { //the word ARC, not "arcade" (before title case, while all lower case)
        const bool wordStart = at == 0 || out[at - 1] == ' ';
        const bool wordEnd = at + 3 == out.size() || out[at + 3] == ' ';
        if (wordStart && wordEnd) { out[at] = 'A'; out[at + 1] = 'R'; out[at + 2] = 'C'; }
    }
    for (size_t i = 0; i < out.size(); i++)
        if (i == 0 || out[i - 1] == ' ') out[i] = (char)std::toupper((unsigned char)out[i]); //title case, as the wiki writes names
    return out.empty() ? raw : out;
}

struct Rule { const char* key; const char* name; };

//ARC machines: exact match on the name after "DA_EnemyType_" (letters/digits only)
//Names as the ARC Raiders wiki lists them (beebom, Nov 2026). The internal keys are the usual ones; a key that does not
//appear in the game just never matches, and the cleaned name is shown instead.
const Rule ARC_NAMES[] = {
    {"tick", "Tick"}, {"rollbotpop", "Pop"}, {"rollbotflamethrower", "Fireball"}, {"lightdrone02", "Wasp"},
    {"lightdroneelite", "Hornet"}, {"turretenemy", "Turret"},
    {"snitch", "Snitch"}, {"surveyor", "Surveyor"}, {"sentinel", "Sentinel"}, {"spotter", "Spotter"},
    {"bombardier", "Bombardier"}, {"bastion", "Bastion"}, {"rocketeer", "Rocketeer"}, {"leaper", "Leaper"},
    {"queen", "The Queen"}, {"shredder", "Shredder"}, {"matriarch", "Matriarch"}, {"firefly", "Firefly"},
    {"comet", "Comet"}, {"vaporizer", "Vaporizer"}, {"arcturbine", "ARC Turbine"}, {"erraticwasp", "Erratic Wasp"},
    {"bully", "Bully"}, {"skulker", "Skulker"}, {"hydra", "Hydra"},
};

//Containers: the first rule whose key appears inside the name wins, so specific ones come first
const Rule CONTAINER_RULES[] = {
    {"whitemedicaldrawer", "White Medical Drawer"},
    {"medicaldrawer", "Medical Drawer"},
    {"bluemultidrawer", "Blue Multi Drawers"},
    {"medicalbag", "Medical Bag"},
    {"computerpaneldrawer", "Computer Panel Drawer"},
    {"electricalcabinet", "Electrical Cabinet"},
    {"controlpanel", "Control Panel"},
    {"breakerbox", "Breaker Box"},
    {"mobilegenerator", "Mobile Generator"},
    {"serverrack", "Server Rack"},
    {"server", "Server Rack"},
    {"computer", "Computer"},
    {"android", "Android"},
    {"terminal", "Terminals"},
    {"carhood", "Car Hood"},
    {"bustrunk", "Bus Trunk"},
    {"cartoolcabinet", "Car Tool Cabinet"},
    {"industrialdrawer", "Industrial Drawer"},
    {"mechanicalcabinet", "Mechanical Cabinet"},
    {"metalcrate", "Metal Crate"},
    {"yellowtoolcabinet", "Yellow Tool Cabinet"},
    {"truck", "Truck"},
    {"museumcrate", "Museum Crate"},
    {"securitylocker", "Security Locker"},
    {"boxlocker", "Box Locker"},
    {"greenlocker", "Green Locker"},
    {"locker", "Locker"},
    {"residentialcabinet", "Residential Cabinet"},
    {"closet", "Closet and Drawer"},
    {"fridge", "Fridge"},
    {"dumpster", "Dumpster"},
    {"trash", "Trash Can"},
    {"suitcase", "Suitcase"},
    {"weaponcase", "Weapon Case"},
    {"ammocase", "Ammo Case"},
    {"ammobox", "Ammo Box"},
    {"backpack", "Backpack"},
    {"grenadetube", "Grenade Tube"},
    {"grenadecontainer", "Grenade Tube"},
    {"raidercache", "Raider Cache"},
    {"wickerbasket", "Wicker Basket"},
    {"gardenbed", "Garden Bed"},
    {"basketoffruit", "Basket of Fruit"},
    {"snowpile", "Snow Pile"},
    {"crashedarcprobe", "Crashed ARC Probe"},
    {"arcprobe", "ARC Probe"},
    {"destroyedarc", "Destroyed ARC"},
    {"arcassessor", "ARC Assessor"},
    {"wasphusk", "Wasp Husk"},
    {"rocketeerhusk", "Rocketeer Husk"},
    {"baronhusk", "Baron Husk"},
    {"deforesterhusk", "ARC Deforester Husk"},
    {"arccourier", "ARC Courier"},
    {"courier", "ARC Courier"},
    {"emperorrelay", "Damaged Emperor Relay"},
    {"dropoffstation", "Drop Off Station"},
    {"firstwavecache", "First Wave Cache"},
};

//Guns and items: the longest key that appears inside the name wins (letters/digits only, lower case)

//Item names as the ARC Raiders wiki spells them (item lists on wzstats.gg and Dexerto, 2026, plus Stiletto, Bantam,
//Rascal and Pickaxe). An item whose internal name matches one of these, letters and digits only and in any case,
//gets the wiki spelling. Anything else keeps the cleaned internal name.
const char* const WIKI_ITEMS = "Acoustic Guitar|Adrenaline Shot|Advanced ARC Powercell|Advanced Electrical Components|Advanced Mechanical Components|Agave|Agave Juice|Air Freshener|Alarm Clock|Ancient Fort Security Code|Angled Grip I|Angled Grip II|Angled Grip III|Antiseptic|Anvil Splitter|Aphelion|Apricot|ARC Alloy|ARC Circuitry|ARC Coolant|ARC Flex Rubber|ARC Motion Core|ARC Performance Steel|ARC Powercell|ARC Synthetic Resin|ARC Thermo Lining|Assessor Matrix|Assorted Seeds|Banana|Bandage|Barricade Kit|Bastion Cell|Battery|Bicycle Pump|Binoculars|Blaze Grenade|Blaze Grenade Trap|Bloated Tuna Can|Blue Gate Cellar Key|Blue Gate Communication Tower Key|Blue Gate Confiscation Room Key|Blue Gate Village Key|Blue Light Stick|Bombardier Cell|Breathtaking Snow Globe|Broken Flashlight|Broken Guidance System|Broken Handheld Radio|Broken Taser|Burgerboy|Buried City Hospital Key|Buried City JKV Employee Access Card|Buried City Residential Master Key|Buried City Town Hall Key|Burned ARC Circuitry|Burnt-Out Candles|Camera Lens|Candle Holder|Candleberries|Canister|Cat Bed|Celeste's Journal|Cheer|Chemicals|Coffee Pot|Combat Mk. 1|Combat Mk. 2|Combat Mk. 3 (Aggressive)|Combat Mk. 3 (Flanking)|Comet Igniter|Compensator I|Compensator II|Compensator III|Complex Gun Parts|Coolant|Cooling Coil|Cooling Fan|Cracked Bioscanner|Crude Explosives|Crumpled Plastic Bottle|Dam Control Tower Key|Dam Staff Room Key|Dam Surveillance Key|Dam Testing Annex Key|Dam Utility Key|Damaged ARC Motion Core|Damaged ARC Powercell|Damaged Fireball Burner|Damaged Heat Sink|Damaged Hornet Driver|Damaged Rocketeer Driver|Damaged Snitch Scanner|Damaged Tick Pod|Damaged Wasp Driver|Dart Board|Deadline|Defibrillator|Deflated Football|Degraded ARC Rubber|Diving Goggles|Dog Collar|Dolabra|Door Blocker|Dried-Out ARC Resin|Duct Tape|Durable Cloth|Electrical Components|Empty Wine Bottle|Energy Clip|Equalizer|ESR Analyzer|Exodus Modules|Experimental Seed Sample|Expired Pasta|Expired Respirator|Explosive Compound|Explosive Mine|Extended Barrel|Extended Light Mag I|Extended Light Mag II|Extended Light Mag III|Extended Medium Mag I|Extended Medium Mag II|Extended Medium Mag III|Extended Shotgun Mag I|Extended Shotgun Mag II|Extended Shotgun Mag III|Fabric|Faded Photograph|Fertilizer|Film Reel|Fine Wristwatch|Fireball Burner|Firecracker|Firefly Burner|Fireworks Box|First Wave Compass|First Wave Rations|First Wave Tape|Flame Spray|Flow Controller|Flushing Terminal Key|Fossilized Lightning|Free Loadout Augment|Frequency Modulation Box|Fried Motherboard|Fruit Mix|Frying Pan|Garlic Press|Gas Grenade|Gas Grenade Trap|Gas Mine|Geiger Counter|Great Mullein|Green Light Stick|Headphones|Heavy Ammo|Heavy Fuze Grenade|Heavy Gun Parts|Heavy Shield|Herbal Bandage|Horizontal Grip|Hornet Driver|Household Cleaner|Hullcracker|Humidifier|Ice Cream Scooper|Il Toro|Impure ARC Coolant|Industrial Battery|Industrial Charger|Industrial Magnet|Integrated Binoculars|Integrated Defibrialtor|Integrated Shield Recharger|Ion Sputter|Jolt Mine|Junior Outfit|Jupiter|Kettle|Kinetic Converter|Laboratory Reagents|Lance's Mixtape (5th Edition)|Launcher Ammo|Leaper Pulse Unit|Lemon|Li'l Smoke Grenade|Lidar Scanner|Light Ammo|Light Bulb|Light Gun Parts|Light Impact Grenade|Light Shield|Lightweight Stock|Looting Mk. 1|Looting Mk. 2|Looting Mk. 3 (Cautious)|Looting Mk. 3 (Safekeeper)|Looting Mk. 3 (Survivor)|Lure Grenade|Lure Grenade Trap|Magnet|Magnetic Accelerator|Major Aiva's Patch|Matriarch Reactor|Mechanical Components|Medium Ammo|Medium Gun Parts|Medium Shield|Metal Brackets|Metal Parts|Microscope|Mini Centrifuge|Mod Components|Moss|Motor|Mushroom|Music Album|Music Box|Muzzle Brake I|Muzzle Brake II|Muzzle Brake III|Noisemaker|Number Plate|Oil|Olives|Osprey|Padded Stock|Painted Box|Patrol Car Key|Photoelectric Cloak|Pilgrim's Peak Security Code|Plastic Parts|Playing Cards|Polluted Air Filter|Pop Trigger|Portable TV|Poster Of Natural Wonders|Pottery|Power Bank|Power Cable|Power Rod|Prickly Pear|Processor|Projector|Pulse Mine|Queen Reactor|Radio|Radio Relay|Radio Renegade|Raider Flag|Raider Hatch Key|Raider Tokens|Raider's Refuge Security Code|Rattler|Recorder|Red Coral Jewelry|Red Light Stick|Remote Control|Remote Raider Flare|Renegade|Resin|Ripped Safety Vest|Rocket Thruster|Rocketeer Driver|Roots|Rope|Rosary|Rotary Encoder|Rubber Duck|Rubber Pad|Rubber Parts|Ruined Accordion|Ruined Augment|Ruined Baton|Ruined Handcuffs|Ruined Parachute|Ruined Riot Shield|Ruined Tactical Vest|Rusted Bolts|Rusted Gear|Rusted Shut Medical Kit|Rusted Tools|Rusty ARC Steel|Sample Cleaner|Seeker Grenade|Sensors|Sentinel Firing Core|Shaker|Shield Recharger|Shotgun Ammo|Shotgun Choke I|Shotgun Choke II|Shotgun Choke III|Shotgun Silencer|Showstopper|Shrapnel Grenade|Shredder Gyro|Signal Amplifier|Silencer I|Silencer II|Silencer III|Silver Teaspoon Set|Simple Gun Parts|Smoke Grenade|Smoke Grenade Trap|Snap Blast Grenade|Snap Hook|Snitch Scanner|Snowball|Spaceport Container Storage Key|Spaceport Control Tower Key|Spaceport Trench Tower Key|Spaceport Warehouse Key|Speaker Component|Spectrometer|Spectrum Analyzer|Spotter Relay|Spring Cushion|Stable Stock I|Stable Stock II|Stable Stock III|Statuette|Steel Spring|Sterilized Bandage|Stitcher|Surge Shield Recharger|Surveyor Vault|Synthesized Fuel|Syringe|Tactical Mk. 1|Tactical Mk. 2|Tactical Mk. 3 (Defensive)|Tactical Mk. 3 (Healing)|Tactical Mk. 3 (Revival)|Tagging Grenade|Tattered ARC Lining|Tattered Clothes|Telemetry Transceiver|Tempest|Thermostat|Tick Pod|Toaster|Torn Blanket|Torn Book|Torrente|Trailblazer|Trigger 'Nade|Turbo Pump|Unusable Weapon|Vaporizer Regulator|Vase|Venator|Vertical Grip I|Vertical Grip II|Vertical Grip III|Very Comfortable Pillow|Vita Shot|Vita Spray|Volcanic Rock|Voltage Converter|Vulcano|Wasp Driver|Water Filter|Water Pump|Wires|Wolfpack|Yellow Light Stick|Zipline|Stiletto|Bantam|Rascal|Pickaxe|Crash Mat|Surge Coil|White Flag|Alien Duck|Familiar Duck|Flashy Duck|Turbine Compressor|Magnetron";

const Rule ITEM_NAMES[] = {
    {"assaultriflebullpup01", "Tempest"}, {"assaultrifleburst01", "Arpeggio"}, {"assaultrifleheavy01", "Bettina"},
    {"assaultriflelowtier01", "Rattler"}, {"assaultriflepneumatic01", "Kettle"},
    {"battleriflebreachaction01", "Ferro"}, {"battleriflenergyburst01", "Aphelion"}, {"battlerifleleveraction01", "Renegade"},
    {"battleriflesemiautolight01", "Stiletto"}, {"battleriflelight01", "Stiletto"}, //Frozen Trail update
    {"beam01", "Equalizer"}, {"specialbeamrifle01", "Equalizer"},
    {"handcannonsingleaction01", "Anvil"},
    {"launcherantiarcmedium01", "Hullcracker"},
    {"launcherantiarcsingleshot01", "Rascal"}, {"launcherantiarclight01", "Rascal"}, {"launchergrenadecompact01", "Rascal"}, //Frozen Trail
    {"lmgmedium01", "Torrente"}, {"lmgstandard01", "Torrente"},
    {"handcannonrevolver01", "Bantam"}, {"handcannondoubleaction01", "Bantam"}, {"handcannondoubleactionsnubnose01", "Bantam"}, //Frozen Trail
    {"pistol01", "Burletta"}, {"pistolhighpower01", "Venator"}, {"pistolsemiauto02", "Venator"}, {"pistolsilenced01", "Hairpin"},
    {"pumpaction01", "Il Toro"}, {"shotgunpumpaction01", "Il Toro"}, {"shotgunenergyadjustable01", "Dolabra"},
    {"shotgunsemiauto01", "Vulcano"},
    {"smg01", "Bobcat"}, {"smg02", "Bobcat"}, {"smglowtier01", "Stitcher"}, {"smgmedium01", "Canto"},
    {"sniperboltaction01", "Osprey"}, {"sniperrifleboltactionmedium01", "Osprey"}, {"sniperenergy01", "Jupiter"},
    {"specialrailgun01", "Jupiter"},
    //short forms, in case a name arrives without its family prefix
    {"basicmeleeweapon", "Pickaxe"}, //the starting tool everyone has (confirmed by the player, not a wiki list)
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

//The wiki spelling of an item, by its letters-and-digits key ("" when the wiki does not list it)
std::string wikiItem(const std::string& key) {
    static const std::unordered_map<std::string, std::string> byKey = [] {
        std::unordered_map<std::string, std::string> m;
        std::string all = WIKI_ITEMS, cur;
        for (size_t i = 0; i <= all.size(); i++) {
            if (i == all.size() || all[i] == '|') { if (!cur.empty()) m[norm(cur)] = cur; cur.clear(); }
            else cur.push_back(all[i]);
        }
        return m;
    }();
    auto it = byKey.find(key);
    return it == byKey.end() ? std::string() : it->second;
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
            const std::string w = wikiItem(t);
            if (!w.empty()) return w;
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
