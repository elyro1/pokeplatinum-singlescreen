# Pokémon Platinum PC Port — Single-Screen Mod

This is a mod for the native PC port of Pokémon Platinum, built on top of **[pret's Pokémon Platinum decompilation](https://github.com/pret/pokeplatinum)** and **[cybervisi0n's Pokémon Platinum PC port](https://github.com/cybervisi0n/pokeplatinum)**.

The focus here is a widescreen, single-screen experience, with adapted interfaces, expanded overworld rendering, and quality-of-life improvements.

**You must provide your own dumped US Pokémon Platinum ROM (CPUE01).** Game assets are extracted from that ROM on first launch. For the full experience, you also need a BW2 ROM.

## Setup Guide
To run the [pre-built binaries found in the releases tab](https://github.com/elyro1/pokeplatinum-singlescreen/releases/), **you MUST dump and provide your own US ROM of Pokémon Platinum (CPUE01).** 

1. Open the project's GitHub Releases page and download the ZIP archive compatible with your operating system (Windows or Linux).
2. Extract the downloaded ZIP archive into a new folder.
3. Copy the US ROM of Pokémon Platinum into the same folder as the extracted files. Ensure it is placed directly alongside the main executable, not inside a subfolder.
4. (Optional) Copy the US ROM of Pokémon B2/W2 into the same folder as the extracted files. 
5. Launch the game by running main.exe

If there is no ROM , you will be prompted to select your .nds ROM file as seen in the images below:

![ROM extraction dialog](images/RomExtraction.png)

* Access in-game settings and debug tools by pressing TAB key

## Features

Configurable gameplay modifiers below are opt-in through `gameplay_config.ini`; built-in gameplay defaults follow original Platinum behavior.

- **Single-screen presentation:** all screens reworked to make the game playable with just the top screen
- **Widescreen rendering:** broader overworld views, adjusted world
- **Black 2 / White 2 like battle visuals**
- **Three save slots:** select a slot from the main menu
- **Pokétch in the in-game menu:** open it after acquiring it, switch apps with L/R, and return to the game with B or X.
- Overworld graphics: richer day/night lighting, improved model lighting, additional post-processing effects, soft character shadows
- [Gameplay config](docs/gameplay_config.md) for independent gameplay overrides. Play the game how you want to play it

## Visual upgrades

Richer day/night lighting, regional haze, and smooth 3D edges are enabled by default.
Toggle them under Tab → Application → Config → Graphics.
See [graphics notes](docs/visual_upgrades.md).

## How to run

Use a build from this fork's [Releases](https://github.com/elyro1/pokeplatinum/releases) or [GitHub Actions artifacts](https://github.com/elyro1/pokeplatinum/actions). Place your dumped US Platinum `.nds` ROM beside the executable and launch the game: missing assets are extracted automatically. Filenames do not matter; ROMs are identified by their internal game codes. If no compatible ROM is found, the game opens the usual file picker.

Optionally place your dumped **US Black 2 or White 2** `.nds` ROM beside the executable too. On startup, the game extracts the required battle assets into `bw2/` and automatically enables **BW2 battle visuals**, saving the setting. Existing assets are reused. While a compatible BW2 ROM remains present, startup enables the setting again; move the ROM away if you want to keep BW2 visuals disabled. Platinum is still required.

Discovery checks the executable's directory first, then the working directory. Extraction uses the working directory, alongside settings and saves; normally this is the game folder. You can also select a BW2 ROM manually under **Tab → libntr → Application → Config**.

## Default keybindings

These are the defaults for a fresh configuration. Existing settings retain your previous bindings. Open **Tab → Input** to remap keyboard and controller buttons.

| Key | DS button / action |
| --- | --- |
| Arrow keys | D-pad: move / navigate |
| Z | A: confirm / interact |
| X | B: cancel / run where available |
| C | X: open / close the in-game menu |
| V | Y: use a registered item |
| Q | L; previous Pokétch app while the Pokétch is open |
| E | R; next Pokétch app while the Pokétch is open |
| Enter | Start |
| Right Shift | Select |
| Tab | Toggle PC settings / debug tools |
| Left Shift (hold) | Fast-forward (5× by default) |
| F11 | Toggle fullscreen |
| Left mouse button | Touch input where supported by the displayed interface |

Inside the Pokétch, **X** or **C** on the keyboard closes it (DS B or X).

Battle Frontier scratch-off cards support buttons in the single-screen layout: use left/right to pick cards, then the D-pad to select a square and A to scratch it. B skips a card or finishes the last card after confirmation; X opens the prize list, and B or X returns. The on-screen hints use DS button names (A = Z, B = X, X = C with the default keyboard bindings).

In **Tab → Input → Gamepad**, each direction has Primary and Secondary bindings. The D-pad is Primary and the left stick is Secondary by default (axis 0 for left/right, axis 1 for up/down), so either control works for movement and menus. Click **Clear** to remove a secondary binding; press Escape to cancel controller binding capture. Saved mappings, including cleared secondary bindings, are preserved; configurations without secondary bindings inherit the left-stick defaults.

The **Axis deadzone** slider applies to all mapped controller axes and saves automatically. Its default is 25%; set it to 0% to disable filtering, or increase it to prevent stick drift. **Reset** restores the default. Movement remains digital, with the original game’s directional behavior and speed.

### Settings

The default layout shows one screen, with 60 FPS and Smooth battle pacing enabled, and Normal Battle UI & Timing disabled by default. Speed correction is automatic at 60 FPS. Existing configuration files may override these defaults.

Press **Tab** to access:

- **libntr → Config:** screen layout, fullscreen, internal resolution, volume, frame pacing, and fast-forward speed (1×–10×).
- **libntr → Input:** keyboard and controller mappings.
- **libntr → Application → Config:** frame rate (30/60 FPS), battle pacing (Original/Smooth/Quick), and the independent Normal Battle UI & Timing checkbox. Smooth keeps original animation timing with sprite interpolation; Quick accelerates battle effects while keeping Pokémon idle animations and idle camera movement at Smooth speed. The checkbox keeps HP/EXP bars, messages and UI delays at normal speed when enabled. See [battle pacing settings](docs/battle_speed_settings.md) for details.
- **libntr → Application tools:** cheats, Pokémon and item generation, and map jumping while a game is loaded.

Settings are stored locally in `sim_config.ini` and `sim_config_prj.ini`. If performance is poor, lower the internal resolution scale in the PC settings.

## Saves

The three slots use `save1.bin`, `save2.bin`, and `save3.bin` in the game folder. Save through the normal in-game Save action before quitting.

The native game also autosaves the active slot every two minutes of real time.
During dialogue, menus, battles, transitions, communication or special activities,
it waits until normal overworld control resumes at a tile boundary. Each autosave
archives the previous slot file in the game folder's `autosave/` directory, for
example `autosave/save1.bin.2026-10-28_14-30-00-0000.bak`. Only the newest ten
archives per slot are kept; older ones are deleted after a successful autosave.
To restore one, close the game and copy it over the corresponding `save1.bin`,
`save2.bin` or `save3.bin`. Autosaves use a temporary file and atomic replacement;
a failed save leaves the active file intact. Manual saves work as usual.

An older upstream `save.bin` is copied into slot 1 when `save1.bin` does not already exist. The original file is retained. Back up your save files before updating builds or deleting a slot.

## Building from source

For a Windows executable built locally on Linux, see [the MinGW build guide](docs/build_mingw.md).

Refer to the [Building from source](BUILD.md).

## Credits and upstream projects

- **[pret/pokeplatinum](https://github.com/pret/pokeplatinum):** Pokémon Platinum decompilation, documentation, and the original source tree.
- **[cybervisi0n/pokeplatinum](https://github.com/cybervisi0n/pokeplatinum):** the native PC port on which this fork is built.
- **[cybervisi0n/libntr](https://github.com/cybervisi0n/libntr):** the NitroSDK replacement libraries powering the port.
- **[elyro1/libntr](https://github.com/elyro1/libntr/tree/lib_single_screen_mod):** the modified library branch used by this fork.
- **[fuddlesworth/pokebw2](https://github.com/fuddlesworth/pokebw2/commits/main/):** BW2 battle engine.

Please report issues specific to this fork in [this repository's issue tracker](https://github.com/elyro1/pokeplatinum/issues). The original PC port's community is available on its [Discord](https://discord.gg/ZgtPszuBeN).
