# HeartGold libntr comparison

Reviewed November 3, 2026. The five reproduced native SDK fixes from `aa2ed50` and the graphics-storage portion of `5d9d4c6` are now integrated locally. Audio shutdown and native GX command submission are also integrated. The related NitroSystem heap change remains a follow-up.

The [audio/GX follow-up review](libntr_audio_gx_review.md) records the reproduced gaps, integration and permanent regression tests, including NitroSystem's model-rendering caller.

The remaining [NitroSystem heap-alignment patch review](libntrsystem_heap_alignment_review.md) now reproduces both alignment faults and validates the candidate under ASan/UBSan, including legacy four-byte requests, resizing and allocator-adapter calls. Integration is recommended.

## Integration update

- Added `subprojects/packagefiles/libntr_patch/heartgold-native-sdk.patch` after the existing field-coverage and upstream-correctness patches in `subprojects/libntr.wrap`. The libntr dependency commit remains pinned to `9598b39`.
- Ported arena pointer masks/alignment, word-pattern fills, main/sub blend and affine register mappings, array-backed graphics tables/matrix results, and sound-channel register mappings. Zero clears and other byte-repeated fills retain the fast libc `memset` path.
- Removed the obsolete `MIi_CpuClearFast` definition from `src/bwbattle/shim/heap_shim.c`, since libntr now supplies this function. This avoids duplicate symbols while keeping battle callers on the same SDK implementation.
- Added `tools/scripts/test_libntr_native_sdk.py` and its real-source C regression harness. It passes with AddressSanitizer and UndefinedBehaviorSanitizer, covering word patterns/tails/guards, low/high arena alignment and allocation failure, both 2D engines and all BG2/BG3 affine registers, native/raw DS address mapping, graphics table initialization and aliases, full matrix readback, all 16 sound channels, master pan, and PSG/noise setup.
- Matrix and lighting regression suites now link production `sim_vars.c` instead of defining synthetic contiguous matrix-register buffers. Both suites pass.
- Linux build, field-lighting/shader tests and terrain-coverage GPU tests pass. GPU checks used Mesa llvmpipe. A stale field-graphics test expectation was updated to match the existing antialias-off default, with an added enable/save/load check; no product default was changed.
- All three Meson patches apply cleanly to pristine pinned sources and reproduce the current patched SDK files. Existing Platinum graphics changes and the earlier wireless/MtxMult33 fixes are preserved. HeartGold's checkout is unchanged.

Run the focused regression suite after configuring the Linux build:

```sh
python3 tools/scripts/test_libntr_native_sdk.py --build build_linux
```

The remaining sections describe the original comparison and pre-integration findings. No interactive game or live multiplayer session was tested.

## Compared sources

- HeartGold checkout: `/home/lukas/code/pokeheartgold/pc/subprojects/libntr`.
- Its wrap pins `lib_hgss_port` at `5d9d4c6bf183e848a127e5c15c41b039321e05e0` and applies `libntr-single-screen-interfaces.patch`.
- Its actual clean libntr checkout is branch `lib_single-screen`, HEAD `11b9c92fdfb0001ba9d9a9071cb9a4a84dc4fa06`, with two interface commits after that pin.
- Platinum: `lib_single_screen_mod` at `9598b3986027e81bdecc49225a4d73209f5db19c`, including the working field-coverage patch and our new wireless/MtxMult33 patch.
- HeartGold's available history starts at `5fd3b5b`, which is also an ancestor of Platinum. Reviewed all five subsequent commits and all 19 changed paths. No network fetch was needed.

## Changes to integrate

