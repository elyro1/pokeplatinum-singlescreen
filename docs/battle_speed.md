# Battle speed settings

Open **Tab → Speed & text**. Changes apply immediately and save to
`sim_config_prj.ini`. The supported tuning baseline is 60 FPS. The retained
30 FPS mode disables the battle-pacing control.

| Setting | Options | Behavior at 60 FPS |
| --- | --- | --- |
| Battle pacing | Smooth / Quick / Turbo | Smooth keeps original effect durations with sprite interpolation. Quick runs Platinum's effects at twice the original update rate, doubles BW2 effect updates, and shortens escape pauses and battle exit fades. Turbo runs effects at three times the original update rate, caps presentation pauses at four updates, and uses near-instant fades. |
| Text speed | Normal / Fast / Faster / Instant | Normal matches the original in-game Normal text timing at both 30 and 60 FPS. Fast uses the existing accelerated character printing. Faster prints twice as many characters per printer update. Instant reveals the current page in one update, stopping at prompts, scrolling, scripted pauses, and callbacks. |
| HP/EXP bar speed | Normal / Fast / Faster | Normal advances at half the speed of Fast. Fast uses the existing accelerated bars. Faster advances four times as far per update, with large changes capped at 30 progression updates (about half a second). |

Defaults are **Smooth**, **Fast text**, and **Fast bars**. Text speed overrides
the saved in-game text option throughout the PC port, including overworld
dialogue. Existing immediate/static labels keep their original rendering path.
Text still respects sound callbacks, page acknowledgements, and authored pauses.
Post-message battle-script pauses belong to battle pacing, separately from the
character reveal speed.

HP damage, healing, numeric HP, and experience progression use the shared
Platinum healthbox calculation. BW2 displays mirror those values, so neither
presentation applies a second bar-speed multiplier. EXP still progresses through
each level's segment and its existing level-up sequence.

Turbo runs Platinum animation owners at 90 updates per second and BW2 effects
at 180 updates per second, compared with Quick's 60/120. Turn logic, input,
text and HP/EXP bars retain their own clocks. BW2 ambient motion keeps its normal
speed. Animation task ordering and cleanup are preserved across the extra steps.

Turbo quarters battle-script presentation waits, Bag message delays, alert-message
delays and the pause after the EXP message, rounding up and capping them at four
updates. Required prompts, sound callbacks, shiny sparkles and battle messages
remain. Menu and battle exit fades finish in about two progression updates;
Quick keeps its existing fades. Link battle-script waits and menu/exit fades
keep their existing timing and button-skip policy.

After a failed capture, Quick and Turbo omit the extra entrance animation before
the breakout message. At every pacing setting and frame rate, BW2 skips
Platinum's hidden send-out, since BW2 has already shown the Pokémon breaking
free and returned the camera. Link timing is unchanged. Ball-shake holds keep
their original timing on the selected animation clock. Capture completion milestones remain
latched across Turbo's extra animation steps so the controller cannot miss them.

In Quick and Turbo, a successful **Run** starts the exit fade immediately,
skipping the escape message and its pause. Escape checks still apply; failed
attempts show their normal message. Smooth retains the escape message and its
30-update pause, which A/B/X/Y or touch can skip. Other escape messages, such
as a Poké Doll or a fleeing wild Pokémon, retain their shortened pauses in
Quick and Turbo. Battle exit fades and their music fade finish sooner in Quick
and Turbo, with palette and Pokémon/shadow fades completing before cleanup.
Messages that are shown still follow Text speed. Link/Frontier forfeit dialogue
and 30 FPS mode retain their existing behavior.

## BW2 intro shortening

**Tab → Speed & text** offers a fast wild battle intro toggle. It saves
automatically and applies to the next battle when BW2 visuals are active; it
has no effect on Platinum visuals or trainer battles. Trainer battles use their
full introductions. Special-battle exceptions and the faster player send-out
are preserved.
The wild opponent's cry starts as its entrance begins, without waiting for the
hidden Platinum sprite slide or its animation-specific cry delay.
Wild shiny sparkles and their sound play during the player's send-out, using
the normal BW2 sparkle effect without adding a separate animation wait. Shiny
Pokémon also skip the duplicate hidden Platinum entrance animation.
The send-out cry continues over the first command menu instead of holding the
completed scene on screen. The shortened opening still waits for its visual
effects, and scheduled cries start before it finishes. Full openings retain their
existing sound waits.
See [the gameplay configuration documentation](gameplay_config.md#fast-bw-battle-introductions).

## Configuration compatibility

The `[General]` section of `sim_config_prj.ini` stores:

```ini
60Fps=true
BattlePacing=Smooth
TextSpeed=Fast
BattleBarSpeed=Fast
FastWildBattleIntro=false
```

`BattlePacing=Original` migrates to Smooth. Old `BattleAnimationSpeedFix=false`
maps to Quick when no valid pacing preset is present; `true` maps to Smooth.
`SmoothBattleAnimations` and `BattleUiSpeedFix` are ignored and no longer written.
Old `BattleBarSpeed=Instant` entries migrate to Faster. Missing speed entries use Fast; invalid entries retain the previously loaded
value or default. The independent normal-speed UI toggle has been removed.

## Verification

After a Linux build:

```sh
python3 tools/scripts/test_battle_speed.py --build build_linux
python3 tools/scripts/test_battle_gauge.py --build build_linux
python3 tools/scripts/test_battle_intro.py --build build_linux
python3 tools/scripts/test_menu_gui.py --build build_linux
```

The speed tests cover config migration and round trips, effect-clock and speed
independence, live pacing changes, HP/EXP progression for all six healthbox types,
low and high maximum values, fainting, healing, EXP level segments, instant text
pages, acknowledgements, timed pauses, callbacks, and offline/link script waits.
Exit checks also cover pacing, 30 FPS/link timing, and palette/sprite completion
before cleanup. They run the real palette fade code in both directions and check
all 128 negative delay values against step overflow. Capture checks run the real
sprite-capture task, shared Bag/party menu fades and exit cleanup across 36
pacing/frame-rate/live-change cases. Deleted sprites cannot hold scene fades open.
Ball tests cover all eight capture commands passing a completion milestone between
controller updates, including repeated shakes. Breakout tests check that
Quick/Turbo Platinum captures omit the extra entrance animation and BW2 captures
skip the hidden send-out at every pacing setting and frame rate; cries, ordinary
send-outs and link behavior retain their existing policy.
Animation-clock tests cover native sprites/particles, 30/60/90 update counts,
input edges, task deletion/reuse, nested animation owners, and live pacing changes.
They also check particle resources are ready when two animation steps share a
frame, before a deferred post-VBlank upload could have run.
The existing BW2 gauge and send-out tests cover visual feedback and cleanup.
The menu test renders the actual Speed & text controls across window sizes and scales.
