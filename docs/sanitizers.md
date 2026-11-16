# Sanitizer build and warnings

## ASan + UBSan

```sh
make linux-asan
```

This builds the Linux port into `build_linux_asan/` with AddressSanitizer and UndefinedBehaviorSanitizer (meson
option `-Dsanitize=true`). Every target is instrumented: the game, `lib/`, `src/bwbattle` and the SDK subprojects
(libntr, libntrsystem, ...), plus the host tools that run during the build. It compiles at `-O1`, so the game stays
playable at about half speed.

- UBSan's `shift` check is off: the DS code shifts negative values everywhere, which GCC defines.
- `make linux-asan` sets `ASAN_OPTIONS=detect_leaks=0` while building, because the asset tools leak on exit. Set it
  yourself if you call `ninja -C build_linux_asan` directly.
- libntr's `sim_vars.c` keeps its globals out of ASan (`--param=asan-globals=0`) and in source order
  (`-fno-toplevel-reorder`): the SDK treats neighbouring DS registers and memory blocks as one block, as in the DS's
  memory map.

### Running the game under it

Run it from a scratch run dir, like any test run (AGENTS.md, "Running the game for checks"), with a copy of
`build_linux_asan/pokeplatinum/main`. The ROM-extracted assets are the same as `build_linux`'s, so symlink those.

```sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 SDL_AUDIODRIVER=dummy \
    PLATINUM_AUTOPILOT=scene.txt PLATINUM_DEBUG_WILD=25:5 ./main 2> asan.log
```

ASan stops at the first bad access and prints where the memory was allocated or which stack variable it hit.
UBSan reports and goes on. Leak checks are off because GL drivers and SDL leak at exit.

Expected UBSan noise:

- `mi_dma.c` and `timer.h`, "insufficient space": the SDK writes neighbouring registers through one register's
  address, as above.
- `g2d_CellAnimation.c` / `g2d_MultiCellAnimation.c`, misaligned `NNSG2dAnimDataSRT`: animation data read in place
  from ROM files. x86 tolerates unaligned loads.

## Warnings

`src/port`, the bwbattle adapter and `src/bwbattle` build with `-Wall`. The decomp code and the SDK keep their
warnings off (`-w`). The SDK's headers are included as system headers in those targets, so only warnings in our own
files show. `-Dport_werror=true` turns them into errors:

```sh
meson configure build_linux -Dport_werror=true
```

The port files build as the static library `libport`, whose objects are linked into `main`. Their objects are in
`build_linux/libport.a.p/`, not `main.p/`.
