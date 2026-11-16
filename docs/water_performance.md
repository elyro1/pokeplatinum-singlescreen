# Water performance measurements

Measured on 2026-10-13: Ryzen 7 5800X, NVIDIA RTX 2060 SUPER, NVIDIA driver 615.78.08, Linux SDL offscreen OpenGL. This is a desktop result; it does not establish Android performance.

960×540 top-screen window; internal scales 2×, 3× and 10×; smooth edges enabled with its normal allocation fallback. VSync/frame cap disabled. Each fixed Lake Verity view alternates off/on twice, with 1,000 warm-up frames and 300 sample frames per phase (about 600 samples per setting). Tests use copied assets/save/config. Other added atmosphere/particle/night options are disabled.

The before executable is game commit `66aef5817b`, SDK `91a0e7717e`. The after executable includes native batched water submission and the single-pass candidate scan. Shimmer coverage stays at 65 shoreline tiles or 337 open-water tiles.

## Water-only timings

CPU covers water-cache maintenance, interaction checks, shimmer submission and ripple submission. GPU sums asynchronous timestamp intervals around shimmer draw calls, excluding ripple draws, resolve and compositing. The GPU value shown by the debug panel is the most recently completed measurement, so these sampled values are delayed estimates of a steady scene. Timing is opt-in and does not block for results.

| Scale | View | CPU median / p95 (ms) | Shimmer GPU median / p95 (ms) |
| --- | --- | ---: | ---: |
| 2× | shore | 0.013 / 0.079 | 0.027 / 0.028 |
| 2× | open | 0.050 / 0.082 | 0.041 / 0.043 |
| 3× | shore | 0.013 / 0.031 | 0.029 / 0.031 |
| 3× | open | 0.049 / 0.478 | 0.061 / 0.064 |
| 10× | shore | 0.013 / 0.270 | 0.052 / 0.053 |
| 10× | open | 0.049 / 0.480 | 0.272 / 0.275 |

CPU p95 includes tile-cache rebuilds and scheduling noise. Costs vary with visible tile count and GPU pixel coverage; eight phase batches do not eliminate fragment work.

## Whole-frame CPU work: controlled off/on comparison

| Scale | View | Before off / on median (ms) | After off / on median (ms) | After off / on p95 (ms) |
| --- | --- | ---: | ---: | ---: |
| 2× | shore | 1.638 / 1.734 | 1.642 / 1.680 | 1.694 / 2.029 |
| 2× | open | 2.245 / 2.620 | 2.263 / 2.320 | 2.471 / 2.745 |
| 3× | shore | 1.649 / 1.733 | 1.651 / 1.675 | 1.711 / 1.901 |
| 3× | open | 2.264 / 2.661 | 2.339 / 2.417 | 3.772 / 5.118 |
| 10× | shore | 2.871 / 3.352 | 6.385 / 4.417 | 10.004 / 8.196 |
| 10× | open | 2.258 / 2.784 | 2.364 / 2.569 | 3.735 / 5.067 |

The 2×/3× open-water runs show a smaller off/on CPU difference after batching. Whole-frame differences include renderer/driver scheduling; negative differences or the noisy 10× results must not be interpreted as water improving performance. This is one run on one machine, not an isolated universal speedup.

## Whole-frame GPU elapsed queries

| Version | Scale | View | Off median / p95 (ms), valid samples | On median / p95 (ms), valid samples |
| --- | --- | --- | ---: | ---: |
| before | 2× | shore | 1.241 / 1.449 (594) | 1.251 / 1.686 (592) |
| before | 2× | open | 1.253 / 1.839 (594) | 1.250 / 1.767 (592) |
| before | 3× | shore | 1.264 / 2.660 (575) | 1.069 / 2.499 (584) |
| before | 3× | open | 1.088 / 2.481 (540) | 1.393 / 3.019 (584) |
| before | 10× | shore | unavailable / insufficient (0) | unavailable / insufficient (4) |
| before | 10× | open | unavailable / insufficient (9) | unavailable / insufficient (12) |
| after | 2× | shore | 1.245 / 2.450 (586) | 1.245 / 2.091 (584) |
| after | 2× | open | 1.255 / 2.482 (592) | 1.252 / 2.009 (590) |
| after | 3× | shore | 1.266 / 2.660 (590) | 1.258 / 2.364 (588) |
| after | 3× | open | 1.240 / 2.848 (298) | 1.388 / 3.330 (298) |
| after | 10× | shore | unavailable / insufficient (0) | unavailable / insufficient (4) |
| after | 10× | open | unavailable / insufficient (10) | unavailable / insufficient (11) |

Unavailable results are omitted rather than counted as zero. The bounded whole-frame query ring drops samples when the headless GPU queue is behind; cells with fewer than 100 valid results are deliberately not presented as representative timings. Elapsed queries can include command starvation/CPU submission gaps and must not be equated to the isolated shimmer draw cost. The 10× whole-frame GPU comparison remains inconclusive in this harness.

## Reproduce

```sh
python3 tools/scripts/benchmark_field_water.py --build build_linux \
  --baseline /path/to/previous/main --scales 2 3 10 \
  --output /tmp/water-benchmark.json
```

The benchmark requires Linux, an unstripped native executable, a packaged game and a usable copied save. It resolves private debug symbols from the matching binary and never warps or writes the original save. Omit `--baseline` to measure the current renderer only.
