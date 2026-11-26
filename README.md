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
    Guns show their level as a plain number, `Stitcher 4` (the game writes `Stitcher IV`), and an amplified gun as `AMP Il Toro` (the game writes `Amplified Il Toro`). This is the same under a player's name, in the inventory window and on the ground. Levels 1 to 4 come from a player's stowed gun slots and, for the gun in their hands, from the weapon itself (both probed on game build `0x6AF5C6FC`), so a player's guns show `Stitcher 1` to `Stitcher 4` in their hands and on their back alike; guns in the inventory window and on the ground show their plain name (`Stitcher`), because the game does not send their level. Amplified guns always show `AMP`.

## Known Issues
1) ESP gets distorted at the edges. Fix it in Insert menu > Settings > Calibration.
2) some annoying stuff with stuttering and for like a split second you teammates are labeled as enemies.

## How to Configure
- Change ESP, radar, text size, colours and the other everyday options in the Insert menu. They are saved to `~/.config/kwin-helper/settings.txt` as one `name value` line each (for example `showArc 1`), and that file overrides `config.h`.
- You can also edit `settings.txt` by hand, with no rebuild. Close the overlay first: it rewrites the file every time the menu closes and when it exits, so edits made while it runs are lost.
- `config.h` holds only the starting defaults (used for any setting missing from `settings.txt`, and by "Reset all settings"), plus the options that are not in the menu: the offsets, vtables, debug flags and a few others. Those still need an edit in `config.h` and a rebuild (I suggest CLion by JetBrains for editing and building); if you are unsure about a parameter, look at the code where it's used.
- To make a changed `config.h` default take effect for one setting, close the overlay and delete just that line from `settings.txt`.

## Updating the Source and Project Outline
This section is dedicated to going through the code and explaining how it works for learning purposes. I will describe this at a high level; if you don't understand something, ask an AI (P.S. if it refuses to help, use a Chinese AI or Google AI Overview and paste this README in chunk by chunk).

### The Process Loop - main.cpp
`main.cpp` is the entry point of the process. The main responsibilities of `main` are:
1) **Initializing the process:** This includes the `SignalHandler`, setting up the overlay window (`draw/OverlayBackend.cpp` and `draw/LayerWindow.cpp`) and starting the Tracker thread (`gamestate/Tracker.cpp`). The Tracker finds the game's PID and reads the game on its own thread.
2) **Looping:** Each frame draws the newest Snapshot from `tracker.latest()` with the ESP (walls and radar).
3) **Shutdown:** The loop catches when you try to close the window and destroys the window properly.

The most important piece for the user is the loop (though getting the PID is also important, it is not very difficult and does not change from update to update).

### Getting Info from the Game - gamestate/GameState.cpp and gamestate/Fast.cpp
The job of this part is to return a list of all players (and associated info), information about your own camera, and what team you are on. From this point, I will refer to the "self player" as the **local player**.

In normal play the Tracker calls `GameState::step()` in `Fast.cpp`, which runs `scan()` (the full actor list, 10 times a second), `updateDynamic()` (players and ARC in between) and `assemble()`. `tick()` and `getRawEnt` below are the old one-read-per-actor path, which runs only with `isDebugMode` or `probeNames` and serves as the reference in the tests.

**Warning:** the player, ARC and loot checks (health, dead, team, `IS_DESTROYED`) exist in both `getRawEnt` and `Fast.cpp`, so a fix to one must be copied to the other.

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
`Overlay.h` holds the drawing helpers (fonts, outlined shapes, labels), not the window. The ESP logic is simple: `DrawESP` gets the tracker's Snapshot and performs the math to transpose 3D coordinates onto a 2D overlay.

The only thing to note is the **Debug Mode**. Normally, only players, loot, and ARC AI are rendered. In debug mode, every object is rendered along with its vtable value.

## isDebugMode - Updating the Program
First, switch the flag in `config.h` to `true`. This will provide debug messages in the console.

Go to the ARC Raiders thread on UnknownCheats and check the last page for a "dump" of offsets. Inside `config.h`, I provide info on how to find these in a dump, but you are often better off looking for people who have already published a list.

This cheat uses no decryption. I have used creative solutions like vtable identification and finding the `ViewMatrix` from the actor array.

