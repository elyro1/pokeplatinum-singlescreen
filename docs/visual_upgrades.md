# PC graphics enhancements

> **TL;DR**
> - 12 options under **Tab → Graphics**. All **on** by default.
> - Saved in `sim_config_prj.ini` `[Graphics]`. Changes apply live.
> - DS build: untouched, original rendering.

## The 12 settings

| Menu name | Key | One line |
| --- | --- | --- |
| Richer day/night lighting | `EnhancedDayNight` | Smooth time-of-day blends + a color grade per time of day |
| Regional atmosphere | `RegionalAtmosphere` | Distance haze that fits the region |
| Smooth 3D edges | `Antialias3D` | MSAA + better downsampling |
| Correct lighting | `CorrectLighting` | Fixes the backend's light math, adds specular highlights |
| Environmental particles | `EnvironmentalParticles` | Leaves, fireflies, snow, motes, rain splashes |
| Soft character shadows | `SoftContactShadows` | Feathered blob under each character |
| Character cast shadows | `CastShadows` | Sprite-shaped sun shadow |
| Water shimmer and ripples | `WaterEffects` | Light bands, sparkles, rings when you move through water |
| Weather effects | `WeatherEffects` | Wet ground + puddles in rain, low mist |
| Coherent night lighting | `NightLighting` | Real dusk/dawn, shadows fade at night |
| Night window and lamp glow | `NightGlows` | Windows, signs, lamps light up |
| Emissive bloom | `EmissiveBloom` | Soft halo around glowing things + reflections on water |

An older ini without these keys still gets them all on.

---

## What each one does

### Richer day/night lighting
- Crossfades the original lighting templates over **30 min** around each time boundary (shorter for short intervals).
- One outdoor color grade (`FieldVisuals_Grade`): exposure → saturation → contrast → split tone.
  - **Day:** original look.
  - **Dawn:** peach light, lavender shadows.
  - **Sunset:** warm light, cool shadows (not one sepia cast).
  - **Night:** dark blue, materials still readable.
  - **Rain:** duller, flatter. **Snow:** crisp.
- Glowing materials skip the grade. Haze gets graded with the scene.
- Midnight wraps smoothly. Interiors stay neutral.

### Regional atmosphere
- Distance haze by region:
  - forest/marsh → green mist
  - snow/mountain → cool haze
  - coast/lake → blue haze
  - cave → subdued haze
- Rain = cooler and denser. Dims at night. Foreground stays clear.
- **Not applied to:** buildings, authored fog, Flash darkness, blizzards, sandstorms, supernatural weather, Distortion
  World, encounter transitions, scenes that suspend area lighting, orthographic cameras.

### Smooth 3D edges
- Up to **4× MSAA** on the world target; color and fog resolve separately.
- Also averages the high-res scene when downsampling, so high internal resolutions win even without MSAA.
- Textures stay nearest-sampled. UI keeps its filters.
- **Fallback:** 2×, then none, on weak GPUs or when buffers would pass ~85 MiB each (~341 MiB total).
- At the default 10× the downsampling does most of the work. Want less GPU load? Use **2×–4×** with this on.

### Correct lighting
- Decodes signed 10-bit light/normal vectors properly (no truncation).
- Adds directional specular + the model's shininess table.
- Outdoor sun/moon direction is **fixed northwest** to match painted ground shadows. Colors still follow time.
- Interior and scripted light directions: unchanged.
- Scaled-up models (terrain at 32×) keep full light on faces toward a light, then ramp off within **20°**. No patchy
  lit/unlit faces.
- Only affects models that use hardware lighting.

### Environmental particles
- Forest: leaves (day), fireflies (night).
- Snow regions: flakes. Caves/coast: motes. Rain: ground splashes.
- World-anchored and on collision height. Uses **no** gameplay randomness.

### Soft character shadows
- Replaces the blob shadow with a feathered ellipse.
- Same anchor as the original (animation position, model bounds, time-based scale).
- Side-by-side shadows blend instead of cutting each other off (alternating polygon IDs).
- Jump → wider and fainter. Night → weaker. Indoors → fixed strength.
- On steps: tilts with the slope.
- Tall grass: still shows (the original blob stays hidden). Water, snow, mud, reflective floors: hidden.

### Character cast shadows
- The character's current sprite frame, laid flat as a silhouette, cast **southeast** like the painted building shadows.
- Shortest at noon (half the sprite height per axis), longer at sunrise/sunset, fades at dusk.
- **None** in rain, snow, other weather, at night, indoors, or on water.
- Crossing into other weather (e.g. rainy Route 215) fades it over **1.5 s**.
- How: redraw the sprite model with the billboard command skipped + a shear onto the ground. Works for every sprite and
  frame, no new assets.
- Edge softens away from the feet. Trees and ledges hide it. Tilts on steps.

### Water shimmer and ripples
- Soft light bands + pixel sparkles on collision-marked water.
- World space, on the DS pixel grid → no seams, matches the pixel art.
- Tile corners fade unless all 4 neighbours are water → never spills onto banks.
- Rings follow you through water; shores clip them. Bridges and waterfalls excluded.
- **Limits:** 2048 tiles per draw, 16 rings, particles in 25 nearby cells.
- Carries the scene's fog, so no unfogged holes.

