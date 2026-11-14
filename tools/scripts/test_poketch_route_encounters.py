#!/usr/bin/env python3
"""Check the route encounters Pokétch app's list builder: methods, dedupe, time of day, seen and caught."""

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
    entry = next(c for c in commands if c["file"].endswith("/src/applications/poketch/route_encounters/data.c"))
    flags = shlex.split(entry["command"])
    flags = flags[:flags.index("-MD")]
    with tempfile.TemporaryDirectory(prefix="poketch-route-encounters-") as directory:
        executable = Path(directory) / "test"
        subprocess.run(flags + ["-UNDEBUG",
                                str(ROOT / "tools/scripts/poketch_route_encounters_test.c"),
                                str(ROOT / "src/applications/poketch/route_encounters/data.c"),
                                "-o", str(executable)], cwd=build, check=True)
        subprocess.run([str(executable)], check=True)
    print("poketch route encounters: ok")


if __name__ == "__main__":
    main()