| Priority | Change | HeartGold source | Current Platinum behavior | Recommendation |
| --- | --- | --- | --- | --- |
| High | Native arena alignment and pointer masks | `aa2ed50`, `libraries/os/src/os_arena.c` | Low allocations ignore requested alignment. The mask used for high allocations can discard the high 32 bits of a native pointer. Both problems reproduced. | Port the pointer-width masks and aligned allocation path together. |
| High | Correct 32-bit fill patterns and direct fast-fill implementation | `aa2ed50`, `libraries/mi/src/mi_memory.c`, `include/nitro/mi/memory.h` | `memset` repeats only the low byte. Both public fast and ordinary word-fill APIs reproduce incorrect results. | Port the implementation and header routing together. Retain optimized zero-fill handling if useful, without changing word semantics. |
| High | Native 2D blend/affine register access | `aa2ed50`, `libraries/gx/src/g2.c` | Treats separately declared register globals as one packed hardware block. Actual blend and affine API calls produce AddressSanitizer global-buffer-overflows. | Port explicit mappings to the main/sub native register variables, retaining the ARM path. |
| High | Contiguous graphics tables and matrix-result storage | `aa2ed50` plus `5d9d4c6`, `libraries/sim/src/sim_vars.c` and three native register declaration headers | Fog, edge/toon and matrix results are independent scalar globals, although SDK routines copy complete tables/matrices starting at their first scalar. Fog upload reproducibly overflows its four-byte global. | Port actual array-backed storage and consistent aliases across Nitro, TWL and simulator headers. Do not import the settings changes mixed into the same commit. |
| High | Native sound channel register mapping | `aa2ed50`, `libraries/sim7/src/snd/snd_channel.c` | Several paths still add DS channel strides to separate native scalar globals, even where a later statement updates the real channel array. Channel-5 timer setting produces an AddressSanitizer global-buffer-overflow before that later update. | Port native mappings to the mixer arrays, retaining Platinum's newer audio behavior. |
| Medium | Stop the audio callback before static destruction | `aa2ed50`, `libraries/sim/src/sim_audio.cpp` | Opens SDL audio without registering matching close/join behavior before SoundTouch objects are destroyed. | Port the successful-open shutdown registration, with an exit test. The asynchronous shutdown race was identified by source inspection, not reproduced here. |
| Medium | Native wrappers for every GX command DMA variant | `aa2ed50`, `libraries/mi/src/mi_dma_gxcommand.c` | The ordinary synchronous path already dispatches natively. Async/fast variants retain hardware FIFO/DMA machinery and 32-bit source storage. | Port all native entry points as a separate compatibility fix, preserving callback arguments/completion. No direct game-source callers of the fast/async names were found; SDK/NitroSystem calls and future uses still need consideration. This item was inspected, not executed. |

The first five rows address defects reproduced against our current implementation, rather than hypothetical differences between the games. Group them into memory/arena and register-storage changes for review and testing. Keep audio shutdown and command submission as focused follow-ups.

### Why the memory changes matter in Platinum

This is not only an HGSS API usage difference. Platinum uses nonzero fill patterns, for example OAM initialization in `src/system.c`, scanline initialization in `src/screen_scroll_manager.c` and battle animation code. Pixel buffers often use byte-replicated fill values, which can hide the bug; arbitrary word patterns do not.

### Graphics tests must use actual register storage

At review time, our lighting and matrix regression harnesses provided synthetic contiguous clip/vector register storage so they could test arithmetic in isolation. They did not validate the production `sim_vars.c` layout. The integration above replaces those fixtures with real storage and adds coverage for fog/toon initialization, edge-color aliases, clip/vector matrix write/readback, blending and all main/sub BG affine variants.

## Already covered or unsuitable to merge

| HeartGold change | Platinum comparison | Decision |
| --- | --- | --- |
| Zero translation in `MTX_RotX43_`, `MTX_RotY43_`, `MTX_RotZ43_` | Our functions already initialize those entries. | Already covered. |
| Signed 10-bit normal/light decoding and submission-time light transforms | Our corrected lighting path already handles these and adds scale handling, specular and shininess support, with an existing regression suite. | Keep Platinum's implementation. |
| Native defaults: dual-screen layout, 1× internal resolution, 1× accelerated-play multiplier | These are HGSS baseline/product choices. | Keep Platinum's defaults. |
| `49b8d4f` / `11b9c92`: primary-engine selection, companion panel, single-screen touch rectangles and cursor changes | These introduce a presentation API coupled to HeartGold's interface flow. They would require corresponding Platinum game-side changes. | Optional feature work, not part of this correctness integration. |
| `64e4562`: generated wrap-hash housekeeping | No gameplay/runtime improvement. | Optional housekeeping only. |

