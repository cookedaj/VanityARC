#include "GameState.h"
#include "utils/labeler.h"
#include "utils/log.h"
#include "utils/offsets.h"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

//Forward declaration for debug functions
void getViewMatrix(ptr actor);
void printVtableSummary(const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm);
void printStatusLine(const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm);
void runNameProbe(const std::vector<RenderEntity>& ents, const std::vector<RenderEntity>& unknown,
                  const FminimalViewInfo& vm, ptr uworld, FILE* out = stdout); //Probe.cpp
void getPosPtr(ptr actor);
void DumpMemoryAround(uintptr_t targetAddress, size_t range);
void FindDeepHealthOffset(ptr actor, float targetHealth);


GameState::GameState(uintptr_t base) {
    this->baseAddr = base;
}

gsRet GameState::tick() {
    ptr uworld = getUworld();
    if (!isValidPtr(uworld)) {
        logThrottled("[-] Invalid Uworld (not in a raid yet?)");
        return{};
    }

    DBG {std::cout << "Uworld: 0x" << std::hex << uworld << std::dec << std::endl;}

    RawInfo raw = getRawEnt(uworld);
    if (raw.rawEnt.empty() || !viewUsable(raw.VM)) {
        logThrottled("[-] Invalid rawEntities or no viewMatrix");
        return{};
    }

    DBG {std::cout << "Raw Ent Size: " << raw.rawEnt.size() << std::endl;
        raw.VM.Print();}

    if (isDebugMode && !scanForCamera) {
        static auto lastSummary = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        auto now = std::chrono::steady_clock::now();
        if (now - lastSummary >= std::chrono::seconds(2)) {
            lastSummary = now;
            printVtableSummary(raw.rawEnt, raw.VM);
        }
    }

    FilterRet filt;
    filt.filtEnt = raw.rawEnt; //debug mode shows everything unfiltered
    filt.localPlayerTeam = this->lastTeamID;
    if (!isDebugMode) { //skip on debug
        filt = filterEnt(raw.rawEnt, raw.VM);
        if (filt.filtEnt.empty()) {
            logThrottled("[-] No ent found, may be in practice range or its just not working");
        }
        if (filt.localPlayerTeam != -1) this->lastTeamID = filt.localPlayerTeam;

        DBG {std::cout << "Filt Ent Size: " << filt.filtEnt.size() << std::endl;}

        //One short line every 2s so a run can be checked at a glance (see printStatusLine).
        //With probeNames on, the bigger PROBE report (every 8s) replaces it.
        static auto lastStatus = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        auto now = std::chrono::steady_clock::now();
        if (now - lastStatus >= std::chrono::seconds(probeNames ? 8 : 2)) {
            lastStatus = now;
            if (probeNames) runNameProbe(raw.rawEnt, raw.unknown, raw.VM, uworld);
            else printStatusLine(raw.rawEnt, raw.VM);
        }
    }

    if (!isDebugMode) {
        for (RenderEntity& e : filt.filtEnt) labeler::fill(e); //names shown next to the markers
        labeler::endTick();
    }

    gsRet out{std::move(filt.filtEnt), raw.VM, this->lastTeamID};
    out.valid = true;
    return out;
}

uintptr_t GameState::getUworld() {
    //v1029 4-step chain. In the main menu (no spawned PlayerController) any
    //link can read 0, which tick() handles as "Invalid Uworld".
    ptr p = ReadMemory<ptr>(this->baseAddr + off::UWORLD_RVA);
    p = ReadMemory<ptr>(p + off::UWORLD_CHAIN_1);
    p = ReadMemory<ptr>(p + off::UWORLD_CHAIN_2);
    p = ReadMemory<ptr>(p + off::UWORLD_CHAIN_3);
    return p;
}


GameState::FilterRet GameState::filterEnt(const std::vector<RenderEntity>& rawEnt, const FminimalViewInfo& vm) {
    FilterRet fr{};
    fr.filtEnt.reserve(rawEnt.size());

    //You are the player closest to the camera (third person puts it about 2 m away). Only that one is you: another
    //player standing right next to the camera (a teammate being revived, an enemy in melee range) must still be drawn.
    int me = -1;
    double best = 2 * 100;
    for (size_t i = 0; i < rawEnt.size(); i++) {
        if (rawEnt[i].type != Object::PLAYER) continue;
        double d = rawEnt[i].pos.Dist(vm.Location);
        if (d < best) { best = d; me = (int)i; }
    }

    for (size_t i = 0; i < rawEnt.size(); i++) {
        if ((int)i == me) {
            fr.localPlayerTeam = rawEnt[i].teamID;
            continue;
        }
        fr.filtEnt.push_back(rawEnt[i]);
        fr.filtEnt.back().dist = rawEnt[i].pos.Dist(vm.Location) / 100;
    }
    return fr;
}

