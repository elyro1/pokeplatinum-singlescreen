# Battle speed test matrix

Use **60 FPS**, normal fast-forward speed, and battle animations enabled.
Select presentation in **Tab → General** and speeds in **Tab → Speed & text**.
Start a new battle after changing presentation. Keep shortened BW2 intros off
for the core matrix; their separate cases are below.

## Core matrix: 26 short battles

Run each row once with Platinum visuals and once with BW2 visuals. Rows 1–9
cover the Fast, Faster and Instant text choices with each pacing and bar setting;
row 10 checks all controls at their fastest, and rows 11–13 cover Normal text.
This reduces the manual core pass from 72 combinations to 26 runs. It does not
provide exhaustive interaction coverage.

Use the same save and a repeatable ordinary wild encounter where possible.
Let both sides take damage, defeat the opponent, and watch EXP and battle exit.

| ID | Battle pacing | Text | HP/EXP bars | Platinum | BW2 |
| --- | --- | --- | --- | --- | --- |
| 1 | Smooth | Fast | Fast | [ ] | [ ] |
| 2 | Smooth | Faster | Faster | [ ] | [ ] |
| 3 | Smooth | Instant | Normal | [ ] | [ ] |
| 4 | Quick | Fast | Faster | [ ] | [ ] |
| 5 | Quick | Faster | Normal | [ ] | [ ] |
| 6 | Quick | Instant | Fast | [ ] | [ ] |
| 7 | Turbo | Fast | Normal | [ ] | [ ] |
| 8 | Turbo | Faster | Fast | [ ] | [ ] |
| 9 | Turbo | Instant | Faster | [ ] | [ ] |
| 10 | Turbo | Instant | Faster | [ ] | [ ] |
| 11 | Smooth | Normal | Normal | [ ] | [ ] |
| 12 | Quick | Normal | Fast | [ ] | [ ] |
| 13 | Turbo | Normal | Faster | [ ] | [ ] |

For every run, check:

- Effects finish, sprites remain correctly positioned, and the next command menu appears.
- Text is complete and readable, without skipped messages or acknowledgements.
- HP bars and numbers agree; fainting occurs at zero HP.
- EXP reaches its correct value, and battle exit returns control to the field.
- No hangs, lingering Poké Balls, incorrect visibility, stuck damage flashes, or missing healthboxes.

Expected differences:

| Control | Expected result |
| --- | --- |
| Smooth → Quick | Effects become faster. Text and bars retain their selected speeds. |
| Quick → Turbo | Effects become 1.5× faster than Quick; presentation waits cap at four updates and fades become near-instant. Text and bars retain their selected speeds. |
| Text Normal → Fast → Faster → Instant | Character reveal becomes faster, then immediate for each page. Prompts, callbacks, and authored pauses still work. |
| Bars Normal → Fast → Faster | Progression becomes faster at each setting. Numeric HP and completion events stay correct. |
| Platinum → BW2 | Different presentation; the selected text/bar progression follows the same policy. BW2 idle motion does not accelerate with Quick/Turbo. |

## Focused cases

These supplement the core matrix. Do not repeat each case across all 72 settings.

| Priority | Case | Settings / presentation | Pass condition | Done |
| --- | --- | --- | --- | --- |
| P0 | Low- and high-maximum HP; one-point damage and full drain | Both visuals; Normal and Faster bars | Correct final numbers; no overshoot; Faster large changes finish in about half a second at 60 FPS. | [ ] |
| P0 | Healing and HP colour thresholds | Both visuals; Normal and Faster bars | Correct green/yellow/red fill and numeric HP; BW2 healing does not leave damage feedback active. | [ ] |
| P0 | EXP crossing one and multiple levels | Both visuals; Normal and Faster bars | Each level is awarded; stats and move-learning prompts appear; EXP continues into the next segment. | [ ] |
| P0 | Text override outside battle | Normal, Fast, Faster, Instant; saved in-game text set to Slow | Tab setting wins in ordinary field dialogue and battles. | [ ] |
| P0 | Multi-page text and a sound/fanfare callback | Instant text; both visuals where applicable | Current page reveals immediately; acknowledgements and sound waits remain effective. | [ ] |
| P0 | Live setting changes | Both visuals; change pacing during an effect, text while printing, bars during a drain | No resets, duplicated messages, lost HP/EXP, visual jumps that persist, or hangs. | [ ] |
| P0 | Successful and failed Run | Both visuals; Smooth, Quick, Turbo; same Text speed | Quick/Turbo immediately start fading after a successful Run, with no escape message/pause. Smooth retains the message and A can skip its pause. Failed/trapped attempts still show their message. Sprites, shadows, scene and music fade out before returning to the field. | [ ] |
| P0 | Successful capture and breakout-to-message delay | Both visuals; Quick and Turbo | Successful catches finish without hanging on the ball. Failed catches show their message after the breakout without replaying the entrance animation. Inter-shake holds retain their original timing on the selected animation clock. | [ ] |
| P0 | Save/restart and menu pause | Set Turbo / Faster text / Normal bars; restart | All choices persist; closing Tab restores the selected pause behavior and game input. | [ ] |
| P1 | Multi-hit moves, recoil, drain, poison/burn and weather damage | Both visuals; row 10 | Each damage/heal event resolves in order; menus and messages remain usable. | [ ] |
| P1 | Switching, faint replacement, capture and failed capture | Both visuals; row 10 | Balls, cries, sprites, gauges and subsequent turns finish correctly. | [ ] |
| P1 | Double/tag battle, including spread damage | Both visuals; row 10 | Correct battlers and bars update; simultaneous effects and replacement sequences complete. | [ ] |
| P1 | Long/complex attack effects and battle animations disabled | Both visuals; Smooth and Turbo | Effects complete correctly when enabled; disabled animations do not stall the turn. | [ ] |
| P1 | Link / recorded battle, if supported fixtures are available | Both visuals; Quick versus Turbo | No timing-related stalls, replay divergence, or communication failure; link script waits retain their timing. | [ ] |

