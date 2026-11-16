# Configuration and environment variables

## Where settings come from

| Source | What it holds |
| --- | --- |
| `sim_config.ini` | Window, screen layout, volume, frame rate, controls, DS firmware settings (libntr) |
| `sim_config_prj.ini` | The mod's features, graphics, developer options and cheats (Tab menu) |
| `gameplay_config.ini` | Gameplay rule overrides, see [gameplay_config.md](gameplay_config.md) |
| `imgui.ini` | Only the positions of the settings menu's windows |
| In-game Options | Text speed, battle style and so on, stored in the save file |
| `PLATINUM_*` variables | Debug and test switches, listed below. None of them overrides an ini setting |

All files are read from the working directory. A missing `sim_config.ini` or `sim_config_prj.ini` is created with
defaults; a missing `gameplay_config.ini` is created as a commented template.

## The Tab menu

Tab opens one panel with these categories: **General**, **Graphics**, **Speed & text**, **Controls**, **Gameplay**,
**Cheats & tools**, **Multiplayer** and **Developer**. On small windows the sidebar becomes a picker. Tab closes it
again; Escape first backs out of a subpage or cancels a key binding. While the menu is open it takes all input, so
nothing reaches the game.

The panel lives in libntr (`sim_gui.cpp`); the game fills its pages through `SIM::GUI::ProjectPage` in
`src/port/gui/sim_gui_prj.cpp`. Two of its own settings are in `sim_config.ini` under `[Interface]`:

- `MenuScalePercent`: `0` follows the display scale, otherwise 100–200 in steps of 25. Internal resolution doesn't
  affect it.
- `PauseWhileOpen`: pauses game logic while the menu is open (never during multiplayer). The separate **Manual pause**
  under Developer stays on after the menu closes.

Changes save at once, except the Gameplay page, which has its own Save button and applies after a restart (see
[gameplay_config.md](gameplay_config.md)). After building, `test_menu_gui.py` checks every category at several window
sizes and scales, plus input capture and pause; `test_gamepad_input.py` covers controllers.

## Mistakes are reported

At startup the game prints a `[config]` line on stderr for each setting it ignores, naming the file and line:

```
[config] ./sim_config_prj.ini:46: BW2BattleVisuals is ignored: it is in [Cheats] but belongs in [General]
[config] ./sim_config_prj.ini:2: [General] widescreenpokedex is ignored: names are case-sensitive, did you mean [General] WidescreenPokedex?
[config] ./sim_config_prj.ini:10: [General] GridBag=yes is ignored: expected true or false
[config] ./sim_config.ini:7: [General] MasterVolume=loud is ignored: expected a whole number
[config] ./gameplay_config.ini:75: [Gameplay] shinyRolls=99999 is ignored: expected a number from 0 to 65535
[config] environment: PLATINUM_DEBUG_WLD is not read by the game; did you mean PLATINUM_DEBUG_WILD?
```

It also reports keys this build doesn't have (often left behind by another branch or an older version), keys set
twice (the later line wins), and lines that aren't `key=value` or `[section]`. A setting with an invalid value keeps
its default. `gameplay_config.ini` is all or nothing: a line that isn't valid ini discards the whole file.

When the game saves `sim_config.ini` or `sim_config_prj.ini` (any change in the Tab menu), it rewrites the file with
only the keys it knows, so ignored keys disappear.

## `--print-config`

```sh
./main --print-config
```

Prints every effective setting from the three ini files, then the `PLATINUM_*` variables that are set, and exits
without opening a window. It creates no files, so it is safe to run in a scratch directory. Each value says where it
came from:

```
; sim_config_prj.ini
[General]
60Fps=true                               ; sim_config_prj.ini:2
BattlePacing=Quick                       ; sim_config_prj.ini:5 (from the old BattleAnimationSpeedFix)
GridBag=true                             ; default (sim_config_prj.ini:10 has an invalid value)
BW2BattleVisuals=false                   ; default
...
; gameplay_config.ini
[Gameplay]
shinyRolls=1                             ; default (no gameplay_config.ini)
```

The output is valid ini, so a section can be pasted back into its file. Warnings go to stderr as usual.

## Environment variables

None of these is needed to play. They are read when the code reaches them (some once at startup, some on every
frame), so set them before launching the game.

### Testing and reproducing bugs

