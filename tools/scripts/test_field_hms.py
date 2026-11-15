#!/usr/bin/env python3
"""Exercise HM field eligibility with mocked Pokemon storage and compatibility."""

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
    entry = next(c for c in commands if c["file"].endswith("/src/port/gameplay/gameplay_config.c"))
    flags = shlex.split(entry["command"])
    flags = flags[:flags.index("-MD")]
    with tempfile.TemporaryDirectory(prefix="field-hms-") as directory:
        executable = Path(directory) / "test"
        subprocess.run(flags + ["-UNDEBUG", "-ffunction-sections", "-fdata-sections",
                               str(ROOT / "tools/scripts/field_hm_test.c"),
                               str(ROOT / "src/unk_02054884.c"),
                               str(ROOT / "src/item.c"),
                               str(ROOT / "src/port/gameplay/gameplay_config.c"),
                               str(ROOT / "subprojects/libntr/libraries/sim/src/config/ini.c"),
                               str(ROOT / "subprojects/libntr/libraries/sim/src/config/config_check.c"),
                               "-Wl,--gc-sections", "-o", str(executable)], cwd=build, check=True)
        subprocess.run([str(executable)], cwd=directory, check=True)


if __name__ == "__main__":
    main()
