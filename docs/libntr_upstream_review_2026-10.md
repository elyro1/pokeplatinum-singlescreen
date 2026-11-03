# libntr upstream review — October 2026

Reviewed on November 3, 2026. The two small correctness fixes below have now been integrated locally through `subprojects/packagefiles/libntr_patch/upstream-correctness.patch`. Native soft reset and the full upstream renderer merge remain deferred.

## Integration update

- Adapted `f34319a`: native wireless termination completes its callback directly, returns `WVR_RESULT_OPERATING`, and bypasses the ARM7 readiness/FIFO path. Both native startup and termination preserve the caller's callback argument.
- Adapted part of `2c8ba30`: `G3SIM_MtxMult33` updates all four columns, retains the translation row, and multiplies position/vector state independently in combined mode. Other matrix operations and the renderer architecture are unchanged.
- Registered the patch after the existing field-coverage patch in `subprojects/libntr.wrap`; the dependency commit remains pinned to `9598b39`. Existing field-coverage working changes were preserved.
- Validation passed: Linux build (`ninja -C build_linux -j 4`), new real-source regression suite, existing lighting suite, and clean application of both Meson patches to pristine pinned sources.
- The focused suite covers callback arguments, NULL callbacks, repeated startup/shutdown, no ARM7 dependency, all four matrix modes, nonzero fourth columns, distinct position/vector matrices, and clip/vector register readback. It runs without a game or GL window; in-game wireless transitions have not been exercised.

Run the regression suite after configuring/building Linux:

```sh
python3 tools/scripts/test_libntr_upstream_fixes.py --build build_linux
```

The remaining sections record the original review and its pre-integration findings.

## Scope and fetched state

