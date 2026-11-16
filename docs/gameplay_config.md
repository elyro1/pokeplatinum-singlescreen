# Gameplay configuration

The PC port creates `gameplay_config.ini` with its built-in defaults on first start, then reads gameplay overrides from it at startup. Existing files are preserved on startup. The built-in defaults are the **Single-screen mod** preset; the **Classic Platinum** preset restores original Platinum gameplay. You do not need to rebuild the game to change these settings.

## Edit through the TAB menu

Open **Tab → Gameplay** to change all 24 options. The page groups them into evolution and eggs, encounters and events, progression and prices, moves and tutors, and battle rules. Options whose label doesn't say everything have a short explanation below it. Numeric options use sliders; hover one for its range, drag to adjust, or Ctrl+click to type an exact value.

Changes save to `gameplay_config.ini` immediately (a slider when you release it) and apply after you restart the game. The page shows a yellow reminder while the file differs from the running game.

Two presets at the top replace every option at once:

- **Single-screen mod** (the built-in default): the mod's quality-of-life rules, the same values as `gameplay_config.example.ini`. The tables below list them.
- **Classic Platinum**: the original rules. Every boolean option is off, BP prices are unchanged, Rare Candy costs 48 BP, trade evolution is trade-only, and all multipliers and shiny rolls are 1.

The button for the preset that matches the current values is greyed out. If you changed options yourself, choosing a preset asks before replacing them. The presets stay visible while the options scroll.

If another program changed the file since the page loaded it, or the file can't be parsed, saving stops, the page shows why, and **Reload file** reads it again.

Saving updates known options while preserving comments, unknown options, unrelated sections, and line endings. It updates duplicate known keys consistently and adds missing options. If another program changes the file after it was loaded, saving stops and asks you to reload. A malformed file remains intact and must be repaired before editing through the menu. A failed save keeps the previous file.

## Create and use a config

1. Start the game once to generate `gameplay_config.ini` in the game's **working directory**, alongside `sim_config_prj.ini` and your saves. This is normally the game folder. If you launch from a terminal, it is the directory you launch from, which can differ from the executable's directory.
2. Edit the generated file in a text editor. Keep the `[Gameplay]` section and spell option names exactly as shown.
3. Restart the game to apply changes.

The generated file includes comments explaining every option. If the working directory is not writable, the game still starts with its built-in defaults.

You can also copy [`gameplay_config.example.ini`](../gameplay_config.example.ini) over the generated file. It holds the **Single-screen mod** preset, which is also the built-in default, so it mainly serves as a commented reference.

A file only needs the options you want to change. For example, this partial file adds free move relearning and shard tutors to the defaults:

```ini
[Gameplay]
freeMoveRelearning=true
freeMoveTutors=true
```

Every omitted option retains its built-in default. Delete or rename `gameplay_config.ini` and restart to regenerate it with built-in settings. This resets configuration, but does not undo gameplay changes already saved, such as evolutions or removed egg cycles.

## Fixed defaults for ROM hacks

Set your hack's defaults in `GameplayConfig_LoadDefaults`, after its call to the Single-screen mod preset, in [`src/port/gameplay/gameplay_config.c`](../src/port/gameplay/gameplay_config.c). In `GameplayConfig_Init` in the same file, comment out this single call and rebuild:

```c
    // GameplayConfig_LoadFile();
```

The game then uses only your code defaults. It neither generates nor reads `gameplay_config.ini`, so even an existing or manually created file cannot override your settings. Leave `GameplayConfig_LoadDefaults(&sGameplayConfig);` enabled. The Gameplay page reports that file overrides are disabled and cannot edit the file in these builds.

## File format and loading rules

- Use one `name=value` entry per line under `[Gameplay]`. Section and option names are case-sensitive.
- Booleans accept `true`, `false`, `TRUE`, `FALSE`, `1`, or `0`. Mixed-case values such as `True` are ignored.
- Numbers must be non-negative decimal integers within the option's range. Negative numbers, fractions, and values outside the range are ignored.
- Use lines beginning with `;` for comments, as in the example.
- Missing options retain defaults. Unknown options and other sections are ignored. Invalid values are ignored, leaving the current value for that option (normally its default). If an option is repeated, the last valid value wins.
- A malformed INI file, such as a line with no `=`, causes **all overrides to be discarded**. An unreadable file or failure to create a missing file also uses all defaults.
- Settings are read at startup, so saving the file during play requires a restart. Existing files are never automatically updated with new defaults or options.

## Boolean options

Set an option to `true` to enable the described behavior. **Default** is the Single-screen mod value; Classic Platinum sets every option to `false`.

