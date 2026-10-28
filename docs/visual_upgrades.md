# PC graphics enhancements

The twelve options under **Tab → Graphics** are
enabled by default, including when an older settings file lacks the new keys.
Changes apply during play and are saved in `sim_config_prj.ini`:

```ini
[Graphics]
EnhancedDayNight=true
RegionalAtmosphere=true
Antialias3D=true
CorrectLighting=true
EnvironmentalParticles=true
SoftContactShadows=true
CastShadows=true
WaterEffects=true
WeatherEffects=true
NightLighting=true
NightGlows=true
EmissiveBloom=true
```

- **Richer day/night lighting** crossfades the original lighting templates over
  30 minutes centered on each time boundary. Short intervals use shorter blends.
  Outdoors, one color grade shapes those palettes over the day
  (`FieldVisuals_Grade`): exposure, saturation and contrast, then a split tone
  that tints shadows and highlights separately. Day is the original look;
  dawn gets peach light and lavender shadows, sunset warm light with cool
  shadows instead of one sepia cast, night stays dark blue while keeping
  materials apart. Rain dulls and flattens the colors; snow keeps light crisp.
  The grade replaces the earlier stacked tint, ambient and key-light
  multipliers. Emissive lights bypass it, and haze is graded with the scene.
  Midnight wraps continuously, and interior lighting stays neutral.
- **Regional atmosphere** adds distance haze: green forest/marsh mist,
  cool snow and mountain haze, blue coastal/lake haze, and subdued cave haze.
  Rain uses a cooler, denser profile. Outdoor haze dims at night and takes the
  scene's grade when richer lighting is enabled. The foreground remains clear.
- **Smooth 3D edges** uses up to 4× MSAA on the world render target, resolving
  color and authored fog separately before the 2D compositor. It also averages
  the high-resolution scene when downsampling, so high internal resolutions
  benefit even when the multisample allocation exceeds the memory budget.
  Original model textures retain nearest sampling; text and UI layer textures
  retain their existing filters.

- **Correct lighting** decodes signed 10-bit light/normal vectors without
  truncation, captures light directions when submitted, and evaluates directional
  specular highlights and the model's shininess table. Disable it to compare the
  previous backend lighting. Outdoors, the primary sun/moon direction is fixed
  northwest to match authored ground shadows; light colors still follow time.
  Interior and scripted directions retain their authored settings. Models
  drawn scaled up (the map terrain, at 32x) keep the full diffuse and
  specular light the field's look was tuned with, on every face turned
  towards a light, but ramp off smoothly within 20 degrees of where it ends,
  instead of switching faces between fully lit and unlit in patches. Painted texture
  shading and ground shadows remain part of the assets. This affects models that
  use hardware lighting.
- **Environmental particles** adds drifting leaves and nighttime fireflies in
  forests, falling flakes in snow regions, motes in caves/coastal areas, and ground
  splashes during ordinary rain. Positions use stable world coordinates and local
  collision heights, without consuming gameplay randomness.
- **Soft character shadows** replaces ordinary character blob shadows with a
  ellipse with a dense core and a narrow feather. It uses the original shadow
  renderer's animation position, model bounds and time-dependent scale so its
  anchor matches the default shadow. Neighboring tiles alternate polygon IDs,
  so shadows of objects side by side blend instead of cutting each other off. Jumping broadens and fades it;
  nighttime reduces its strength. Special shadow variants retain their models.
  On steps, which are steep ramps across one tile, both character shadows
  sit on the collision slope sampled around them and tilt with it, instead
  of sinking into the ramp. In tall grass, where the game hides its shadow
  blob, the soft and cast shadows still show (the blob stays hidden); water,
  snow, mud and reflective floors keep hiding them.
