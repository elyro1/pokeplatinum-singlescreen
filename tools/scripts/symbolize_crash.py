#!/usr/bin/env python3
"""Add function names and source lines to the crash and stall reports in a log.txt.

Reports print code addresses as module+offset (`./main(+0x1234) [...]` on Linux, `main.exe+0x1234 [...]` on
Windows). This resolves the ones in the game binary with addr2line, against the unstripped binary of the same build
(CI keeps it as the `symbols` artifact), and names map headers.

    tools/scripts/symbolize_crash.py log.txt --binary build_linux/pokeplatinum/main
"""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]

LINUX_ADDRESS = re.compile(r'(?P<module>[^\s(]+)\((?P<symbol>[^)+]*)\+0x(?P<offset>[0-9a-f]+)\) \[0x[0-9a-f]+\]')
WINDOWS_ADDRESS = re.compile(r'(?P<module>[\w.\-]+\.(?:exe|dll))\+0x(?P<offset>[0-9a-f]+) \[0x[0-9a-f]+\]',
                             re.IGNORECASE)
BINARY_ID = re.compile(r'binary (\S+)')
MAP_HEADER = re.compile(r'map header (\d+)')


def read_output(command):
    try:
        return subprocess.run(command, capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return ''


def binary_info(binary):
    """(is_pe, id as the game logs it, image base)"""
    with open(binary, 'rb') as file:
        is_pe = file.read(2) == b'MZ'
    if not is_pe:
        match = re.search(r'Build ID: ([0-9a-f]+)', read_output(['readelf', '-n', str(binary)]))
        return False, match.group(1) if match else None, 0
    headers = read_output([shutil.which('x86_64-w64-mingw32-objdump') or 'objdump', '-p', str(binary)])
    base = re.search(r'^ImageBase\s+([0-9a-fA-F]+)', headers, re.MULTILINE)
    # The timestamp as the game logs it: the raw value from the COFF header
    data = binary.read_bytes()
    pe_offset = int.from_bytes(data[0x3c:0x40], 'little')
    raw_stamp = int.from_bytes(data[pe_offset + 8:pe_offset + 12], 'little')
    return True, f'pe-{raw_stamp:08x}', int(base.group(1), 16) if base else 0x140000000


def addr2line(binary, is_pe, addresses):
    tool = shutil.which('x86_64-w64-mingw32-addr2line') if is_pe else None
    tool = tool or shutil.which('addr2line')
    if not tool:
        sys.exit('addr2line not found (install binutils)')
    output = read_output([tool, '-a', '-f', '-C', '-i', '-e', str(binary), *(f'0x{a:x}' for a in addresses)])
    results, current = {}, None
    lines = output.splitlines()
    i = 0
    while i < len(lines):
        if lines[i].startswith('0x'):
            current = int(lines[i], 16)
            results[current] = []
            i += 1
            continue
        function = lines[i]
        location = lines[i + 1] if i + 1 < len(lines) else '??:0'
        if current is not None and function != '??':
            location = re.sub(r'^.*?/(src|include|subprojects|tools)/', r'\1/', location)
            results[current].append(f'{function} at {location}')
        i += 2
    return results


def map_header_names():
    path = ROOT / 'generated/map_headers.txt'
    return [line.strip() for line in path.read_text().splitlines() if line.strip()] if path.exists() else []


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', type=Path, help='log.txt or log.prev.txt from the game folder')
    parser.add_argument('--binary', type=Path, default=ROOT / 'build_linux/pokeplatinum/main',
                        help='unstripped main or main.exe of the same build (default: %(default)s)')
    args = parser.parse_args()

    is_pe, binary_id, image_base = binary_info(args.binary)
    module_name = args.binary.name.lower()
    text = args.log.read_text(errors='replace').splitlines()

    for logged in sorted(set(BINARY_ID.findall('\n'.join(text)))):
        if binary_id and logged != binary_id:
            print(f'warning: the log is from binary {logged}, but {args.binary} is {binary_id}; '
                  'names below are probably wrong', file=sys.stderr)

    def address_of(line):
        match = (WINDOWS_ADDRESS if is_pe else LINUX_ADDRESS).search(line)
        if not match or Path(match.group('module')).name.lower() != module_name:
            return None
        if not is_pe and match.group('symbol'):
            return None
        offset = int(match.group('offset'), 16)
        return image_base + offset if is_pe else offset

    addresses = sorted({a for a in map(address_of, text) if a is not None})
    names = addr2line(args.binary, is_pe, addresses) if addresses else {}
    maps = map_header_names()
    for line in text:
        address = address_of(line)
        if address is not None and names.get(address):
            frames = names[address]
            print(f'{line}  -> {frames[0]}')
            for inlined in frames[1:]:
                print(f'{" " * 8}   (inlined into {inlined})')
            continue
        match = MAP_HEADER.search(line)
        if match and int(match.group(1)) < len(maps):
            line += f' ({maps[int(match.group(1))]})'
        print(line)


if __name__ == '__main__':
    main()
