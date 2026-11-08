# Pokémon Platinum PC Port — Single-Screen Mod

Pokémon Platinum on PC, widescreen, on one screen.
This is a mod for the native PC port of Pokémon Platinum. The focus here is a widescreen, single-screen experience, with adapted interfaces, expanded overworld rendering, and quality-of-life improvements.

You must provide your own dumped US Pokémon Platinum ROM (CPUE01). Game assets are extracted from that ROM on first launch. For the full experience, you also need a BW2 ROM.

## Start playing

You need a **dumped US Pokémon Platinum ROM (CPUE01)**. It is not included.

1. Download the ZIP for Windows or Linux from [Releases](https://github.com/elyro1/pokeplatinum-singlescreen/releases/).
2. Extract it into a new folder.
3. Put your Platinum `.nds` file in that folder, right next to the executable.
4. *Optional, recommended:* add a **US Black 2 or White 2** `.nds` too. This turns on BW2 battle visuals.
5. Run `main.exe` (Windows) or `main` (Linux).

Done. Assets extract by themselves on first launch. 
Press **Tab** for settings.

- **ROM filename?** Doesn't matter.
- **No ROM found?** A file picker opens. Choose your `.nds`.
- **Running slow?** Lower the internal resolution under Graphics.
- **Bug in this fork?** Use the [issue tracker](https://github.com/elyro1/pokeplatinum-singlescreen/issues).

## Features

- **Single-screen:** every screen reworked to play on just the top screen
- **Widescreen:** wider overworld view, adjusted world
- **Black 2 / White 2 style battles**
- **Three save slots:** pick one from the main menu
- **Pokétch in the in-game menu:** once you have it, switch apps with L/R, leave with B or X
- **Better graphics:** richer day/night lighting, improved model lighting, regional haze, smooth 3D edges, post-processing, soft character shadows
- **[Gameplay config](docs/gameplay_config.md):** independent gameplay overrides, so you can play how you want

Visual upgrades are **on by default**. Toggle them under **Tab → Graphics** ([graphics notes](docs/visual_upgrades.md)).

## Keys

| Key | Does |
| --- | --- |
| **Arrow keys** | Move / navigate (D-pad) |
| **Z** | Confirm / interact (A) |
| **X** | Cancel / run where available (B) |
| **C** | Open / close the in-game menu (X) |
| **V** | Use a registered item (Y) |
| **Q** / **E** | L / R, or previous / next Pokétch app |
| **Enter** | Start |
| **Right Shift** | Select |
| **Tab** | Settings and debug tools |
| **Left Shift** (hold) | Fast-forward (5× by default) |
| **F11** | Fullscreen |
| **Left mouse** | Touch, where the screen supports it |

**Remap:** Tab → Controls. These are fresh-install defaults; existing settings keep your old bindings.

<details>
<summary>Controller setup</summary>

- Found in **Tab → Controls → Controller**.
- Each direction has a Primary and a Secondary binding.
- Primary is the D-pad. Secondary is the left stick (axis 0 left/right, axis 1 up/down). Either one works for movement and menus.
- **Clear** removes a secondary binding.
- **Escape** cancels binding capture.
- Saved mappings are preserved, including cleared secondary bindings.
- Configurations without secondary bindings inherit the left-stick defaults.

</details>

## Settings (press Tab)

Out of the box: one screen, 60 FPS, Smooth battle pacing, Fast text, Fast HP/EXP bars. Speed correction is automatic at 60 FPS. Existing config files may override these.

| Page | What's there |
| --- | --- |
| **General** | Fullscreen, menu scale, master volume, ROM info, BW2 battle visuals |
| **Graphics** | Internal resolution, VSync, frame cap, lighting, atmosphere, shadows, water, weather, bloom |
| **Speed & text** | Fast-forward speed, fast wild intros (BW2), battle pacing, text speed, HP/EXP bar speed, low-HP beep toggle |
| **Controls** | Keyboard and controller mappings, directions, deadzone, PC shortcuts |
| **Gameplay** | All gameplay overrides |
| **Cheats & tools** | Cheats, Pokétch unlocks, Pokémon/item creation, map jumping |
| **Multiplayer** | Host/connect settings, player info |
| **Developer** | 30/60 FPS, screen layout and swap, frame/GPU timings, lighting previews, assertions, build info, ImGui demo |


<details>
<summary>Speed & text options</summary>

| Setting | Options |
| --- | --- |
| Battle pacing | **Smooth:** original effect timing with sprite interpolation. **Quick:** faster effects. **Turbo:** 3× effects, minimal pauses, near-instant fades. |
| Text | Normal, Fast, Faster, Instant. Overrides the in-game setting, in and out of battle. |
| HP/EXP bars | Normal, Fast, Faster |

Works with both Platinum and BW2 visuals. More in [battle speed settings](docs/battle_speed.md).

</details>

<details>
<summary>Changing bindings, gameplay options and tools</summary>

**Bindings:** select one, press the new input. Escape cancels. The window stays responsive during capture.

**Gameplay page** has battle rules, moves/tutors, evolution/eggs, encounters/events, and progression/prices.

- Edit values, choose **Save changes**, restart the game.
- **Reload / discard** reloads the file.
- **Defaults** stages the built-in defaults until you save.
- More in [gameplay configuration](docs/gameplay_config.md).

**Cheats & tools:** creation and travel need normal overworld control.

</details>

<details>
<summary>How the menu behaves</summary>

**Layout**

- It is one centered panel. Categories replace the current page, and tools stay inside the panel.
- It remembers your category and scroll positions during the session.
- On small windows or large menu scales, categories collapse into a picker and pages scroll.

**Pausing**

- **Pause while open** is off by default.
- Closing with Tab, Escape, or Resume clears the automatic pause.
- **Developer → Manual pause** is separate and stays active after closing the panel.
- Automatic pause is suspended while hosting or connected to multiplayer. Menu input still stays out of gameplay.
- Escape cancels a binding or dialog, or returns from a tool, before it closes settings.

**Menu scale** (General)

- Auto, or 100–200% in 25% steps.
- Auto uses the display scale SDL reports. Use a manual value if yours is reported wrong.
- Fonts, spacing, controls, and dialogs scale together.
- Internal resolution changes game sharpness separately.

</details>

<details>
<summary>Where settings are stored</summary>

- PC settings save automatically to `sim_config.ini` and `sim_config_prj.ini`.
- The `[Interface]` section of `sim_config.ini` stores `MenuScalePercent` (0 = Auto) and `PauseWhileOpen`.
- Files without those values get Auto and no automatic pause.
- Gameplay overrides use **Save changes** and load from `gameplay_config.ini` at startup.

</details>

## 💾 Saves

- **Save in-game before you quit.** Slots are `save1.bin`, `save2.bin`, and `save3.bin` in the game folder.
- **Back up your saves** before updating builds or deleting a slot.
- Loading a slot always uses your last **manual** save. Autosaves are separate.

<details>
<summary>Autosaves and restoring one</summary>

**What happens**

- A separate autosave is made every 2 minutes of real time.
- It goes in `autosave/`, for example `autosave/save1.bin.2026-10-28_14-30-00-0000.bak`.
- The newest 10 per slot are kept. Older ones are deleted after a successful autosave.
- During dialogue, menus, battles, transitions, communication or special activities, it waits for normal overworld control at a tile boundary.
- Autosaves never replace or create your normal slot files.
- Autosaves use a temporary file and atomic publication, so a failed one leaves existing saves intact.

**Restore one**

1. Close the game.
2. Copy or move the autosave over the slot file.
3. Rename it to `save1.bin`, `save2.bin`, or `save3.bin`.

**Turn it off:** **Tab → Developer → Saving → Autosave**. It applies immediately and is stored as `[Developer] Autosave` in `sim_config_prj.ini` (default: `true`).

</details>


## Build from source

- [Building from source](BUILD.MD)
- [Windows executable, built on Linux (MinGW)](docs/build_mingw.md)

## Credits

| Project | Role |
| --- | --- |
| [pret/pokeplatinum](https://github.com/pret/pokeplatinum) | Decompilation, documentation, original source tree |
| [cybervisi0n/pokeplatinum](https://github.com/cybervisi0n/pokeplatinum) | The native PC port this fork is built on |
| [cybervisi0n/libntr](https://github.com/cybervisi0n/libntr) | NitroSDK replacement libraries powering the port |
| [fuddlesworth/pokebw2](https://github.com/fuddlesworth/pokebw2/commits/main/) | BW2 battle engine |