GameState::RawInfo GameState::getRawEnt(ptr uworld) {
    RawInfo ri{};
    //PersistentLevel at UWorld+0x170 is encrypted this patch; read the plain
    //pointer from LevelCollections[0] instead.
    ptr levelCollection = ReadMemory<ptr>(uworld + off::LEVEL_COLLECTIONS);
    if (!isValidPtr(levelCollection)) {
        logThrottled("[-] No level collection yet");
        return {};
    }
    ptr persistentLevel = ReadMemory<ptr>(levelCollection + off::PERSISTENT_LEVEL);
    DBG{std::cout << "PersistentLvl: 0x" << std::hex <<  persistentLevel << std::dec << std::endl;}
    if (!isValidPtr(persistentLevel)) {
        logThrottled("[-] No persistent level yet");
        return {};
    }

    ptr actors = ReadMemory<ptr>(persistentLevel + off::ACTORS_PTR);
    DBG{std::cout << "Actors: 0x" << std::hex << actors << std::dec << std::endl;}
    int actorsCount = ReadMemory<int>(persistentLevel + off::ACTORS_PTR + 0x8);
    DBG{std::cout << "actorsCount: " << actorsCount << std::endl;}
    if (!(isValidPtr(actors) && actorsCount > 0 && actorsCount < 250000)) {
        logThrottled("[-] Actor list not readable yet");
        return {};
    }

    //One read for the whole pointer list instead of one read per actor
    std::vector<ptr> actorList((size_t)actorsCount);
    ReadMemoryBuffer(actors, actorList.data(), actorList.size() * sizeof(ptr));

    for (int a = 0; a < actorsCount; a++) {
        ptr actor = actorList[(size_t)a];
        if (!isValidPtr(actor)) continue;

        RenderEntity ent;

        ptr vt = ReadMemory<ptr>(actor);
        bool otherClass = false; //kept only for the probe, never drawn
        if (!isDebugMode) { //allows everything through when debugging
            if (vt == vtabels::PLAYER) ent.type = Object::PLAYER;
            else if (vt == vtabels::ARC) ent.type = Object::ARC;
            else if (vt == vtabels::PICKUP) { if (probeNames) otherClass = true; else continue; } //v1029: this vtable is GC_StaminaDrain_C (a gameplay effect), not loot
            else if (vt == vtabels::SEARCH) ent.type = Object::SEARCH;
            else if (vt == vtabels::CAMMANAGER) ent.type = Object::NONE;
            else if (probeNames) otherClass = true;
            else continue;
        }
        ent.actor = actor;
        ent.vt = vt;

        //Before the rootComp/pos checks so the camera never depends on its transform component
        if (vt == vtabels::CAMMANAGER) { ri.VM = ReadMemory<FminimalViewInfo>(actor + off::VIEW_MATRIX); camActor_ = actor; }
        if (scanForCamera) getViewMatrix(actor);

        ptr rootComp = ReadMemory<ptr>(actor + off::ROOT_COMPONENT_PTR);
        if (!isValidPtr(rootComp)) continue;

        Vector3 pos = ReadMemory<Vector3>(rootComp + off::POS_PTR);
        if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) continue; //half-built actor
        if (std::abs(pos.x) < 100) continue;
        if (std::abs(pos.x) > 1e8 || std::abs(pos.y) > 1e8 || std::abs(pos.z) > 1e8) continue; //garbage, not a world position
        ent.pos = pos;
        if (otherClass) {
            ri.unknown.push_back(ent); //all of them: the probe keeps the nearest (runNameProbe)
            continue;
        }

        //Only if player or debugging
        if (ent.type == Object::PLAYER || isDebugMode) {
            //Not adding this to offsets because they cannot typically be found in SDK dumps
            //can also use method below to find another pointer chain to memory
            ptr healthComponent = ReadMemory<ptr>(actor + off::HEALTH_COMPONENT);
            if (isValidPtr(healthComponent)) {
                ent.playerHealth= ReadMemory<double>(healthComponent + off::HEALTH_CACHED);
                if (ent.playerHealth < 1) ent.isDead = true;
            }
            // // FindDeepHealthOffset(actor, 75.1292); //find second value from memsed
            // auto dep1 = ReadMemory<ptr>(actor + 0xdc8);
            // auto dep2 = ReadMemory<ptr>(dep1 + 0x1b0);
            // float health = ReadMemory<float>(dep2 + 0xb8);
            if (ent.playerHealth < 1) ent.isDead = true;

            //Does not work
            // ptr mesh = ReadMemory<ptr>(actor + off::MESH);
            // if (isValidPtr(mesh)) {
            //     float lastSubmitTime = ReadMemory<float>(mesh + off::LAST_SUBMIT_TIME);
            //     float lastRenderTime = ReadMemory<float>(mesh + off::LAST_SUBMIT_TIME + sizeof(float));
            //     float lastRenderTimeOnScreen = ReadMemory<float>(mesh + off::LAST_SUBMIT_TIME + sizeof(float)*2);
            //     bool isOnScreen = (lastRenderTime == lastRenderTimeOnScreen);
            //     bool isRecent = (lastSubmitTime - lastRenderTime) <= 0.06f;
            //     ent.isVisible = isOnScreen && isRecent;
            // }

            ent.teamID = ReadMemory<int>(actor + off::TEAM_ID) & 0xFF; //the byte at TeamId only; the bytes after it belong to other fields

            if (live::shieldValue >= 0 && ent.type == Object::PLAYER) { //shield (utils/offsets.h)
                const ptr comp = live::shieldComponent >= 0 ? ReadMemory<ptr>(actor + (ptr)live::shieldComponent) : healthComponent;
                ent.hasShield = isValidPtr(comp);
                if (ent.hasShield) {
                    auto number = [&](int64_t o) {
                        uint64_t raw = ReadMemory<uint64_t>(comp + (ptr)o);
                        double v;
                        if (live::shieldIsFloat) { float f; std::memcpy(&f, &raw, sizeof f); v = f; } else std::memcpy(&v, &raw, sizeof v);
                        return (std::isfinite(v) && v >= 0 && v <= 100000.0) ? v : 0.0;
                    };
                    ent.shield = number(live::shieldValue);
                    if (live::shieldMax >= 0) ent.shieldMax = std::max(number(live::shieldMax), ent.shield);
                }
            }
        } else if (ent.type == Object::ARC) {
            ent.isDead = ReadMemory<uint8_t>(actor + off::IS_DESTROYED) == 1; //same rule as step(): only exactly 1 is destroyed
        }


        // getViewMatrix is called higher up, before the rootComp/pos checks
        // getPosPtr(actor); //can find both Root Component AND position, print out is Off: rootComp posPtr

        //Ground items use the container vtable; the actor's name tells them apart
        if (ent.type == Object::SEARCH) labeler::refine(ent);

        ri.rawEnt.push_back(ent);
    }
    return ri;
}