## BW2 intro cases

Wild intro changes apply to the next battle. Use **Quick / Instant text /
Normal bars** unless stated otherwise. Trainer battles always keep their full
openings.

| Encounter | Intro setting | Expected result | Done |
| --- | --- | --- | --- |
| BW2 ordinary wild | Wild shortening off | Full opening. | [ ] |
| BW2 ordinary wild | Wild shortening on | Encounter text/opening camera shots skipped; faster player release; gauges appear correctly. | [ ] |
| BW2 shiny wild, shiny player, both shiny, and double wild with one/both opponents shiny | Wild shortening on; Smooth, Quick, Turbo | Each shiny sparkles once with sound. Wild sparkles overlap the player throw; no separate sparkle pause or duplicate hidden entrance delay. Player sparkles remain visible after release. | [ ] |
| BW2 ordinary, major and multi-trainer battles | Wild shortening on/off; legacy `fastTrainerBattleIntro=all` entry | Full openings for every trainer; the removed setting is ignored. | [ ] |
| BW2 shortened wild opening | Repeat with Smooth and Turbo | Same shortening policy; correct send-out completion with both pacing modes. | [ ] |
| BW2 shortened wild opening with a long cry, such as Luxray | Smooth and Quick | The first command menu appears after the visual cleanup while the cry continues; audio finishes and music volume recovers normally. | [ ] |
| Platinum wild and trainer | Wild shortening on | Standard Platinum openings; the BW2 wild intro option has no effect. | [ ] |
| First battle, catching tutorial, Safari, Pal Park, Giratina, link and recorded battles | Shortening enabled; use available fixtures | Existing special openings remain intact. | [ ] |

## Existing automated coverage

`test_battle_speed.py` checks 72 configuration/policy combinations, 1,728 shared
gauge scenarios, text rendering boundaries, and offline/link script waits. These
are isolated tests, not 72 played battles or exhaustive BW2 renderer coverage.
It also checks all negative palette delay values in both fade directions and 36
capture-sprite/menu/exit cases, including inactive sprites with unfinished fades
and live pacing changes.
Capture-ball regressions cover all eight commands retaining completion signals
across extra animation steps, plus repeated shakes clearing the previous signal.

```sh
python3 tools/scripts/test_battle_speed.py
python3 tools/scripts/test_battle_gauge.py
python3 tools/scripts/test_battle_intro.py
python3 tools/scripts/test_menu_gui.py
python3 tools/scripts/test_gameplay_config.py
python3 tools/scripts/test_gameplay_config_editor.py
```

Record failures with the row/case ID, visual style, all three speed values,
intro options, battle type and move/event, and a save or reproducible sequence.
Mark unavailable special-battle fixtures as untested rather than passed.

## Turbo softlock audit — 2026-11-01

Runtime smoke checks used temporary copies of the packaged game and save, with
Turbo at 60 FPS. Both Platinum and BW2 visuals completed successful Master Ball
captures through the Pokédex/nickname-decline flow and back to the field, failed
Poké Ball captures back to the next turn, Potion healing, X Attack, and switching.
Item checks confirmed restored HP or the raised attack stage; switching confirmed
a different active party slot. The Platinum recall crash reproduced before the
particle resource-ordering fix and completed afterward.

These representative checks do not complete the full matrix or cover every item,
ball, nickname-entry/PC-transfer branch, or special battle type.

## Capture completion and breakout audit — 2026-11-02

A Turbo successful-catch hang was reproduced with a Great Ball: the hidden ball
had advanced past state 2 to state 4 while the capture controller still awaited
state 2. The regression test fails on the previous code and passes with latched
completion milestones.

Debugger-driven smoke checks in temporary game/save copies completed successful
Great Ball captures and failed captures in Quick and Turbo with both Platinum
and BW2 visuals (eight runs). Outcomes were forced to exercise both branches.
Successful catches completed the nickname-decline flow and battle teardown; one
BW2 run also exercised the Pokédex entry. Failed catches printed their breakout
message and returned to the next turn. BW2's message followed its completed ball
effect within a few updates. These checks do not cover every ball, species,
nickname-entry branch or special battle type.

## Shortened opening cry audit — 2026-11-02

The Luxray/Fearow matchup reproduced an extra cry-completion hold after the
visible send-out settled. At 60 FPS, the Smooth command menu appeared 51 frames
earlier after overlapping that cry; Smooth and Quick both reached command
selection two updates after visual cleanup. The cry was still playing at menu
readiness and had finished, released its voice, and restored music volume 120
updates later. The unshortened opening retained its cry wait. Intro regressions
also cover delayed cry cues, trainer waits and all 65,536 wild shiny masks.