| Variable | Value | Effect |
| --- | --- | --- |
| `PLATINUM_AUTOPILOT` | path | Runs a script of timed key presses and screenshots. Each line is `<frame> key <SDL key name> [hold frames]`, `<frame> shot <file.ppm>` or `<frame> quit`; `#` starts a comment and spaces in key names are written as `_` (`Left_Shift`). Lines run in file order and wait for their frame, so sort the file by frame. Default keys: z=A, x=B, c=X, v=Y, Return=Start, Right Shift=Select, q/e=L/R |
| `PLATINUM_STORY_TEST_FD` | file descriptor (3 or above) | Hands the controller to the gym story test runner over that descriptor. Only in builds configured with `-Dautoplay_tests=true`, see [autoplay_testing.md](autoplay_testing.md) |
| `PLATINUM_FIXED_CLOCK` | `YYYY-MM-DDTHH:MM[:SS]`, e.g. `2026-06-01T14:00` | Freezes the game clock at that local date and time (years 2000–2099), so time of day, lighting, shadow length, the Pokétch clock and berry growth repeat between runs. An invalid value prints a `[config]` warning and uses the real clock |
| `PLATINUM_DEBUG_MAP` | `<header>:<x>:<z>` | Flies to that map and tile once the field is first shown. Lasts until the game is saved |
| `PLATINUM_DEBUG_WILD` | `<species>:<level>` | Starts a wild battle 180 field frames (3 seconds at 60 FPS) after the field is first shown |
| `PLATINUM_DEBUG_WILD_KIND` | `safari` or `palpark` | Makes the `PLATINUM_DEBUG_WILD` battle a Safari Zone or Pal Park battle |
| `PLATINUM_DEBUG_TRAINER` | `<id>[,<id>[,<id>]]` | Starts that trainer battle instead (up to three trainer IDs, for double and tag battles). Sets the battle style to Set, so a script's presses don't answer the switch prompt |
| `PLATINUM_DEBUG_LEVEL_UP` | level, or any value | With a debug battle: puts the first party Pokémon one experience point short of that level (2–100), or of its next level, so the battle levels it up |
| `PLATINUM_DEBUG_MOVE` | move ID | With a debug battle: puts that move in the first party Pokémon's first move slot |
| `PLATINUM_DEBUG_SURF` | species ID | Turns the first party Pokémon into that species, teaches it Surf and surfs onto the tile the player faces. Warp to a shore with `PLATINUM_DEBUG_MAP` (the player faces south); if that tile isn't water it prints the surfable tiles around the player |
| `PLATINUM_DEBUG_OVERWORLD_POKEMON` | count (1–32) | Spawns that many temporary overworld Pokémon around the player, mixing normal and shiny palettes. Aborts if one leaks into the save |
| `PLATINUM_DEBUG_FOLLOWER_TRACE` | any value | With `PLATINUM_DEBUG_OVERWORLD_POKEMON`: logs the player's and follower's positions every second |
| `PLATINUM_DEBUG_FOLLOWER_SAVE` | frame | With `PLATINUM_DEBUG_OVERWORLD_POKEMON`: saves the game once, at that field frame or later (at least frame 180), when the follower exists and the player stands still |

### BW2 battle visuals

| Variable | Value | Effect |
| --- | --- | --- |
| `PLATINUM_BWBATTLE_WEATHER` | overworld weather ID | Uses that weather instead of the map's when choosing the BW2 battle weather |
| `PLATINUM_BWBATTLE_BG_CYCLE` | directory | Steps through every BW2 battle background and terrain and saves a screenshot of each to `<dir>/bgNN_tNN.ppm` |
| `PLATINUM_BWBATTLE_MOVE_CYCLE` | list such as `38,90,200-202` | Plays those moves' effects one after another once the battle waits for a command, and logs any that doesn't end. Numbers may be hex; from `0x231` on they are BW2's other effects. Always pass a short list: all moves take over 10 minutes |

### Performance and loading

| Variable | Value | Effect |
| --- | --- | --- |
| `PLATINUM_FRAME_TRACE` | path | Writes one CSV row of timings and loading counters per frame |
| `PLATINUM_GPU_TRACE` | `1` | Adds GPU timings to `PLATINUM_FRAME_TRACE` |
| `PLATINUM_CACHE_MB` | 128–1024 (default 256) | Memory budget for cached archive data; a quarter of it also caps the texture source copies |
| `PLATINUM_GPU_CACHE_MB` | 16–512 (default 64) | GPU texture cache budget |
| `PLATINUM_PREPARE_US` | 100–10000 (default 1000) | Time per frame, in microseconds, for preparing map chunks ahead of the player |
| `PLATINUM_UPLOAD_KB` | 16–4096 (default 64) | Map data uploaded to the GPU per frame, in KiB |
| `PLATINUM_LEGACY_LOADING` | `1` | Turns off the PC loading path (resident archives, chunk preparation) and loads maps like the DS |

Out-of-range numbers fall back to the default. `test_config_check.py` checks that this list matches the code.
