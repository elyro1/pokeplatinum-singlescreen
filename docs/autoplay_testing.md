# Automated gym story tests

The first scenario plays a fresh new game through the Coal Badge. A separate
second-gym scenario continues through the Forest Badge; a third extends it through
Fantina's Relic Badge in Hearthome (the third gym in Platinum). All scenarios save
through the in-game menu and restart the process to verify the badges and test
party load successfully. This is a progression and stability test, not a
difficulty or battle-balance test.

`sim_autopilot.cpp` and `PLATINUM_AUTOPILOT` remain independent and unchanged. They
can still reproduce bugs using their existing frame scripts.

## Build and run

Linux is currently required. Use the normal native build dependencies and an
already extracted runtime from your own ROM. Enable the test bridge explicitly:

```sh
python3 subprojects/meson-1.12.0/meson.py configure build_linux -Dautoplay_tests=true
ninja -C build_linux main
python3 tools/autoplay/first_gym.py --build build_linux --output /tmp/platinum-first-gym
```

The output directory must not already exist. The runner copies runtime assets
from `build_linux/pokeplatinum` and the executable from `build_linux/main`. It
excludes saves, backup saves, and user settings. It writes its own settings into
the copied runtime. It does not update the packaged executable or player saves.
Inherited debug/cheat environment variables and `LD_PRELOAD` are excluded from
the child process.

The default run uses SDL's offscreen OpenGL driver, dummy audio, 1x internal
resolution, 60 FPS game logic, Smooth battle pacing, the standard battle view,
and uncapped presentation. Use `--visible` to watch the game on the desktop.
The game still renders; this is not a renderer-free simulation. A functioning
OpenGL driver and the local extracted assets are required.

Options include `--timeout` (whole-run seconds) and `--max-frames` (logical input
frames). The first scenario defaults to 1800 seconds and 180000 frames; the
second defaults to 3600 seconds and 350000 frames; the third defaults to 5400
seconds and 500000 frames. Creating a file named `cancel` in the
output directory stops the run with a failure report. The bridge also terminates
if the controller disconnects or supplies no input for 30 seconds.

For a normal build, configure `-Dautoplay_tests=false` and rebuild. The bridge
source is excluded and its observation calls compile to no-ops. Even a test
build behaves normally without the runner's private descriptor environment.

## Custom TAB settings

All gym runners and `interactions.py` accept these optional config files:

| Option | TAB settings | Runtime filename |
| --- | --- | --- |
| `--sim-config PATH` | Display, resolution, frame cap, layout, and input settings | `sim_config.ini` |
| `--project-config PATH` | Battle view, speed and text, graphics, and other project settings | `sim_config_prj.ini` |
| `--gameplay-config PATH` | Gameplay rules | `gameplay_config.ini` |

For example, reuse your saved battle view and speed settings:

```sh
python3 tools/autoplay/first_gym.py --build build_linux --visible \
  --project-config build_linux/pokeplatinum/sim_config_prj.ini \
  --output build_linux/autoplay-runs/custom-tab
```

Supply any combination of the three options. Each supplied file replaces the
corresponding isolated runtime file in full before the game starts; it is not
merged with the runner's settings. Missing keys follow the game's normal config
loading behavior. Files you do not supply retain the runner's existing defaults.
Paths are relative to the directory you run the command from. Missing or unreadable
files fail before the output directory is created; INI syntax and values are
handled by the game's normal loaders.

The runner copies the supplied bytes, so changes during the run affect only the
isolated copies under `OUTPUT/game/`. Those copies also remain in use for the
save/reload verification. `report.json` records the original absolute paths and
SHA-256 hashes under `custom_configs`. Your source settings and saves stay intact.
`--visible` still controls whether the runner selects SDL's offscreen driver.

These options change the settings used by the test; the controller still follows
its authored route and does not exercise the TAB menu itself. Some settings, such
as overworld encounters, can change behavior enough to require controller changes.

## Test fixture and allowed actions

The bot completes naming and starter selection through ordinary controls. Once
the starter gift exists, the bridge replaces that party once with:

| Pokémon | Level | Moves |
| --- | --- | --- |
| Mew | 100 | Psychic, Aura Sphere, Flamethrower, Thunderbolt |
| Bibarel | 20 | Surf, Waterfall, Rock Climb, Rock Smash |
| Tropius | 20 | Cut, Fly, Strength, Defog |