| Option | Default | Behavior when enabled |
| --- | --- | --- |
| `prohibitBattleItems` | `false` | Blocks player Bag items in battle except Poké Balls. Held-item effects and opponents' items still work. |
| `forceSetBattleStyle` | `false` | Always uses Set style, suppressing the Shift prompt regardless of the saved Options selection. |
| `unlimitedVitaminEVs` | `true` | Vitamins can raise a stat past 100 EVs, up to the normal 255 per-stat and 510 total EV limits. EV-reducing berries retain normal behavior. |
| `forgettableHMs` | `true` | HM moves can be replaced when learning another move, including after leveling up in battle. |
| `unlearnedHMs` | `false` | Compatible non-egg party members can use HMs without learning them or owning the HM item. Interact with obstacles or use **HMs** in the Pokémon's party menu. Badge and location restrictions still apply; non-HM field moves still need to be learned. |
| `freeMoveRelearning` | `false` | The Pastoria City Move Maniac requires and consumes no Heart Scale. Other move tutors are unaffected. |
| `freeMoveTutors` | `false` | All three shard move tutors require and consume no shards. Their displayed costs and greetings follow this setting. |
| `mainStoryMoveTutors` | `false` | The Route 212 tutor offers all three shard tutors' move lists, including Survival Area moves. Compatibility and known-move checks still apply. Use `freeMoveTutors` separately to remove shard costs. |
| `unlimitedGreatMarshSteps` | `false` | Stops counting Great Marsh steps and disables the 500-step exit. The entry fee and Safari Ball limit remain. Disabling this resumes counting from the saved counter. |
| `zeroEggCycles` | `false` | New eggs have zero remaining hatch cycles; existing party eggs are set to zero at their next hatch check. Hatching still needs a hatch check. Disabling this does not restore cycles already removed. |
| `instantHoneyTrees` | `true` | Unexpired, slathered trees become encounter-ready when checked again, skipping the six-hour wait. Already-slathered trees work too. Encounter tables, failure chances, Honey costs, and the 24-hour expiration remain. |
| `guaranteedFishing` | `true` | Guarantees a bite at valid fishing spots and automatically reels in the Pokémon. Encounter selection and battle capture rates remain unchanged. |
| `infiniteTMs` | `true` | Teaching a move does not consume its TM. Selling or discarding it still removes it. HMs remain reusable either way. |
| `levelUpHeldItemEvolutions` | `true` | Held-item evolutions, including held-item trade evolutions, trigger on the next level-up with the required item at any time of day. The item is consumed. Normal trade evolution remains available. |
| `instantSpiritomb` | `true` | Skips Underground conversations when encountering Spiritomb. An Odd Keystone is still required; already-inserted Keystones work, and each successful encounter still uses one. |
| `feebasWaterStoneEvolution` | `true` | Feebas evolves with a Water Stone instead of leveling up with sufficient Beauty. Disabling it restores Beauty evolution and removes the Water Stone alternative. |
| `disableOverworldPoisonDamage` | `false` | Skips walking poison damage and its visual/survival effects. Poison status and battle damage remain. |

## Nickname offers

**Tab → Speed & text → Ask for a nickname** controls automatic naming offers for caught Pokémon, hatched eggs (including Manaphy), your starter, gift Eevee/Porygon, and revived fossils:

- **Always** opens the keyboard directly, skipping the nickname question, backdrop, and caught-Pokémon slide. You can still leave without choosing a nickname.
- **Never** skips the entire nickname presentation, keeping the species name. New Pokédex entries still appear. Closing an entry finishes the catch directly, without creating the nickname screen; full-party catches go straight to the PC without an additional transfer screen. Previously registered species retain the normal PC transfer message.
- **Ask** (default) preserves the original Yes/No prompt.

Changes save automatically to `[General]` in `sim_config_prj.ini` as `NicknameMode=Ask`, `Always`, or `Never`, and apply to the next naming offer without restarting. Missing values default to Ask; invalid values are ignored. The Name Rater, fixed nicknames, and events without a naming offer retain their original behavior.

## Fast BW battle introductions

**Tab → Speed & text → Fast wild battle intro** is enabled by default for shorter wild encounters. This setting saves automatically to `sim_config_prj.ini` and applies to the next battle when BW2 visuals and extracted assets are available. It has no effect on Platinum visuals. An explicitly saved `FastWildBattleIntro=false` remains disabled. Trainer battles use their full introductions. The former `fastTrainerBattleIntro` gameplay option has been removed; existing entries are ignored.

**Tab → Speed & text → Disable low-HP beep** silences the low-HP warning in Platinum and BW2 battles. It applies immediately and saves to `sim_config_prj.ini`. The former `disableLowHPBeep` gameplay option has been removed; existing entries are ignored.

