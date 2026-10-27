#!/usr/bin/env python3
"""Exercise gameplay config startup, fixed defaults, BP pricing, and vitamin EV boundaries."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build_linux")
    args = parser.parse_args()
    build = args.build.resolve()
    commands = json.loads((build / "compile_commands.json").read_text())
    entry = next(c for c in commands if c["file"].endswith("/src/port/gameplay_config.c"))
    flags = shlex.split(entry["command"])
    flags = flags[:flags.index("-MD")]
    with tempfile.TemporaryDirectory(prefix="gameplay-config-") as directory:
        source = ROOT / "src/port/gameplay_config.c"
        # Exercise the documented ROM hack workflow by commenting out the actual startup call.
        disabled_source = Path(directory) / "gameplay_config_defaults_only.c"
        contents = source.read_text()
        call = "    GameplayConfig_LoadFile();"
        assert contents.count(call) == 1
        disabled_source.write_text(contents.replace(call, "    // GameplayConfig_LoadFile();"))
        for name, config_source, defines in [
            ("file", source, []),
            ("defaults-only", disabled_source, ["-DGAMEPLAY_CONFIG_DEFAULTS_ONLY"]),
        ]:
            run_directory = Path(directory) / name
            run_directory.mkdir()
            executable = run_directory / "test"
            subprocess.run(flags + defines + ["-UNDEBUG", "-ffunction-sections", "-fdata-sections",
                                              str(ROOT / "tools/scripts/gameplay_config_test.c"),
                                              str(config_source),
                                              str(ROOT / "subprojects/libntr/libraries/sim/src/config/ini.c"),
                                              "-Wl,--gc-sections", "-o", str(executable)], cwd=build, check=True)
            subprocess.run([str(executable)], cwd=run_directory, check=True)


if __name__ == "__main__":
    main()
