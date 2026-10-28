# Night rendering performance investigation

Every measured night scene renders well inside a 60 FPS frame budget at 3× and 10× internal resolution, with every effect on; see the per-effect costs below. Live slowdowns that these offscreen measurements do not reproduce are covered under Live diagnosis.

## Per-effect costs (October 2026)

Linux, Ryzen 7 5800X, RTX 2060 SUPER, NVIDIA driver 615.78.08; 3840×2068
top-screen window; game `722e8a1604` with the rain change below.
Same harness as the earlier measurements (VSync and caps off, glFinish after
presentation, so these are frame costs rather than gameplay FPS). Every
mode ran twice, interleaved; the two repeats of each mode agree within
0.07 ms of GPU time, so differences above about 0.1 ms are real. Raw data:
`night_benchmark_results.json`.

#### Totals (GPU median / whole frame median, ms)

| Scale | Scene | Day all GPU / frame | Night none GPU / frame | Night all GPU / frame | Night all frame p95 |
| --- | --- | ---: | ---: | ---: | ---: |
| 3x | Canalave canal | 3.18 / 4.27 | 2.36 / 4.20 | 2.64 / 4.25 | 4.53 |
| 3x | Cycling Road | 2.35 / 4.59 | 2.32 / 4.55 | 2.57 / 4.13 | 4.72 |
| 3x | Eterna | 2.27 / 4.42 | 2.22 / 4.30 | 2.35 / 3.85 | 4.17 |
| 3x | Jubilife | 3.45 / 4.56 | 3.18 / 4.43 | 3.04 / 4.52 | 4.84 |
| 3x | Route 215 rain | 2.43 / 4.90 | 2.18 / 4.61 | 2.60 / 4.48 | 4.86 |
| 10x | Canalave canal | 3.63 / 4.97 | 3.31 / 5.26 | 3.92 / 5.56 | 6.08 |
| 10x | Cycling Road | 3.36 / 5.59 | 3.32 / 5.53 | 3.63 / 5.18 | 5.91 |
| 10x | Eterna | 3.22 / 5.37 | 3.15 / 5.25 | 3.32 / 4.80 | 5.07 |
| 10x | Jubilife | 3.79 / 5.22 | 3.71 / 5.08 | 4.00 / 5.38 | 5.84 |
| 10x | Route 215 rain | 3.98 / 6.42 | 3.28 / 5.65 | 4.63 / 6.48 | 6.93 |

#### Cost of each effect at night (GPU median ms: all effects minus all but this one)

| Scale | Scene | night lighting | night glows | bloom | soft shadows | cast shadows | water | weather | particles |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3x | Canalave canal | +0.05 | +0.07 | +0.07 | -0.02 | +0.00 | +0.02 | -0.00 | +0.14 |
| 3x | Cycling Road | -0.01 | +0.26 | +0.21 | +0.01 | -0.01 | -0.01 | +0.00 | -0.00 |
| 3x | Eterna | -0.04 | +0.14 | +0.12 | -0.02 | -0.05 | +0.03 | -0.03 | -0.04 |
| 3x | Jubilife | +0.02 | +0.45 | +0.12 | -0.08 | +0.02 | -0.01 | +0.02 | -0.01 |
| 3x | Route 215 rain | +0.11 | +0.05 | +0.04 | -0.03 | -0.00 | -0.00 | +0.10 | +0.15 |
| 10x | Canalave canal | +0.11 | +0.18 | +0.17 | -0.01 | +0.00 | +0.12 | -0.00 | +0.14 |
| 10x | Cycling Road | -0.05 | +0.31 | +0.18 | -0.01 | +0.00 | +0.00 | +0.00 | +0.00 |
| 10x | Eterna | -0.09 | +0.17 | +0.15 | -0.03 | -0.00 | -0.01 | -0.00 | -0.00 |
| 10x | Jubilife | -0.01 | +0.24 | +0.17 | -0.02 | -0.00 | -0.00 | -0.00 | -0.00 |
| 10x | Route 215 rain | +0.49 | +0.16 | +0.16 | -0.03 | -0.01 | -0.00 | +0.65 | +0.15 |


Findings:

- Every scene stays far inside a 60 FPS budget (16.67 ms) at 3x and 10x;
  the slowest, Route 215 in rain at 10x, takes 4.6 ms of GPU time and
  6.5 ms per frame at night with every effect.
- The new effects together cost 0.1–0.5 ms of GPU time in towns at 10x:
  mostly night glows and bloom (0.15–0.3 ms each). Shadows, water and
  particles are within noise except where they have work to do (water
  reflections in Canalave about 0.1 ms, particles on the coast and in rain
  about 0.15 ms).
- Weather was the one outlier: 2.4 ms on Route 215 at 10x. Its rain
  shader worked out puddle shapes and nine rain-drop cells on every wet
  pixel, and mist noise on every pixel. Puddle shapes now run only on
  puddle ground, full drops only inside puddles (one cell elsewhere), and
  mist noise only below the mist layer: 0.65 ms, with the same look.
- Night lighting costs about 0.5 ms in tree-heavy scenes at 10x (Route
  215): tree atlases whose painted shadows fade draw in both the opaque
  and the translucent pass. It is left as is for now.

