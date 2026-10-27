#!/usr/bin/env python3
"""Independently verify Route 227's cliff mesh and member-level archive delta.

Usage: verify_route_227.py original.bin generated.bin before.narc after.narc
The baseline archive must contain the same other port scenery edits.
"""
import argparse
from pathlib import Path
import struct

# This decoder uses struct only and does not import the scenery generator.
from verify_route_201 import added_vertices, entries, members, model, num


def covers(quads, point, axis):
    """Point-in-convex-polygon check in a wall's two-dimensional plane."""
    dimensions = [i for i in range(3) if i != axis]
    for quad in quads:
        if any(p[axis] != point[axis] for p in quad):
            continue
        cross = []
        for a, b in zip(quad, quad[1:] + quad[:1]):
            u, v = dimensions
            cross.append((b[u] - a[u]) * (point[v] - a[v])
                         - (b[v] - a[v]) * (point[u] - a[u]))
        if any(cross) and (all(c >= 0 for c in cross) or all(c <= 0 for c in cross)):
            return True
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "generated", "before", "after"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    old, new = args.original.read_bytes(), args.generated.read_bytes()
    a, am, ads, start = model(old)
    b, bm, bds, bstart = model(new)
    assert start == bstart and old[:8] == new[:8]
    assert old[12:start] == new[12:start]  # permissions and props
    assert old[start + len(a):] == new[start + len(b):]  # BDHC and padding
    assert len(b) < 0xF000 and len(ads) == len(bds) == 18
    changed = [i for i, (x, y) in enumerate(zip(ads, bds)) if x != y]
    assert changed == [4]
    assert all(y.startswith(x) for x, y in zip(ads, bds))
    scale = num(b, bm + 28, "i") / 4096
    vertices = added_vertices(bds[4][len(ads[4]):], scale)
    assert len(vertices) == 208
    quads = [vertices[i:i + 4] for i in range(0, len(vertices), 4)]
    # Both full-height side walls and both south returns must cover the void.
    for sign in (-1, 1):
        for z in (161, 192, 224, 255):
            for y in (17, 40, 72, 95):
                assert covers(quads, (sign * 256, y, z), 0)
        for x in (257, 300, 400, 479):
            for y in (17, 40, 72, 95):
                assert covers(quads, (sign * x, y, 256), 2)
    assert all(abs(p[0]) >= 256 and 16 <= p[1] <= 96 for p in vertices)
    # Dictionaries, nodes, materials and original GX streams are unchanged.
    table = am + num(a, am + 12)
    headers = [table + num(a, entry) for entry in entries(a, table)]
    tail = min(h + num(a, h + 8) for h in headers)
    prefix = bytearray(b[:tail])
    fields = [(8, 4), (num(a, 16) + 4, 4), (am, 4), (am + 16, 4),
              (am + 36, 4), (am + 42, 2), (am + 44, 12)]
    fields += [(h + 8, 8) for h in headers]
    assert num(b, headers[4] + 4) == num(a, headers[4] + 4) | 7
    fields.append((headers[4] + 4, 4))
    for offset, length in fields:
        prefix[offset:offset + length] = a[offset:offset + length]
    assert bytes(prefix) == a[:tail]
    for offset, delta in ((36, 208), (38, 52), (42, 52)):
        assert num(b, bm + offset, "H") == num(a, am + offset, "H") + delta
    box = struct.unpack_from("<6h", b, bm + 44)
    unit = num(b, bm + 56, "i") / 4096 / 4096
    assert all(box[d] * unit <= p[d] <= (box[d] + box[d + 3]) * unit
               for p in vertices for d in range(3))
    before, after = members(args.before.read_bytes()), members(args.after.read_bytes())
    assert len(before) == len(after) == 666
    assert [i for i, (x, y) in enumerate(zip(before, after)) if x != y] == [170]
    assert after[170][:len(new)] == new
    print("PASS: only member 170 / shape 4 changed; both cliff corners covered; "
          "original geometry, materials, gameplay data, bounds and counts verified")


if __name__ == "__main__":
    main()