To update the `ViewMatrix`: also set `scanForCamera = true` in `config.h` and rebuild. In a raid, not scoped in, the console prints `=== CAMERA FOUND` with your new `off::VIEW_MATRIX` and `vtabels::CAMMANAGER`.

You can also use a tool like `memsed`. If you find your exact health value, you can scan for it using `void FindDeepHealthOffset(ptr actor, float targetHealth)` if the pointer chain is not posted on UC.

Once you find your `ViewMatrix`, you will see a BUNCH of text on the screen (hex codes like `0x14c767570`). These are the vtables. If you look at yourself, you will see the vtable for the player; a loot chest will have a different number. Once you put these hex codes into the `vtables` namespace in `config.h`, your ESP should start working.

Players, ARC and containers usually work without this hunt: while object names can be read, the normal build learns vtables that `config.h` does not know from their actors' names (a `DA_EnemyType_` asset means ARC, a `CharacterMesh` with a PlayerState means a player, a container or pickup class name means loot) a few seconds into each raid. It prints a `[+] Learned vtable 0x... = PLAYER` line for each one; copy those values into `config.h` so they are known from the first second. The camera (`CAMMANAGER`) still has to be found by hand.
## Tests

The tests use a fake in-memory "game", so no game is needed:

```
cmake -B cmake-build-tests -DKWINHELPER_TESTS=ON && cmake --build cmake-build-tests && ctest --test-dir cmake-build-tests
```

On a machine without the graphics packages (a container, WSL), use `-DKWINHELPER_TESTS_ONLY=ON` instead: it builds only the tests. The tests always run with `HOME` set to `cmake-build-tests/test-home`, so they never touch your real `~/.config/kwin-helper`.

They cover the name decoders, the label tables, the keyboard layout, the Insert menu (typing, hover ID clashes), a fuzz run over garbage memory, the batched memory reads, the projection and label decluttering, the ESP drawing, and the tracker thread against the old read path (`gamestate_test`).

## How the overlay works now

- **Tracker thread** (`gamestate/Tracker.cpp`): reads the game on its own thread, so drawing never waits for it. A full scan of the actor list runs 10 times a second (`gamestate/Fast.cpp`), and in between only players and ARC are re-read (about 120 times a second, many reads per system call). The old one-read-per-actor path is still there as `GameState::tick()` for the debug modes and as the reference in the tests.
- **Camera**: read again at the start of every frame, so markers follow the view without lag.
- **Window** (`draw/LayerWindow.cpp`, `draw/OverlayBackend.cpp`): the overlay is a Wayland layer-shell surface, which KDE keeps above an active fullscreen window (the game in borderless fullscreen, or gamescope `-f`). A normal window with a "keep above" rule cannot do that. If the compositor has no layer-shell the old GLFW window is used instead (windowed games only). `bash start.sh` picks your primary monitor; `bash start.sh DP-1` picks another.
- **Stretched resolution / gamescope**: the game draws one size (say 1440x1080) and gamescope stretches it to the screen. The overlay reads the running gamescope's command line (`-w -h -W -H -S`) and works the 3D position out in the game's own pixels, then stretches it the same way (`utils/gamescope.cpp`, `utils/gameview.h`). Insert menu > Settings > Game view shows what it found. With Detect automatically off, you pick the game's picture size from a list (Custom gives big -100 to +100 step buttons) and choose whether it is stretched to fill the screen.
- **Drawing** (`draw/ESP.h`): outlined markers, corner-bracket boxes, labels that never pile up on each other, edge arrows for enemies you cannot see on screen, and a radar with a view cone.
- **Calibration** (Insert menu): if markers slide away from targets near the screen edge, switch "FOV is horizontal" first, then adjust Zoom and Shift. Big step buttons under the sliders move Zoom by 0.01 or 0.05 and Shift X / Y by 1 or 10 px. "Reset calibration" asks first ("Yes, reset calibration"), then puts all five back to their defaults. Saved with the other settings.
- **Status card** (Insert menu > Settings > OTHER > Status card): a card at the top middle of the screen with fps, updates per second and read times, to check it is all running smoothly.

## The Insert menu, hotkeys and the player list

Insert opens the menu: black and white, tabs on the left. Switching tabs starts the new page at its top.