//------------------------------------------------------------------------
//          HELPER FUNCTION INTENDED TO HELP WITH DEBUGGING             //
//------------------------------------------------------------------------
// These functions only sometimes have to be used and only for the purpose of getting offsets when they are
// not found on the reversal and offsets thread, the only one that really needs to be used every update
// is the getViewMatrix one because I have not seen anybody else use this type of method to get the ViewMatrix


void DumpMemoryAround(uintptr_t targetAddress, size_t range) {
    // Align start address to a clean 16-byte boundary
    uintptr_t startAddress = (targetAddress - range) & ~0xF;
    uintptr_t endAddress = (targetAddress + range + 0xF) & ~0xF;

    std::cout << std::hex << std::uppercase;
    std::cout << "--- Memory Dump Around: 0x" << targetAddress << " ---\n";

    for (uintptr_t current = startAddress; current < endAddress; current += 16) {
        // Print row memory address
        std::cout << "0x" << std::setw(16) << std::setfill('0') << current << ": ";

        std::vector<uint8_t> rowBytes(16);

        // Read 16 bytes for the current row
        for (size_t i = 0; i < 16; ++i) {
            rowBytes[i] = ReadMemory<uint8_t>(current + i);
        }

        // Hexadecimal representation
        for (size_t i = 0; i < 16; ++i) {
            // Highlight the exact target address if it falls in this row
            if (current + i == targetAddress) {
                std::cout << "[" << std::setw(2) << (int)rowBytes[i] << "]";
            } else {
                std::cout << " " << std::setw(2) << (int)rowBytes[i] << " ";
            }
        }

        std::cout << " | ";

        // ASCII representation
        for (size_t i = 0; i < 16; ++i) {
            char c = rowBytes[i];
            // Check for printable characters
            if (c >= 32 && c <= 126) {
                std::cout << c;
            } else {
                std::cout << '.';
            }
        }
        std::cout << "\n";
    }
}


