# kwin-helper
https://youtu.be/tiK0ihPw6m8

## Setup
1) The program ONLY works on Linux.
2) Personally, I use Kubuntu; anything with KDE Plasma should have a similar setup, and I'm sure this can work with other Wayland displays (must be Wayland, no Xorg).
3) Steam MUST be installed under Flatpak, or else the game can see your process. Using Flatpak provides PID isolation and hides the process. To check it, run `bash check-isolation.sh` with the game open (best: in a raid), and again after a game or Steam update. It only reads and needs no sudo.
4) No KDE window rule is needed: the overlay is a Wayland layer-shell surface and stays above the game by itself (see step 11 if it cannot).
5) Download CLion from JetBrains (the non-commercial version is free).
6) Install dependencies: `sudo apt install cmake g++ pkg-config libglfw3-dev libgl-dev libwayland-dev libwayland-bin libegl-dev`
7) Download the code (which you have presumably done already) and unzip it.
8) Build:
```
Using cmake:
cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build-release

NOTE: This will likely fail due to an error about only cmake 3.2 being available. 
Ask AI/Google how to get the required version (or just change it in CMakeLists.txt, though I am unsure of the consequences).

OR

Google/ask AI how to set up the build utility inside of CLion (Release build).
```
9) Run with: `bash start.sh`. If this does not work, it is probably because your output of kwin-helper is not in the relative path `./cmake-build-release/kwin-helper`. Find where your program built to in the last step and start it with `KWINHELPER_BIN=/path/to/kwin-helper bash start.sh` (no need to edit `start.sh`). The overlay goes on your primary monitor; `bash start.sh DP-1` puts it on another (list them with `kscreen-doctor -o`).
10) Run the game in borderless fullscreen (or fullscreen) at your monitor's own resolution. No window rule and no Keep Above are needed.
11) Only if the overlay shows `OVERLAY WINDOW PROBLEM` with `No layer-shell overlay` (the console prints `[!] No layer-shell overlay (...). Using a normal window`): your desktop has no layer-shell, so the old overlay window is used. Run `bash fix-overlay-rule.sh [MONITOR]` once (the monitor the game is on, e.g. `DP-1`; default: your primary monitor), restart kwin-helper, and run the game in windowed mode.
12) Press Insert to open the in-game menu (show/hide players, ARC, items, containers, names, distances, radar). While it is open the overlay takes the mouse; press Insert again to close it. Insert is read from /dev/input, so you need to be in the `input` group once: `sudo usermod -aG input $USER`, then log out and back in. Without that, `pkill -USR1 -x kwin-helper` toggles the menu instead. Menu settings are saved in `~/.config/kwin-helper/settings.txt`.
13) Names: ARC machines, containers, ground items, players and the guns they carry get a name next to the marker (toggle each under ESP > Names). The names follow the ARC Raiders wiki. If you want your own name for something, add a line to `~/.config/kwin-helper/labels.txt` by hand: the name you see on screen, `=`, then your name. Capitals and spaces in the shown name do not matter, and a player's game name works too:
    ```
    Trash Can = Bin
    Ferro = My rifle
    ```
    Delete the line to go back to the built-in name. Lines starting with `#` are ignored. The overlay only reads this file, it never changes it. (The older form `category<TAB>internal key<TAB>your name`, for example `item<TAB>BattleRifle_BreachAction_01<TAB>My rifle`, still works and wins over a shown-name line.)

## Known Issues
1) ESP gets distorted at the edges.
2) some annoying stuff with stuttering and for like a split second you teammates are labeled as enemies.

## How to Configure
All configuration options are located in the file `config.h`. It is expected that you have a system with the environment set up to properly build this project. I suggest using CLion by JetBrains for editing and building the project (that is how I do it). Look at the top of `config.h` and edit the parameters; if you are unsure about any specific parameter, just look at the code where it's used. Rebuild afterward.

