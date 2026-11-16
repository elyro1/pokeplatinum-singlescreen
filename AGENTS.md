# AGENTS.md

Pokémon Platinum PC port (pret decomp → cybervisi0n's native port → this single-screen/widescreen mod).
Main branch: `pc_single_screen_mod`. User-facing docs: README.md, BUILD.MD, docs/.

## Ground rules

- **Every mod feature sits behind a setting** (Tab menu → `sim_config.ini` / `sim_config_prj.ini`, or
  `gameplay_config.ini` for gameplay rules). The original behaviour must stay reachable, and a feature that needs
  optional assets falls back cleanly when they're missing.
- **No Nintendo assets beyond what pret already ships.** Platinum assets are extracted from the user's ROM at first
  launch; BW2 assets (battles, sprites, maps, music) come from an optional Black 2/White 2 ROM at runtime. Never commit
  extracted BW2 data, and never commit ROMs or saves.
- **SDK subprojects are forks, not patches.** libntr, libntrsystem etc. live on `elyro1/*` forks (see the `.wrap`
  files). Change them inside `subprojects/<lib>`, commit there, bump the wrap's `revision`, and remind the user that the
  fork commit has to be pushed before a fresh checkout can fetch it. Don't add diff files under `subprojects/packagefiles/`.
- **Don't edit the pokebw2 decomp.** `src/bwbattle/` holds copies of the BW2 files we need, adapted in place
  (BW2 bugs fixed in copies are marked `BUG`). The decomp itself is a separate matching project.
- Leave decomp naming (`unk_*`, `ov*_`) alone unless the task is about it; that's upstream pret's job.
- Commit or push only when asked.

## Layout

- `src/port/`: port-only features, with headers at the same paths under `include/port/`:
  - `config/`: the settings model (`sim_config_prj`) and `--print-config`.
  - `gameplay/`: gameplay rules (`gameplay_config`), autosave, battle pacing, and the setting accessors `cheats.h`,
    `game_speed.h`, `port_options.h`.
  - `visuals/`: field visuals/lights/effects, BW sprites, battle smoothing, wide evolution, and the accessors
    `widescreen.h`, `frame_rate.h`.
  - `assets/`: ROM extraction, resource patches, follower sprites, the DS save layout and import.
  - `gui/`: the Tab menu (ImGui) and the gameplay config editor.
  - `debug/`: debug map/battle jumps, the autoplay test bridge, `asserts.h`.
- Decomp code reads settings through those accessors (`Widescreen_*`, `FrameRate_*`, `GameSpeed_*`, `PortOptions_*`,
  `Cheats_*`, `FieldVisuals_*Enabled`, `GameplayConfig_Get()`), never `SIM_Config_prj_GetConfig()` or
  `SIM_GetConfigPtr()` directly. A new setting that decomp code checks gets an accessor there. The accessors are
  `static inline` so test harnesses that compile decomp files against stubs still link, and they return the
  original behaviour on the DS build. An accessor also folds in what a feature needs besides its setting
  (e.g. followers need the BW2 sprites).
- `src/bwbattle/{adapter,view,gfl,system,shim,assets}/`: BW2 battle visuals. The adapter maps Platinum's
  `BattleDisplay_*` commands (src/battle/battle_display.c) onto the BW2 view; Gen 4 battle logic is unchanged.
- `src/` elsewhere: decomp code, with port changes guarded by `#ifdef SDK_BUILD_ARM` (DS build) vs the PC builds.
- `tools/scripts/`: build helpers, asset importers, map-extension scripts and the test suites (`test_*.py`).
- `res/port_patch_baselines/`: patches applied to retail script/text files at startup (`iris_patch_baselines.py`).

## Build

```bash
./drun.sh make linux        # Docker; or `make linux` with deps installed. Output: build_linux/pokeplatinum/main
make win64                  # inside the MSYS2 shell (Launch_MSys2.ps1)
make mingw                  # Windows build cross-compiled on Linux
make                        # original DS ROM;
make linux-asan             # ASan + UBSan build in build_linux_asan/ (docs/sanitizers.md)
```

We can ignore the DS ROM build. PC-only code goes behind the platform guards or in `src/port/`.

`src/port` and `src/bwbattle` build with `-Wall` (the decomp and SDK stay at `-w`); keep them warning-free.
`-Dport_werror=true` makes the warnings errors. The port files are their own static library, so their objects are
in `build_linux/libport.a.p/`, not `main.p/`.

## Tests

- Unit and integration suites: `python3 tools/scripts/test_<name>.py --build build_linux`. They compile their
  `*_test.c/.cpp` against the build's `compile_commands.json`, so build first.
- Run only the suites and cases related to the change, not everything. For BW2 move animations, pass an explicit list
  (`PLATINUM_BWBATTLE_MOVE_CYCLE=38,90,200-202`); never sweep all moves (10+ minutes).
- CI (`.github/workflows/build_*.yml`) runs the resource audit (`audit_release_resources.py --tree-only`) and a few
  suites; run the audit when adding files the release needs.

## Running the game for checks

Never run tests in `build_linux/pokeplatinum` itself: it holds the user's real saves and configs. Make a scratch run
dir that symlinks the build's entries but **copies** `main`, the `*.ini` files and `save*.bin`, then edit the copies.

- Saves are slots: the game loads `save1.bin` from the working dir. Copy the slot you need there.
- Debug environment variables (full list: docs/environment_variables.md):
  - `PLATINUM_AUTOPILOT=<file>` runs a key/screenshot script (keys: z=A, x=B, c=X, Return=Start, q/e=L/R; shots are
    PPM). Lines run in file order and wait for their frame, so **sort by frame** (`sort -n -s -k1,1`), or later lines
    are skipped without warning.
  - `PLATINUM_DEBUG_MAP=<header>:<x>:<z>`, `PLATINUM_DEBUG_WILD=<species>:<level>`, `PLATINUM_DEBUG_TRAINER=<id>[,<id>]`
    jump straight to a map or battle.
  - `PLATINUM_FRAME_TRACE`, `PLATINUM_GPU_TRACE` for timing.
- Config traps in the run dir copies:
  - `./main --print-config` in the run dir lists every effective setting with its source file and line, and `[config]`
    lines on stderr name ignored keys and values. Check them before trusting a run.
  - Keys must be in the right ini section. A key appended at the end of `sim_config_prj.ini` lands in `[Cheats]` and
    is ignored, with only a `[config]` warning (e.g. `BW2BattleVisuals=true` belongs in `[General]`).
  - Set `WalkThroughWalls=false`, or collision tests mean nothing.
  - Screenshot runs need `CapFrameRate=true`; uncapped, the boot hits the frame guard before the field.
  - Widescreen-only bugs don't show in the default 4:3 window. Test wide layouts with e.g. `WindowWidth=2160`,
    `WindowHeight=1000` in `sim_config.ini`.
- Time of day, lighting and shadow length follow the real clock, and water textures animate. Shots taken at
  different times or frames differ for those reasons alone, so don't read that as a regression.
- Headless audio: `SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=<file>`.
- `gdb -p` is blocked (ptrace_scope); start the game under gdb instead (`gdb -batch -ex run -ex "bt 20" ./main`).
- Scratch runs copy files; delete them when done, a full `/tmp` breaks builds.

## Known pitfalls

- **OAM lags VRAM by one frame.** `main.c` presents, then the VBlank callback transfers OAM, so immediate VRAM writes
  (`GX_LoadOBJ`, `Bg_LoadTiles`, `Window_CopyToVRAM`) show for a frame with the previous frame's sprites: a one-frame
  flash. Fix: set up the new OAM, do the immediate writes at the start of the next frame (see `FlushPendingDraws` in
  the Pokédex grid). Exception: `SpriteSystem_ReplaceCharResObj` and `Window_ScheduleCopyToVRAM` already land in VBlank
  together with the OAM, so deferring those causes the flash. Check suspect screens with a per-frame shot script.
- `AddMenuEntryImm` takes a byte string ID; messages past 255 need `AddMenuEntry`.
- New scripts, events, encounters or text need entries in `res/field/{scripts/scripts,events/zone_event,encounters/encounters}.order`
  and `generated/text_banks.txt`, plus `tools/scripts/iris_patch_baselines.py`. Editing an already-patched script means
  regenerating its `res/port_patch_baselines/scripts/*.patch`.
- New IDs (map headers, area IDs, object-event graphics, BGM IDs, flags) share ranges with other feature branches.
  Check what other branches already claim before taking one, and prefer generator output over hand edits
  (e.g. `tools/scripts/gen_bw2_overworld.py` on the BW2 map branch).
- The dev GPU is NVIDIA, which tolerates GL errors Mesa rejects (e.g. two sampler types on one texture unit). Don't
  treat "works here" as proof for GL changes.

## Style

- C follows pret's conventions (CONTRIBUTING.md): PascalCase types and `Module_Function` names, `.clang-format`.
- Keep comments short; explain why, not what.
- Commit messages: imperative, one-line subject like the existing history ("Fix route 227 follower despawn").
