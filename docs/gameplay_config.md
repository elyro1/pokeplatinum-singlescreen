# Gameplay configuration

The PC port creates `gameplay_config.ini` with its built-in defaults on first start, then reads gameplay overrides from it at startup. Existing files are never overwritten. Built-in defaults preserve original Platinum gameplay: every boolean modifier is off, BP prices are unchanged, and all multipliers are 1. You do not need to rebuild the game to change these settings.

## Create and use a config

1. Start the game once to generate `gameplay_config.ini` in the game's **working directory**, alongside `sim_config_prj.ini` and your saves. This is normally the game folder. If you launch from a terminal, it is the directory you launch from, which can differ from the executable's directory.
2. Edit the generated file in a text editor. Keep the `[Gameplay]` section and spell option names exactly as shown.
3. Restart the game to apply changes.

The generated file includes comments explaining every option. If the working directory is not writable, the game still starts with its built-in defaults.

You can also copy [`gameplay_config.example.ini`](../gameplay_config.example.ini) over the generated file. The repository example is a ROM hack-style quality-of-life preset, rather than a defaults template. It enables reusable TMs, compatible-party HM use, free move relearning and shard tutors, earlier tutor access, easier evolutions, unlimited Great Marsh steps, instant Honey Trees and Spiritomb, vitamins past 100 EVs, and no walking poison damage. It also halves ordinary BP prices, prices Rare Candy at 1 BP, doubles friendship gains, and speeds up hatch checks by 5× while retaining normal egg cycles. Battle items, the saved battle style, and catch/shiny rates retain their original behavior.

The tables below describe the built-in defaults, not the preset values. For a smaller quality-of-life setup, you can instead create this partial file:

```ini
[Gameplay]
forgettableHMs=true
unlearnedHMs=true
freeMoveRelearning=true
infiniteTMs=true
tradeEvolutionLevel=40
```

Every omitted option retains its built-in default. Delete or rename `gameplay_config.ini` and restart to regenerate it with built-in settings. This resets configuration, but does not undo gameplay changes already saved, such as evolutions or removed egg cycles.

## Fixed defaults for ROM hacks

Set your hack's defaults in `GameplayConfig_LoadDefaults` in [`src/port/gameplay_config.c`](../src/port/gameplay_config.c). In `GameplayConfig_Init` in the same file, comment out this single call and rebuild:

```c
    // GameplayConfig_LoadFile();
```

The game then uses only your code defaults. It neither generates nor reads `gameplay_config.ini`, so even an existing or manually created file cannot override your settings. Leave `GameplayConfig_LoadDefaults(&sGameplayConfig);` enabled. The libntr menu no longer contains a gameplay config generation button.

## File format and loading rules

- Use one `name=value` entry per line under `[Gameplay]`. Section and option names are case-sensitive.
- Booleans accept `true`, `false`, `TRUE`, `FALSE`, `1`, or `0`. Mixed-case values such as `True` are ignored.
- Numbers must be non-negative decimal integers within the option's range. Negative numbers, fractions, and values outside the range are ignored.
- Use lines beginning with `;` for comments, as in the example.
- Missing options retain defaults. Unknown options and other sections are ignored. Invalid values are ignored, leaving the current value for that option (normally its default). If an option is repeated, the last valid value wins.
- A malformed INI file, such as a line with no `=`, causes **all overrides to be discarded**. An unreadable file or failure to create a missing file also uses all defaults.
- Settings are read at startup, so saving the file during play requires a restart. Existing files are never automatically updated with new defaults or options.

## Boolean options

All options in this table default to `false`. Set an option to `true` to enable the described behavior.