**Tab → Speed & text → Improved Pokétch** is enabled by default. It adds a Pokétch app after the retail apps that lists the wild Pokémon of the area you are in: grass or cave (with the current time of day's slots), Surf, and each rod. Tap the screen, or press A/Left/Right in the Pokétch menu, to switch method. Species you have not seen in the Pokédex show as a question mark; species you own get a Poké Ball mark. The app is never written to the save, so turning the option off removes it and saves stay DS-compatible. It saves to `sim_config_prj.ini` as `ImprovedPoketch`. The former `poketchRouteEncounters` gameplay option has been removed; existing entries are ignored.

**Tab → Speed & text → Single-screen bag** is enabled by default. With the single-screen (`top_only`) layout the bag shows each pocket as a grid of item icons, with the selected item's name and count below it; X or Y switches to a list of icons with names. Left and Right past the edge of the items change pockets, as L and R do. Turning it off keeps the classic bag. It applies the next time the bag opens and saves to `sim_config_prj.ini` as `GridBag`.

Fast openings skip the encounter text and opening camera shots, show the player throwing their Poké Ball and releasing their Pokémon at twice the usual effect speed, and then proceed to battle. Opponent and health-box setup still runs. The option applies only when BW battle view is active. Link and recorded battles, the first battle, catching tutorial, Safari Zone, Pal Park, and Giratina's special entrance retain their existing presentation. Later switches and battle animations are unaffected. The exact speedup varies with text, encounter animations, and system performance.

With BW2 battle visuals, a successful capture of a new species fades from the capture camera directly into its Pokédex entry. The camera return and the brief “data was added” message are hidden during this transition. Previously registered species and special capture modes keep their existing flow.

The nickname prompt uses the Black 2 / White 2 Pokédex backdrop when **BW2 battle visuals** is enabled and the backdrop is available. Use **Re-extract BW2 ROM** to add it to an older extraction.

## Developer encounter settings

**Tab → Developer → Overworld encounters → Overworld encounter count** controls the maximum number of visible wild Pokémon. It defaults to `8`, accepts `0–30`, and saves automatically as `OverworldEncounterCount` under `[Developer]` in `sim_config_prj.ini`. Changes affect new spawns immediately; existing Pokémon remain until they leave. `0` stops new spawns. Available space and map objects can limit the actual count. Overworld encounters must be enabled.

## Numeric options

Classic Platinum uses `100`, `48`, `0`, and `1` for every other numeric option.

| Option | Default | Accepted range | Effect |
| --- | --- | --- | --- |
| `bpPricePercent` | `25` | `0–200` | Percentage of original BP prices, rounded up. `50` halves prices; `0` makes them free. Rare Candy uses its separate price below. |
| `rareCandyBPPrice` | `1` | `0–100` | Exact Rare Candy BP price. Overrides `bpPricePercent`; `0` makes Rare Candy free. |
| `tradeEvolutionLevel` | `40` | `0–100` | `0` retains trade-only evolution. `1–100` also allows plain trade evolutions on level-up at or above that level. Held-item trades use `levelUpHeldItemEvolutions` instead. |
| `shinyRolls` | `16` | `0–100` | Total shiny rolls for newly generated eligible Pokémon. `0` and `1` keep original odds; extra rolls each have a 1/8192 chance. Existing Pokémon, fixed-personality gifts, and shiny locks are preserved, as are nature, gender, ability, and Wurmple's evolution branch. |
| `friendshipGainMultiplier` | `1` | `0–10` | Multiplies positive friendship gains after existing bonuses. `0` and `1` retain original gains. Friendship caps at 255; losses, initial friendship, and egg cycles are unaffected. |
| `eggHatchSpeedMultiplier` | `5` | `0–10` | Divides steps per hatch check, rounding down to at least one step. `5` reduces 255 steps to 51. `0` and `1` keep normal timing. Flame Body/Magma Armor bonuses still apply. With `zeroEggCycles`, eggs hatch at their next check. |
| `catchRateMultiplier` | `1` | `0–10` | Multiplies the final catch rate after ball, HP, status, and Safari modifiers. A resulting rate of 255 or higher guarantees capture. `0` and `1` keep original rates. Master Balls and guaranteed catches still work; trainer Pokémon cannot be caught. |

For example, `bpPricePercent=50` makes an ordinary 48 BP item cost 24 BP, but a 1 BP item still costs 1 BP because prices round up. Rare Candy ignores the percentage and always costs `rareCandyBPPrice`.

## Troubleshooting and source reference

If changes do not apply, restart the game, check the filename is exactly `gameplay_config.ini` (not `.ini.txt`), and verify it is in the working directory. Check `[Gameplay]`, option spelling, accepted values, and INI syntax. The loader falls back silently; try a minimal file like the partial example above to isolate a problem.

The authoritative defaults, option names, ranges, parser, template generator, and BP pricing are in [`src/port/gameplay/gameplay_config.c`](../src/port/gameplay/gameplay_config.c). The config structure and file path are in [`include/port/gameplay/gameplay_config.h`](../include/port/gameplay/gameplay_config.h). The existing config regression checks can be run against a configured Linux build with:

```sh
python3 tools/scripts/test_gameplay_config.py --build build_linux
python3 tools/scripts/test_gameplay_config_editor.py --build build_linux
python3 tools/scripts/test_battle_speed.py --build build_linux
python3 tools/scripts/test_nickname_prompts.py --build build_linux
```
