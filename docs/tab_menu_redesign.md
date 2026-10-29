# TAB menu redesign — proposal

Status: implemented with the clean dark style. TAB opens one centered panel with a category sidebar (a picker when space is limited), automatic pause, and a keep-running option. The target monitor setup has not been supplied; SDL auto scaling and a manual override are available.

## Goals

- Open one predictable menu with TAB. Selecting a category replaces the content in the same panel.
- Keep the panel centered and inside the game window when resizing or switching monitors.
- Make text, controls, spacing, and hit targets readable at the user's display scale.
- Organize settings by purpose instead of by the library that owns them.
- Preserve existing settings and tool functionality.

## Findings in the current code

- `subprojects/libntr/libraries/sim/src/sim_gui.cpp` creates the `libntr` launcher. Each `AppButton` has its own open flag and draws an independent window.
- `src/port/sim_gui_prj.cpp` adds a second launcher, `pokeplatinum`, with more independent windows. A typical settings operation therefore leaves multiple overlapping windows open.
- Neither launcher specifies a deliberate panel position or size. Buttons often use fixed dimensions such as 100 × 20 or 80 × 20.
- GUI initialization has no application-level font/style scale setup. `ImGuiIO igIO = ImGui::GetIO()` also copies the IO object, so the intended keyboard-navigation flag is not applied to the context.
- The SDL backend reports framebuffer scale and provides content-scale helpers, but the application does not use those helpers to size its UI. The backend itself notes limitations in Linux DPI detection. A reliable manual override is needed alongside automatic scaling.
- `sim_gui_config.cpp` mixes display, audio, frame limiting, and fast-forward. `gui_configPrj.cpp` mixes frame rate, battle timing, BW2 assets, assertions, graphics preferences, and rendering diagnostics.
- Input capture currently has special handling for rebinding and some keyboard paths, rather than one consistent menu-input policy. Review all game input paths during the redesign.
- Pause already exists as a manual game-logic toggle. Opening TAB does not automatically enable it; closing the menu does not clear it.

## Proposed structure

| Category | Contents |
| --- | --- |
| Display | Fullscreen/windowed, screen layout, screen swap where applicable, internal resolution, VSync, menu scale |
| Graphics | Day/night lighting, atmosphere, model lighting, character shadows, particles, water, weather, bloom, BW2 battle visuals and asset selection |
| Audio | Existing master volume control |
| Controls | Keyboard and controller subpages; DS button names with their actions; primary/secondary directions; deadzone; PC shortcuts |
| Gameplay | All 23 gameplay config values, descriptions, explicit Save/Reload/Defaults, and restart status |
| Game speed | 30/60 FPS, battle pacing, normal battle UI timing, frame cap, fast-forward multiplier |
| Cheats & tools | Existing cheats, Pokétch unlocks, Pokémon creation, item creation, map jumping as subpages within the panel |
| Multiplayer | Existing host/connect controls and connection state |
| Developer | Frame/GPU/water timings, renderer information, emission view, lighting preview/reload, assertions, build hashes, ImGui demo |

The Audio page is deliberately small because the current runtime exposes only master volume. Do not imply separate music/effects mixers already exist.

Keep game-state-dependent tools visible with a short availability reason where appropriate. Selecting a creation or map tool opens its content in place, with a Back action. File selection and genuine confirmations may use a focused modal.

## Panel and interaction

- Use one root panel with a title, sidebar, scrollable page content, and footer. Give page content the available space; avoid per-page floating windows.
- Size from the available game viewport and effective UI scale, with a comfortable maximum width. On small windows or large scaling, reduce margins and reflow navigation/rows before clipping controls.
- Remember the last selected category and per-page scroll position. Tool form values should survive category changes during a session.
- Use labels on the left and consistently aligned controls on the right, with short inline explanations for ambiguous options.
- TAB closes the panel. Escape cancels binding capture or closes the active dialog/subpage before closing the panel. Resolve keyboard navigation's use of TAB explicitly; use arrows/controller for category navigation and preserve the existing menu-toggle shortcut.
- Consume game actions while interacting with the menu, including when game logic is deliberately left running. Clear held gameplay inputs on transitions and handle releases so movement cannot become stuck.
- Keep current automatic persistence for existing settings. Clearly label any future setting that requires restart.

## Scaling

- Add an independent menu-scale preference: Auto and an explicit override, initially 100–200% in 25% steps. Validate whether a broader range is useful on the target setup.
- Auto should follow the platform's effective content scale without applying framebuffer scaling twice. Reevaluate it after monitor/display changes.
- Apply the chosen scale to fonts, padding, row heights, buttons, sidebar width, and relevant dialog sizes. Base each recalculation on an unscaled reference style to prevent cumulative scaling.
- Use font/spacing-relative sizing instead of retaining literal 20-pixel button heights.
- Handle DPI awareness and high-density rendering at the SDL/window level where required. Verify actual behavior on Windows and Linux; physical DPI and desktop fractional scaling are not interchangeable.
- Keep internal rendering resolution independent. Raising game sharpness must not be the way to enlarge menu text.

