# Scene runs: screenshots and state dumps of one game situation

`tools/scripts/scene.py` starts the game once in a throwaway directory, loads a save, optionally warps or starts a
debug battle, presses keys, takes screenshots (PNG) and state dumps (JSON), and quits. Use it to check a visual change
or to reproduce a bug without touching your own saves and settings. For whole story playthroughs, use the
[gym autoplay tests](autoplay_testing.md) instead.

```sh
# Jubilife at 14:00, wide window: shot once the player has control, and check where the player stands
python3 tools/scripts/scene.py --map 3:174:752 --clock 14:00 --wide --shot field --expect tile=174,752

# Open the field menu and shoot it once it is up
python3 tools/scripts/scene.py --key field:x --shot xmenu=menu

# BW2 battle: the command menu, then the move list
python3 tools/scripts/scene.py --wild 25:5 --set prj.General.BW2BattleVisuals=true \
  --shot battle_menu --key battle_menu:a --shot moves=battle.menu=11+10
```

It prints the paths of the PNGs and JSONs. By default they go to `<build>/scene-shots/<date-time>/`, together with
`settings.ini` (the effective settings), `output.txt` (the game's stdout/stderr) and `log.txt`. `--out DIR` picks
another place.

## Two modes

**Bridge mode (events).** In a build configured with `-Dautoplay_tests=true`, scene.py drives the game frame by frame
through the test bridge (`src/port/debug/autoplay_test.c`, the same one the gym runners use). Each step waits for a
game event: field control, a map, a menu, the battle command menu, a line of text. A longer intro or a slower load
just makes the wait longer, and a step that never happens fails with a description of where the game is. Each
`--shot` also writes `<name>.json`, a state dump of that frame. `build_linux` is configured this way:

```sh
python3 subprojects/meson-1.12.0/meson.py configure build_linux -Dautoplay_tests=true && make linux
```

The bridge does nothing unless scene.py or a gym runner starts the game, so this build plays normally.

**Frame mode (fixed frames).** Without the bridge, or with `--frames`, scene.py writes a `PLATINUM_AUTOPILOT` script:
keys and shots at fixed frame numbers, with milestones that are only rough guesses (below). It can't dump state or
check conditions.

scene.py prints which mode it uses.

## Steps

Steps are `--shot`, `--key`, `--dump`, `--wait` and `--expect`. In bridge mode **they run in the order given**. Each
step looks for its condition starting from the frame where the previous step's condition held, so
`--key field:x --shot later=field+40` counts both from the same field frame.

| Step | Effect |
| --- | --- |
| `--shot [NAME=]WHEN` | Screenshot `NAME.png`, plus `NAME.json` in bridge mode. Without a name, it is made from WHEN |
| `--key WHEN:KEY[:HOLD]` | Holds a key for HOLD frames (default 4). KEY is a DS button: `a b x y l r start select up down left right`, or `run` (B held). Join buttons with `+` (`b+left`). Frame mode also takes `tab` and SDL key names |
| `--dump [NAME=]WHEN` | State dump only (bridge) |
| `--wait WHEN` | Waits for it, e.g. before an `--expect` (bridge) |
| `--expect CONDITION` | Must hold on the frame the previous step happened, or the run fails (bridge) |
| `--until WHEN` | Keeps running after the last step until WHEN |

A shot name in front of `=` is only taken as a name when it isn't `map`, `tile`, `x`, `z` or `frame`: `--shot map=3`
waits for map 3, and `--shot city=map=3` names that shot `city`.

### Conditions (WHEN)

| WHEN | Bridge mode | Frame mode |
| --- | --- | --- |
| `1234` | Bridge frame 1234 or later (bridge frames count the game's input reads, not rendered frames) | Rendered frame 1234 |
| `title` | Title screen | ~330 |
| `field` | The player has control: field running, not busy or moving, no menu or battle. With `--map`, also on that map | ~1300 |
| `battle` | The battle app is running | ~1300 (the send-out) |
| `battle_menu` | The Fight/Bag/Pokémon/Run menu is up | ~1800 |
| `menu` | A list menu is open (field menu, yes/no, shop) | — |
| `map=3`, `map=jubilife_city` | The player is on that map header (number or `MAP_HEADER_` name without the prefix) | — |
| `tile=X,Z` | The player stands on that tile | — |
| `text~words` | The last dialog text contains these words (any case) | — |
| `menu~choice`, `app~Name` | An open menu has that choice; the running app's function name contains Name | — |
| `path=value`, `path!=value`, `path~text` | Any value in the state dump, e.g. `field.x=12`, `battle.menu=11`, `field.busy=0` | — |
| `...+N` | N frames later | N frames later |
| `...-N` | — | N frames earlier |

`--wait-limit` (default 3600 bridge frames) caps how long each step may wait. When a step times out or an `--expect`
fails, scene.py takes `failed.png`, writes `wait_failed.json` or `expect_failed.json` and exits non-zero.

### What a state dump holds

`NAME.json` holds `frame`, the step's condition, `settings` (the effective content of all three ini files plus the
`PLATINUM_*` variables, from `main --print-config`) and `state`, the bridge snapshot of that frame:

- `app_name`: the running application's main function (`FieldMap_Main`, `ApplicationMain_Battle`, `TitleScreen_Main`).
- `field` (when the field runs): `map`, `map_name`, tile `x`/`z`, `height`, facing `dir`, `busy`, `moving` and
  `objects` (`[local id, x, z, script]` per NPC).
- `menu`: `kind`, `cursor`, `choices`; `text`: the last dialog line of 20 or more characters (it stays until the next
  one, so it can be stale); `ui`: the current widget of screens that report one.
- `battle` (while choosing a command): `menu` (1–10 command menu variants, 11 move list), `cursor`, attacker and
  defender `[level or species, HP, ...]` and `moves` (`[move, PP, power, type, class, accuracy, immune]`).
- `heaps`: every live heap with `name`, `size`, `used`, `free` in bytes.
- `party`, `mons` (`[species, level, HP, max HP, personality, egg]`), `money`, `inventory`, `boxed`, `vars` and a few
  story flags the gym runners use.

`--trace` writes every frame's raw snapshot to `trace.jsonl`.

## What it sets up

1. A temporary run dir (in `$TMPDIR`, default `/tmp`). It symlinks the build's assets from `<build>/pokeplatinum`,
   but copies `main` and the save, makes an empty `autosave/`, and writes fresh ini files. It never links saves,
   `*.ini`, logs or `autosave/`, so the game can't write to your files.
2. The boot: in bridge mode it presses A at the title screen and Continue, and B to close the journal, until the field
   or a battle shows. In frame mode it presses fixed keys at fixed frames. `--no-boot` leaves both out.
3. SDL's offscreen video driver and dummy audio. Inherited `PLATINUM_*` variables and `LD_PRELOAD` are dropped.
4. The PNGs, the logs, and deleting the run dir (`--keep` keeps it). The game ignores SIGTERM, so it is killed.

It exits non-zero when the game crashed or timed out, a step failed, or a shot is missing.

## Other options

| Option | Effect |
| --- | --- |
| `--save N` / `--save FILE` / `--save none` | Loads `<build>/pokeplatinum/saveN.bin`, a file there (`aasave1.bin`) or any path, copied to the run dir's `save1.bin`. Default `1`. `none` starts with no save (pair with `--no-boot`) |
| `--map H:X:Z` | `PLATINUM_DEBUG_MAP`: header and tile, see `build_linux/generated/map_headers.h` |
| `--wild S:L`, `--trainer ID[,ID]` | Debug wild or trainer battle |
| `--clock HH:MM` or `--clock YYYY-MM-DDTHH:MM` | `PLATINUM_FIXED_CLOCK`, so lighting and shadows repeat between runs. Without it the real clock is used |
| `--wide` | 2160x1000 window. Widescreen-only bugs don't show in the default 1024x768 |
| `--set FILE.Section.Key=value` | Ini override; `FILE` is `sim`, `prj` or `gameplay`, e.g. `prj.General.GridBag=false`. Naming the section avoids the "key landed in `[Cheats]`" trap |
| `--user-ini` | Starts from the build's own three ini files instead of the defaults below |
| `--env NAME=value` | Any other variable, e.g. `--env PLATINUM_BWBATTLE_MOVE_CYCLE=38,90` |
| `--asan` / `--exe PATH` | Runs `build_linux_asan/main` (with `detect_leaks=0`) or another executable, with the build's assets. Bridge mode needs that executable built with the bridge too |
| `--frames` | Frame mode even when the build has the bridge |
| `--timeout S` | Bridge mode: whole-run limit in seconds (default 600) |
| `--visible` | A real window instead of offscreen. In bridge mode the game ignores your keyboard |
| `--strict-config` | Fails when the game reports an ignored ini setting. Those `[config]` lines are always printed |
| `--keep`, `--keep-ppm` | Keeps the run dir or the raw PPMs |
| `--dry-run` | Prints the plan, variables and inis without running |

### Default settings

Without `--user-ini` the run gets only these keys, and the game's defaults for the rest:

- `sim_config.ini`: `InternalResolutionScale=2`, 1024x768 window, `ScreenLayout=top_only`, `VsyncInterval=0`,
  `CapFrameRate=true`, `MasterVolume=0`, log file and crash handler on.
- `sim_config_prj.ini`: `60Fps=true`, `EnableAsserts=true`, `Autosave=false`, `WalkThroughWalls=false`,
  `DisableRandomEncounters=true`.

`CapFrameRate=true`, `FullScreen=false`, `Autosave=false` and `WalkThroughWalls=false` are applied last, even over
`--set` and `--user-ini`: uncapped, frame mode's numbers mean nothing, and walking through walls makes collision tests
meaningless.

## Things to know

- Water animates and NPCs wander, so shots taken a few frames apart differ even with a fixed clock.
- A shot is the window as drawn, so the PNG has the window's size. `InternalResolutionScale` changes the detail, not
  the size.
- An event can come a frame before its graphics: `menu` holds as soon as the menu's choices exist. Add `+N` when a shot
  catches an opening animation.
- OAM lags VRAM by one frame on this port, so a one-frame flash needs a shot on every frame around it
  (`--shot f0=menu --shot f1=menu+1 --shot f2=menu+2`).
- `python3 tools/scripts/test_scene.py` tests the script itself; add `--live --build build_linux` to also run two
  short scenes.
