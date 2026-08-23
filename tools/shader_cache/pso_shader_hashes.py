#!/usr/bin/env python3
"""Compute reblue shader-cache content hashes for loose .vso/.pso files.

Replicates XenosRecomp's container scan (XenosRecomp/main.cpp): scan for the
X360 shader container magic ((flags & 0xFFFFFF00) == 0x102A1100, big-endian),
hash XXH3_64 over virtualSize+physicalSize bytes from the container start.
These hashes match ShaderCacheEntry.hash at runtime.

Usage: pso_shader_hashes.py <shader-dir> [out.csv]
Output CSV: name,hash (hash 16-hex uppercase, one row per container found;
files with multiple containers get name#N suffixes).
"""
import struct
import sys
from pathlib import Path

import xxhash

CONTAINER_HEADER = struct.Struct(">9I")  # flags..field20, all big-endian


def containers(data: bytes):
    i = 0
    limit = len(data) - CONTAINER_HEADER.size - 1
    while i < limit:
        (flags, virtual_size, physical_size, _field_c, _const_tab, _def_tab,
         _shader_off, field_1c, field_20) = CONTAINER_HEADER.unpack_from(data, i)
        data_size = virtual_size + physical_size
        if ((flags & 0xFFFFFF00) == 0x102A1100 and data_size <= len(data) - i
                and field_1c == 0 and field_20 == 0):
            yield data[i:i + data_size]
            i += data_size
        else:
            i += 4


def main():
    shader_dir = Path(sys.argv[1])
    rows = []
    for path in sorted(shader_dir.rglob("*")):
        if path.suffix.lower() not in (".vso", ".pso"):
            continue
        found = list(containers(path.read_bytes()))
        for n, blob in enumerate(found):
            name = path.name if len(found) == 1 else f"{path.name}#{n}"
            rows.append((name, xxhash.xxh3_64_hexdigest(blob).upper()))
    out = "\n".join(f"{name},{h}" for name, h in rows) + "\n"
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_text("name,hash\n" + out)
    else:
        sys.stdout.write(out)


if __name__ == "__main__":
    main()