- **Character cast shadows** lays each character's current sprite frame flat
  on the ground as a dark silhouette. On clear days the sun casts it
  southeast, the same way as the buildings' painted shadows: shortest at
  noon (half the sprite's height along each axis), longer towards sunrise
  and sunset, fading with the sun at dusk. Rain, snow and other weather
  cast none; walking across a map border into other weather (for example
  onto the rainy Route 215) fades the shadow out or in over 1.5 seconds.
  Night lights cast none (they were distracting while
  running past). The sprite's model is drawn a second time with its
  camera-facing billboard command skipped and a shear that maps its height
  onto the ground, so animation frames and every character sprite work
  without new assets. Its outline softens with distance from the feet, like
  a penumbra. Outdoors only; characters on water cast none. Nearby trees and
  ledges hide the shadow; on steps it tilts with the slope.
- **Water shimmer and ripples** lights collision-marked water with slow, soft
  light bands and sparse pixel-sized sparkles that twinkle in and fade out. The
  pattern is computed in world space on the DS pixel grid, so it runs across
  tiles without seams and matches the pixel art. Each tile corner fades out
  unless all four surrounding tiles are water, so the effect never spills onto
  banks that share a tile with the water. Expanding rings follow the player
  through water; shorelines clip each ring, and bridges and waterfalls are
  excluded. Water is bounded to 2048 visible loaded tiles in one draw and 16
  simultaneous rings; particles use 25 nearby world cells. Like every overlay,
  water carries the scene's authored fog (snow, rain), so it never cuts
  unfogged patches into it.

- **Weather effects** gives the shader each pixel's world position (from
  its clip position and the field camera; the screen-space derivatives of
  that position give the surface normal), so weather stays put on the
  ground as the camera moves:
  - In rain, terrain and buildings get darker and richer in color. Up-facing
    paths and sand (`lgreen`, `nsand`, `l02_road`) collect small puddles on
    the DS pixel grid, each inside one 16-unit tile so it never runs into a
    tile of another material, styled like the game's own puddles (light sky blue
    over the ground, more opaque along the edge), with rain drops landing in
    them. Water and the game's puddle tiles show rain drops too. The ground
    takes 4.5 seconds to get wet or dry when crossing into or out of rain.
  - Mist drifts low over the ground, settled around the player's height so
    hollows hold more of it: fine drifting snow in snow regions (more while
    it snows), morning mist in forests and over lakes and coasts that
    gathers before sunrise and burns off by mid-morning, and thin spray in
    rain. It covers surfaces and sprites and thins shadows and water
    shimmer rather than brightening them.
- **Coherent night lighting** separates sun/moon intensity, authored
  directional-shadow opacity, contact-shadow strength and emission.
  Dawn blends from 05:00–07:00; dusk from 18:00–20:00. Night brightness and
  color come from the grade above.
  Explicit `h_kage` shadow materials and the `shadowchip` shadows under
  bridges and boardwalks fade to 4% at night; contact shadows remain.
  Tree shadows fade to 35%, so trees stay grounded and dense tree rows keep
  their gaps filled. They are painted two ways: `tshadow` is an opaque grass
  tile with a darker disc, which lifts towards the tile's lit corner color
  (tiles whose corners or center are not uniform, such as the Great Marsh
  grass, stay as they are). The tree atlases (`conttree_b`, `conttree2_b`,
  `tree01`, `tree04_2`, `tree2_01`) paint a disc around each stump in a
  32×32 stump cell: a cell whose center is red-brown stump with disc on
  both sides; its non-red texels inside the disc's ellipse fade to show the
  ground. Palm stumps and Eterna Forest's gray stumps fail that test
  and keep their discs.
  Other shadows painted into textures and untagged materials retain their assets.
