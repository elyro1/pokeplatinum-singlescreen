# Audio shutdown and native GX submission review

Checked November 3, 2026 against Platinum's current libntr, including the previously integrated native SDK patches. Both HeartGold fixes from `aa2ed509d78477e0285d602626885eb0e2055c0e` are now integrated locally.

## Integration update

- Added `subprojects/packagefiles/libntr_patch/audio-gx-lifecycle.patch` as the fourth patch in `subprojects/libntr.wrap`; the dependency commit remains pinned to `9598b39`.
- Audio registers `SDL_CloseAudio` for successful initialization, stopping and joining the callback before static SoundTouch destruction. Failed opens do not register cleanup.
- All four native GX entry points use one copy-and-decode path, with inline completion after decoding for async calls. Native submission no longer uses DS DMA/IRQ machinery. The ARM implementation is preserved in the other preprocessor branch.
- Added `tools/scripts/test_libntr_lifecycle.py` with production-source audio and GX harnesses. All four audio exit/open cases pass against real SDL dummy audio and SoundTouch: return from main, `std::exit`, and both exit styles after an intentional open failure. Cleanup is called exactly once after success, never after failure, and no callback work occurs after cleanup.
- All 46 GX cases pass under ASan/UBSan, covering direct variants, zero lengths, NULL callbacks/arguments, input ownership, the hardware chunk boundary, repeated and reentrant submissions, and NitroSystem's busy flag. Hardware/IRQ entry points fail the test if called.
- The existing lighting/decoder suite now submits a real packed color command through each GX variant and verifies the decoded state before completion. It passes, as does the native SDK sanitizer suite.
- Linux build passes. All four Meson patches apply to pristine pinned sources and reproduce every patched file. Earlier fixes and Platinum-specific rendering/audio behavior are retained.

Run after configuring/building Linux:

```sh
python3 tools/scripts/test_libntr_lifecycle.py --build build_linux
python3 tools/scripts/test_lighting_correctness.py --build build_linux
```

The sections below retain the original pre-integration review. Its isolated candidate probes were built in `/tmp/libntr-lifecycle-review/`; persistent regression tests now live in `tools/scripts/`. No interactive gameplay or physical audio device was tested.

## Audio shutdown: confirmed missing cleanup

`SIM_Audio_Init` opens and unpauses SDL's legacy audio device. There is no corresponding close in our runtime. The callback uses the global `s_effectTempo` SoundTouch object, including `clear`, `setTempo`, sample submission and sample draining. Normal return from `main` and `std::exit` both proceed into static destruction while that callback can still run. Window-close handling currently calls `exit(0)`.

HeartGold registers `std::atexit([] { SDL_CloseAudio(); });` after a successful open. Because that registration happens during initialization in main, its cleanup runs before the static SoundTouch destructor. The installed SDL2 API documentation confirms that legacy `SDL_CloseAudio` closes device 1, and closing waits for any active callback to finish before returning.

### Reproduction

Compiled Platinum's actual audio source and blip-buffer implementation against installed SDL2/SoundTouch. The test uses SDL's real dummy audio driver and the real simulator audio callback, with game sequence/channel tick entry points stubbed. It switches the multiplier to 5× so the callback exercises SoundTouch. A static destruction probe checks whether the audio device was stopped before teardown.

| Exit path | Current code | Current code plus HeartGold cleanup |
| --- | --- | --- |
| Return from `main` | Audio still open during static destruction. | Audio stopped before static destruction. |
| `std::exit(0)` | Audio still open during static destruction. | Audio stopped before static destruction. |

The callback ran in all cases (98 sequence tick calls in the recorded runs). For the unfixed case the probe stops the process before the SoundTouch destructor to avoid deliberately inducing a nondeterministic use-after-free. This confirms the unsafe lifetime ordering, rather than claiming a reproduced random crash.

Integrate the successful-open cleanup registration. No mixer rewrite or additional audio thread is needed. Promote the probe to a regression test covering both process exit paths and unsuccessful audio-open handling.

## Native fast/async GX submission: confirmed incompatible paths

