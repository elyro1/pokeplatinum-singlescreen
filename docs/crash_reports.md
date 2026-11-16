# Logs and crash reports

## log.txt

Every start moves the previous `log.txt` in the game folder to `log.prev.txt` and starts a new one. Everything the game
prints to stdout and stderr goes there, and still to the terminal. The log starts with:

- the date, the build (git hashes) and the binary's ID (ELF build ID, or `pe-<timestamp>` for `main.exe`)
- the OS and the OpenGL renderer
- an `effective settings` block: everything `--print-config` would list (`sim_config.ini`, `sim_config_prj.ini`,
  `gameplay_config.ini`, the `PLATINUM_*` variables). It's written again about 2 s after one of those files changes,
  so the last block is the one that was in effect.

The log stops growing at 64 MB, apart from crash and stall reports.

## Crash reports

When the game crashes (invalid memory access, illegal instruction, `abort()` or a failed `assert`, an unhandled
Windows exception), it appends a report to `log.txt` and prints it to stderr:

```
=== CRASH: SIGSEGV (invalid memory access) at address 0x10 in thread "main" (game thread) ===
faulting instruction:
./main(+0xb0221) [0x5629396f6221]
stack (the first lines are the crash handler):
...
frame 1580, game thread phase: game
build: e8ddb9a304, libntr 7136fa4, binary 07d907e2f935fbd1ad0b1eab8c1036428a4efdc1
game: frame 1400, vblank 1579
application: ./main(+0x16e9b7) [0x5629397b49b7]
map header 326, player at 15,6, field map running
settings: the last "effective settings" block above
=== end of crash report; please send log.txt ===
```

- The stack is module+offset per frame. The names come from the symbolizer below.
- `frame` counts presented frames. `phase` says whether the game thread was rendering or running game logic.
- `game: frame` counts main-loop iterations (what `PLATINUM_DEBUG_CRASH` counts). `application` is the running
  application's main function, with its child applications below it.
- `map header` is the field map (its name is in `generated/map_headers.txt`). The player position is in tiles.

Afterwards the game exits with the usual signal, so core dumps and exit codes don't change. On Windows it exits with the
exception code, without the Windows Error Reporting dialog.

ASan builds (`make linux-asan`) don't install the crash handlers, because ASan's own reports are more detailed.

## Stall reports

When no frame is presented for `StallWatchdogSeconds` (default 5), the game logs the same context plus the game
thread's current stack, without stopping the game, and logs `stall over` once frames resume. A loading screen that
really takes that long shows up as a stall followed by `stall over`.

The watchdog only starts after the first frame, so first-launch ROM extraction doesn't trigger it. It stays quiet while a
debugger is attached.

## Settings

`[Diagnostics]` in `sim_config.ini`, also under **Tab → Developer → Diagnostics**. Changes apply on the next start.

| Key | Default | Effect |
| --- | --- | --- |
| `WriteLogFile` | `true` | Writes `log.txt`. Off, output only goes to the terminal, and reports go to stderr |
| `CrashHandler` | `true` | Crash reports. Off, crashes behave as they did before |
| `StallWatchdogSeconds` | `5` | Seconds without a frame before a stall report (0–600, 0 turns it off) |

## Reading a report

```bash
tools/scripts/symbolize_crash.py log.txt --binary <unstripped main or main.exe>
```

This prints the log with a function and source line next to every address in the game binary, and the name next to
the map header. It needs the unstripped binary of the exact same build:

- your own build: `build_linux/pokeplatinum/main` (the default), or `build_win64/pokeplatinum/main.exe`
- a release: the release's own `main` or `main.exe`, which keep their debug info. The AppImage's copy is stripped, so
  for it use the `symbols_linux` artifact of the same CI run (or the `main` from the same release zip)

If the binary's ID doesn't match the one in the log, the script warns, because the names would be wrong. For
`main.exe` it uses `x86_64-w64-mingw32-addr2line` when that is installed.

To try it, crash on purpose with `PLATINUM_DEBUG_CRASH` (see [environment_variables.md](environment_variables.md)),
for example `PLATINUM_DEBUG_CRASH=segv:600 ./main`. `tools/scripts/test_crash_report.py` does this for each kind
and checks the reports.

## Implementation

The libntr side is `libraries/sim/src/sim_crash.cpp` (`include/simulator/sim_crash.h`): the log file with its two
copy threads, the signal handlers or exception filter, the stack walking and the watchdog. The game adds its lines
through `SIM_Crash_SetContextWriter` in `src/port/debug/crash_context.c`, which copies the values it reports once per
frame, so a report never follows game pointers. Reports run inside a signal handler, so they only use
`SIM_Crash_Printf`, a formatter that doesn't allocate.