- **Menu text size** (Settings > LOOK, 15 px by default, up to 30): makes the menu, the player list and the inventory windows bigger, with their buttons, switches, spacing and scrollbars. It changes when you let go of the slider. The menu never gets bigger than the screen; its page scrolls instead, rows of buttons wrap onto a second line, and the Players tab's players in view scroll sideways when they are wider than the page. On a narrow screen the player list stops growing once its rows fill the screen's width. High visibility raises it to at least 24.
- **Text size** (Settings > LOOK, 15 px by default, up to 40): everything on the game picture. Markers (ARC diamonds, containers, items, the crosses of down players), the health and shield bars, edge arrows and the radar's lines and dots grow with it too (at 15 px they look as before). A line of text never gets wider than the screen: on a narrow screen a very long one (the close-enemy card for several enemies, the GAME UPDATED banner) stops growing once it fills the screen's width. High visibility raises it to at least 28 and never lowers a bigger size.
- **Full-size small text** (Settings > LOOK, off by default): distances, guns, the labels by markers, the radar's ring ranges and the status card are drawn in the full Text size instead of 0.8 to 0.9 times it. High visibility turns it on.

| Tab | What it has |
| --- | --- |
| ESP | High visibility button, master switch, what to show (players, ARC, edge arrows, ARC edge arrows, items, containers), names and label cards, box style (corners, full, sides, or skeleton only) / colour / thickness, monochrome, health and shield bars (side by side, health only, or one bar for both; optional HP and SH numbers), which way they face, your health (big HP/SH readout, red LOW HEALTH card under Low health from, 35 by default), skeleton (head marker filled or ring, skeleton colour same as box / white / yellow, range, dim players behind cover and how faint), ranges (players, ARC, ARC arrow range, loot), close enemy warning (big card with an arrow toward the enemy, and its Warning card range) |
| Loot | ground items on/off, a Show list (all kinds, only guns, only ammo, only meds ... or mixed) and one switch per kind (guns, ammo, meds, shields, grenades and mines, gadgets, gun mods, augments, blueprints, keys and quest items, materials, valuables, other), colour items by kind, highlight valuable items (and the price they start at), containers on/off with the same Show list and a switch per group (medical, weapons and ammo, tech, industrial, home, lockers and crates, trash, ARC, nature, caches, other) |
| Players | the player list options, show item values (off by default; a price on every inventory row, most valuable first, and a total), teammate marks, and every player in view, enemies first, with distance and side and LOW for a low health (click a name for the inventory, TEAM marks a teammate) |
| Radar | on/off, size, range, position presets |
| Settings | High visibility button, overlay and menu opacity, text size (10-40 px), full-size small text, menu text size (15-30 px), spoken cues (Spoken warning range, Speech speed), update rate (30-240 Hz), game view (stretched resolution, gamescope, calibration), status card, hotkeys, keyboard controls the menu, stream hide, overlay window (which monitor, setup problem), reset all settings / reset to high visibility, quit |