Only `MI_SendGXCommand` currently performs a complete native submission without relying on hardware DMA completion. The other paths retain DS machinery:

- `MI_SendGXCommandAsync` initially dispatches a native copy, but then installs FIFO/DMA callbacks, stores the source in a 32-bit field and writes DMA registers. Completion still depends on the hardware path.
- `MI_SendGXCommandFast` writes DMA registers without submitting the display list to the native decoder.
- `MI_SendGXCommandAsyncFast` likewise uses hardware DMA and does not submit the native list or complete the callback through the native path.

### This affects NitroSystem model rendering

`NNS_G3dGeSendDL` uses an async GX API for display lists of at least 256 bytes when DMA is enabled. It sets `NNS_G3dFlagGXDmaAsync = 1` before submission and expects `simpleUnlock_` to clear it from the completion callback. `NNS_G3dGeUseFastDma` selects the fast or ordinary async variant. This is called from NitroSystem's shape/SBC rendering code, even though direct Platinum game-source references to the fast/async SDK names were absent from the earlier search. The default DMA channel is 3.

### Paired submission tests

Compiled the actual Platinum and HeartGold GX submission modules, actual Platinum NitroSystem `gecom.c`, and actual native memory helpers. The decoder boundary is instrumented to verify an independent heap copy, size and exact payload bytes, then consume/free that copy using the production ownership convention. Hardware/IRQ operations are recorded instead of executing DS DMA. This tests host dispatch and completion semantics without a GL context.

Representative results for nonempty display lists:

| API/path | Current behavior | HeartGold native wrappers |
| --- | --- | --- |
| Ordinary synchronous | One native submission. | One native submission. |
| Async | One native submission, no completion, followed by hardware operations. | One submission, then one completion with the original argument. |
| Fast synchronous | No native submission; DMA write. | One native submission. |
| Fast async | No native submission or completion; hardware operations. | One submission, then one completion with the original argument. |
| NitroSystem, ordinary async | Busy flag stays set. | Busy flag cleared before returning. |
| NitroSystem, fast async | No native submission; busy flag stays set. | One submission; busy flag cleared before returning. |

All 34 candidate cases pass under AddressSanitizer and UndefinedBehaviorSanitizer: direct APIs at 0, 4, 472, 476 and 1024 bytes, both async APIs with NULL callbacks, and both NitroSystem choices at 256 and 1024 bytes. These include the old 472-byte hardware chunk boundary. No candidate hardware/IRQ calls, duplicate submissions, payload errors, callback argument errors or callback ordering errors were recorded. LeakSanitizer was disabled because the execution environment does not support its tracing requirements; ASan/UBSan remained enabled.

### Integration semantics

Use a native-only implementation for all four entry points:

1. Copy and submit a nonempty list once through the existing native decoder.
2. Run the async completion callback once after decoding, preserving `arg`; tolerate NULL callbacks and complete zero-length async calls.
3. Delegate the fast native variants to the same submission behavior.
4. Preserve the ARM implementation under the other preprocessor branch.

On the native port, async completion will occur inline on the calling thread. The tested NitroSystem caller already sets its busy state before submission and handles this correctly. The decoder owns/frees the copied command bytes; the submission wrapper frees its message allocation. Preserve that ownership to avoid freeing the caller's input or leaking the copy. No render thread or deferred queue should be added as part of this fix.

## Reproduction artifacts and limits

- `/tmp/libntr-lifecycle-review/audio.cpp` and `audio_run.py`: real dummy-device audio exit probes, current code and the cleanup candidate.
- `/tmp/libntr-lifecycle-review/gx.c`, `gx_run.py` and `gx_sanitized_run.py`: paired native GX probes and sanitizer runs.
- Per-case output is recorded beside the scripts in `*-audio-*.log`, `*-gx.log` and `*-gx-sanitized.log`.

The original probes tested lifecycle/submission behavior in isolation. They do not establish audible output quality, GPU rendering correctness, or a complete in-game shutdown/session. No full game build was run during the initial read-only review; the subsequent integration build and persistent regression checks are recorded above.