## Earlier investigation

The reported drop below 60 FPS in Jubilife and Eterna at 10× is **not yet reproduced in the controlled offscreen tests**. Do not treat this pass as proof that the reported issue is fixed. Two before/after runs showed small, inconsistent differences; there is no demonstrated general speedup.

### Changes

- Opaque and translucent geometry append into unused vertex-buffer space instead of overwriting offset zero for every draw. Exhausted storage is orphaned so queued draws retain their data. The current streaming allocation is about 4.76 MiB; driver-retained in-flight allocations may temporarily add to that.
- Repeated effect and fog uniforms are skipped when their applied state is identical. Caches follow actual draw order, including deferred polygons, and invalidate when the renderer initializes or the shader program changes.
- Texture modulation and emissive window/lamp calculations share a texture sample. The lighting, shadow opacity, glow placement and resolution settings are preserved.
- **Measure frame timings** in the Developer panel adds optional live CPU work, total frame time, asynchronous whole-frame GPU elapsed time, sample age and the active OpenGL renderer. Timing is session-only and uses nonblocking query reads. No frame timing query is enabled by default.

### Controlled measurements

Linux, Ryzen 7 5800X, RTX 2060 SUPER, NVIDIA driver 615.78.08; 3840×2068 top-screen window, 10× internal resolution, normal AA/fallback settings. Baseline: game `971293157b`, SDK `6854f36`. Copied saves/assets only. Views: Jubilife tile 174,752 and Eterna tile 318,522. Each mode runs 90 transition/warm-up frames and about 90 sampled steady frames. Water/particles are disabled to isolate the night controls; the ordinary graphics settings are retained otherwise.

This diagnostic harness disables VSync/caps and calls **glFinish after presentation**, only in the test process, to complete GPU queries and expose frame costs without queue backlog. These elapsed values are not ordinary gameplay FPS measurements. GPU intervals may include CPU submission gaps. Short samples and process scheduling add noise. Initial loading, moving cameras and desktop presentation behavior need separate live validation.

Night lighting and glow both enabled:

| View | Version | CPU median / p95 (ms) | GPU median / p95 (ms) | Total median / p95 (ms) | Transition peak / steady peak (ms) |
| --- | --- | ---: | ---: | ---: | ---: |
| Jubilife | before | 4.10 / 5.78 | 4.01 / 5.46 | 7.35 / 9.81 | 10.94 / 10.76 |
| Eterna | before | 3.63 / 5.43 | 3.06 / 4.35 | 7.63 / 9.91 | 11.76 / 10.54 |
| Jubilife | after | 4.18 / 6.15 | 4.26 / 6.33 | 7.39 / 9.38 | 9.83 / 11.97 |
| Eterna | after | 3.88 / 5.16 | 3.32 / 4.90 | 7.74 / 9.44 | 10.70 / 10.90 |

Raw data includes day, legacy night, night lighting alone, glow alone and both: `night_benchmark_results.json`. The second controlled run is recorded here; the first showed a modest Jubilife improvement, while this run did not confirm it. Both runs remained below a 16.67 ms frame budget in these sampled town views. That does not rule out the user's live slowdown.

## Live diagnosis

Enable **Developer → Measure frame timings** in the normal game window, keep the same camera and compare Day and Night preview while the slowdown is visible.

- Record CPU work, GPU elapsed, total frame time, sample age, renderer, resolution and AA mode.
- GPU time near/over 16.67 ms suggests GPU work needs attention. CPU work may include driver stalls or time waiting for game processing, so it is not exclusively CPU computation.
- A low CPU/GPU value with a high total frame time points toward swap/pacing/wait costs. CPU and GPU overlap; do not add their values to estimate frame time.
- A large GPU sample age means the query is delayed, so it cannot describe the current frame reliably.

## Reproduce the isolated test

Requires Linux, matching unstripped native binaries, packaged assets and a usable copied save:

```sh
python3 tools/scripts/benchmark_field_night.py --build build_linux \
  --scales 3 10 --width 3840 --height 2068 --output /tmp/night-benchmark.json
# --scenes Jubilife Eterna ... to run some scenes; --repeats N (default 2);
# --baseline /path/to/previous/main to compare another binary.
```

Each scene runs day with every effect, night with no effect, night with
every effect, and night with every effect but one, for each effect in turn
(night lighting, night glows, bloom, soft and cast shadows, water, weather,
particles). The whole list repeats, interleaved, and samples are pooled.
An effect's cost is the difference between night with every effect and
night without it. The scenes: Jubilife's lamps and Center, Eterna's windows
and trees, Canalave's canal reflections, the Cycling Road's downlights and
Route 215 in rain.

Close every other game window first: anything else drawing on the GPU
makes the differences meaningless (the script warns about other running
game processes). Each result also records every repeat's GPU median in
`gpu_by_repeat`; when repeats disagree by more than an effect's cost, the
run was disturbed and should be repeated.

Validation: both lighting/shader suites passed. In-game captures verified the emissive window, lamp debug view, water/ripples and snow after vertex streaming. The session-only GPU timing API returned valid results without an environment-enabled trace.