The fixture has fixed IVs and personalities and uses the player's original
trainer details for obedience. Its installation is the only direct gameplay
state mutation. Badges, story flags, key items, collision, encounters, HP, PP,
opponents, and battle results follow the game's ordinary rules. Existing
gameplay configuration defaults remain in effect unless `--gameplay-config` is supplied.
Normal story gifts may add party members later; checkpoint validation checks
that the three original test Pokémon remain at the front of the party.

After setup, the controller supplies DS buttons and touch coordinates before
the normal input edge/repeat processing. Menus and battles consume those inputs
normally. The bridge never returns fabricated menu results or battle commands.

## Controller and route

`src/port/debug/autoplay_test.c` provides read-only observations and an inherited local
socket protocol. It exposes application identity, dialogue, menu choices,
battle choices, party state, story variables, map coordinates, nearby objects,
and loaded collision tiles, including the rotating Eterna gym clock collision. The game waits for one input response per logical
frame; rendering speed does not determine route timing. The snapshot also lists every live heap's size and free
bytes. [scene.py](scene_runs.md) uses the same bridge for short event-driven screenshot runs.

`tools/autoplay/first_gym.py` owns the story route and decisions. Its route covers
the opening rival encounter, starter battle, Lake Verity, Rowan's laboratory,
the parcel, catching tutorial, Jubilife and Pokétch campaign, Route 203, the
Oreburgh mine, healing, and Roark. A local A* search follows known collision
tiles and avoids NPCs. Party followers and wild overworld sprites do not block
the player, so paths may cross them when no short detour exists; a follower
standing behind the player must not seal a one-tile corridor. Authored objectives
select doors and story interactions;
blocked movement edges are learned with bounded retries. Collision knowledge
follows the shared map matrix across town/route boundaries; unloaded tiles do
not erase it. Clock-state observations invalidate the Eterna gym cache after
rotations, including the final layout needed to leave after Gardenia.
Navigation holds B to run wherever the game permits, including
while a step finishes. B is released for dialogue, menus, and battles. Movement
finishes one step before the next direction is chosen.
Steering stays held through turn animations so Cycling Road's automatic
downhill movement cannot cancel a turn.
Door approaches keep replanning across adjacent overworld headers. Failed-edge
memory resets on a change of height layer, so bridge railings do not block the
ground route underneath; small model offsets on the same floor preserve that
memory. Route 215 uses an explicit waypoint to approach beneath its western
bridge.

Ordinary dialogue advances automatically. Menu policies handle supplied rival
names, declining nicknames, story confirmations, and saving. Battle choices
score legal damaging moves using PP, power, physical/special stats, STAB,
the repository's type chart, accuracy, and reliable knockout estimates. An
engine observation accounts for reported immunities without consuming RNG.
No LLM or external service is used while playing.

`tools/autoplay/second_gym.py` extends that route through the Jubilife Galactic
tag battle, Ravaged Path and Rock Smash, Floaroma Meadow and the Works Key,
Commander Mars at Valley Windworks, Cheryl in Eterna Forest, and Eterna's gym
clock trainers and Gardenia. It uses the shared driver without changing the
first-gym target. Battle observations identify the active player Pokémon and
an opponent that has not fainted, including target selection in double battles.

`tools/autoplay/third_gym.py` adds the Explorer Kit, Cynthia's Cut gift, the
Galactic Eterna building and Commander Jupiter, the bicycle and Cycling Road,
Route 207, Mt. Coronet, Hearthome's Contest Hall, and Fantina. The controller
uses the normal Bag UI to mount the bicycle. Read-only observations expose
Hearthome's randomized floor clue and symbol; the route approaches the clue
and selects the matching door in each dark room. It never sets puzzle answers,
story flags, badges, items, or avatar state.

## General interaction scenario

`tools/autoplay/interactions.py` continues from Fantina's save with system
interaction checks. It visits every available start-menu entry (Pokédex,
Pokémon, Bag, Pokétch, trainer card, Options, Save, and Quit), opens each app,
and returns to the field. Quit's confirmation is cancelled. These are app
entry/exit checks, not exhaustive tests of every page inside each app.

The scenario buys Poké Balls and Potions in Hearthome, finds ordinary wild
encounters, switches to Bibarel and back to Mew, wins a wild battle, and catches
another Pokémon. Medicine-use testing is explicitly skipped because the game
configuration prohibits it. Poké Balls remain usable and their consumption is
checked during capture. The scenario does not change gameplay configuration.