## Pause policy — selected

Pause game logic while the menu is open, with a visible option to keep it running for visual previews and diagnostics. Show the current state in the header.

Separate user-requested manual pause from menu-induced pause so closing TAB removes only the pause it owns. Verify multiplayer behavior before applying automatic pause to an active connection; a menu should not silently stall a session.

## Visual direction

The implementation uses the clean modern direction. A future skin can retain the same layout:

- Clean modern: restrained neutral surfaces, clear typography, aligned rows, subtle selection highlight.
- Platinum-inspired: recognizable framed surfaces and blue accents, while retaining readable scalable type and the same sidebar/page structure.

The conversation concept demonstrates the first direction and grouping only. Its browser interactions do not change game settings.

## Implementation sequence

1. **Panel foundation:** replace launcher/open-window state with active-page state. Split page contents from `ImGui::Begin/End`, introduce a small library/project page registration or callback boundary, and retain library fallback behavior for other applications. Fix the IO reference, add shared sizing and menu-scale persistence, and establish input ownership.
2. **Migrate settings:** regroup existing controls, preserve existing config keys and immediate runtime effects, move BW2 setup into Graphics, and isolate diagnostics in Developer. Make file dialogs use the same scale and stay inside the viewport.
3. **Migrate tools:** embed cheats, creation forms, map jumping, and multiplayer in the same panel. Preserve initialization requirements and unsaved form state; show availability based on actual game state.
4. **Polish and verify:** settle theme and pause defaults, add keyboard/controller navigation as agreed, update README menu paths, and test scaling, input, settings persistence, and tool behavior.

The redesign does not require replacing ImGui or rewriting the game renderer. The work is mainly a host/page refactor plus consistent sizing and input behavior.

## Larger optional changes

- **Gameplay configuration page — implemented:** all options from `gameplay_config.ini`, with explicit saving, restart status, comment/unknown-entry preservation, and protection against overwriting external edits. Draft and saved rules remain separate from the running game.
- **Setting search:** useful if gameplay options expand the menu considerably. Results should navigate to the actual control and use the same page metadata.
- **Graphics presets:** useful after establishing measured combinations and indicating when individual settings create a custom preset.
- **Full controller navigation:** especially useful for Steam Deck/couch use. Explicitly separate navigating the menu from capturing a new binding.

Prioritize the single panel, scale, and grouping first. Those directly address the reported friction without requiring these expansions.

## Acceptance checks

- Every existing setting/tool is reachable without arranging floating windows.
- All controls remain reachable at 1024 × 576, 1280 × 800, 1920 × 1080, and 3840 × 2160, including common fractional scaling and manual overrides. Scroll content rather than hiding actions.
- Move between monitors with different scale factors; toggle fullscreen; resize while open. Text stays sharp and the panel remains within bounds.
- Changing internal resolution leaves menu scale unchanged. Auto/manual menu scaling persists across restart.
- Menu clicks, typing, and controller navigation do not move the player or activate DS buttons. Opening/closing while holding a direction does not leave input stuck.
- Binding capture can receive TAB/Escape according to its documented policy without accidentally closing the menu or storing an unintended binding.
- Menu pause and manual pause behave independently, including close/reopen and connection changes. Rendering and menu input continue while logic is paused.
- Existing INI values survive migration. Graphics, audio, timing, and input changes still take effect and save as before.
- Tools work only in valid game states, retain form state across page changes, and do not require a second launcher.
- Build the Linux target and Windows target when available; perform focused runtime checks for layout/input/DPI rather than relying on compilation alone.

## Implementation and validation

The project pages use the shared SDK panel through SIM::GUI::ProjectPage.
The SDK changes are tracked in subprojects/packagefiles/libntr/tab-menu.patch
and applied by subprojects/libntr.wrap on fresh dependency checkouts.

Run **python3 tools/scripts/test_menu_gui.py --build build_linux** after building.
It exercises real SDL/OpenGL/ImGui code offscreen without a ROM, checks all nine
categories at four window sizes and six scale modes, and covers config migration,
invalid scale values, input capture, binding cancellation, key repeat, and pause
ownership/network policy. The existing controller regression suite remains
**python3 tools/scripts/test_gamepad_input.py --build build_linux**.

Actual game captures were also reviewed for the settings and tool pages. Physical
monitor switching and Windows DPI behavior still need testing on those displays.
The gameplay editor's file behavior is covered by
**python3 tools/scripts/test_gameplay_config_editor.py --build build_linux**.
Search, presets, and additional controller shortcuts remain optional follow-up features.


