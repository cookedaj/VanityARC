#include "labels.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
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
std::unordered_map<std::string, std::string> g_shown; //"Trash Can = Bin" lines, by norm() of the shown name (any category)
std::string g_dir;
//The tracker and draw threads ask for names; load() fills them
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
//Keys and names from the game's own DA_EnemyType and DA_EnemyMetaData files (Teralix42/ArcRaidersAssets, game 2.4.0)
//and ARES-Server, checked against the ARC Raiders wiki (November 2026). A machine not listed shows its cleaned name.
const Rule ARC_NAMES[] = {
    {"tick", "Tick"}, {"rollbotpop", "Pop"}, {"rollbotflamethrower", "Fireball"}, {"lightdrone02", "Wasp"},
    {"lightdroneelite", "Hornet"}, {"turretenemy", "Turret"},
    {"snitchdrone", "Snitch"}, {"snitchdroneonboarding", "Snitch"}, {"rollbot", "ARC Surveyor"}, {"sniper", "Sentinel"},
    {"pinger", "Spotter"}, {"chonkplatformmortar", "Bombardier"}, {"chonk", "Bastion"}, {"heavydronemissile", "Rocketeer"},
    {"bullcrab", "Leaper"}, {"peppermint", "Queen"}, {"runner", "Shredder"}, {"spearmint", "Matriarch"},
    {"elitedroneflamethrower", "Firefly"}, {"rollbotboom", "Comet"}, {"heavydronelaser", "Vaporizer"},
    {"monolith", "ARC Turbine"}, {"tarragon01", "Bully"}, {"meerkat", "Skulker"}, {"kebabgatling", "Hydra"},
    {"courier", "ARC Reclaimer"}, {"rose", "Frigate Cannon"}, //Rose: the ARC Frigate's big gun (game text)
};