It deposits the caught Pokémon in a PC Box and withdraws it, checking species
and personality in both locations. It then travels through Solaceon and Route
215 to buy items in Veilstone's department store. Purchases must add the requested
quantity and deduct money. The final ordinary save is reloaded to verify party
identities, Box contents, inventory, money, and the three badges. All actions use
normal buttons and touch input; the bridge adds read-only inventory, PC, and
UI observations. The battle party observation includes the displayed slot
order, which changes after switching.

```sh
python3 tools/autoplay/interactions.py --build build_linux \
  --checkpoint build_linux/autoplay-runs/third-gym-06/checkpoint-relic-badge.bin \
  --start-after relic-badge --visible \
  --output build_linux/autoplay-runs/interactions
```

In addition to the story checkpoints, this scenario accepts `relic-badge`,
`menus`, `supplies`, `wild`, `pc`, `solaceon`, `route215`, `route215-west`,
`route215-central`, and `veilstone`. Each checkpoint is an ordinary
save. `report.json` includes a `checks` object with evidence for each interaction
executed in that run. Checks before a resumed milestone are skipped, so use the
Relic Badge checkpoint when validating the entire interaction scenario.

NPC interactions use a progress-aware stall limit, plus a hard total limit.
Long battles and badge/item fanfares can finish without consuming one short
combined dialogue deadline; stalled interactions and repeating text still fail.
This is not yet a full-story bot or a general solver for arbitrary menus and puzzles.
Runs use the game's ordinary random seeds and record observed state and inputs;
the trace is diagnostic evidence, not a promise of deterministic RNG replay.

## Evidence and focused reruns

Use an output directory under `build_linux/autoplay-runs/` to keep checkpoints
across reboots; `/tmp` evidence is temporary. These build outputs are ignored by Git.

Each output directory contains:

- `report.json`: pass/fail, fresh-start status, executable hash, failure
  objective, final state, elapsed time, battle count, and total input frames.
- `trace.jsonl`: observations, route objective, and input response per frame.
- `game.log`: game and bridge diagnostics.
- `controller-error.txt` and `game/story-test.ppm`: failure details and a
  screenshot when the renderer remains responsive.
- `checkpoint-*.bin`: ordinary saves made through the game's Save menu at
  milestones. The isolated runtime is retained under `game/`.
- `coal-badge.ppm`: a screenshot after defeating Roark, when that milestone
  is reached.

For development, resume a saved milestone explicitly:

```sh
python3 tools/autoplay/first_gym.py --build build_linux \
  --checkpoint /tmp/platinum-first-gym/checkpoint-parcel.bin \
  --start-after parcel --output /tmp/platinum-first-gym-rerun
```

The second scenario has its own entry point:

```sh
python3 tools/autoplay/second_gym.py --build build_linux \
  --output /tmp/platinum-second-gym --timeout 3600 --max-frames 350000
# For focused work beyond Roark, reuse an ordinary first-gym save:
python3 tools/autoplay/second_gym.py --build build_linux \
  --checkpoint /tmp/platinum-first-gym/checkpoint-coal-badge.bin \
  --start-after coal-badge --output /tmp/platinum-second-gym-rerun
```

Supported first-gym starting milestones are `starter`, `parcel`, `school`,
`jubilife`, and `oreburgh`. The second scenario also accepts `coal-badge`,
`ravaged`, `floaroma`, `mars`, `windworks`, and `eterna`, and writes `forest-badge.ppm` at its target.
The third scenario also accepts `forest-badge`, `cut`, `jupiter`, `bicycle`, and
`hearthome`, and writes `relic-badge.ppm`:

```sh
python3 tools/autoplay/third_gym.py --build build_linux \
  --visible --output /tmp/platinum-third-gym
# Focus on the new stage with an ordinary second-gym checkpoint:
python3 tools/autoplay/third_gym.py --build build_linux \
  --checkpoint /tmp/platinum-second-gym/checkpoint-forest-badge.bin \
  --start-after forest-badge --output /tmp/platinum-third-gym-rerun
```

A checkpoint run records `fresh_start: false`; it cannot establish that the
complete fresh-start route passed. The full acceptance run must start without
a checkpoint and reach the badge in one continuous playthrough. Its final
restart is solely the save/reload verification.

Run focused controller checks with:

```sh
python3 -m unittest discover -s tools/autoplay -p 'test_*.py' -v
# Or, with the test build configured:
python3 subprojects/meson-1.12.0/meson.py test -C build_linux --suite autoplay
```

These check move legality, immunity and type handling, knockout reliability,
collision/NPC detours, learned blocked edges, dialogue stall/loop bounds, double
battle targeting, deferred save scenes, badge persistence checks, and isolation
of player saves and assets. Passing them does not substitute for the actual game playthrough.
