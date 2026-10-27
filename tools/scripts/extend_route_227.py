#!/usr/bin/env python3
"""Close the two exposed rock filler corners beside Route 227's ponds.

Only the port's generated northern land model changes. The original model,
permissions, props, BDHC, map matrix and Nintendo DS assets stay intact.
"""
import argparse
import hashlib
import math
from pathlib import Path
import struct

from map_scenery import read_land, repack_land, u32

SOURCE_SHA256 = "f74786fea05292b88d703b9627d1e4449c01f57ff6cd1f2c1c3efb045505eb33"


def extend(source):
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise ValueError("Expected the original Route 227 northern BIN")
    model, _, mdl, _, shapes = read_land(source)
    additions = [[] for _ in shapes]
    # Inspected SBC: polygon4 binds criffp_lm55, the route's rounded rock
    # face. The adjacent MAP_175 fillers sit at Y=96; the forest is at Y=16.
    # Follow every authored terrace at X=+/-256 before turning south around
    # each filler. These faces remain outside the playable terrain.
    for x in (-256, 256):
        profile = []
        for polygon in shapes[4][4]:
            for a, b in zip(polygon, polygon[1:] + polygon[:1]):
                pa, pb = a[0], b[0]
                if pa[0] == pb[0] == x and pa[1] != pb[1]:
                    profile.append(tuple(sorted(((pa[2], pa[1]), (pb[2], pb[1])))))
        profile.sort()
        if len(profile) != 15 or any(a[1] != b[0] for a, b in zip(profile, profile[1:])):
            raise ValueError("Unexpected Route 227 side cliff profile")
        profile.append((profile[-1][1], (256, 16)))
        for (z0, y0), (z1, y1) in profile:
            points = [(x, 96, z0), (x, 96, z1), (x, y1, z1), (x, y0, z0)]
            if x > 0:
                points.reverse()
            additions[4].append([(p, (p[2] * 2, (96 - p[1]) * 2),
                                  0x7FFF, 0x1FF if x < 0 else 0x201) for p in points])

        # Continue the south-facing wall fourteen tiles into each margin,
        # matching the extent used by the earlier widescreen map edits.
        # Tile the original 16-unit cliff bands instead of stretching texels.
        left, right = sorted((x, x + (-224 if x < 0 else 224)))
        for x0 in range(left, right, 112):
            for y0 in range(16, 96, 16):
                points = [(x0, y0 + 16, 256), (x0, y0, 256),
                          (x0 + 112, y0, 256), (x0 + 112, y0 + 16, 256)]
                additions[4].append([(p, (p[0] * 2, (y0 + 16 - p[1]) * 2),
                                      0x7FFF, 0x1FF00000) for p in points])

    # Include the original overhanging tree crowns and the new corner walls.
    quantum = struct.unpack_from("<i", model, mdl + 56)[0] / 4096 / 4096
    points = [v[0] for shape in shapes for polygon in shape[4] for v in polygon]
    points += [v[0] for extra in additions for polygon in extra for v in polygon]
    low = [math.floor(min(p[i] for p in points) / quantum) * quantum for i in range(3)]
    high = [math.ceil(max(p[i] for p in points) / quantum) * quantum for i in range(3)]
    return repack_land(source, additions, low + [high[i] - low[i] for i in range(3)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result, count = extend(args.input.read_bytes())
    args.output.write_bytes(result)
    print(f"Route 227: {count} cliff corner quads; model {u32(result, 8)} bytes")


if __name__ == "__main__":
    main()
