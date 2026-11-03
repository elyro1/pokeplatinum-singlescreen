#!/usr/bin/env python3
"""Import HG/SS's shiny overworld palettes for the BW2 follower sprites.

Usage: python3 tools/scripts/import_follower_shiny_palettes.py HEARTGOLD.nds BLACK2.nds

BW2 ships the same 566 Gen 4 walking sprites as HG/SS (a/0/4/8, listed by a/2/0/8) with the
same normal colours, but without shiny palettes. This writes HG/SS's shiny palettes, and the
BW2 species/sex/form key of each manifest variant, to bw2_sprites.json. Only colours are
imported; the desktop build converts the textures from the player's BW2 extraction.
"""
import argparse
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / 'res/graphics/following_pokemon'
HG_SPRITE_BASE = 297


def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]


def rom_file(rom, path):
    fnt, fat = u32(rom, 0x40), u32(rom, 0x48)
    directory = 0xF000
    parts = path.split('/')
    for depth, name in enumerate(parts):
        entry = fnt + (directory & 0xFFF) * 8
        pos, file_id = fnt + u32(rom, entry), u16(rom, entry + 4)
        while True:
            kind = rom[pos]
            pos += 1
            if kind == 0:
                raise KeyError(path)
            length = kind & 0x7F
            entry_name = rom[pos:pos + length].decode()
            pos += length
            if kind & 0x80:
                sub = u16(rom, pos)
                pos += 2
                if entry_name == name and depth < len(parts) - 1:
                    directory = sub
                    break
            else:
                if entry_name == name and depth == len(parts) - 1:
                    return rom[u32(rom, fat + file_id * 8):u32(rom, fat + file_id * 8 + 4)]
                file_id += 1
    raise KeyError(path)


def narc(data):
    if data[:4] != b'NARC':
        raise ValueError('Not a NARC')
    pos = u16(data, 12)
    count = u32(data, pos + 8)
    table = [(u32(data, pos + 12 + i * 8), u32(data, pos + 16 + i * 8)) for i in range(count)]
    pos += u32(data, pos + 4)
    pos += u32(data, pos + 4)
    if data[pos:pos + 4] != b'GMIF':
        raise ValueError('Missing NARC images')
    return [data[pos + 8 + start:pos + 8 + end] for start, end in table]


def palettes(btx):
    if btx[:4] != b'BTX0':
        raise ValueError('Expected an uncompressed NSBTX')
    tex = u32(btx, 16)
    pal_dict = tex + u16(btx, tex + 52)
    entry = pal_dict + u16(btx, pal_dict + 6) + 4
    data = tex + u32(btx, tex + 56)
    result = []
    for i in range(btx[pal_dict + 1]):
        offset = data + (u16(btx, entry + i * 4) << 3)
        result.append([u16(btx, offset + c * 2) for c in range(16)])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('heartgold', type=Path)
    parser.add_argument('black2', type=Path)
    args = parser.parse_args()

    hg, bw = args.heartgold.read_bytes(), args.black2.read_bytes()
    if hg[12:16] not in (b'IPKE', b'IPGE'):
        raise SystemExit('The first ROM must be US HeartGold or SoulSilver')
    if bw[12:16] not in (b'IREO', b'IRDO'):
        raise SystemExit('The second ROM must be US Black 2 or White 2')
    manifest = json.loads((DEST / 'manifest.json').read_text())
    hg_sprites = narc(rom_file(hg, 'a/0/8/1'))
    tpoke = narc(rom_file(bw, 'a/2/0/8'))[0]
    bw_table = narc(rom_file(bw, 'a/0/4/7'))[0]
    bw_sprites = narc(rom_file(bw, 'a/0/4/8'))
    entries = [struct.unpack_from('<4H', tpoke, i) for i in range(0, len(tpoke) - 7, 8)]

    keys = {}
    for mon in manifest['species']:
        if mon['female']:
            keys[mon['base']] = (mon['species'], 0, 0)
            keys[mon['base'] + 1] = (mon['species'], 1, 0)
        else:
            for form in range(mon['max_form'] + 1):
                keys[mon['base'] + form] = (mon['species'], 0, form)

    gen4 = [i for i, e in enumerate(entries) if e[0] <= 493]
    lines = []
    for asset in manifest['assets']:
        species, sex, form = keys[asset['index']]
        # BW2's lookup (tpoke_data.c): sex 2 fits either sex
        match = next(i for i, e in enumerate(entries)
                     if e[0] == species and (e[1] == 2 or e[1] == sex) and e[2] == form)
        if match != gen4[asset['index']]:
            raise SystemExit(f'{asset["name"]}: BW2 lists it out of HG/SS order')
        obj = entries[match][3]
        record = 4 + (obj - 0x1000 + 0x179) * 28
        bw_normal = palettes(bw_sprites[u16(bw_table, record + 16)])[0]
        hg_normal, hg_shiny = palettes(hg_sprites[HG_SPRITE_BASE + asset['index']])
        # Colour 0 is transparent; Fearow's differs there only
        if bw_normal[1:] != hg_normal[1:]:
            raise SystemExit(f'{asset["name"]}: BW2 normal colours differ from HG/SS')
        colours = ' '.join(f'{c:04x}' for c in hg_shiny)
        lines.append(f'    {{"index": {asset["index"]}, "name": "{asset["name"]}", "species": {species}, '
                     f'"sex": {sex}, "form": {form}, "shiny": "{colours}"}}')

    out = DEST / 'bw2_sprites.json'
    out.write_text('{\n  "format": 1,\n  "sprites": [\n' + ',\n'.join(lines) + '\n  ]\n}\n')
    print(f'Wrote {len(lines)} shiny palettes to {out.relative_to(ROOT)}')


if __name__ == '__main__':
    main()