### Weather effects
- The shader rebuilds each pixel's world position and normal, so effects stay put as the camera moves.
- **Rain:**
  - Ground gets darker and richer.
  - Puddles on up-facing paths and sand (`lgreen`, `nsand`, `l02_road`), one per 16-unit tile, styled like the game's
    own puddles, with drops landing in them.
  - Water and real puddle tiles get drops too.
  - Wetting/drying takes **4.5 s** when you cross a weather border.
- **Mist** (low, settles in hollows):
  - Snow regions: drifting snow (more while it snows).
  - Forests, lakes, coasts: morning mist before sunrise, gone by mid-morning.
  - Rain: thin spray.
  - Thins shadows and shimmer instead of brightening them.

### Coherent night lighting
- Separate controls for sun/moon, painted shadow opacity, contact shadows, glow.
- **Dawn** 05:00–07:00. **Dusk** 18:00–20:00. Colors come from the grade above.
- Fade to **4%** at night: `h_kage` shadow materials, `shadowchip` under bridges and boardwalks.
- Tree shadows fade to **35%** (trees stay grounded):
  - `tshadow` grass tiles: the dark disc lifts toward the tile's lit corner color. Non-uniform tiles (Great Marsh grass)
    stay.
  - Tree atlases (`conttree_b`, `conttree2_b`, `tree01`, `tree04_2`, `tree2_01`): non-red texels inside the disc of a
    32×32 red-brown stump cell fade. Palm and Eterna gray stumps keep their discs.
- Everything else painted or untagged: unchanged.

### Night window and lamp glow
- Only **explicitly tagged** materials glow. Untagged never do.
- Glow skips daylight tint and keeps dark frame details.

**What glows:**

| Where | Material / prop |
| --- | --- |
| House glass | `h_mado` (warm) |
| Pokémon Center / Poké Mart signs + trim | `light_a`, `light2` |
| Houses the game already lights (Twinleaf, Sandgem; 40% dimmer there) | `light` / `Light` |
| Jubilife lit windows, red roof beacons | `c1_lamp01/02`, `lamp01` / `lamp03` |
| Jubilife street lamps (near-white texels only) | `c1_o02` |
| Hearthome / Amity Square globe lamps (near-white only) | `c5_light` |
| Canalave canal bollards | `c2_light_a/b` |
| Battle Frontier plaza lamps | `bf_light*` |
| Cycling Road downlights (Route 206 only) | lens `lambert14`, pool over `lambert13` |
| Stark Mountain lava (day **and** night, pulsing) | prop 584, `magma` |

**Tagged per prop model:** every Gym emblem · Veilstone Game Corner (crown sign + blinking door bulbs, the brightest at
bloom 3) · Sunyshore Market windows · Pokémon League emblem · Battle Frontier Tower/Hall/Factory/Arcade · Route 210
Café · Trainers' School emblem (yellow texels) · Jubilife offices, round tower, Global Terminal, Pokétch Co., the
flickering TV screen.

**Special ones:**
- Vista Lighthouse: two beams every 8 s; the lantern flares when a beam faces the camera.
- Cycling Road: glowing lens + a blooming cone down to a warm pool that follows the deck's height. On while the head
  or the pool is in view.
- Stark Mountain: glow + bloom by day on those two maps; the ash keeps the game's haze.

**Light pools** (characters in them get warm light, up to 70% near the source):
- All 16 Jubilife street lamps, 12 Cycling Road lights, Eterna `c4_h01` round window (tile **317,520**), Pokémon
  Center entrances.