## Updating the Source and Project Outline
This section is dedicated to going through the code and explaining how it works for learning purposes. I will describe this at a high level; if you don't understand something, ask an AI (P.S. if it refuses to help, use a Chinese AI or Google AI Overview and paste this README in chunk by chunk).

### The Process Loop - main.cpp
`main.cpp` is the entry point of the process. The main responsibilities of `main` are:
1) **Initializing the process:** This includes initializing the overlay and `SignalHandler` (responsible for the overlay), as well as finding the PID and starting the `GameState` class.
2) **Looping:** Passing information from `GameState` to the ESP (walls and radar).
3) **Shutdown:** The loop catches when you try to close the window and destroys the window properly.

The most important piece for the user is the loop (though getting the PID is also important, it is not very difficult and does not change from update to update).

### Getting Info from the Game - gamestate/gamestate.cpp
This is the only file that reads memory from the game. The job of the program is to return a list of all players (and associated info), information about your own camera, and what team you are on. From this point, I will refer to the "self player" as the **local player**.

The main function of the `GameState` class is the `tick()` function, which actually performs the aforementioned responsibilities.

**UWorld**
It gets the 'UWorld' through the function `getUworld();`. While the UWorld changes every update, the function does not need to be touched because the offset for UWorld is pulled from the file `config.h` under the namespace `off`. Basically, after an update, you will get the error `[-] Invalid Uworld`, at which point you should look on UnknownCheats in the ARC Raiders reversal and offset thread for the new UWorld.

**GetRawEnt**
The next thing the `tick()` function does is call `getRawEnt(uworld)`. This function is 99% of the cheat. Like the previous one, this also uses offsets from `config.h`. Basically, this function:
1) Uses `off::PERSISTENT_LEVEL` to get the `persistentLevel` and checks if it's a valid pointer.
2) Uses `off::ACTORS_PTR` to get the base of the list of all entities in the game (and the entry count).
3) Loops through every entity. The actor pointer list contains pointers to the pawn of every entity.

For every loop, we have the pawn of the entity, whether that be a player, ARC AI, or loot.

This next part is somewhat complicated. We read the "VT" (vtable) by reading the pawn of the current entity. This points us to the "vtable" of the entity. Imagine you have a "player class"—all players have the same class, variables, and functions. When compiled, that structure is saved to the `.text` section. By looking at this structure, we can know if two entities are of the same type. We know what the player class looks like, so we can identify the entity. You will also see `isDebugMode`; this is needed because when the game updates, it breaks vtables, and they have to be rediscovered.

Next, it gets the `rootComp`, which is needed to get the player position. Then, we read the "VM" (View Matrix/camera info). As you can see, we also use vtables for this, except this vtable is not skipped in debug mode because it is essential for rediscovering vtable values. We assume the camera structure we find is our own (ARC servers don't give you other players' camera info in the actor array).

We then perform player-specific checks (except in debug mode), covering health, death status, and team. There are also bot-specific checks to determine if they are killed. Finally, we save the entity into a list and return it with the camera info.

### Drawing the Info - draw/ESP.h
The actual drawing is done by the `Overlay.h` file, which is standalone. The ESP logic is simple: when `tick()` returns the info, it is passed to the ESP function, which performs the math to transpose 3D coordinates onto a 2D overlay.

The only thing to note is the **Debug Mode**. Normally, only players, loot, and ARC AI are rendered. In debug mode, every object is rendered along with its vtable value.

## isDebugMode - Updating the Program
First, switch the flag in `config.h` to `true`. This will provide debug messages in the console.

Go to the ARC Raiders thread on UnknownCheats and check the last page for a "dump" of offsets. Inside `config.h`, I provide info on how to find these in a dump, but you are often better off looking for people who have already published a list.

This cheat uses no decryption. I have used creative solutions like vtable identification and finding the `ViewMatrix` from the actor array.