| Option | Behavior when enabled |
| --- | --- |
| `prohibitBattleItems` | Blocks player Bag items in battle except Poké Balls. Held-item effects and opponents' items still work. |
| `forceSetBattleStyle` | Always uses Set style, suppressing the Shift prompt regardless of the saved Options selection. |
| `unlimitedVitaminEVs` | Vitamins can raise a stat past 100 EVs, up to the normal 255 per-stat and 510 total EV limits. EV-reducing berries retain normal behavior. |
| `forgettableHMs` | HM moves can be replaced when learning another move, including after leveling up in battle. |
| `unlearnedHMs` | Compatible non-egg party members can use HMs without learning them or owning the HM item. Interact with obstacles or use **HMs** in the Pokémon's party menu. Badge and location restrictions still apply; non-HM field moves still need to be learned. |
| `freeMoveRelearning` | The Pastoria City Move Maniac requires and consumes no Heart Scale. Other move tutors are unaffected. |
| `freeMoveTutors` | All three shard move tutors require and consume no shards. Their displayed costs and greetings follow this setting. |
| `mainStoryMoveTutors` | The Route 212 tutor offers all three shard tutors' move lists, including Survival Area moves. Compatibility and known-move checks still apply. Use `freeMoveTutors` separately to remove shard costs. |
| `unlimitedGreatMarshSteps` | Stops counting Great Marsh steps and disables the 500-step exit. The entry fee and Safari Ball limit remain. Disabling this resumes counting from the saved counter. |
| `zeroEggCycles` | New eggs have zero remaining hatch cycles; existing party eggs are set to zero at their next hatch check. Hatching still needs a hatch check. Disabling this does not restore cycles already removed. |
| `instantHoneyTrees` | Unexpired, slathered trees become encounter-ready when checked again, skipping the six-hour wait. Already-slathered trees work too. Encounter tables, failure chances, Honey costs, and the 24-hour expiration remain. |
| `infiniteTMs` | Teaching a move does not consume its TM. Selling or discarding it still removes it. HMs remain reusable either way. |
| `levelUpHeldItemEvolutions` | Held-item evolutions, including held-item trade evolutions, trigger on the next level-up with the required item at any time of day. The item is consumed. Normal trade evolution remains available. |
| `instantSpiritomb` | Skips Underground conversations when encountering Spiritomb. An Odd Keystone is still required; already-inserted Keystones work, and each successful encounter still uses one. |
| `feebasWaterStoneEvolution` | Feebas evolves with a Water Stone instead of leveling up with sufficient Beauty. Disabling it restores Beauty evolution and removes the Water Stone alternative. |
| `disableOverworldPoisonDamage` | Skips walking poison damage and its visual/survival effects. Poison status and battle damage remain. |

## Numeric options

| Option | Default | Accepted range | Effect |
| --- | --- | --- | --- |
| `bpPricePercent` | `100` | `0–65535` | Percentage of original BP prices, rounded up and capped at 65535 BP. `50` halves prices; `0` makes them free. Rare Candy uses its separate price below. |
| `rareCandyBPPrice` | `48` | `0–65535` | Exact Rare Candy BP price. Overrides `bpPricePercent`; `0` makes Rare Candy free. |
| `tradeEvolutionLevel` | `0` | `0–100` | `0` retains trade-only evolution. `1–100` also allows plain trade evolutions on level-up at or above that level. Held-item trades use `levelUpHeldItemEvolutions` instead. |
| `shinyRolls` | `1` | `0–65535` | Total shiny rolls for newly generated eligible Pokémon. `0` and `1` keep original odds; extra rolls each have a 1/8192 chance. Existing Pokémon, fixed-personality gifts, and shiny locks are preserved, as are nature, gender, ability, and Wurmple's evolution branch. |
| `friendshipGainMultiplier` | `1` | `0–255` | Multiplies positive friendship gains after existing bonuses. `0` and `1` retain original gains. Friendship caps at 255; losses, initial friendship, and egg cycles are unaffected. |
| `eggHatchSpeedMultiplier` | `1` | `0–255` | Divides steps per hatch check, rounding down to at least one step. `5` reduces 255 steps to 51. `0` and `1` keep normal timing. Flame Body/Magma Armor bonuses still apply. With `zeroEggCycles`, eggs hatch at their next check. |
| `catchRateMultiplier` | `1` | `0–255` | Multiplies the final catch rate after ball, HP, status, and Safari modifiers. A resulting rate of 255 or higher guarantees capture. `0` and `1` keep original rates. Master Balls and guaranteed catches still work; trainer Pokémon cannot be caught. |

For example, `bpPricePercent=50` makes an ordinary 48 BP item cost 24 BP, but a 1 BP item still costs 1 BP because prices round up. Rare Candy stays at 48 BP unless you also change `rareCandyBPPrice`.

## Troubleshooting and source reference

If changes do not apply, restart the game, check the filename is exactly `gameplay_config.ini` (not `.ini.txt`), and verify it is in the working directory. Check `[Gameplay]`, option spelling, accepted values, and INI syntax. The loader falls back silently; try a minimal file like the partial example above to isolate a problem.

The authoritative defaults, option names, ranges, parser, template generator, and BP pricing are in [`src/port/gameplay_config.c`](../src/port/gameplay_config.c). The config structure and file path are in [`include/port/gameplay_config.h`](../include/port/gameplay_config.h). The existing config regression checks can be run against a configured Linux build with:

```sh
python3 tools/scripts/test_gameplay_config.py --build build_linux
```