- Fetched `https://github.com/cybervisi0n/libntr.git`, branch `main`, including previously missing history. `cyber/main` advanced from `1555d91` to `de00e09ae207def1077a0717c0898921ee4e90ca`.
- Latest commit: “prospero: Improve filesystem access,” November 3 at 03:20 CET (November 2 in the author's timezone).
- Compared against our pinned `elyro1/libntr` branch `lib_single_screen_mod`, HEAD `9598b3986027e81bdecc49225a4d73209f5db19c`, and inspected the existing working-tree graphics patch.
- “Last month” means October 1–31, 2026 in Europe/Berlin, using commit timestamps: 48 reachable commits, including one merge. Five are already in shared ancestry. Common ancestor: `f20fa9e`.
- Used ancestry, `git cherry`, individual diffs, and current source inspection. Different commit hashes alone do not mean a feature is missing.
- During the initial review, no source changes, dependency revision changes, cherry-picks, merges, commits, or pushes were performed. See the integration update above for subsequent local changes.

## Changes worth integrating

### 1. Wireless shutdown completion — adapt f34319a

[Upstream commit](https://github.com/cybervisi0n/libntr/commit/f34319a9c592147513c8dca0bee6ceade73c2349)

Our native `WVR_StartUpAsync` immediately completes its callback, but `WVR_TerminateAsync` still requires an ARM7 callback and FIFO response. If the callback is unavailable, it returns `WVR_RESULT_DISABLE` without completing shutdown. The game ignores this return value in `WirelessDriver_Shutdown`; its callback is what clears the connected state and releases `SleepLock(4)`. This is a concrete asymmetry affecting wireless cleanup paths (GTS, Mystery Gift, communications, Ranger link, etc.). No end-to-end hang was reproduced during this review.

Port the native completion behavior, preserving the caller's `arg` and reporting success explicitly. Do not copy the upstream `WvrReceiveCallback(..., (uPtr)arg, ...)` literally: that callback interprets its data as a command/result word, not a caller pointer. NULL happens to decode as success, but arbitrary arguments do not. A direct native callback branch matching startup, or a correctly encoded terminate/success response with appropriate state cleanup, is preferable.

Validate callback invocation exactly once with NULL and non-NULL arguments; verify game status and sleep-lock cleanup.

### 2. Full matrix multiplication for MtxMult33 — adapt part of 2c8ba30

[Upstream commit](https://github.com/cybervisi0n/libntr/commit/2c8ba30e3131dfc419d408943766a5365e9bb32d)

Our `G3SIM_MtxMult33` loops only over the upper-left 3×3 region. Upstream embeds the input in a homogeneous 4×4 matrix and multiplies the complete current matrix. This also updates the fourth column, which matters for non-affine projection matrices.

An isolated C++ harness extracted the actual current fork function, with fixed-point types/helpers and unrelated globals stubbed. With current projection `[0][3] = 7` and an input X scale of 2, the fork leaves `[0][3] = 7`; the complete multiplication gives 14. This proves the arithmetic gap, but does not establish a specific visible gameplay symptom.

Adapt only this operation into the existing renderer. Preserve separate position/vector semantics and validate projection, texture, position, and position/vector modes. Our 4×3/4×4 operations already use the upstream multiplication order, so those portions do not need importing.

### 3. Native soft reset — separate feature, not a direct cherry-pick

[October reset/relaunch commit](https://github.com/cybervisi0n/libntr/commit/961047d2c371c66f409819519275a86b970abc23), following `d81b267` and `0f454a5`.

Our native `OS_ResetSystem` still follows the old hardware/PXI reset sequence without restarting the executable. Game reset callers include normal reset, game-clear flow, and menu/network paths. Native process relaunch is useful work, but the upstream series is coupled to render-context/thread restructuring.

The November 1 follow-up [125a6dd](https://github.com/cybervisi0n/libntr/commit/125a6dddb679b008115fff662481081a626b7052) explicitly puts NitroMain back on the main thread and explains that restarting the thread left initialized statics and caused bugs. This follow-up is outside October but materially changes how the October work should be integrated.

Implement a platform-aware process restart around our existing startup path. Preserve required arguments and launch/save-slot context, support packaged executable paths, define failure handling, and account for reset-parameter behavior. Test repeated resets, save completion, audio/input reinitialization, desktop packaging, and Android's separate lifecycle if sharing this API there. Upstream's bare `execl(argv[0], argv[0], nullptr)` is not sufficient to establish all of those guarantees.

## Why defer the complete renderer refactor

The October 23–30 G3 work introduces `GlobalState`, `CommandHandler`, `DrawCall`, and `GlRenderer`. It is a substantial architecture migration rather than a contained feature update. Our renderer has additional fog/translucency behavior, widescreen fixes, extended textures, texture smoothing, field lighting, bloom, water reflections, profiling, and the native field-coverage patch.

Several late fixes restore behavior already present in our old architecture: polygon state is read after vertex submission; clip matrices use position × projection; projection/texture pushes save values; deferred translucent items render; fog color is uploaded as a vec4. They should not be mistaken for missing independent fixes.

Our matrix utility implementations also exceed upstream in several places: upstream still has assertion stubs for operations we implement, and its Scale43/Copy-to-44 paths initialize fewer entries. A wholesale replacement would require deliberate preservation of these fork implementations. Existing GPU caching likewise already includes bounded memory, metadata and byte-level identity checks, and deferred references that should be retained.

## Complete October commit inventory

Dates below are commit dates in Europe/Berlin.

| Date | Commit | Upstream change | Assessment |
| --- | --- | --- | --- |
| 2026-10-31 | [961047d](https://github.com/cybervisi0n/libntr/commit/961047d2c371c66f409819519275a86b970abc23) | SIM/OS: relaunch exe on softreset | Adapt later: process relaunch for soft reset; preserve launch arguments and handle platform/failure paths. |
| 2026-10-31 | [0f454a5](https://github.com/cybervisi0n/libntr/commit/0f454a506cfe6696353e4a1f4a0d4e0bbcdeb6d9) | Improve shutdown/initialization of NitroMain thread | Part of reset experiment; do not import thread/arena restart infrastructure independently. |
| 2026-10-31 | [dcfdf59](https://github.com/cybervisi0n/libntr/commit/dcfdf59faf05cb06e513f8b44e742431424a79e2) | SIM: turn O2 optimization back on | Already equivalent: fork already compiles simulator C++ with -O2. |
| 2026-10-31 | [d81b267](https://github.com/cybervisi0n/libntr/commit/d81b267ef69283ec8015fe03e13edfdcaef76076) | Add initial support for resetting by running NitroMain on a separate thread | Reset feature is useful, but do not take the original threaded design; see November 1 follow-up 125a6dd. |
| 2026-10-31 | [fad522e](https://github.com/cybervisi0n/libntr/commit/fad522ebee1b1241961f2e0a07b8248179e448d8) | Merge 'origin/features/g3_refactor' into main | Defer full G3 refactor merge; migrate custom graphics only in a dedicated effort. |
| 2026-10-31 | [dcdf148](https://github.com/cybervisi0n/libntr/commit/dcdf148dde4a5f2116da86620e6327f1a4a07103) | Actually push the projection and texture matrices to the stack | Refactor repairs; fork already saves projection/texture matrices, uploads fog as vec4, and uses -O2. |
| 2026-10-30 | [cd15326](https://github.com/cybervisi0n/libntr/commit/cd15326431bfa2c23fa971bb8882a56192713b37) | Fix some rendering bugs | Refactor repairs; fork already reads updated polygon state and renders deferred translucent items. |
| 2026-10-30 | [e58fe91](https://github.com/cybervisi0n/libntr/commit/e58fe91f938f68cb634ebe51a2cdd2ba89644ef5) | Fix polygon index | Refactor repair; fork already checks polygon state after G3SIM_Vtx. |
| 2026-10-30 | [9788966](https://github.com/cybervisi0n/libntr/commit/9788966744f0673847df1cf74f8029cddd1ad266) | Fix some matrix multiplication errors | Refactor repair; fork already computes position × projection and multiplies distinct matrix operands. |
| 2026-10-30 | [2c8ba30](https://github.com/cybervisi0n/libntr/commit/2c8ba30e3131dfc419d408943766a5365e9bb32d) | Change behavior of mtxmult33 | Adapt MtxMult33 full-matrix multiplication; fork still updates only its 3×3 block. Other multiply-order changes already equivalent. |
| 2026-10-30 | [ce3fe77](https://github.com/cybervisi0n/libntr/commit/ce3fe77e122720370144294036fc7283e663abd4) | Refactor bugfixes | Refactor-specific VRAM bridge and build changes; no independent desktop fix to import. |
| 2026-10-30 | [e4ff832](https://github.com/cybervisi0n/libntr/commit/e4ff83242907c7085796daf5c7a15b7ba371679c) | Eliminate more global variables | Defer renderer state migration. |
| 2026-10-30 | [9acde8b](https://github.com/cybervisi0n/libntr/commit/9acde8b19e31e8e5b92b402d84b77ef35e554206) | Move shader uniform variables over to GlRenderer | Defer renderer/uniform migration. |
| 2026-10-30 | [c5f3249](https://github.com/cybervisi0n/libntr/commit/c5f3249ee50f8e90e44dcd19ae6638bc11449636) | Add copy constructor for DrawCall | Refactor-specific DrawCall ownership; fork already captures deferred draw state through its existing queue. |
| 2026-10-30 | [6206506](https://github.com/cybervisi0n/libntr/commit/6206506b9d9e6bebb33d42fb2cfd3cdd9cd01059) | Start moving G3_Draw over to new G3::GlRenderer class | Defer GlRenderer migration. |
| 2026-10-30 | [1075512](https://github.com/cybervisi0n/libntr/commit/10755121a4bf959c7f58c52f8964c6af8f5438f9) | Remove some global variables | Defer renderer state migration. |
| 2026-10-30 | [af687bd](https://github.com/cybervisi0n/libntr/commit/af687bd36ddcecf2a8ff7da0b63de1c918ae039e) | Refactor most of CommandHandler | Defer CommandHandler migration; fork already has fuller matrix-copy implementations. |
| 2026-10-30 | [df9dfd0](https://github.com/cybervisi0n/libntr/commit/df9dfd0517e045d28c99e16733cbe13899d289ac) | Fix linux build | Adds includes to new refactor files absent from fork; not an existing Linux build fix. |
| 2026-10-29 | [fe31905](https://github.com/cybervisi0n/libntr/commit/fe31905b72ed6eeb1705cdebcae9f1615595d23d) | Move more matrix functionality and global variables to SIM::G3::GlobalState | Defer GlobalState matrix migration. |
| 2026-10-28 | [42fb756](https://github.com/cybervisi0n/libntr/commit/42fb756c96d70ccf0127730e93737ea760c4bf47) | Add meson subproject file to gitignore | Optional housekeeping: ignore generated Meson wrap hash; not a gameplay fix. |
| 2026-10-28 | [40732b3](https://github.com/cybervisi0n/libntr/commit/40732b353ee4034d50fe611840d5ca2ec3746152) | Remove meson subproject file | Optional housekeeping: stop tracking generated Meson wrap hash; coordinate with wrap handling. |
| 2026-10-28 | [576ddee](https://github.com/cybervisi0n/libntr/commit/576ddee784f387b3c56f8f93357e6a6a34d01889) | G3: More refactoring work | Defer renderer/GlobalState migration. |
| 2026-10-28 | [f6b3a69](https://github.com/cybervisi0n/libntr/commit/f6b3a690c72b1484fda86c47021dbda2b54ffb69) | prospero: Fix crash when creating config file | Skip for current targets: Prospero-specific config path and GL loader/startup handling. |
| 2026-10-25 | [b278476](https://github.com/cybervisi0n/libntr/commit/b27847616743f1888d6bca18c6bedb66685bd03a) | prospero build fixes part 2 | Skip for current targets: Prospero GL/build portability. |
| 2026-10-25 | [4f78c75](https://github.com/cybervisi0n/libntr/commit/4f78c752c0cbbb66d3da9c7b6c66173d120b9786) | prospero build fixes | Skip for current targets: Prospero compiler/network/build portability. |
| 2026-10-25 | [380a3d7](https://github.com/cybervisi0n/libntr/commit/380a3d7e60de82aced82075e7b3b9acf5f68b1cd) | Add prospero build target | Skip unless adding a Prospero target. |
| 2026-10-24 | [37ccb42](https://github.com/cybervisi0n/libntr/commit/37ccb425073e35e927259a7e81403b9ee747e389) | Start working on G3 refactor | Defer new G3 architecture; contained Rot44 initialization/sign fixes are already in fork. |
| 2026-10-10 | [1555d91](https://github.com/cybervisi0n/libntr/commit/1555d911ac80c0190ac1c321f99f1323de8b3c09) | SIM: Add fullscreen support | Already adapted as d1f82b1, with later fullscreen improvements. |
| 2026-10-10 | [c4b7736](https://github.com/cybervisi0n/libntr/commit/c4b7736d060244ef37bc1eb70bd4911b99c5e71b) | SIM: Add support for getting a pointer to the SDL window | Already patch-equivalent as e364586 (git cherry confirms). |
| 2026-10-10 | [f5e3951](https://github.com/cybervisi0n/libntr/commit/f5e39517c293eb69011cb0779b09858758baf2b2) | SIM: Set 1280x800 resolution automatically on steam deck | Already adapted as 871a912; fork has SteamDeck detection and 1280×800 sizing. |
| 2026-10-09 | [aa45e52](https://github.com/cybervisi0n/libntr/commit/aa45e528882b447639e42da82ff5631e0adb4fe7) | Revert change to set current directory | No feature to import: undoes preceding chdir change because it broke AppImage. |
| 2026-10-09 | [55b3b23](https://github.com/cybervisi0n/libntr/commit/55b3b234ddfa1c04c913bf9e221015a091596dd9) | Add agents file | Contributor policy only; no runtime/build improvement. |
| 2026-10-09 | [2b42bd9](https://github.com/cybervisi0n/libntr/commit/2b42bd9ccb008d8794f7467766eca7dfeb34469f) | SIM: change directory to the game directory on launch | Skip: upstream reverted the chdir behavior in aa45e52. |
| 2026-10-09 | [1b7c6e5](https://github.com/cybervisi0n/libntr/commit/1b7c6e5103aa53e169706799a2f4971abd9de067) | G3: Fix G3_Color calculating colors wrong | Already adapted in 1601331: G3SIM_Color uses SIM_u16ToRGB. |
| 2026-10-09 | [f34319a](https://github.com/cybervisi0n/libntr/commit/f34319a9c592147513c8dca0bee6ceade73c2349) | WVR: Shutdown properly on ported platforms | Adapt wireless shutdown callback completion; fix payload handling rather than copying upstream verbatim. |
| 2026-10-09 | [5f6b529](https://github.com/cybervisi0n/libntr/commit/5f6b5292a8cf55f0a40f00f72b41bbc6b6da4885) | SND: Fix sound register corruption | Already patch-equivalent in fork (git cherry confirms); no current file diff. |
| 2026-10-07 | [35d218e](https://github.com/cybervisi0n/libntr/commit/35d218e04ff11386336300264a8d89561ac51070) | Fast forward/GUI improvements | Fork already has configurable hold fast-forward and independent GUI controls; unique ImGui IDs use field addresses. Toggle is an optional UX choice. |
| 2026-10-07 | [eef1eca](https://github.com/cybervisi0n/libntr/commit/eef1ecaff838164942073396cf92056f58e82851) | FX: Implement MTX_Scale43_ | Already implemented more completely: fork initializes all MTX_Scale43_ entries. |
| 2026-10-07 | [da12184](https://github.com/cybervisi0n/libntr/commit/da121847ecfd9457c6e59e791da088b270c4becc) | G3: Remove now unused static size textures | Already equivalent: old fixed-size pool removed; fork uses its own GPU cache. |
| 2026-10-07 | [f0fba0d](https://github.com/cybervisi0n/libntr/commit/f0fba0d734611128bbd17d2a860e1e407d484752) | G3: Improve texture cache | Already equivalent or stronger: bounded GPU cache, metadata/content validation, queued texture pinning. |
| 2026-10-07 | [242eae2](https://github.com/cybervisi0n/libntr/commit/242eae2c58656bc536741a1cac88445b91a58ca2) | Adds frame cap key to the simulator, and adjusts btnIDs in sim_gui_pad.cpp to not overlap (#2) | Do not overwrite fork speed controls; hold speedup and unique UI IDs are present. Persistent uncap toggle would be an optional separate feature. |
| 2026-10-05 | [12fbb5c](https://github.com/cybervisi0n/libntr/commit/12fbb5cf0e4c9c99f09ea20a5494fbebb89056b1) | Add internal resolution slider | Already adapted in 1601331 and subsequent render-target work. |
| 2026-10-04 | [71e1b73](https://github.com/cybervisi0n/libntr/commit/71e1b73fc77a595bdf53e2877fa08c09d0c099c4) | SND: Fix sound volume, add volume slider to imgui | Already adapted in 1601331 and later audio fixes; saved master volume is present. |
| 2026-10-02 | [f20fa9e](https://github.com/cybervisi0n/libntr/commit/f20fa9ef095ea663e494b998f87f13607ab19387) | SIM: change fs path separator | Already in shared ancestry. |
| 2026-10-02 | [78b5ab6](https://github.com/cybervisi0n/libntr/commit/78b5ab6ed23543c2f7e07c1d536289adcff500f9) | SIM: Add ROM extractor via fireflower | Already in shared ancestry; fork also adds ROM extraction validation in f793843. |
| 2026-10-02 | [fd3fc0b](https://github.com/cybervisi0n/libntr/commit/fd3fc0b7f39dbdfc2e4104be47d3843d2d9e76c5) | SIM/FS: Set up initial ROM extraction flow | Already in shared ancestry. |
| 2026-10-02 | [72a881a](https://github.com/cybervisi0n/libntr/commit/72a881a9d92f9032fd6667c4915614a2f8164e16) | SIM: use std::filesystem for ImGUI File dialog | Already in shared ancestry. |
| 2026-10-02 | [d3b679d](https://github.com/cybervisi0n/libntr/commit/d3b679d4340095d432992d300ffedf1797939c88) | Add ImGui file dialog | Already in shared ancestry. |

## Verification limits

The initial review used source/history inspection plus one isolated arithmetic reproduction, without game runtime validation. The pre-integration reproduction files were `/tmp/libntr-october-review-mtx33.cpp` and `/tmp/libntr-october-review-mtx33`. The subsequent integration uses the persistent regression suite and build validation described above.