To update the `ViewMatrix`: in `getRawEnt`, uncomment the line `// getViewMatrix(actor);`. Ensure your FOV is at 70.0 and you are not scoped in. It will dump memory; look for a pattern of three numbers (~20,000), two spaces, another three numbers, two spaces, and then your FOV number. Look at the hex code to the left (e.g., `0xC88`); that is your new `off::VIEW_MATRIX`.

You can also use a tool like `memsed`. If you find your exact health value, you can scan for it using `void FindDeepHealthOffset(ptr actor, float targetHealth)` if the pointer chain is not posted on UC.

Once you find your `ViewMatrix`, you will see a BUNCH of text on the screen (hex codes like `0x14c767570`). These are the vtables. If you look at yourself, you will see the vtable for the player; a loot chest will have a different number. Once you put these hex codes into the `vtables` namespace in `config.h`, your ESP should start working.

Players, ARC and containers usually work without this hunt: while object names can be read, the normal build learns vtables that `config.h` does not know from their actors' names (a `DA_EnemyType_` asset means ARC, a `CharacterMesh` with a PlayerState means a player, a container or pickup class name means loot) a few seconds into each raid. It prints a `[+] Learned vtable 0x... = PLAYER` line for each one; copy those values into `config.h` so they are known from the first second. The camera (`CAMMANAGER`) still has to be found by hand.
## Tests

The tests use a fake in-memory "game", so no game is needed:

```
cmake -B cmake-build-tests -DKWINHELPER_TESTS=ON && cmake --build cmake-build-tests && ctest --test-dir cmake-build-tests
```

They cover the name decoders, the label tables, the keyboard layout, the Insert menu (typing, hover ID clashes), a fuzz run over garbage memory, the batched memory reads, the projection and label decluttering, the ESP drawing, and the tracker thread against the old read path (`gamestate_test`).

## How the overlay works now

- **Tracker thread** (`gamestate/Tracker.cpp`): reads the game on its own thread, so drawing never waits for it. A full scan of the actor list runs 10 times a second (`gamestate/Fast.cpp`), and in between only players and ARC are re-read (about 120 times a second, many reads per system call). The old one-read-per-actor path is still there as `GameState::tick()` for the debug modes and as the reference in the tests.
- **Camera**: read again at the start of every frame, so markers follow the view without lag.
- **Window** (`draw/LayerWindow.cpp`, `draw/OverlayBackend.cpp`): the overlay is a Wayland layer-shell surface, which KDE keeps above an active fullscreen window (the game in borderless fullscreen, or gamescope `-f`). A normal window with a "keep above" rule cannot do that. If the compositor has no layer-shell the old GLFW window is used instead (windowed games only). `bash start.sh` picks your primary monitor; `bash start.sh DP-1` picks another.
- **Stretched resolution / gamescope**: the game draws one size (say 1440x1080) and gamescope stretches it to the screen. The overlay reads the running gamescope's command line (`-w -h -W -H -S`) and works the 3D position out in the game's own pixels, then stretches it the same way (`utils/gamescope.cpp`, `utils/gameview.h`). Insert menu > Settings > Game view shows what it found, and lets you type the size by hand.
- **Drawing** (`draw/ESP.h`): outlined markers, corner-bracket boxes, labels that never pile up on each other, edge arrows for enemies you cannot see on screen, and a radar with a view cone.
- **Calibration** (Insert menu): if markers slide away from targets near the screen edge, switch "FOV is horizontal" first, then adjust Zoom and Shift. Saved with the other settings.
- **Stats** (Insert menu): fps, updates per second and read times, to check it is all running smoothly.

## The Insert menu, hotkeys and the player list

Insert opens the menu: black and white, tabs on the left.

