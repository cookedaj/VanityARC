#include "GameState.h"
#include "utils/labeler.h"
#include "utils/log.h"
#include "utils/offsets.h"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

//Forward declaration for debug functions
void getViewMatrix(ptr actor);
void printVtableSummary(const std::vector<RenderEntity>& ents, const FminimalViewInfo& vm);
void printStatusLine(const std::vector<RenderEntity>& others, const RenderEntity* me, int posed = 0, int poseWanted = 0);
void runNameProbe(const std::vector<RenderEntity>& ents, const std::vector<RenderEntity>& unknown,
                  const FminimalViewInfo& vm, ptr uworld, FILE* out = stdout); //Probe.cpp
void getPosPtr(ptr actor);
void DumpMemoryAround(uintptr_t targetAddress, size_t range);
void FindDeepHealthOffset(ptr actor, float targetHealth);


GameState::GameState(uintptr_t base) {
    this->baseAddr = base;
    const char* sc = std::getenv("KWINHELPER_SELFCHECK"); //set and not "0": check the numbers in config.h once in a raid
    if (sc && *sc && std::strcmp(sc, "0") != 0) self_.stage = 1;
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
        //With probeNames on, the bigger PROBE report (every 8s, into ~/.config/kwin-helper/probe-names.txt) replaces it.
        static auto lastStatus = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        auto now = std::chrono::steady_clock::now();
        if (now - lastStatus >= std::chrono::seconds(probeNames ? 8 : 2)) {
            lastStatus = now;
            if (probeNames) {
                //The latest report replaces the file (written beside it, then renamed: an open editor never sees half a report)
                const char* home = std::getenv("HOME");
                const std::string path = std::string(home ? home : ".") + "/.config/kwin-helper/probe-names.txt", tmp = path + ".tmp";
                FILE* f = std::fopen(tmp.c_str(), "w");
                if (f) runNameProbe(raw.rawEnt, raw.unknown, raw.VM, uworld, f);
                if (f && std::fclose(f) == 0 && std::rename(tmp.c_str(), path.c_str()) == 0)
                    std::printf("[probe] names report (SUMMARY at the top) saved to %s\n", path.c_str());
                else runNameProbe(raw.rawEnt, raw.unknown, raw.VM, uworld); //file not writable: print it here as before
                std::fflush(stdout);
            }
            else printStatusLine(filt.filtEnt, nullptr);
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
    //v1029 4-step chain. In the main menu (no spawned PlayerController) any link can read 0: the chain stops there
    //(no more reads from a bad address) and 0 comes back, which the callers handle as "Invalid Uworld".
    //chain_ keeps every link and chainStop_ the one that read 0, so the "no game world" text and the self-check report can say where.
    ptr p = ReadMemory<ptr>(this->baseAddr + off::UWORLD_RVA);
    chain_[0] = p;
    int k = 0;
    for (ptr o : {off::UWORLD_CHAIN_1, off::UWORLD_CHAIN_2, off::UWORLD_CHAIN_3}) {
        if (!isValidPtr(p)) break;
        p = ReadMemory<ptr>(p + o);
        chain_[++k] = p;
    }
    chainStop_ = isValidPtr(p) ? -1 : k;
    return isValidPtr(p) ? p : 0;
}

ptr GameState::cameraViaController(ptr vtableCam) {
    if (vtableCam && camFieldTold_) return vtableCam;    //found, and its field already printed
    if (!vtableCam && !off::PC_CAMERA_MANAGER) return 0; //nothing to fall back on (not set in config.h)
    const ptr pc = ReadMemory<ptr>(ReadMemory<ptr>(this->baseAddr + off::UWORLD_RVA) + off::UWORLD_CHAIN_1); //see getUworld
    if (!isValidPtr(pc)) return vtableCam;
    if (!vtableCam) {
        const ptr cam = ReadMemory<ptr>(pc + off::PC_CAMERA_MANAGER);
        if (!isValidPtr(cam)) return 0;
        const FminimalViewInfo v = ReadMemory<FminimalViewInfo>(cam + off::VIEW_MATRIX);
        return viewUsable(v) && std::abs(v.FOV - 70.0) <= 60.0 ? cam : 0; //the check scan() makes on the vtable's camera
    }
    //The vtable works: find which PlayerController field holds that camera, so it can go in config.h before a patch.
    //Diagnostic only: the "PlayerController" / "config.h" / PC_CAMERA_MANAGER hint must not reach the release binary's strings(1).
#ifdef KWINHELPER_PROBES
    ptr words[0x800 / sizeof(ptr)] = {};
    ReadMemoryBuffer(pc, words, sizeof words);
    if (words[0] != 0) { //not readable now (a failed read is all zeros): try again at the next scan
        camFieldTold_ = true;
        size_t at = 0;
        for (size_t i = 1; i < sizeof words / sizeof words[0] && !at; i++) if (words[i] == vtableCam) at = i * sizeof(ptr);
        if (at) std::printf("[i] The camera is also at PlayerController+0x%zX: off::PC_CAMERA_MANAGER = 0x%zX%s\n", at, at,
                            at == off::PC_CAMERA_MANAGER ? "   (matches config.h)" : "   (put it in config.h: a stale CAMMANAGER then no longer blanks the overlay)");
        else std::printf("[i] The camera is not in the PlayerController's first 0x800 bytes: off::PC_CAMERA_MANAGER not learned\n");
        std::fflush(stdout);
    }
#endif
    return vtableCam;
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

    ptr vtCam = 0; //the camera found by its vtable
    for (int a = 0; a < actorsCount; a++) {
        ptr actor = actorList[(size_t)a];
        if (!isValidPtr(actor)) continue;

        RenderEntity ent;

        ptr vt = ReadMemory<ptr>(actor);
        bool otherClass = false; //kept only for the probe, never drawn
        if (!isDebugMode) { //allows everything through when debugging
            if (vt == vtabels::PLAYER) ent.type = Object::PLAYER;
            else if (vt == vtabels::ARC) ent.type = Object::ARC;
            else if (vt == vtabels::SEARCH) ent.type = Object::SEARCH;
            else if (vt == vtabels::CAMMANAGER) ent.type = Object::NONE;
            else if (probeNames) otherClass = true;
            else continue;
        }
        ent.actor = actor;
        ent.vt = vt;

        //Before the rootComp/pos checks so the camera never depends on its transform component
        if (vt == vtabels::CAMMANAGER) { ri.VM = ReadMemory<FminimalViewInfo>(actor + off::VIEW_MATRIX); camActor_ = vtCam = actor; }
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
            ptr healthComponent = ReadMemory<ptr>(actor + off::HEALTH_COMPONENT);
            if (isValidPtr(healthComponent)) {
                ent.playerHealth= ReadMemory<double>(healthComponent + off::HEALTH_CACHED);
                if (ent.playerHealth < 1) ent.isDead = true;
            }
            if (ent.playerHealth < 1) ent.isDead = true;

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

        //Ground items use the container vtable; the actor's name tells them apart
        if (ent.type == Object::SEARCH) labeler::refine(ent);

        ri.rawEnt.push_back(ent);
    }
    //No actor with the CAMMANAGER vtable (stale after a patch): the camera through the PlayerController, if config.h has it
    if (const ptr cam = cameraViaController(vtCam); cam && !vtCam) { ri.VM = ReadMemory<FminimalViewInfo>(cam + off::VIEW_MATRIX); camActor_ = cam; }
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
//"others" is everything except you; "me" is the pawn the caller already picked as you (nullptr: not known). Health is
//read straight from memory here, so a wrong PLAYER vtable shows up as NO HEALTH instead of a fake 100.
//"pose n/m": n of the m players near enough for a stick figure (you included) have one (left out when nobody is that near).
void printStatusLine(const std::vector<RenderEntity>& others, const RenderEntity* me, int posed, int poseWanted) {
#ifdef KWINHELPER_PROBES //diagnostic status line (team id, HP, entity census): never in the release build, never in its strings(1)
    int players = 0, playersDead = 0, arcs = 0, arcsDead = 0, pickups = 0, searches = 0;
    for (const auto& e : others) {
        switch (e.type) {
            case Object::PLAYER: players++; if (e.isDead) playersDead++; break;
            case Object::ARC:    arcs++; if (e.isDead) arcsDead++; break;
            case Object::PICKUP: pickups++; break;
            case Object::SEARCH: searches++; break;
            default: break;
        }
    }

    char meBuf[96] = "me: NOT FOUND";
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
    int len = std::snprintf(line, sizeof line, "[+] %s | other players %d (dead %d) | ARC %d (dead %d) | pickups %d | containers %d",
                            meBuf, players, playersDead, arcs, arcsDead, pickups, searches);
    if (poseWanted > 0 && len > 0 && (size_t)len < sizeof line)
        std::snprintf(line + len, sizeof line - (size_t)len, " | pose %d/%d", posed, poseWanted);
    static std::string lastLine;
    static std::chrono::steady_clock::time_point lastPrint{};
    const auto now = std::chrono::steady_clock::now();
    if (lastLine != line || now - lastPrint > std::chrono::seconds(60)) {
        lastLine = line;
        lastPrint = now;
        std::printf("%s\n", line);
        std::fflush(stdout);
    }
#else
    (void)others; (void)me; (void)posed; (void)poseWanted; //nothing printed in Release
#endif
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