void getViewMatrix(ptr actor) {
    //Finds the camera (FminimalViewInfo) inside an actor: a float that looks like
    //a FOV, with a sane Location 0x50 before it and a sane Rotation 0x28 after
    //Location (the layout kwin-helper's FminimalViewInfo expects). Works with any
    //in-game FOV; the Location/Rotation checks are what reject false hits.

    //v1029 expects FOV at actor+0xE60 (VIEW_MATRIX 0xE10 + 0x50). One read of
    //this window per actor; if nothing is ever printed, widen it.
    constexpr int SCAN_LO = 0xC00;
    constexpr int SCAN_HI = 0x1100;
    constexpr int FOV_IN_STRUCT = 0x50;
    constexpr int ROT_IN_STRUCT = 0x28;

    uint8_t buf[SCAN_HI - SCAN_LO];
    ReadMemoryBuffer(actor + SCAN_LO, buf, sizeof(buf));

    auto dbl = [&](int actorOff) {
        double v;
        std::memcpy(&v, buf + (actorOff - SCAN_LO), sizeof(v));
        return v;
    };

    for (int j = SCAN_LO + FOV_IN_STRUCT; j + (int)sizeof(float) <= SCAN_HI; j += 4) {
        float fov;
        std::memcpy(&fov, buf + (j - SCAN_LO), sizeof(fov));
        if (!(fov >= 30.0f && fov <= 130.0f)) continue; //NaN fails this too

        int vmOff = j - FOV_IN_STRUCT;
        double lx = dbl(vmOff), ly = dbl(vmOff + 8), lz = dbl(vmOff + 16);
        double pitch = dbl(vmOff + ROT_IN_STRUCT), yaw = dbl(vmOff + ROT_IN_STRUCT + 8), roll = dbl(vmOff + ROT_IN_STRUCT + 16);

        bool locOk = std::isfinite(lx) && std::isfinite(ly) && std::isfinite(lz) &&
                     std::abs(lx) < 1e7 && std::abs(ly) < 1e7 && std::abs(lz) < 1e7 &&
                     ((std::abs(lx) > 1) + (std::abs(ly) > 1) + (std::abs(lz) > 1)) >= 2;
        bool rotOk = std::isfinite(pitch) && std::isfinite(yaw) && std::isfinite(roll) &&
                     std::abs(pitch) <= 90.5 && std::abs(yaw) <= 360.0 && std::abs(roll) <= 360.0;
        if (!locOk || !rotOk) continue;

        ptr vt = ReadMemory<ptr>(actor);
        std::cout << "\n=== CAMERA FOUND: FOV " << fov << " at actor+0x" << std::hex << j << " ===\n"
                  << "    off::VIEW_MATRIX    = 0x" << vmOff
                  << (vmOff == (int)off::VIEW_MATRIX ? "   (matches config.h)" : "   !! DIFFERS from config.h - update it")
                  << "\n    vtabels::CAMMANAGER = 0x" << vt << std::dec << "\n"
                  << "    Location " << lx << " " << ly << " " << lz << "\n"
                  << "    Rotation " << pitch << " " << yaw << " " << roll << std::endl;
    }
}

void printVtableSummary(const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm) {
    struct Row { int count = 0; int withHealth = 0; double nearest = 1e18; };
    std::map<ptr, Row> rows;
    for (const auto& e : ents) {
        Row& r = rows[e.vt];
        r.count++;
        r.nearest = std::min(r.nearest, e.pos.Dist(vm.Location) / 100.0);

        //Players have a HealthComponent (actor+0xD98) with MaxHealth @+0x380 and CachedHealth @+0x708
        ptr hc = ReadMemory<ptr>(e.actor + off::HEALTH_COMPONENT);
        if (isValidPtr(hc)) {
            double maxHp = ReadMemory<double>(hc + off::HEALTH_MAX);
            double hp = ReadMemory<double>(hc + off::HEALTH_CACHED);
            if (maxHp >= 50 && maxHp <= 500 && hp >= 0 && hp <= maxHp + 1) r.withHealth++;
        }
    }

    std::vector<std::pair<ptr, Row>> sorted(rows.begin(), rows.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second.nearest < b.second.nearest; });

    std::printf("\n=== vtables, nearest first. 'health' > 0 means players (PLAYER). The ~0 m row is the camera ===\n");
    for (size_t i = 0; i < sorted.size() && i < 15; ++i) {
        std::printf("  0x%lx   nearest %6.1f m   count %4d   health %d\n",
                    (unsigned long)sorted[i].first, sorted[i].second.nearest,
                    sorted[i].second.count, sorted[i].second.withHealth);
    }
    std::fflush(stdout);
}

