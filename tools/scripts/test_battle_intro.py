#!/usr/bin/env python3
"""Check fast intro throw cleanup and overlapping BW2 wild shiny sparkles."""

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
    with tempfile.TemporaryDirectory(prefix="battle-intro-") as directory:
        for test, source in [("battle_intro", "/src/battle/battle_display.c"),
                             ("battle_intro_policy", "/adapter/bw_battle_view.c"),
                             ("battle_shiny_intro", "/view/btlv_effvm.c")]:
            entry = next(c for c in commands if c["file"].endswith(source))
            flags = shlex.split(entry["command"])
            flags = flags[:flags.index("-MD")]
            executable = Path(directory) / test
            subprocess.run(flags + ["-UNDEBUG", "-ffunction-sections", "-fdata-sections",
                                    str(ROOT / f"tools/scripts/{test}_test.c"),
                                    "-Wl,--gc-sections", "-o", str(executable)], cwd=build, check=True)
            subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == "__main__":
    main()