//Containers: the first rule whose key appears inside the name wins, so specific ones come first
//Names from the game's text where it has one, else the ARC Raiders wiki (November 2026). Furniture has game names only as Outpost pieces, so
//Residential Closet, Terminal, Medical Drawer and Refrigerator are the wiki's; Trash Can has no source yet.
const Rule CONTAINER_RULES[] = {
    {"whitemedicaldrawer", "White Medical Drawer"},
    {"medicaldrawer", "Medical Drawer"},
    {"drawermedical", "Medical Drawer"}, //Res_DrawerMedical_01_A
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
    {"terminal", "Terminal"},
    {"carhood", "Car Hood"},
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
    {"closet", "Residential Closet"},
    {"fridge", "Fridge"},
    {"refrigerator", "Refrigerator"},
    {"dumpster", "Dumpster"},
    {"trash", "Trash Can"},
    {"suitcase", "Suitcase"},
    {"weaponcase", "Weapon Case"},
    {"ammocase", "Ammo Case"},
    {"ammobox", "Ammo Box"},
    {"backpack", "Worn Backpack"},
    {"grenadetube", "Grenade Tube"},
    {"grenadecontainer", "Grenade Tube"},
    {"raidercache", "Raider Cache"},
    {"wickerbasket", "Wicker Basket"},
    {"fruitbasket", "Basket of Fruit"},
    {"snowpile", "Snow Pile"},
    {"candleberries", "Candleberry Bush"},
    {"volcanicrock", "Volcanic Rock"},
    {"crashedarcprobe", "Crashed ARC Probe"},
    {"arcprobe", "ARC Probe"},
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

//Item names as the game spells them (game text 2.4.0, the ARC Raiders wiki and MetaForge, November 2026; Pickaxe as
//the player confirmed). An item whose internal name matches one of these, letters and digits only and in any case,
//gets this spelling. Anything else keeps the cleaned internal name.
const char* const WIKI_ITEMS = "\"Leviathan's Crown\" Ship Model|\"Sirena Dorata\" Ship Model|\"Twilight Compass\" Ship Model|\"Velocity\" Ship Model|\"Wind Sprite\" Ship Model|Acoustic Guitar|Adrenaline Shot|Advanced ARC Powercell|Advanced Camera|Advanced Electrical Components|Advanced Mechanical Components|Agave|Agave Juice|Air Freshener|Alarm Clock|Alien Duck|Amplification Module MK. I|Amplification Module MK. II|Amplification Module MK. III|Amplification Module MK. IV|Amplification Module MK. V|Amplified Fragments|Ancient Fort Security Code|Angled Grip I|Angled Grip II|Angled Grip III|Antiseptic|Anvil Splitter|Aphelion|Apricot|ARC Alloy|ARC Circuitry|ARC Conductive Coupler|ARC Coolant|ARC Data Module|ARC Flex Rubber|ARC Insulated Coupler|ARC Motion Core|ARC Performance Steel|ARC Plated Coupler|ARC Powercell|ARC Synthetic Resin|ARC Thermo Lining|Arcade Duck|Archive Desk Lamp|Assessor Matrix|Assorted Seeds|Banana|Bandage|Banjo|Bantam|Barricade Kit|Basic Camera|Bastion Cell|Battered Paperback|Battery|Bent Clothes Hanger|Bicycle Pump|Binoculars|Blaze Grenade|Blaze Grenade Trap|Bloated Tuna Can|Blown Fuses|Blue Gate Cellar Key|Blue Gate Communication Tower Key|Blue Gate Confiscation Room Key|Blue Gate Village Key|Blue Light Stick|Bombardier Cell|Breathtaking Snow Globe|Broken Compass|Broken Flashlight|Broken Guidance System|Broken Handheld Radio|Broken Taser|Bronze Statuette|Bully Fragmenter|Bureau Desk Lamp|Burgerboy|Buried City Hospital Key|Buried City JKV Employee Access Card|Buried City Residential Master Key|Buried City Town Hall Key|Burned ARC Circuitry|Burnt-out Candles|Cable Stripper|Calculator|Camera Lens|Candle Holder|Candleberries|Canister|Cat Bed|Celeste's Journal|Cheer|Chemicals|Coffee Pot|Combat Mk. 1|Combat Mk. 2|Combat Mk. 3 (Aggressive)|Combat Mk. 3 (Flanking)|Comet Igniter|Compensator I|Compensator II|Compensator III|Complex Gun Parts|Coolant|Cooling Coil|Cooling Fan|Cracked Bioscanner|Crash Mat|Crude Explosives|Crumpled Plastic Bottle|Dam Control Tower Key|Dam Controlled Access Zone Key|Dam Staff Room Key|Dam Surveillance Key|Dam Testing Annex Key|Dam Utility Key|Damaged ARC Motion Core|Damaged ARC Powercell|Damaged Fireball Burner|Damaged Heat Sink|Damaged Hornet Driver|Damaged Leaper Pulse Unit|Damaged Rocketeer Driver|Damaged Snitch Scanner|Damaged Tick Pod|Damaged Wasp Driver|Dart Board|Dead Flowers|Deadline|Defibrillator|Deflated Football|Degraded ARC Rubber|Desk Lamp|Differential Microphone|Diving Goggles|Dockmaster's Detector|Dodger's Note|Dog Collar|Dolabra|Doodly Duck|Door Blocker|Dried-Out ARC Resin|Duct Tape|Durable Cloth|Dusty Film Reel|Electrical Components|Elephant Obelisk|Emperor Beacon|Emperor Gateway Conduit|Emperor Modulator|Empty Wine Bottle|Energy Clip|Epoxy Bucket|Equalizer|Equatorial Sundial|Espresso Machine Parts|ESR Analyzer|Exodus Modules|Exodus Technical Handbook|Experimental Seed Sample|Expired Pasta|Expired Respirator|Expired Security Code|Explosive Compound|Explosive Mine|Extended Barrel I|Extended Barrel II|Extended Barrel III|Extended Light Mag I|Extended Light Mag II|Extended Light Mag III|Extended Medium Mag I|Extended Medium Mag II|Extended Medium Mag III|Extended Shotgun Mag I|Extended Shotgun Mag II|Extended Shotgun Mag III|Fabric|Faded Photograph|Familiar Duck|Fancy Plate|Fertilizer|Film Reel|Fine Wristwatch|Fireball Burner|Firecracker|Firefly Burner|Fireworks Box|First Wave Compass|First Wave Rations|First Wave Tape|Flame Spray|Flashy Duck|Flow Controller|Flushing Terminal Key|Fluted Rotor Core|Food Wrapper|Fossilized Lightning|Free Loadout Augment|Frequency Modulation Box|Fried Motherboard|Frigate Diagnostic Node|Frigate Inductor|Frosty Duck|Fruit Mix|Frying Pan|Garlic Press|Gas Grenade|Gas Grenade Trap|Gas Mine|Geiger Counter|Gentle Duck|Glitched ARC Circuitry|Glitched ARC Light Ring|Glitched ARC Phased Array|Glitched ARC Power Converter|Glitched ARC Transmitter|Grappling Hook|Great Mullein|Green Light Stick|Hand Drill|Harmonica|Headphones|Heavy Ammo|Heavy Fuze Grenade|Heavy Gun Parts|Heavy Shield|Herbal Bandage|Horizontal Grip|Hornet Driver|Household Cleaner|Hullcracker|Humidifier|Husk Lily|Ice Cream Scooper|Il Toro|Impure ARC Coolant|Industrial Battery|Industrial Charger|Industrial Magnet|Insulation Roll|Integrated Binoculars|Integrated Defibrillator|Integrated Shield Recharger|Ion Sputter|Jolt Mine|Jupiter|Kettle|Kinetic Converter|Laboratory Reagents|Lance's Mixtape (5th edition)|Laser Level|Launcher Ammo|Leaper Pulse Unit|Lemon|Li'l Smoke Grenade|Library Book|LiDAR Scanner|Light Ammo|Light Bulb|Light Gun Parts|Light Impact Grenade|Light Shield|Lightweight Stock|Looting Mk. 1|Looting Mk. 2|Looting Mk. 3 (Cautious)|Looting Mk. 3 (Safekeeper)|Looting Mk. 3 (Survivor)|Lure Grenade|Lure Grenade Trap|Magnet|Magnetic Accelerator|Magnetron|Major Aiva's Mementos|Major Aiva's Patch|Marano Market Business Card|Mask Fragment|Matriarch Reactor|Mechanical Components|Medium Ammo|Medium Gun Parts|Medium Shield|Metal Brackets|Metal Parts|Microscope|Mini Centrifuge|Mini Pump|Mod Components|Moisture Meter|Moss|Motor|Mountaineer's Detector|MRI Duck|Mushroom|Music Album|Music Box|Muzzle Brake I|Muzzle Brake II|Muzzle Brake III|Noisemaker|Number Plate|Nutrient Meter|Official Shutdown Documentation|Oil|Old World Books|Olives|Osprey|Padded Stock|Painted Box|Patrol Car Key|Pendola Pass Observatory Key|Pendola Pass Old Town Key|Pendola Pass Railyard Key|Pendola Pass Train Station Key|Photoelectric Cloak|Pickaxe|Pilgrim's Peak Security Code|Planks|Plastic Parts|Plastic Trash|Playing Cards|Pneumatic Actuator|Polluted Air Filter|Pop Trigger|Portable TV|Possibly Toxic Plant|Poster of Natural Wonders|Potted Plant|Pottery|Power Bank|Power Cable|Power Rod|Powered Descender|Precision Gimbal|Pressure Regulator|Prickly Pear|Processor|Project Heartwood Blueprints|Project Pathos Empathy Core|Projector|Propellant Valve Assembly|Pulse Mine|Purification System Blueprint|Queen Reactor|Radial Press|Radio|Radio Relay|Radio Renegade|Raider Flag|Raider Hatch Key|Raider Logbook|Raider Tokens|Raider's Refuge Security Code|Railway Lantern|Rascal|Rattler|Recorder|Red Coral Jewelry|Red Light Stick|Reinforced Reception Security Code|Remote Control|Remote Raider Flare|Renegade|Resin|Ripped Safety Vest|Riven Tides Classified Records Keycard|Riven Tides Crane House Keycard|Riven Tides Hotel Keycard No. 102|Riven Tides Hotel Keycard No. 107|Riven Tides Hotel Keycard No. 113|Riven Tides Hotel Keycard No. 205|Riven Tides Hotel Keycard No. 208|Riven Tides Hotel Keycard No. 311|Riven Tides Hotel Keycard No. 404|Riven Tides Secure Storage Keycard|Rock Salt|Rocket Thruster|Rocketeer Driver|Roots|Rope|Rosary|Rotary Encoder|Rubber Duck|Rubber Pad|Rubber Parts|Ruined Accordion|Ruined Augment|Ruined Baton|Ruined Emperor Beacon|Ruined Handcuffs|Ruined Parachute|Ruined Riot Shield|Ruined Tactical Vest|Rusted Bolts|Rusted Gear|Rusted Shut Medical Kit|Rusted Tools|Rusty ARC Steel|Sample Cleaner|Scout Patrol Note|Secret Meeting Info|Seed Vault Box Key|Seeker Grenade|Sensors|Sentinel Firing Core|Sextant|Shaker|Sheet Metal|Shield Recharger|Shotgun Ammo|Shotgun Choke I|Shotgun Choke II|Shotgun Choke III|Shotgun Silencer|Showstopper|Shrapnel Grenade|Shredder Gyro|Signal Amplifier|Silencer I|Silencer II|Silencer III|Silver Teaspoon Set|Simple Gun Parts|Skulker Targeter|Smoke Grenade|Smoke Grenade Trap|Snap Blast Grenade|Snap Hook|Snitch Scanner|Snowball|Spaceport Container Storage Key|Spaceport Control Tower Key|Spaceport Trench Tower Key|Spaceport Warehouse Key|Speaker Component|Spectrometer|Spectrum Analyzer|Spotter Relay|Spring Cushion|Stable Stock I|Stable Stock II|Stable Stock III|Stack of Movie Tapes|Statuette|Steel Cable|Steel Spring|Stella Montis Archives Key|Stella Montis Assembly Admin Key|Stella Montis Medical Storage Key|Stella Montis Security Checkpoint Key|Stencil Parts|Sterilized Bandage|Stiletto|Stitcher|Surge Coil|Surge Shield Recharger|Surveyor Vault|Synthesized Fuel|Syringe|Tactical Mk. 1|Tactical Mk. 2|Tactical Mk. 3 (Defensive)|Tactical Mk. 3 (Healing)|Tactical Mk. 3 (Revival)|Tactical Mk. 3 (Smoke)|Tagging Grenade|Tattered ARC Lining|Tattered Clothes|Telemetry Transceiver|Tellurion|Tempest|Tether Launcher|Thermal Shaker|Thermostat|Tick Pod|Toaster|Torn Blanket|Torn Book|Torrente|Trailblazer|Train Model|Trigger 'Nade|Tropical Duck|Turbine Compressor|Turbo Pump|Unusable Weapon|Vaporizer Regulator|Vase|Venator|Vertical Grip I|Vertical Grip II|Vertical Grip III|Very Comfortable Pillow|Vintage Steering Wheel|Vita Shot|Vita Spray|Volcanic Rock|Voltage Converter|Vulcano|Wasp Driver|Water Filter|Water Pump|White Flag|Wires|Wolfpack|Yellow Light Stick|Zipline";

//Gun keys as the datamines have them (Teralix42/ArcRaidersAssets, ARES-Server, bukueal asset index, November 2026)
const Rule ITEM_NAMES[] = {
    {"assaultriflebullpup01", "Tempest"}, {"assaultrifleburst01", "Arpeggio"}, {"assaultrifleheavy01", "Bettina"},
    {"assaultriflelowtier01", "Rattler"}, {"assaultriflepneumatic01", "Kettle"},
    {"battleriflebreachaction01", "Ferro"}, {"battlerifleenergyburst01", "Aphelion"}, {"battlerifleleveraction01", "Renegade"},
    {"battleriflesemiautolight01", "Stiletto"}, //Frozen Trail update
    {"specialbeamrifle01", "Equalizer"},
    {"handcannonsingleaction01", "Anvil"},
    {"launcherantiarcmedium01", "Hullcracker"},
    {"launcherantiarcsingleshot01", "Rascal"}, //update 1.29.0
    {"lmgmedium01", "Torrente"},
    {"handcannondoubleactionsnubnose01", "Bantam"}, //Frozen Trail
    {"pistol01", "Burletta"}, {"pistolhighpower01", "Venator"}, {"pistolsilenced01", "Hairpin"},
    {"pumpaction01", "Il Toro"}, {"shotgunpumpaction01", "Il Toro"}, {"shotgunenergyadjustable01", "Dolabra"},
    {"shotgunsemiauto01", "Vulcano"},
    {"smg01", "Bobcat"}, {"smglowtier01", "Stitcher"}, {"smgmedium01", "Canto"},
    {"sniperrifleboltactionmedium01", "Osprey"},
    {"specialrailgun01", "Jupiter"},
    //stowed guns: DA_WeaponVisuals_HandCannon_01 and the amplified Jupiter (..._SniperRifle_Railgun_01_Ascended_...)
    {"handcannon01", "Anvil"}, {"specialsniperriflerailgun01", "Jupiter"},
    //short forms, in case a name arrives without its family prefix
    {"basicmeleeweapon", "Pickaxe"}, //the starting tool everyone has (confirmed by the player, not a wiki list)
    {"breachaction01", "Ferro"}, {"leveraction01", "Renegade"}, {"pneumatic01", "Kettle"}, {"singleaction01", "Anvil"},
};

//Items whose internal name does not spell their name: exact match on the name after "DA_Item_" (letters/digits only).
//Looked at before the guns, so a blueprint never shows as its gun. Each key is in the datamines (bukueal asset index
//with its English name, raidertools mapping, ARES-Server) and each name in the game's text, MetaForge or the wiki
//(November 2026).
const Rule KEY_NAMES[] = {
    //keys and key cards
    {"salvagebluegateutilitykey4", "Blue Gate Cellar Key"},
    {"salvagebluegateutilitykey2", "Blue Gate Communication Tower Key"},
    {"salvagebluegateutilitykey1", "Blue Gate Confiscation Room Key"},
    {"salvagebluegateutilitykey3", "Blue Gate Village Key"}, {"salvageburiedcityutilitykey1", "Buried City Hospital Key"},
    {"salvageburiedcityutilitykey2", "Buried City JKV Employee Access Card"},
    {"salvageburiedcityutilitykey4", "Buried City Residential Master Key"},
    {"salvageburiedcityutilitykey3", "Buried City Town Hall Key"}, {"salvagethedamutilitykey2", "Dam Control Tower Key"},
    {"salvagethedamutilitykey3", "Dam Staff Room Key"}, {"salvagethedamutilitykey4", "Dam Surveillance Key"},
    {"salvagethedamutilitykey1", "Dam Testing Annex Key"}, {"keyitemraiderhatchkey", "Raider Hatch Key"},
    {"salvagesecuritycode", "Reinforced Reception Security Code"},
    {"salvageriventideswavebreakerkey1", "Riven Tides Classified Records Keycard"},
    {"salvageriventidescranehousekey", "Riven Tides Crane House Keycard"},
    {"salvageriventideshotelkey1", "Riven Tides Hotel Keycard No. 102"},
    {"salvageriventideshotelkey2", "Riven Tides Hotel Keycard No. 107"},
    {"salvageriventideshotelkey3", "Riven Tides Hotel Keycard No. 113"},
    {"salvageriventideshotelkey4", "Riven Tides Hotel Keycard No. 205"},
    {"salvageriventideshotelkey5", "Riven Tides Hotel Keycard No. 208"},
    {"salvageriventideshotelkey6", "Riven Tides Hotel Keycard No. 311"},
    {"salvageriventideshotelkey7", "Riven Tides Hotel Keycard No. 404"},
    {"salvageriventideswavebreakerkey2", "Riven Tides Secure Storage Keycard"},
    {"salvagespaceportutilitykey3", "Spaceport Container Storage Key"},
    {"salvagespaceportutilitykey1", "Spaceport Control Tower Key"},
    {"salvagespaceportutilitykey2", "Spaceport Trench Tower Key"},
    {"salvagespaceportutilitykey4", "Spaceport Warehouse Key"},
    {"salvagemountaincompoundutilitykey3", "Stella Montis Archives Key"},
    {"salvagemountaincompoundutilitykey4", "Stella Montis Assembly Admin Key"},
    {"salvagemountaincompoundutilitykey2", "Stella Montis Medical Storage Key"},
    {"salvagemountaincompoundutilitykey1", "Stella Montis Security Checkpoint Key"},
    {"salvagefrozentrailobservatorykey01", "Pendola Pass Observatory Key"},
    {"salvagefrozentrailrailyardkey01", "Pendola Pass Railyard Key"},
    {"salvagefrozentrailstationkey01", "Pendola Pass Train Station Key"},
    {"salvagefrozentrailvillagekey", "Pendola Pass Old Town Key"},
    {"salvagecommunityeventtt1bunkerkey", "Hidden Bunker Key"},
    //quest items
    {"salvagequestcelestejournal", "Celeste's Journal"}, {"salvagequestrecordingtape", "Dusty Film Reel"},
    {"salvagequestseedpacket", "Experimental Seed Sample"}, {"salvagequestfirstwavecamcorder", "First Wave Tape"},
    {"salvagequestterminalkey", "Flushing Terminal Key"}, {"salvagequestmementos", "Major Aiva's Mementos"},
    {"salvagequestaivaspatch", "Major Aiva's Patch"}, {"salvagequestoldmarketcard", "Marano Market Business Card"},
    {"salvagequestmoistureprobe", "Moisture Meter"}, {"salvagequestecmeter", "Nutrient Meter"},
    {"salvagequestrarebooks", "Old World Books"}, {"salvagequestplant", "Possibly Toxic Plant"},
    {"salvagequestoldreport", "Project Heartwood Blueprints"}, {"salvagequestseedvaultkey", "Seed Vault Box Key"},
    {"salvagequestvideotape", "Stack of Movie Tapes"}, {"salvagequestcollectorcircuitry", "Unknown ARC Circuitry"},
    //materials and ARC parts
    {"salvagematintactbaronrubber", "ARC Flex Rubber"}, {"salvagematintactbaroningot", "ARC Performance Steel"},
    {"salvagematintactbaronoil", "ARC Synthetic Resin"}, {"salvagematintactbaroncloth", "ARC Thermo Lining"},
    {"salvageextractorpart", "Assessor Matrix"}, {"salvagechonkpart", "Bastion Cell"},
    {"salvagemonkpart", "Bombardier Cell"}, {"salvageflashlightbroken01", "Broken Flashlight"},
    {"salvageguidancesystem01", "Broken Guidance System"}, {"salvageradiobroken01", "Broken Handheld Radio"},
    {"salvagetaserbroken01", "Broken Taser"}, {"salvagematburnedarccircuitboard", "Burned ARC Circuitry"},
    {"salvagecometpart", "Comet Igniter"}, {"salvagescanner01", "Cracked Bioscanner"},
    {"salvagebrokenarcmechanism", "Damaged ARC Motion Core"}, {"salvagematdamagedarccore", "Damaged ARC Powercell"},
    {"salvagematrollbotdamagedpart", "Damaged Fireball Burner"}, {"salvagematdamagedhornetpart", "Damaged Hornet Driver"},
    {"salvagematdamagedbisonpart", "Damaged Leaper Pulse Unit"},
    {"salvagematdamagerocketeerpart", "Damaged Rocketeer Driver"}, {"salvagemattickdamagedpart", "Damaged Tick Pod"},
    {"salvagematdamagedwasppart", "Damaged Wasp Driver"}, {"salvagematbaronrubber", "Degraded ARC Rubber"},
    {"salvagematbaronoil", "Dried-Out ARC Resin"}, {"salvagefireballpart", "Fireball Burner"},
    {"salvagehornetflamethrowerpart", "Firefly Burner"}, {"salvagemathornetpart", "Hornet Driver"},
    {"salvagearcimpurecoolant01", "Impure ARC Coolant"}, {"salvagematbisonpart", "Leaper Pulse Unit"},
    {"salvagespearmintpart", "Matriarch Reactor"}, {"salvagepoppart", "Pop Trigger"},
    {"salvagepeppermintpart", "Queen Reactor"}, {"salvagematheavydronepart", "Rocketeer Driver"},
    {"salvageaccordion01", "Ruined Accordion"}, {"salvagebaton01", "Ruined Baton"},
    {"salvagehandcuffs", "Ruined Handcuffs"}, {"salvageparachute01", "Ruined Parachute"},
    {"salvageriotshieldbroken01", "Ruined Riot Shield"}, {"salvagetacticalwest", "Ruined Tactical Vest"},
    {"salvagemedicalkitbroken01", "Rusted Shut Medical Kit"}, {"salvagematbaroningot", "Rusty ARC Steel"},
    {"salvagesniperpart", "Sentinel Firing Core"}, {"salvagerunnerpart", "Shredder Gyro"},
    {"salvagematsnitchpart", "Snitch Scanner"}, {"salvagepingerpart", "Spotter Relay"},
    {"salvagerollbotpart", "Surveyor Vault"}, {"salvagematbaroncloth", "Tattered ARC Lining"},
    {"salvagemattickpart", "Tick Pod"}, {"salvagemonolithpart", "Turbine Compressor"},
    {"salvagerustedweapon", "Unusable Weapon"}, {"salvageheavydronelaserpart", "Vaporizer Regulator"},
    {"salvagematwasppart", "Wasp Driver"}, {"salvagematarcmetal", "ARC Alloy"},
    {"salvagematarccircuitboard", "ARC Circuitry"}, {"salvagematarcmechanism", "ARC Motion Core"},
    {"salvagematarccore", "ARC Powercell"}, {"salvageexodusprototype", "Exodus Modules"},
    {"salvagetorchginger", "Great Mullein"}, {"salvagegunparts", "Simple Gun Parts"}, {"salvagescrapmetal", "Metal Parts"},
    {"salvageplastic", "Plastic Parts"}, {"salvagerubber", "Rubber Parts"},
    {"salvageadvancedgunparts", "Advanced Mechanical Components"}, {"salvageexplosivemixture", "Crude Explosives"},
    {"salvagewovencloth", "Durable Cloth"}, {"salvagerefinedexplosives", "Explosive Compound"},
    {"salvageseeds", "Assorted Seeds"}, {"salvageeventitemcandleberries", "Candleberries"},
    {"salvagetarragonpart", "Bully Fragmenter"},
    {"salvagemeerkatpartturret", "Skulker Targeter"}, {"salvagerubberband01", "Rubber Bands"},
    //Frozen Trail materials, amplification parts, Emperor and Frigate parts, books
    {"salvagefrozentrailmat1", "Steel Cable"}, {"salvagefrozentrailmat2", "Epoxy Bucket"},
    {"salvagefrozentrailmat3", "Insulation Roll"}, {"salvagefrozentrailmechanical1", "Cable Stripper"},
    {"salvagefrozentrailmechanical2", "Hand Drill"}, {"salvagefrozentrailmechanical3", "Laser Level"},
    {"salvagefrozentrailmechanical4", "Radial Press"}, {"salvagefrozentrailtech1", "Mini Pump"},
    {"salvagefrozentrailtech2", "Calculator"}, {"salvagefrozentrailtech3", "Differential Microphone"},
    {"salvagefrozentrailtech4", "Thermal Shaker"}, {"salvageascensionmaterial1", "Propellant Valve Assembly"},
    {"salvageascensionmaterial2", "Pressure Regulator"}, {"salvageascensionmaterial3", "Fluted Rotor Core"},
    {"salvageascensionmaterial4", "Pneumatic Actuator"}, {"salvageascensionfragments", "Amplified Fragments"},
    {"salvagecommonascensionmodule", "Amplification Module MK. I"},
    {"salvageuncommonascensionmodule", "Amplification Module MK. II"},
    {"salvagerareascensionmodule", "Amplification Module MK. III"},
    {"salvageepicascensionmodule", "Amplification Module MK. IV"},
    {"salvagelegendaryascensionmodule", "Amplification Module MK. V"},
    {"salvagecollector1", "Emperor Modulator"}, {"salvagecollectorkey01", "Emperor Gateway Conduit"},
    {"salvagecollectorkeycomponent01", "ARC Plated Coupler"}, {"salvagecollectorkeycomponent02", "ARC Insulated Coupler"},
    {"salvagecollectorkeycomponent03", "ARC Conductive Coupler"}, {"salvagefrigate1", "Frigate Inductor"},
    {"salvagebrokentransmitter", "Ruined Emperor Beacon"}, {"transmitter", "Emperor Beacon"},
    {"salvagearcmemorycard", "ARC Data Module"}, {"salvageexodusbook", "Exodus Technical Handbook"},
    {"salvagefrigatememorymodule", "Frigate Diagnostic Node"}, {"salvageraiderjournal", "Raider Logbook"},
    {"salvageoldbook", "Battered Paperback"},
    //trinkets
    {"salvagetrinketboatmodel", "\"Leviathan's Crown\" Ship Model"},
    {"salvagetrinketboatmodelc02", "\"Leviathan's Crown\" Ship Model"},
    {"salvagetrinketboatmodele", "\"Sirena Dorata\" Ship Model"},
    {"salvagetrinketboatmodele02", "\"Sirena Dorata\" Ship Model"},
    {"salvagetrinketboatmodelb", "\"Twilight Compass\" Ship Model"},
    {"salvagetrinketboatmodelb02", "\"Twilight Compass\" Ship Model"},
    {"salvagetrinketboatmodeld", "\"Velocity\" Ship Model"}, {"salvagetrinketboatmodeld02", "\"Velocity\" Ship Model"},
    {"salvagetrinketboatmodela", "\"Wind Sprite\" Ship Model"},
    {"salvagetrinketboatmodela02", "\"Wind Sprite\" Ship Model"}, {"salvagetrinketrubberduckalien", "Alien Duck"},
    {"salvagematjunk3", "Bloated Tuna Can"}, {"salvagetrinketrubberduckglitter", "Doodly Duck"},
    {"salvagematjunk2", "Expired Pasta"}, {"salvagetrinketfadedpictureframe", "Faded Photograph"},
    {"salvagetrinketrubberduckscrappy", "Familiar Duck"}, {"salvagetrinketrubberducklance", "Flashy Duck"},
    {"salvagetrinketrubberduckcow", "Gentle Duck"}, {"salvagetrinketlancemixtape", "Lance's Mixtape (5th edition)"},
    {"salvageposternature01", "Poster of Natural Wonders"}, {"salvagetrinketrubberduckstandard", "Rubber Duck"},
    {"salvagetrinketrubberduckflamingo", "Tropical Duck"}, {"salvagecomfortablepillow01", "Very Comfortable Pillow"},
    {"salvagerubberduckwinner01", "MRI Duck"}, {"salvagerubberduckwinner02", "Arcade Duck"},
    {"salvagerubberduckwinner03", "Frosty Duck"}, {"salvagefootballshoes01green", "Colorful Shoes"},
    {"salvagefootballshoes01red", "Colorful Shoes"}, {"salvagefootballshoes01silver", "Colorful Shoes"},
    {"salvagedeadflowerbouquet01", "Dead Flowers"}, {"salvagebronzemaskfragment01", "Mask Fragment"},
    {"salvagemajolicatazza01", "Fancy Plate"},
    {"salvagecorruptedarcpart01", "Glitched ARC Power Converter"}, {"salvagecorruptedarcpart02", "Glitched ARC Transmitter"},
    {"salvagecorruptedarcpart03", "Glitched ARC Phased Array"}, {"salvagecorruptedarcpart04", "Glitched ARC Light Ring"},
    {"salvagecorruptedarcpart05", "Glitched ARC Circuitry"},
    //shields
    {"armorheavybody", "Heavy Shield"}, {"armorlightbody", "Light Shield"}, {"armormediumbody", "Medium Shield"},
    //augments
    {"augmentcombatt1", "Combat Mk. 1"}, {"augmentcombatt2", "Combat Mk. 2"},
    {"augmentcombatt3aggressive", "Combat Mk. 3 (Aggressive)"}, {"augmentcombatt3flanking", "Combat Mk. 3 (Flanking)"},
    {"augmentlootingfreeloadout", "Free Loadout Augment"}, {"augmentlootingt1", "Looting Mk. 1"},
    {"augmentlootingt2", "Looting Mk. 2"}, {"augmentlootingt3cautious", "Looting Mk. 3 (Cautious)"},
    {"augmentraidert4safekeeper", "Looting Mk. 3 (Safekeeper)"}, {"augmentlootingt3dbno", "Looting Mk. 3 (Survivor)"},
    {"augmentutilityt1", "Tactical Mk. 1"}, {"augmentutilityt2", "Tactical Mk. 2"},
    {"augmentutilityt3defensive", "Tactical Mk. 3 (Defensive)"}, {"augmentutilityt3healing", "Tactical Mk. 3 (Healing)"},
    {"augmentraidert4revival", "Tactical Mk. 3 (Revival)"}, {"augmentutilityt3smoke", "Tactical Mk. 3 (Smoke)"},
    //gadgets
    {"commonbinoculars", "Binoculars"}, {"metaldetector01", "Dockmaster's Detector"}, {"spraycanfire", "Flame Spray"},
    {"deployablesoundtrap", "Noisemaker"}, {"arcfriendifiergadgetv2", "Photoelectric Cloak"},
    {"gadgetinstrumentflute", "Recorder"}, {"deployablebarricade", "Barricade Kit"},
    {"remotetrapfireworkbox", "Fireworks Box"}, {"deployableremoteflaretrap", "Remote Raider Flare"},
    {"emittertrapteslacoil", "Surge Coil"}, {"crashpad", "Crash Mat"},
    {"tethergun", "Tether Launcher"}, {"metaldetector02", "Mountaineer's Detector"},
    //healing
    {"adrenalinshot", "Adrenaline Shot"}, {"healinghotsmall", "Bandage"}, {"healinghotimprovised", "Herbal Bandage"},
    {"consumableshieldovertimepack", "Shield Recharger"}, {"healinghotmedium", "Sterilized Bandage"},
    {"armorpack", "Surge Shield Recharger"}, {"healthpack01", "Vita Shot"}, {"healthspray", "Vita Spray"},
    {"integratedshieldrepair", "Integrated Shield Recharger"},
    //throwables
    {"throwablevolcanicrock", "Volcanic Rock"}, {"throwabletethergrenade", "Yank Grenade"},
    //weapon mods (the level is part of the name)
    {"weaponmodunderbarrelangledgrip01", "Angled Grip I"}, {"weaponmodunderbarrelangledgrip02", "Angled Grip II"},
    {"weaponmodunderbarrelangledgrip03", "Angled Grip III"}, {"weaponmodunderbarrelhorizontalgrip01", "Horizontal Grip"},
    {"weaponmodunderbarrelverticalgrip01", "Vertical Grip I"}, {"weaponmodunderbarrelverticalgrip02", "Vertical Grip II"},
    {"weaponmodunderbarrelverticalgrip03", "Vertical Grip III"}, {"weaponmodmuzzlecompensator01", "Compensator I"},
    {"weaponmodmuzzlecompensator02", "Compensator II"}, {"weaponmodmuzzlecompensator03", "Compensator III"},
    {"weaponmodmuzzleextendedbarrel01", "Extended Barrel I"}, {"weaponmodmuzzleextendedbarrel02", "Extended Barrel II"},
    {"weaponmodmuzzleextendedbarrel03", "Extended Barrel III"}, {"weaponmodmuzzlebrake01", "Muzzle Brake I"},
    {"weaponmodmuzzlebrake02", "Muzzle Brake II"}, {"weaponmodmuzzlebrake03", "Muzzle Brake III"},
    {"weaponmodmuzzlesilencer01", "Silencer I"}, {"weaponmodmuzzlesilencer02", "Silencer II"},
    {"weaponmodmuzzlesilencer03", "Silencer III"}, {"weaponmodshotgunmuzzlechoke01", "Shotgun Choke I"},
    {"weaponmodshotgunmuzzlechoke02", "Shotgun Choke II"}, {"weaponmodshotgunmuzzlechoke03", "Shotgun Choke III"},
    {"weaponmodmagazinelightextended01", "Extended Light Mag I"},
    {"weaponmodmagazinelightextended02", "Extended Light Mag II"},
    {"weaponmodmagazinelightextended03", "Extended Light Mag III"},
    {"weaponmodmagazinemediumextended01", "Extended Medium Mag I"},
    {"weaponmodmagazinemediumextended02", "Extended Medium Mag II"},
    {"weaponmodmagazinemediumextended03", "Extended Medium Mag III"},
    {"weaponmodmagazineshotgunextended01", "Extended Shotgun Mag I"},
    {"weaponmodmagazineshotgunextended02", "Extended Shotgun Mag II"},
    {"weaponmodmagazineshotgunextended03", "Extended Shotgun Mag III"},
    {"weaponmodstockactivestabilizer01", "Kinetic Converter"}, {"weaponmodstocklightweight01", "Lightweight Stock"},
    {"weaponmodstockpadded01", "Padded Stock"}, {"weaponmodstockstable01", "Stable Stock I"},
    {"weaponmodstockstable02", "Stable Stock II"}, {"weaponmodstockstable03", "Stable Stock III"},
    {"weaponmodshotgunmuzzlesilencer01", "Shotgun Silencer"}, {"weaponmodtechprojectilesplitter", "Anvil Splitter"},
    //ammo
    {"energyammo", "Energy Clip"},
    //grenades and throwables
    {"hegrenade", "Heavy Fuze Grenade"}, {"dotgrenade", "Blaze Grenade"}, {"throwablestaminadotgrenade", "Gas Grenade"},
    {"stungrenade", "Showstopper"}, {"basicstickygrenade", "Snap Blast Grenade"},
    {"homingmissilegrenade", "Seeker Grenade"}, {"throwableshrapnelgrenade", "Shrapnel Grenade"},
    {"stickyremotegrenade", "Trigger 'Nade"}, {"throwabletrailblazer", "Trailblazer"},
    {"scattermissilegrenade", "Wolfpack"}, {"throwabledecoy", "Lure Grenade"},
    {"throwablesmokegrenadesmall", "Li'l Smoke Grenade"}, {"throwabletagginggrenade", "Tagging Grenade"},
    {"throwableimpactgrenadesmall", "Light Impact Grenade"}, {"throwableglowstickblue", "Blue Light Stick"},
    {"throwableglowstickgreen", "Green Light Stick"}, {"throwableglowstickred", "Red Light Stick"},
    {"throwableglowstickyellow", "Yellow Light Stick"}, {"firecrackers", "Firecracker"},
    //traps, mines and deployables
    {"lasertrapdotgrenade", "Blaze Grenade Trap"}, {"lasertrapstaminadot", "Gas Grenade Trap"},
    {"lasertrapthrowabledecoy", "Lure Grenade Trap"}, {"lasertrapsmokegrenade", "Smoke Grenade Trap"},
    {"deployablejumpmineexplosive", "Explosive Mine"}, {"jumpminestaminadot", "Gas Mine"},
    {"jumpmineimpulse", "Pulse Mine"}, {"timedmineexplosive", "Deadline"},
    //blueprints
    {"recipeadvancedcamerarecipe", "Advanced Camera Blueprint"},
    {"reciperecipeangledgrip02", "Angled Grip II Blueprint"}, {"reciperecipeangledgrip03", "Angled Grip III Blueprint"},
    {"reciperecipeanvil", "Anvil Blueprint"}, {"recipechargeriflerecipe", "Aphelion Blueprint"},
    {"reciperecipeassaultrifleburst", "Arpeggio Blueprint"}, {"recipebanjo", "Banjo Blueprint"},
    {"recipesnubnose01recipe", "Bantam Blueprint"}, {"reciperecipebarricadekit", "Barricade Kit Blueprint"},
    {"recipeassaultrifleheavyrecipe", "Bettina Blueprint"}, {"reciperecipeblazegrenade", "Blaze Grenade Blueprint"},
    {"recipeblueglowstickrecipe", "Blue Light Stick Blueprint"}, {"reciperecipebobcat", "Bobcat Blueprint"},
    {"reciperecipeburletta", "Burletta Blueprint"}, {"recipecantorecipe", "Canto Blueprint"},
    {"recipecombatagumentt3aggressiverecipe", "Combat Mk. 3 (Aggressive) Blueprint"},
    {"recipecombatagumentiiiflankingrecipe", "Combat Mk. 3 (Flanking) Blueprint"},
    {"reciperecipecompensator02", "Compensator II Blueprint"}, {"reciperecipecompensator03", "Compensator III Blueprint"},
    {"recipecomplexgunpartsrecipe", "Complex Gun Parts Blueprint"}, {"recipecrashpadrecipe", "Crash Mat Blueprint"},
    {"recipetimedminerecipe", "Deadline Blueprint"}, {"reciperecipedefib", "Defibrillator Blueprint"},
    {"recipedolabrarecipe", "Dolabra Blueprint"}, {"recipecollectorkey", "Emperor Gateway Conduit Blueprint"},
    {"reciperecipeequlizer", "Equalizer Blueprint"},
    {"reciperecipejumpmine", "Explosive Mine Blueprint"}, {"recipeextendedbarrel02recipe", "Extended Barrel II Blueprint"},
    {"reciperecipeextendedlightmag02", "Extended Light Mag II Blueprint"},
    {"reciperecipeextendedlightmag03", "Extended Light Mag III Blueprint"},
    {"reciperecipeextendedmediummag02", "Extended Medium Mag II Blueprint"},
    {"reciperecipeextendedmediummag03", "Extended Medium Mag III Blueprint"},
    {"reciperecipeextendedshotgunmag02", "Extended Shotgun Mag II Blueprint"},
    {"reciperecipeextendedshotgunmag03", "Extended Shotgun Mag III Blueprint"},
    {"recipefireworksboxblueprint", "Fireworks Box Blueprint"}, {"recipegasminerecipe", "Gas Mine Blueprint"},
    {"recipegrapplinghookrecipe", "Grappling Hook Blueprint"},
    {"recipegreenglowstickrecipe", "Green Light Stick Blueprint"},
    {"reciperecipeheavygunparts", "Heavy Gun Parts Blueprint"}, {"reciperecipeantiarclauncher", "Hullcracker Blueprint"},
    {"reciperecipeiltoro", "Il Toro Blueprint"}, {"reciperecipejoltmine", "Jolt Mine Blueprint"},
    {"reciperecipejupiter", "Jupiter Blueprint"}, {"reciperecipelightgunparts", "Light Gun Parts Blueprint"},
    {"reciperecipelightweightstock", "Lightweight Stock Blueprint"},
    {"recipelootingaugmentt3custodianrecipe", "Looting Mk. 3 (Safekeeper) Blueprint"},
    {"recipelootingagumentiiidbnorecipe", "Looting Mk. 3 (Survivor) Blueprint"},
    {"reciperecipeluregrenade", "Lure Grenade Blueprint"}, {"reciperecipemediumgunparts", "Medium Gun Parts Blueprint"},
    {"reciperecipemuzzlebrake02", "Muzzle Brake II Blueprint"}, {"reciperecipemuzzlebrake03", "Muzzle Brake III Blueprint"},
    {"reciperecipeboltactionmedium", "Osprey Blueprint"}, {"reciperecipepaddedstock", "Padded Stock Blueprint"},
    {"recipepowereddescenderrecipe", "Powered Descender Blueprint"}, {"recipeimpulseminerecipe", "Pulse Mine Blueprint"},
    {"recipelauncherantiarcsingleshot01recipe", "Rascal Blueprint"},
    {"reciperecipeassaultriflelowtier", "Rattler Blueprint"}, {"reciperedglowstickrecipe", "Red Light Stick Blueprint"},
    {"reciperecipeflaretrap", "Remote Raider Flare Blueprint"}, {"recipelonewolfrecipe", "Seeker Grenade Blueprint"},
    {"reciperecipeshotgunchoke02", "Shotgun Choke II Blueprint"},
    {"reciperecipeshotgunchoke03", "Shotgun Choke III Blueprint"},
    {"reciperecipeshotgunsilencer", "Shotgun Silencer Blueprint"}, {"reciperecipeshowstopper", "Showstopper Blueprint"},
    {"reciperecipesilencer01", "Silencer I Blueprint"}, {"reciperecipesilencer02", "Silencer II Blueprint"},
    {"recipesmokegrenaderecipe", "Smoke Grenade Blueprint"}, {"reciperecipesnaphook", "Snap Hook Blueprint"},
    {"reciperecipestablestock02", "Stable Stock II Blueprint"}, {"reciperecipestablestock03", "Stable Stock III Blueprint"},
    {"recipebattleriflelight01recipe", "Stiletto Blueprint"}, {"recipesurgecoilrecipe", "Surge Coil Blueprint"},
    {"recipeutilityagumentt3defensiverecipe", "Tactical Mk. 3 (Defensive) Blueprint"},
    {"recipeutilityagumentt3healingrecipe", "Tactical Mk. 3 (Healing) Blueprint"},
    {"recipetacticalaugmentt3revivalrecipe", "Tactical Mk.3 (Revival) Blueprint"},
    {"recipeutilityaugmentt3smokerecipe", "Tactical Mk.3 (Smoke) Blueprint"},
    {"reciperecipetagginggrenade", "Tagging Grenade Blueprint"}, {"reciperecipetempest", "Tempest Blueprint"},
    {"recipetethergunrecipe", "Tether Launcher Blueprint"},
    {"reciperecipemediumlmg", "Torrente Blueprint"}, {"recipetrailblazerrecipe", "Trailblazer Blueprint"},
    {"reciperecipetriggernade", "Trigger Nade Blueprint"}, {"reciperecipehipowerpistol", "Venator Blueprint"},
    {"reciperecipeverticalgrip02", "Vertical Grip II Blueprint"},
    {"reciperecipeverticalgrip03", "Vertical Grip III Blueprint"}, {"reciperecipevitashot", "Vita Shot Blueprint"},
    {"reciperecipevitaspray", "Vita Spray Blueprint"}, {"reciperecipevulcano", "Vulcano Blueprint"},
    {"recipewhiteflagrecipe", "White Flag Blueprint"}, {"reciperecipewolfpack", "Wolfpack Blueprint"},
    {"recipeyellowglowstickrecipe", "Yellow Light Stick Blueprint"}, {"recipetethergrenaderecipe", "Yank Grenade Blueprint"},
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

//The gun table's entry for a letters/digits item key: the longest gun key inside it (nullptr: none)
static const Rule* gunRule(const std::string& t) {
    const Rule* best = nullptr;
    size_t bestLen = 0;
    for (const Rule& r : ITEM_NAMES) {
        size_t len = std::char_traits<char>::length(r.key);
        if (len > bestLen && t.find(r.key) != std::string::npos) { best = &r; bestLen = len; }
    }
    return best;
}

bool isGun(const std::string& key) {
    const Rule* g = gunRule(norm(itemToken(key)));
    return g && defaultName(Cat::Item, key) == g->name; //not a blueprint or another item that only contains a gun key
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
            std::string t = norm(itemToken(key));
            for (const Rule& r : KEY_NAMES) if (t == r.key) return r.name;
            if (t.starts_with("recipe")) break; //a blueprint not in the table keeps its cleaned name, never the gun's
            if (const Rule* g = gunRule(t)) return g->name;
            const std::string w = wikiItem(t);
            if (!w.empty()) return w;
            const std::string w2 = wikiItem(norm(cleanName(itemToken(key)))); //without the _01 variant suffix
            if (!w2.empty()) return w2;
            //"Salvage_Trinket_BurntoutCandles": again without the group words in front
            std::string s = itemToken(key);
            for (const char* p : {"Salvage_", "Consumable_", "Gadget_Instrument_"})
                if (startsWith(s, p)) { s.erase(0, std::char_traits<char>::length(p)); break; }
            for (const char* p : {"Trinket_", "Mat_", "Quest_"})
                if (startsWith(s, p)) { s.erase(0, std::char_traits<char>::length(p)); break; }
            if (const std::string w3 = wikiItem(norm(s)); !w3.empty()) return w3;
            if (const std::string w4 = wikiItem(norm(cleanName(s))); !w4.empty()) return w4;
            break;
        }
        case Cat::Player: return key;
    }
    return cleanName(itemToken(key));
}