- **Night window and lamp glow** makes explicitly named materials emit light
  at night. Tagged `h_mado` glass glows warm. Light fixtures keep their own
  colors: the Pokémon Center (`light_a`) and Poké Mart (`light2`) signs and roof
  trim, house windows the game already lights at night (`light`/`Light`, e.g.
  Twinleaf and Sandgem), Jubilife's lit building windows (`c1_lamp01/02`) and
  red roof beacons (`lamp01`/`lamp03`), Canalave's canal bollards
  (`c2_light_a/b`) and the Battle Frontier plaza lamps (`bf_light*`). Lamp
  meshes that share one texture between pole and glass emit only their
  near-white texels: Jubilife street lamps (`c1_o02`) and Hearthome and Amity
  Square globe lamps (`c5_light`). The Cycling Road lights are downlights:
  the lens under each head (`lambert14`, scoped to Route 206's land mesh,
  where it is used for nothing else) glows, and a soft, blooming cone falls
  from it, widening and dissolving into a warm pool. Each pool covers the
  light patch the game paints on the road (`lambert13`, one flat quad per
  lamp; the table holds its center and height). It follows the deck as it
  climbs and levels out: a strip of quads on collision heights sampled from
  below the head, 3 units up, or on the patch's plane where the visible road
  runs further above its collision (the two-lane row under the tunnels). A light stays on while either its
  head or its pool is in view.
  Twinleaf and Sandgem house windows are 40% dimmer than elsewhere.
  Buildings whose lights share a texture or a generic material name are tagged
  per prop model: every Gym's emblem, the Veilstone Game Corner (crown sign
  and blinking door bulbs), the Sunyshore Market windows, the Pokémon League
  emblem, the Battle Frontier Tower windows, Hall light pillars, sign and
  windows (Tower and Hall windows are dimmer), Factory light strips and Arcade
  neon arches, the Route 210 Café windows, and the Trainers' School Poké Ball
  emblem (only its yellow texels emit). The Game Corner door bulbs are the
  brightest lights (bloom 3). In Jubilife, office windows that stayed dark, the round
  tower's window bands, the Global Terminal logo, Pokétch Co.'s sign and
  windows, and the Jubilife TV screen, which flickers gently, light up.
  Keys pick the lit parts of a shared texture: near-white glass, only the
  brightest texels for light bars, or saturated texels for neon bulbs and
  colored window panes. Vista Lighthouse turns two light beams every eight
  seconds; its lantern flares whenever a beam faces the camera.
  Stark Mountain's lava pool and stream (prop 584, `magma`, seen from
  Route 227) glow in their own colors by day and night, a little brighter
  at night, pulsing slowly; bloom runs by day on those two maps. Their falling
  ash keeps the game's own haze and colors, but the effects (lava glow,
  soft shadows, water) run in it.
  Emission bypasses daylight tint and preserves dark frame details.
  Untagged materials never emit.

  **The light table** is `field_lights.txt` in the game's folder (source:
  `res/pc_port/field_lights.txt`, its format documented at the top). It
  lists the night materials (shadows that fade, glowing materials, puddle
  ground, water), the lamps placed in land meshes, the lights on props and
  the maps where something glows by day. Entries can be scoped to a map or
  a prop model. The game reads it at start and again from **Reload light
  table** in the Developer panel, so it can be tuned while playing; the
  panel shows where the table came from. A file with an error is reported
  there by line and the previous table stays; a missing file falls back to
  a built-in copy. The build copies the source file into the game's folder
  when the source changes.
  Terrain light pools use bounded attachment metadata: all 16 Jubilife street
  lamps and the 12 Cycling Road lights (positions read from the land mesh,
  the `c1_o02b` glass and the `r206_pol2` heads, since the lamps are not
  props), the circular front window of Eterna's `c4_h01`
  house near tile **317,520** (west of the statue), and the Pokémon Center
  entrance. Characters standing in a pool take on its warm light: their
  sprite blends from the night grade towards its own colors lit warm, up to
  70% close to the source, fading with distance and with the night lights at
  dawn. Walls are not relit.