//One line for normal (non-debug) runs, so a pasted console can be checked without scrolling.
//"me" is the PLAYER-typed actor within 2 m of the camera (the code treats that as you). Health is read
//straight from memory here, so a wrong PLAYER vtable shows up as NO HEALTH instead of a fake 100.
void printStatusLine(const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm) {
    int players = 0, playersDead = 0, arcs = 0, arcsDead = 0, pickups = 0, searches = 0;
    const RenderEntity* me = nullptr;
    double meDist = 1e18;
    for (const auto& e : ents) {
        double d = e.pos.Dist(vm.Location);
        switch (e.type) {
            case Object::PLAYER:
                if (d < 200 && d < meDist) { me = &e; meDist = d; }
                else { players++; if (e.isDead) playersDead++; }
                break;
            case Object::ARC:    arcs++; if (e.isDead) arcsDead++; break;
            case Object::PICKUP: pickups++; break;
            case Object::SEARCH: searches++; break;
            default: break;
        }
    }

    char meBuf[96] = "me: NOT FOUND (no PLAYER within 2 m)";
    if (me) {
        ptr hc = ReadMemory<ptr>(me->actor + off::HEALTH_COMPONENT);
        double maxHp = isValidPtr(hc) ? ReadMemory<double>(hc + off::HEALTH_MAX) : 0;
        double hp = isValidPtr(hc) ? ReadMemory<double>(hc + off::HEALTH_CACHED) : 0;
        if (maxHp >= 50 && maxHp <= 500 && hp >= 0 && hp <= maxHp + 1)
            std::snprintf(meBuf, sizeof meBuf, "me: team %d hp %.0f/%.0f", me->teamID, hp, maxHp);
        else
            std::snprintf(meBuf, sizeof meBuf, "me: NO HEALTH (wrong PLAYER vtable?)");
    }

    //Only when something changed (or once a minute as a sign of life), so the console stays readable
    char line[256];
    std::snprintf(line, sizeof line, "[+] %s | other players %d (dead %d) | ARC %d (dead %d) | pickups %d | containers %d",
                  meBuf, players, playersDead, arcs, arcsDead, pickups, searches);
    static std::string lastLine;
    static std::chrono::steady_clock::time_point lastPrint{};
    const auto now = std::chrono::steady_clock::now();
    if (lastLine != line || now - lastPrint > std::chrono::seconds(60)) {
        lastLine = line;
        lastPrint = now;
        std::printf("%s\n", line);
        std::fflush(stdout);
    }
}

void getPosPtr(ptr actor) {
    //Only works when you close
    for (int i = 0; i < 0x400; i++) {
        auto rootComp = ReadMemory<ptr>(actor + i);
        for (int j = 0; j < 0x400; j++) {
            auto pos = ReadMemory<Vector3>(rootComp + j);
            if (pos.Dist({198076.59, -199831.00, 150590.18}) < 10000) {
                std::cout << "Off: " << std::hex << i << " " << j << std::dec << std::endl;
                pos.Print();
            }
        }
    }
}

void FindDeepHealthOffset(ptr actor, float targetHealth) {
    const int MAX_OFFSET = 0x1000; // 4KB scan range
    const float EPSILON = 0.0001f;

    // Layer 1: Scan for pointers on the Actor
    for (int off1 = 0; off1 < MAX_OFFSET; off1 += 8) {
        ptr layer1 = ReadMemory<ptr>(actor + off1);
        if (!isValidPtr(layer1)) continue;

        // Layer 2: Scan for pointers inside Layer 1
        for (int off2 = 0; off2 < 0x800; off2 += 8) {
            ptr layer2 = ReadMemory<ptr>(layer1 + off2);

            // CHECK A: Is the health here? (2-Layer check again just in case)
            float checkValA = ReadMemory<float>(layer1 + off2);
            if (std::abs(checkValA - targetHealth) < EPSILON) {
                std::cout << "FOUND (2-Layer): Actor + " << std::hex << off1
                          << " -> " << off2 << std::dec << std::endl;
            }

            if (!isValidPtr(layer2)) continue;

            // Layer 3: Scan for the float inside Layer 2
            for (int off3 = 0; off3 < 0x500; off3 += 4) {
                float val = ReadMemory<float>(layer2 + off3);
                if (std::abs(val - targetHealth) < EPSILON) {
                    std::cout << "FOUND (3-Layer): Actor + 0x" << std::hex << off1
                              << " -> + 0x" << off2
                              << " -> + 0x" << off3 << " = " << val << std::dec << std::endl;
                }
            }
        }
    }
}