| Tab | What it has |
| --- | --- |
| ESP | master switch, what to show, names, box style / colour / thickness, health and shield bars, monochrome, skeleton (range, dim players behind cover and how faint), ranges |
| Players | the player list options, teammate marks, and every player in view (click a name for the inventory, TEAM marks a teammate) |
| Radar | on/off, size, range, position presets |
| Settings | overlay and menu opacity, text size, spoken cues, update rate (30-240 Hz), game view (stretched resolution, gamescope, calibration), status card, hotkeys, stream hide, reset, quit |

- **Hotkeys** (read from /dev/input, switch off in Settings): F6 marks or unmarks the player nearest your crosshair as a teammate (only when one player is clearly meant), F7 clears all marks, F10 closes the overlay, F8 turns stream hide on or off. F7 and F10 only act on a second press within 2 seconds (a message asks for it). Insert opens and closes the menu whatever else is held (Shift to sprint, Ctrl, Alt, movement keys); it is the only key that does, and it cannot be changed. F7, F8 and F10 do nothing while Ctrl, Shift, Alt or Meta is held (F6: only Alt or Meta), so shortcuts like Shift+F10 in other programs leave the overlay alone. Marks are kept by account id in `~/.config/kwin-helper/teammates.txt`.
- **Stream hide (F8)**: while on, the overlay draws nothing, so a screen recording or stream shows none of it. F8 works even with Hotkeys off, and while it is on the menu cannot be opened (press F8 first). It is not saved: the overlay always starts visible. From a terminal: `pkill -USR2 -x kwin-helper` does the same as F8.
- **Spoken cues** (Settings > Spoken cues, on by default): the result of F6, F7, F8 and F10 is said out loud, and an enemy player first coming within the warning range (default 40 m) is announced with his side and distance ("Enemy behind, 30 meters"), at most one warning every 2 seconds. It uses `spd-say` from speech-dispatcher with your own voice settings; without it the overlay stays silent and the Settings tab says so. Enemy warnings are quiet while stream hide is on.
- **Boxes follow the pose**: a crouching player gets a shorter box. A faint skeleton or box means the game is not drawing that player right now.
- **Player list**: at the bottom of the screen (Players tab > Player list: off / always / only with the menu). With the menu open, click a name to open that player's inventory window.
- **Safe values**: every setting has a range; a damaged `settings.txt` cannot break the overlay, and "Reset all settings" restores the defaults.
- **Old data is not drawn**: if the tracker stalls for more than 250 ms the markers disappear instead of showing where things were.

## Shield and inventory: probes

The shield bar and the inventory window need numbers the public dumps do not have. They are found once with a probe and saved in `~/.config/kwin-helper/offsets.txt`. One raid is enough.

The release build has no probe code in it. Build a probe version in its own folder (it is never used for normal play):

```
cmake -B cmake-build-probe -DKWINHELPER_PROBES=ON && cmake --build cmake-build-probe
```

Then start it with the probe build and the probe variable:

```
KWINHELPER_BIN=./cmake-build-probe/kwin-helper KWINHELPER_PROBE=shield,inventory bash start.sh
```

1. Start it like that, then start a raid. Stay in third person, so the overlay can tell which player is you.
2. **Shield:** when `=== SHIELD PROBE ===` appears, move around but keep your shield full and take no damage. About 6 seconds later `=== NOW take a hit ===` appears: now take a hit (or do anything that changes your shield). Lines appear for every number that changed. Health is marked `<- known: health`. The shield is the other number that moves with it. Under each candidate there is a ready line: `if this is the shield: shield_component ... shield_value ...`.
3. **Inventory:** every 8 seconds `=== INVENTORY PROBE ===` lists your item lists and those of the two nearest players, each with a ready `offsets.txt:  inv_array ...` line. Stand near another player for a moment.
4. Copy the console lines (or just the shield candidates that moved with health, and the inventory lists that name real items) and send them over, or put the ready lines into `offsets.txt` yourself, then go back to the release build (no probe variable).

Both probes only read memory. Until the numbers are in `offsets.txt` the shield bar is not drawn and the inventory window shows the guns only.