- **Emissive bloom** spreads light from those materials into a soft halo that
  follows their actual shape: the Center trim reads as neon, windows and lamps
  glow. The 3D shader writes emission to a separate buffer, where nearer
  surfaces occlude it. The buffer is box-filtered to one texel per DS pixel and
  blurred through six half-resolution levels, so the halo radius and the cost
  of each blur pass stay fixed at any internal resolution. The result is screen-blended onto
  the 3D layer after fog, so UI, menus, text and 2D sprites are never affected
  and bright sources do not clip to white. The passes run only on night field
  frames that opt in. Each material has its own bloom strength in the table:
  2 for windows and lamps, 0.85 for the large Center and Mart signs so their
  halo does not swamp the building. Emission is stored relative to the
  strongest value (`FIELD_BLOOM_PEAK`). With bloom disabled, windows and the
  Center logo keep their original restrained halos.
  At night, water mirrors this glow: lamps, windows, signs and Canalave's
  canal bollards make wavy columns in the water in front of them, in their
  own color. While a field frame draws, libntr records glowing polygons
  (merged into lights about a tile across, colored by their texture's
  brightest texels; lit windows glow warm) and level water surfaces in world
  space, on screen or not. Water is the land meshes' `sea`, `lake`, `asasea`
  and `bf_lake` materials, so scenery water that cannot be surfed reflects
  too. For the 48 lights nearest the middle of the screen, each reflection
  sits on the water right below its light, or, for a lamp on a bank within
  12 units of the water, 9 units onto the nearest water. Lights set back
  from the water, such as the Canalave Gym's roof lights, do not reflect in
  it. The placement depends only on the light, so reflections stay put as
  the camera moves. The water material draws a column of dashes there that
  reaches down the screen, at most about three tiles long, on the DS pixel
  grid: each two-pixel row shifts sideways and changes length six times a
  second, the light catches mostly on the crests of the water's own painted
  waves, rows drop out further from the light, the column tapers and fades,
  its start glows nearly white, and a faint haze of the light's color
  surrounds it. It uses the previous frame's lights and water, in
  world space, so the delay does not show, and reflections stay while their
  light is off screen. It needs water shimmer and night glows.

Forests with the dappled overlay and exterior lakes receive outdoor lighting
although their map headers classify them as caves. The Developer panel shows
active particles, water tiles and ripples, the AA allocation and night-source
count. **Show emission only** is a session-only debugging view; **Measure water
timings** enables asynchronous GPU queries and displays CPU submission and
shimmer draw timings. Resume game logic if paused when changing time/effects.

Authored fog, Flash darkness, blizzards, sandstorms, supernatural weather,
Distortion World, encounter transitions and scenes that suspend area lighting
retain their existing atmosphere. Buildings do not receive regional haze.
Orthographic cameras retain their original fog because perspective clip W is
needed for the new distance calculation.

MSAA falls back to 2× or the original render target on unsupported GPUs or when
its additional buffers would exceed about 85 MiB per attachment (color, fog,
emission and depth: about 341 MiB in all). At the existing default 10×
internal resolution, scene downsampling supplies the edge improvement without
allocating large multisample buffers. For less GPU work, try 2×–4× internal
resolution with smooth edges enabled.

## Debug time preview

Under **Tab → Developer**, click **Cycle time of
day** to preview **Night (00:00) → Dawn (06:00) → Day (12:00) → Sunset (18:00)
→ Real time**. The displayed label shows the active preview. **Use real time**
restores the clock immediately. The preview resets on restart and affects
lighting, haze and cosmetic effects only; saved time, encounters and other clock-based gameplay
continue to use the real game clock. If game logic is paused in the settings UI,
resume it to see the selected lighting. Compare the same outdoor scene with
**Richer day/night lighting** enabled and disabled.

## Where to compare

| Feature | Good comparison |
| --- | --- |
| Regional atmosphere | Eterna Forest or Snowpoint City; compare distant trees/terrain at the top of the view. City haze is intentionally lighter. Authored fog takes priority. |
| Smooth 3D edges | Diagonal roofs, lamp posts and fences in Jubilife or Canalave at 2×–4× internal resolution. At 10×, high-resolution rendering already hides much of the aliasing. Pixel-art textures and sprites retain their edges. |
| Correct lighting | Lit building faces, trees and rotating 3D models; compare with richer day/night disabled first, then cycle time. |
| Particles | Eterna Forest by day for leaves; Night preview for fireflies. Snowpoint for flakes, a regular cave for motes. |
| Contact shadows | A town pavement with several NPCs; watch the feet while walking/jumping. Toggle Soft character shadows. |
| Cast shadows | Jubilife's central crossing by day and at sunset; compare with Soft character shadows on and off. Toggle Character cast shadows. |
| Water | Canalave canal or Lake Verity shoreline for glints; Surf across water or walk through shallow water/puddles for rings. |
| Weather | Route 215 in rain (puddles on the worn path, drops on them); Lake Verity or Eterna Forest's outskirts in the Dawn preview for mist. |