- Walls are **not** relit (see [Not done yet](#not-done-yet)).

#### The light table
- File: `field_lights.txt` in the game folder. Source: `res/pc_port/field_lights.txt` (format at the top).
- Lists: fading shadows, glowing materials, puddle ground, water, lamps in land meshes, prop lights, maps that glow by
  day. Entries can be scoped to a map or prop.
- **Developer → Reload light table** re-reads it live. Errors are shown by line and the old table stays.
- Missing file → built-in copy. The build copies the source over when it changes.

### Emissive bloom
- Halo follows the real shape: Center trim looks like neon, windows and lamps glow.
- Pipeline: emission buffer (occluded by nearer surfaces) → box filter to one texel per DS pixel → 6 half-res blur
  levels → screen-blend onto the 3D layer after fog.
  - Same halo size and blur cost at any internal resolution.
  - UI, menus, text and 2D sprites are never touched. No clipping to white.
- Night field frames only (plus the lava maps by day).
- Per-material strength in the table: **2** windows/lamps, **0.85** big Center/Mart signs. Peak: `FIELD_BLOOM_PEAK`.
- Bloom off → windows and the Center logo keep their original small halos.

**Water reflections** (needs water shimmer + night glows):
- Lamps, windows, signs and bollards make wavy columns on the water below them, in their own color.
- libntr records glowing polygons (merged per ~1 tile) and level water (`sea`, `lake`, `asasea`, `bf_lake`) each frame,
  on screen or not.
- 48 lights nearest screen center. Bank lamps within 12 units reflect 9 units onto the water; lights further back
  don't.
- Column: dashes on the DS pixel grid, ≤ ~3 tiles long, shifting 6×/s, catching on the painted wave crests, tapering,
  white at the start with a colored haze.
- Uses last frame's data in world space; the delay doesn't show.

---

## Edge cases

- Forests with the dappled overlay and outdoor lakes get outdoor lighting, even though their map headers say cave.
- If game logic is paused in the Tab menu, resume it to see time or effect changes.

## Debug tools (Tab → Developer)

- **Cycle time of day:** Night (00:00) → Dawn (06:00) → Day (12:00) → Sunset (18:00) → Real time.
  - Cosmetic only. Saved time, encounters and clock gameplay use the real clock.
  - Resets on restart. **Use real time** snaps back.
- **Show emission only:** see what glows.
- Counters: particles, water tiles, ripples, AA allocation, night sources.
- **Measure water timings:** async GPU queries for shimmer + CPU submission time.
- **Measure frame timings:** CPU work, frame time, GPU time + result age, renderer.
  - CPU and GPU overlap: don't add them up.
  - High frame time but low CPU and GPU → swap/pacing/waiting, not rendering.
  - GPU near 16.67 ms → GPU-bound.
  - Large result age → that sample is from an older frame.

## Where to compare

| Feature | Go here |
| --- | --- |
| Atmosphere | Eterna Forest, Snowpoint: look at the far trees. Cities are lighter on purpose. |
| Smooth edges | Jubilife/Canalave roofs, lamp posts, fences at 2×–4×. |
| Correct lighting | Building faces, trees, rotating 3D models. Try with richer day/night off first. |
| Particles | Eterna Forest (day: leaves, Night preview: fireflies), Snowpoint, any cave. |
| Contact shadows | Town pavement with NPCs. Walk, jump, toggle. |
| Cast shadows | Jubilife central crossing, day and sunset. |
| Water | Canalave canal, Lake Verity shore. Surf or wade for rings. |
| Weather | Route 215 in rain. Lake Verity / Eterna outskirts at Dawn for mist. |

**Full in-game pass:** town at dawn and sunset · Eterna Forest · snow route · coast · cave · Flash area · authored fog ·
Distortion World · native **and** wide view · battle/menu transitions · toggle each option while playing.

## Tests

```sh
python3 tools/scripts/test_field_visuals.py --build build_linux       # lighting archives, haze, MSAA, effect shaders
python3 tools/scripts/test_lighting_correctness.py --build build_linux # 10-bit normals, specular, shininess
python3 tools/scripts/test_terrain_coverage.py --build build_linux    # gap repair + compositor (--benchmark for 4K GPU time)
```

These run headless on Mesa via SDL's offscreen driver.

## Benchmarks

Close every other game window first: anything else on the GPU ruins the numbers.

```sh
# Night: per-effect cost in Jubilife, Eterna, Canalave, Cycling Road, Route 215 rain
python3 tools/scripts/benchmark_field_night.py --build build_linux \
  --scales 3 10 --width 3840 --height 2068 --output /tmp/night-benchmark.json
# --scenes Jubilife Eterna ... · --repeats N (default 2) · --baseline /path/to/old/main

# Water: off/on alternated, median + p95
python3 tools/scripts/benchmark_field_water.py --build build_linux --output /tmp/water-benchmark.json
```

- Night: an effect's cost = night with everything − night without that effect.
- If repeats (`gpu_by_repeat`) disagree by more than an effect's cost, the run was disturbed. Run it again.
- Last measured water results: [water_performance.md](water_performance.md). As of October 2026 every night scene
  fit in a 60 FPS frame at 3× and 10× with everything on.

---

## How it's built

- **Game side:** `src/port/visuals/field_visuals.c`. The field picks region, time and whether the scene qualifies;
  every frame starts neutral until the field opts in.
- **Backend:** libntr, pinned in `subprojects/libntr.wrap`. The shader applies tint and haze before authored fog.
- **Gap repair:** at internal resolution 1 with MSAA off, a 448×384 pass fills isolated empty pixels inside opaque
  terrain (black specks at mesh T-junctions). Bigger openings and translucent pixels stay. Cost doesn't grow with
  window size.
- **Water draw path:** one scan per candidate tile, batched native geometry reusing one GPU vertex buffer. GPU timers
  are opt-in and async.

## Not done yet

- Walls and terrain aren't relit by lamps. Needs libntr to keep world positions + normals to the fragment shader;
  today it lights vertices on the CPU.
- No shadow maps. Cast shadows are sheared sprites, so buildings and props cast nothing new, and the sun stays fixed
  northwest.

**Rules for future work:**
- Keep nearest sampling and pixel art. No scene lighting or bloom on UI, menus, text.
- Only fade **tagged** shadow materials. Painted shadows need masks or clean textures. Never guess from dark pixels.
- No double shadows: replace an authored shadow only where the replacement exists.
- Budget: ~**2 ms** extra GPU at 3× for any new lighting or shadow pass (measure with the night benchmark).