- **Hotkeys** (read from /dev/input, switch off in Settings): F6 marks or unmarks the player nearest your crosshair as a teammate (only when one player is clearly meant; a crouching player counts by his box as the ESP draws it, so aiming just over his head picks the player behind him), F7 clears all marks, F9 says your status, F10 closes the overlay, F8 turns stream hide on or off. F7 and F10 only act on a second press within 2 seconds (a message asks for it). Up to three messages show at the top of the screen at once, one under the other in the order they came, each for its own time, so a new message never wipes the one before it. Insert opens and closes the menu whatever else is held (Shift to sprint, Ctrl, Alt, movement keys); it is the only key that opens it, and it cannot be changed. With "Keyboard controls the menu" on, Esc also closes it. F7, F8, F9 and F10 do nothing while Ctrl, Shift, Alt or Meta is held (F6: only Alt or Meta), so shortcuts like Shift+F10 in other programs leave the overlay alone. Marks are kept by account id in `~/.config/kwin-helper/teammates.txt`.
- **Say status (F9)**: one press says your health and shield, how many enemies are within the Spoken warning range and where the nearest one is, for example "Health 80, shield 40. 2 enemies within 40 meters. Nearest left, 12 meters, above.", and shows the same at the top of the screen. It says nothing while stream hide is on.
- **Stream hide (F8)**: while on, the overlay draws nothing, so a screen recording or stream shows none of it. Its see-through layer is also taken off the screen, so nothing sits over the game and the desktop can show the game directly (smoother, a little less lag); F8 puts it back. F8 works even with Hotkeys off, and while it is on the menu cannot be opened (press F8 first). It is not saved: the overlay always starts visible. From a terminal: `pkill -USR2 -x kwin-helper` does the same as F8.
- **Spoken cues** (Settings > Spoken cues, on by default): the result of F6, F7, F8, F9 and F10 is said out loud, and an enemy player first coming within the Spoken warning range (Settings > Spoken cues, default 40 m; separate from the ESP tab's Warning card range for the on-screen card) is announced with his side and distance ("Enemy behind, 30 meters"). When he then comes within the Warning card range he is announced once more ("Enemy close, left, 8 meters", or "3 enemies close, nearest left, 8 meters" when several are that close); he is said again only after going 5 m past a range. Someone more than 3 m higher or lower gets "above" or "below". At most one warning every 2 seconds. It also says "Low health" once when your health drops under Low health from (again only after it was 5 above it), "Shield broken" when your shield runs out, "Game data lost" when live data stops (also when a raid ends) and "Game data back" once it has been live for 2 seconds again. These warnings go before anything else waiting to be said and are never cut off, not even by the next enemy warning; a warning does cut off a hotkey result that is being said. It uses `spd-say` from speech-dispatcher with your own voice settings; Speech speed (-50 to +50) makes it slower or faster, and 0 keeps your own speed. Without spd-say the overlay stays silent and the Settings tab says so. All of these are quiet while stream hide is on.
- **Close enemy card** (ESP tab > Close enemy warning): "ENEMY 9 m BEHIND" in big text under the crosshair (kept on the screen also with Shift X set) for the nearest enemy within the Warning card range, with a big white arrow pointing the way he is (up: ahead, down: behind) and ", ABOVE" or ", BELOW" when he is more than 3 m higher or lower. It keeps off that enemy's box and labels; once it has moved below or above him it stays on that side while it still fits, so it does not jump back and forth as he moves. When the screen has no room for LOW HEALTH under the card, LOW HEALTH goes above the card and above the crosshair, so the crosshair stays free, and still above the player list (at the screen's top when the list reaches almost that high). On a short, narrow screen with a big Text size (for example 1280x800 at Text size 40), LOW HEALTH moves to the right so it sits beside your HP/SH readout instead of over it. The card keeps off your HP/SH readout, so your shield number stays readable: on a short screen with a big Text size (for example 1440x900 at Text size 36) it moves right, beside the readout, or just under the readout when the screen is too narrow for both side by side (LOW HEALTH then goes above the crosshair when there is no room for it under the card, except where it would lie there over the nearest enemy or the messages at the top: then the card stays under the crosshair over part of the readout, with LOW HEALTH under it). Only where a short, narrow screen and a long player list leave room for none of these (for example 1280x800 at Text size 40 with 5 or more players listed) does the card cover part of the readout: it then moves right as far as the screen allows, and never goes over the crosshair or the player list instead. The card is drawn over everything else, the player list and the menu included, so a tall player list (a big Menu text size on a short screen) never hides it.
- **Edge arrows**: an enemy within the Warning card range gets a bigger arrow. An arrow that would land under the player list, your HP/SH readout, LOW HEALTH or the close enemy card moves just above it, with its distance (and above the next one too when that puts it under another of them); when there is no room above it (LOW HEALTH at the screen's top over a tall player list), it moves beside it instead, so it never leaves the screen. The distance next to an arrow is in the full Text size. Arrows stay inside the game's picture also with Shift X / Shift Y set. Live ARC get kite-shaped arrows within ARC arrow range (ESP tab > Distance, 50 m by default).
- **Radar**: three rings, each labelled with its range ("67m", "133m", "200m"; the inner two only when they fit), solid lines that grow thicker with Text size, and a white title. A dot more than 3 m above or below you gets a small caret over or under it. It follows the same switches as the main view: squad mates only with Teammates on, no turrets with Turrets off, and nobody with Hide players with unknown team while your team is not known.
- **Teammates**: a squad mate's name starts with TEAM and a player you marked (F6 or the player list) with MARKED, in the teammate colour (white in monochrome, where the word tells them apart). The word shows even with Player names off, and also by the cross of a down player, so a down squad mate is not mistaken for a down enemy.
- **Labels**: names, distances and guns stay inside the screen's width (near the edge they slide in instead of being cut off; a line wider than the screen at your Text size is drawn just small enough to fit, all of it on the screen), and a marker's label never moves to a place off the screen. Cards (Label cards, the status card, the messages at the top, your health readout) are nearly solid with a light border, and every label keeps room for its whole card or outline, so neighbouring cards never overlap.
- **Down players and destroyed ARC**: solid, never faint, in a light greyish red and a light greyish orange (mid grey in monochrome), with DOWN or DEAD on their label; their cross and diamond shapes stay as they were.
- **Warnings stay solid**: Overlay opacity fades the markers, but never the close enemy card, your health readout, LOW HEALTH, NO LIVE GAME DATA or the card about Insert and the F-keys not working.
- **Boxes follow the pose**: a crouching player gets a shorter box, a player lying down a wider one, and the box always reaches round the head marker. A faint skeleton or box means the game is not drawing that player right now (Dim players behind cover).
- **Skeleton**: its lines are never thinner than the box's, grow thicker as a player comes closer (up to 12 px), and have round joints, a dot at each wrist and a heavier dark edge. The head is a filled disc with a dark edge (or a ring: ESP tab, Head marker), never smaller than the lines allow. Far away (about 40 m and more on a 1440p screen) the figure changes smoothly into a few thick lines: spine, arms, legs and head. Limbs right next to you are cut at the screen's edge instead of disappearing. A dashed ring for a head means the game is not animating that player right now, so the pose is an estimate of his stance; one bad bone leaves out only that arm or leg. With a skeleton, the arrow for which way a player faces starts at his head.
- **Player list**: at the bottom of the screen (Players tab > Player list: off / always / only with the menu). Enemies come first, nearest first, then teammates, so a full list leaves out teammates before enemies. WHERE shows how far a player is and on which side of where you look (`12 m left`, `40 m behind`), and a health under Low health from (ESP tab > YOUR HEALTH) is followed by `LOW`. With the menu open, click a name to open that player's inventory window. While the menu is closed, your HP/SH readout, LOW HEALTH, the close enemy card and the messages and cards at the top of the screen (the F6, F7 and F9 messages, the status card, the card about Insert and the F-keys) are drawn over the list, so a tall list never hides them. When such a list pushes LOW HEALTH up to the screen's top, the messages and cards there go under LOW HEALTH, so its red card never covers one; LOW HEALTH is also drawn over the readout when a narrow screen and a big Text size leave no room for it beside the readout.
- **Item values** (Players tab > Show item values, off by default): sell prices and a total in the inventory windows. Each row shows what it is worth (its price times how many) at the window's right edge, the most valuable rows come first within each list, and numbers have thousands separators (`145,000`). Prices come from `~/.config/kwin-helper/values.txt`, one item per line, for example `Snap Hook | 14000`; the price may also be written `14,000`, `14 000`, `14k`, `1.5k` or `2m`. An item is found by the game's own name, its internal name or your own name for it from `labels.txt`, the same way in the inventory windows and for Highlight valuable items, so renaming an item never loses its price. A name may be written in any script (`Бинт | 500`); a line whose name has no letters or digits at all is ignored. A saved change shows within 2 seconds. While this switch and Highlight valuable items (Loot tab) are both off the file is not read and the windows show no prices.
- **Loot tab**: the Show list picks all kinds, only one kind (only guns, only ammo, only meds and so on) or says Mixed when you set the switches below it yourself; it always shows what the switches are set to. The kind comes from the item's internal name, so your own names in `labels.txt` never change it; an item whose name is not read yet counts as Other. Containers have the same list with groups (medical, weapons and ammo, tech and so on). These only change what is drawn; everything is still read. Colour items by kind (off by default) draws guns magenta, ammo white, meds green and shields blue, the rest cyan, with the same triangle and size (monochrome keeps them grey). Highlight valuable items (off by default) draws ground items whose price in `values.txt` is at least Valuable from (10000 by default) in gold and half as big again, always with their name and a short price such as `14k`, anywhere within the Loot range.
- **Safe values**: every setting has a range; a damaged `settings.txt` cannot break the overlay, and "Reset all settings" restores the defaults.
- **Old data is not drawn**: if the tracker stalls for more than 250 ms the markers disappear instead of showing where things were.

## Self-check: are the numbers in config.h still right?

After a game update, this tells you which game numbers (offsets) still work, one line each. It is in the normal release build; nothing needs rebuilding.

```
KWINHELPER_SELFCHECK=1 bash start.sh
```

Start a raid and wait a few seconds after you spawn. The console then prints one report (once each time the game is started; if it ran in a menu or lobby, under 300 actors, or before you were in your character, it runs again by itself in the raid), also saved to `~/.config/kwin-helper/selfcheck.txt`. Every line starts with:

- **PASS**: the number looks right.
- **FAIL**: the number is wrong for this game version; the line says what was read. Fix the first FAIL first: the ones after it often follow from it.
- **SKIP**: it could not be checked (the line says why, for example no ARC nearby).

It checks the UWorld chain (`UWORLD_RVA` and its 3 offsets), `LEVEL_COLLECTIONS`, `PERSISTENT_LEVEL`, the actor count (`ACTORS_PTR`), `CONTROLLER`, the `PLAYER` vtable, your position (`ROOT_COMPONENT_PTR`, `POS_PTR`), your health (`HEALTH_COMPONENT`, `HEALTH_MAX`, `HEALTH_CACHED`), `TEAM_ID`, `MESH`, the bone count (`BONE_ARRAY`), `RENDER_TIME` and the ARC `IS_DESTROYED` flag. It finds your own player without any vtable (through the PlayerController of the UWorld chain), so when the `PLAYER` line fails it prints the right vtable for `config.h`. It only reads memory. If no full report appears at all, the overlay never found a game world. Until then `selfcheck.txt` shows where the UWorld chain stops (`UWORLD_RVA` to `UWORLD_CHAIN_3`, each PASS or FAIL), and the message on screen names the same link. A stop in the main menu is normal; still stopping in a raid means those offsets are stale for the game's build.

## Shield and inventory: probes

The shield bar and the inventory window need numbers the public dumps do not have. Both are **built in** (`utils/offsets.h`, probed on game build `0x6AF5C6FC`): the shield bar, and in the inventory window what a player has in their hands besides their guns. Other players' backpacks are not sent to your game, and other players' shields are not yet confirmed to be. You only need the probes again after a game update, or to try something new; lines in `~/.config/kwin-helper/offsets.txt` override the built-in values.

The release build has no probe code in it. Build a probe version in its own folder (it is never used for normal play):

```
cmake -B cmake-build-probe -DKWINHELPER_PROBES=ON && cmake --build cmake-build-probe
```

Then start it with the probe build and the probe variable:

```
KWINHELPER_BIN=./cmake-build-probe/kwin-helper KWINHELPER_PROBE=shield,inventory bash start.sh
```

The same probe build also takes `bones` (stick figures), `gunlevel` (where a gun in a player's hands keeps its level), `mesh`, `flags`, `rendertime`, `stance` and `team` in `KWINHELPER_PROBE`, several at once separated by commas (for example `KWINHELPER_PROBE=bones`); copy their console lines and send them over the same way as in step 4.

1. Start it like that, then start a raid. Stay in third person, so the overlay can tell which player is you.
2. **Shield:** when `=== SHIELD PROBE ===` appears, move around but keep your shield full and take no damage. About 6 seconds later `=== NOW take a hit ===` appears: now take a hit (or do anything that changes your shield). Lines appear for every number that changed. Health is marked `<- known: health`. The shield is the other number that moves with it. Under each candidate there is a ready line: `if this is the shield: shield_component ... shield_value ...`.
3. **Gun level** (`KWINHELPER_PROBE=gunlevel`): carry two guns of different levels (for example a level I and a level III), and swap between them every few seconds for a minute or two; standing near other players adds more samples. A line shows each time a held gun's level is known (from when it was on the back). Every 10 seconds `=== GUN LEVEL PROBE ===` lists the places that held that level every time, best first; send over that list.
4. **Inventory:** every 8 seconds `=== INVENTORY PROBE ===` lists your item lists and those of the two nearest players, each with a ready `offsets.txt:  inv_array ...` line. Stand near another player for a moment.
5. Copy the console lines (or just the shield candidates that moved with health, and the inventory lists that name real items) and send them over, or put the ready lines into `offsets.txt` yourself (add `shield_max` with a new `shield_value`; a stride from a one-item list is only a guess), then go back to the release build (no probe variable).

Both probes only read memory.