std::string display(Cat c, const std::string& key, const std::string& def) {
    std::lock_guard<std::mutex> lk(g_mu);
    std::string base = def.empty() ? defaultName(c, key) : def;
    auto it = g_override[(int)c].find(key);
    if (it != g_override[(int)c].end()) return it->second;
    if (!g_shown.empty())
        for (const std::string& k : {norm(base), norm(key)}) //the name on screen first, then the raw name
            if (auto s = g_shown.find(k); s != g_shown.end()) return s->second;
    return base;
}

void setDir(const std::string& dir) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_dir = dir;
}

int load() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::ifstream f(configDir() + "/labels.txt");
    if (!f) return 0;
    std::string line;
    //spaces around a field are dropped, so "item <TAB> Key <TAB> Name " still works
    auto trim = [](std::string s) {
        const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    int lineNo = 0, ignored = 0;
    while (std::getline(f, line)) {
        lineNo++;
        if (!line.empty() && line.back() == '\r') line.pop_back(); //file edited on another system
        if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3); //UTF-8 byte-order mark (some Windows editors add one)
        const std::string all = trim(line);
        if (all.empty() || all[0] == '#') continue; //blank line or comment
        size_t a = line.find('\t');
        size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
        bool used = false, tabLine = false;
        if (b != std::string::npos) { //"category<TAB>raw key<TAB>name"
            std::string cat = lower(trim(line.substr(0, a))), key = trim(line.substr(a + 1, b - a - 1)), name = trim(line.substr(b + 1));
            for (int i = 0; i < CAT_COUNT; i++)
                if (cat == catName((Cat)i)) { tabLine = true; if (!key.empty() && !name.empty()) { g_override[i][key] = name; used = true; } }
        }
        //"Trash Can = Bin": the name you see on screen, then your own (any case, spaces or TABs around '=')
        if (const size_t eq = line.find('='); !tabLine && eq != std::string::npos) {
            const std::string shown = norm(line.substr(0, eq)), name = trim(line.substr(eq + 1));
            if (!shown.empty() && !name.empty()) { g_shown[shown] = name; used = true; }
        }
        if (!used && ++ignored <= 5) //a few are enough to find them; the toast gives the total
            std::printf("[!] labels.txt line %d ignored: \"%s\" (expected: the name on screen = your name, or arc, container, item or player, a TAB, the internal name, a TAB, your name)\n",
                        lineNo, line.c_str());
    }
    return ignored;
}

} // namespace labels