## Implementation and validation

Game-side math is in `src/port/field_visuals.c`. Field rendering selects the
region, time and scene eligibility; the 3D shader applies tint/haze before the
existing authored fog and background composition. Enhancements reset to neutral
for each new frame until the field explicitly opts in. The Nintendo DS target
keeps the original rendering path.

Backend changes are committed in libntr on the `lib_single_screen_mod` branch.
`subprojects/libntr.wrap` pins the SDK commit directly, including the visual
upgrades, extended battle VRAM, matrix and DMA behavior, widescreen edge
rendering, compressed-texture cache support, simulator automation hooks, and
gamepad settings. The tracked subprojects/packagefiles/libntr/tab-menu.patch
adds the centered PC settings panel and input/scaling behavior through Meson's
diff_files mechanism on fresh SDK checkouts. For an existing clean checkout at
the pinned revision, apply it once from the project root with:

```sh
git -C subprojects/libntr apply ../packagefiles/libntr/tab-menu.patch
```

Android compatibility changes live on separate Android branches.

After building the Linux executable, run:

```sh
python3 tools/scripts/test_field_visuals.py --build build_linux
python3 tools/scripts/test_lighting_correctness.py --build build_linux
```

The first suite samples all four real lighting archives at every second of the
day, checks midnight and palette continuity, verifies protected weather and old
configuration migration, and runs actual shaders and multisample resolve on
desktop OpenGL. GPU checks cover near/far haze, both depth modes,
authored fog, edge coverage, resize, toggling, downsampling and memory fallback. Effect shader checks cover radial shadow falloff, ring lifetimes, shimmer animation,
particle fade and restoring ordinary model materials. The lighting suite exercises
the real CPU decoder, all signed 10-bit normals, submission-time transforms,
directional specular and packed shininess-table command framing. SDL's offscreen
driver with Mesa can run these checks without opening a window.

Visual acceptance in game should include an outdoor town at dawn/sunset,
Eterna Forest, a snow route, a coast, a cave, a Flash area, authored fog and a
Distortion World scene. Check both native and wide views, battle/menu transitions,
and toggling each graphics option while playing.

## Frame profiling

**Measure frame timings** is a session-only Developer control. It shows CPU work,
total frame duration, asynchronous GPU elapsed time and result age, plus the
active renderer. CPU/GPU work overlaps; do not add the numbers. A delayed query
may describe an older frame. See `night_performance.md` for the night rendering
investigation, limits and reproduction command.

## Water profiling

Water scans each candidate tile once, then submits geometry through a native
batched path that reuses bucket capacity and the renderer's GPU vertex buffer.
It no longer constructs individual Nitro command messages per vertex. The
existing clipping, depth planes, translucency and polygon-ID rules are retained.
CPU timing includes cache rebuilds and interaction-ring submission; GPU timing
sums only the shimmer draw batches, excluding rings, resolve and compositing.
GPU timers are opt-in and read asynchronously.

The Linux benchmark uses a copied executable/assets/save, disables VSync and
frame caps, warms up each phase, alternates water off/on twice, and records
whole-frame work/GPU elapsed median and p95. Unavailable GPU results are omitted,
not counted as zero. The headless EGL buffer is flushed without a blocking wait.

```sh
python3 tools/scripts/benchmark_field_water.py --build build_linux \
  --output /tmp/water-benchmark.json
# Optionally add --baseline /path/to/an/older/main to compare implementations.
```

See `docs/water_performance.md` for the measured hardware/results and limits.