Do not replace our renderer, lighting, graphics effects, audio mixer, menu or input code wholesale with the older HGSS versions. The two upstream fixes added earlier in this chat remain applicable and should be preserved.

## Paired checks performed

Compiled the actual memory, arena, GX, register-storage and sound-channel C sources from each checkout with AddressSanitizer, using the configured Platinum SDK/build definitions and the corresponding checkout's headers. Small host-side probes use production APIs and register definitions; no game assets, SDL window or audio device are involved. Assertion-reporting functions are stubbed to abort, and native sound arrays are fixture storage with their production types.

| Probe | Platinum | HeartGold |
| --- | --- | --- |
| `MI_CpuFillFast(..., 0x12345678, ...)` | Produces `0x78787878`. | Produces `0x12345678`. |
| `MI_CpuFill32(..., 0x00000200, ...)` | Produces zero. | Produces `0x00000200`. |
| Low arena allocation requesting 32-byte alignment from an unaligned boundary | Returned pointer remainder is 3. | Returned pointer remainder is 0. |
| High arena allocation requesting 64-byte alignment | Returns NULL despite sufficient space. | Returns an aligned non-NULL pointer. |
| `G2_SetBlendAlpha` | AddressSanitizer global-buffer-overflow. | Expected alpha register value `0x0907`. |
| `G2_SetBG2Affine` | AddressSanitizer global-buffer-overflow. | Expected PA/PD/X/Y values. |
| `G3X_SetFogTable` | 32-byte write overflows the four-byte `s_reg_G3X_FOG_TABLE_0` global. | Completes without bounds errors. |
| `SND_SetChannelTimer7(5, 0x2000)` | AddressSanitizer global-buffer-overflow. | Channel 5 timer is `0xe000`. |

The reproduction script, source and complete logs are in `/tmp/libntr-heartgold-review/`; rerun with `python3 /tmp/libntr-heartgold-review/run.py`. The script bypasses the existing precompiled headers and includes each real PCH source header directly because adding AddressSanitizer to the existing GCC PCH triggered a compiler crash. This workaround does not change production source.

These were focused correctness checks of the pre-integration implementations. They establish the API/storage differences above, not end-to-end visual or gameplay equivalence. The initial review changed no production source, dependency pin, branch or commit; see the integration update for subsequent local changes. The HeartGold checkout was left untouched throughout.

## Related fix outside libntr

HeartGold also carries `pc/subprojects/packagefiles/libntrsystem-native-heap.patch`. Platinum's current NitroSystem exp-heap still uses a four-byte minimum block alignment even though its metadata contains native pointers. The HeartGold patch raises the native minimum to pointer alignment and normalizes existing positive/negative four-byte caller requests. This is a sensible additional candidate to audit and test separately; it belongs to libntrsystem and was not runtime-tested in this comparison.

## Commit inventory

- `aa2ed509d78477e0285d602626885eb0e2055c0e` — mixed native SDK, graphics, sound and lifetime fixes; selectively port the missing hunks.
- `64e4562c89d9570bb512fb1fdb388487c440f5a0` — generated-file ignore/removal housekeeping.
- `5d9d4c6bf183e848a127e5c15c41b039321e05e0` — complete native graphics-table/matrix storage fix plus HGSS default speed; port storage only.
- `49b8d4f2b31110c486d8cf2425691aba40bd6fed` — adaptive single-screen interfaces; optional game-specific feature.
- `11b9c92fdfb0001ba9d9a9071cb9a4a84dc4fa06` — start-menu/interface refinements; optional game-specific feature.
