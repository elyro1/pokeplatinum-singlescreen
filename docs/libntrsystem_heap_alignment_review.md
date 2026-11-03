# NitroSystem exp-heap alignment review

Checked November 3, 2026. Recommendation: integrate HeartGold's `libntrsystem-native-heap.patch`. It fixes two reproduced native alignment defects and preserves Platinum's four-byte allocation requests.

## Scope

- Current NitroSystem checkout: `subprojects/libntrsystem`, HEAD `9c2309a2c995c919a67093858677f0f43459d7fc`.
- Candidate: `/home/lukas/code/pokeheartgold/pc/subprojects/packagefiles/libntrsystem-native-heap.patch`.
- The patch applies cleanly to our current `libraries/fnd/src/expheap.c`.
- Candidate source was prepared and tested under `/tmp/libntrsystem-heap-review/`. This review changed no production source, wrap, dependency pin or existing patches. HeartGold's checkout is unchanged.

## Defect and patch behavior

Our exp-heap uses `MIN_ALIGNMENT = 4` for heap boundaries, block sizes and splitting. Its native heap/block headers contain pointers and require eight-byte alignment on this 64-bit build. Four-byte boundaries therefore permit misaligned accesses to those headers.

The patch makes the native minimum `sizeof(void *)`, while preserving the four-byte minimum for ARM builds. It also promotes native allocation and maximum-size query requests smaller than that minimum, preserving their sign. On this build, `4` becomes `8`, and `-4` becomes `-8`; the sign still selects front or rear allocation.

This promotion is necessary because Platinum's `src/heap.c` explicitly passes `4` and `-4`. Its 16-byte `MemoryBlock` prefix preserves the resulting pointer alignment. The SDK's default-alignment macros and allocator adapter also continue to work.

The already integrated libntr arena fix does not replace this change: arena alignment controls the outer buffer, while this patch keeps the headers and split blocks *inside* a NitroSystem heap aligned.

## Reproduced failures in current code

With the actual current exp-heap, heap-common, list and native memory implementations compiled under ASan/UBSan:

| Probe | Current result | Patched result |
| --- | --- | --- |
| Create a heap from a buffer starting one byte past an aligned boundary | UBSan reports misaligned `NNSiFndHeapHead` member access in `heapcommon.c`. | Passes; heap and internal headers align correctly. |
| Allocate one byte from an aligned heap with alignment `4` | UBSan reports misaligned `NNSiFndExpHeapMBlockHead` member access when creating the remaining free block. | Passes; the split header stays aligned. |

These are undefined-behavior defects even where x86 tolerates the physical loads/stores. The candidate fixes them by aligning both the enclosing heap and all size-rounded split points.

## Candidate validation

Tests ran against our allocator sources with only the candidate patch applied, not a replacement of the allocator with HeartGold's older source tree. Both the configured final-ROM build definitions and a second build with NitroSystem's internal heap consistency checks compiled in passed ASan/UBSan.

Each configuration passed:

- 256 combinations of buffer offsets `0..15`, alignments `±4`, `±8`, `±16`, `±32`, and first-fit/near allocation modes.
- 16,384 small-block allocations, holding 64 blocks simultaneously per heap and covering request sizes `0..63`, with pointer/header alignment, list links, bounds, clear-on-allocation data, payload preservation and interleaved frees checked.
- Complete free-space recovery after freeing each allocation set.
- Maximum-allocatable-size queries in both directions: the reported maximum can be allocated, further allocation fails, and freeing restores the original capacity.
- 63 shrink/grow cases with payload preservation, aligned new free-block headers, successful regrowth and unchanged blocks after impossible growth requests.
- 512 calls through the actual `NNSFndAllocator` adapter, including legacy positive and negative four-byte alignment.
- Guard bytes around each supplied heap buffer.
- Correct free-hook invocation: the animation-bank cleanup boundary was reached 17,215 times per full run; the other native bank cleanup hooks remained linked as test stubs.

The harness uses the real exp-heap, heap-common, list, allocator-adapter and memory code. Resource-bank cleanup hooks are stubbed because no game graphics resources are loaded. Unused non-exp-heap adapter entries abort if called. LeakSanitizer was disabled for the execution environment; ASan and UBSan remained enabled.

## Integration recommendation

Carry the single-file patch alongside our existing `libntrsystem_patch` files and register it in `subprojects/libntrsystem.wrap`. Promote the paired reproduction into a persistent regression suite, retaining checks for `4` and `-4` calls and the actual heap consistency checker.

Allocation sizes now round to eight bytes instead of four on 64-bit hosts. Some blocks therefore use four additional payload bytes, and heap-boundary trimming can also reduce capacity slightly. The tests verify capacity reporting and free-space recovery under this rule; they do not measure an entire game's peak heap usage. Exact block addresses and free-size values should not be expected to match the old unsafe layout.

ARM behavior remains at four-byte alignment. This patch addresses the exp-heap; it does not establish correctness for the separate frame-heap, unit-heap or every native debug-validation path.

## Reproduction artifacts

- `/tmp/libntrsystem-heap-review/heap.c`: paired creation/splitting probes and allocator sweep/resize/adapter harness.
- `/tmp/libntrsystem-heap-review/run.py`: current-versus-candidate sanitizer checks.
- `/tmp/libntrsystem-heap-review/candidate_run.py`: candidate checks with normal definitions and heap consistency checks enabled.
- `/tmp/libntrsystem-heap-review/*-create.log`, `*-split.log`, `*-sweep.log`: recorded diagnostics and results.

No full game build or interactive gameplay session was run during this source/allocator review.
