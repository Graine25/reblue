#!/usr/bin/env python3
"""Aggregate recorded PSO coverage CSVs into the compiled-in static cache.

Inputs are CSV files or folders to scan recursively, so this can be pointed
straight at the cache dir where per-session pso_misses_<timestamp>.csv files
land. Rows with a zero vsHash (unresolvable runtime hcg-HLSL shaders) or a
non-1 sampleCount (MSAA forced off at runtime) are dropped, both from new CSV
rows and from lines already in the output header.
"""
import argparse
import csv
import sys
from pathlib import Path

# plume enum value -> name (small, stable enums). RenderFormat is large/varied,
# so it is emitted as static_cast<plume::RenderFormat>(N) instead.
ENUMS = {
    "RenderBlend": {1: "ZERO", 2: "ONE", 3: "SRC_COLOR", 4: "INV_SRC_COLOR",
                    5: "SRC_ALPHA", 6: "INV_SRC_ALPHA", 7: "DEST_ALPHA",
                    8: "INV_DEST_ALPHA", 9: "DEST_COLOR", 10: "INV_DEST_COLOR",
                    11: "SRC_ALPHA_SAT", 12: "BLEND_FACTOR",
                    13: "INV_BLEND_FACTOR", 14: "SRC1_COLOR",
                    15: "INV_SRC1_COLOR", 16: "SRC1_ALPHA", 17: "INV_SRC1_ALPHA"},
    "RenderBlendOperation": {1: "ADD", 2: "SUBTRACT", 3: "REV_SUBTRACT",
                             4: "MIN", 5: "MAX"},
    "RenderCullMode": {0: "UNKNOWN", 1: "NONE", 2: "FRONT", 3: "BACK"},
    "RenderFillMode": {0: "UNKNOWN", 1: "SOLID", 2: "WIREFRAME"},
    "RenderFrontFace": {0: "UNKNOWN", 1: "CLOCKWISE", 2: "COUNTER_CLOCKWISE"},
    "RenderComparisonFunction": {0: "UNKNOWN", 1: "NEVER", 2: "LESS", 3: "EQUAL",
                                 4: "LESS_EQUAL", 5: "GREATER", 6: "NOT_EQUAL",
                                 7: "GREATER_EQUAL", 8: "ALWAYS"},
    "RenderStencilOp": {0: "UNKNOWN", 1: "KEEP", 2: "ZERO", 3: "REPLACE",
                        4: "INCREMENT_AND_CLAMP", 5: "DECREMENT_AND_CLAMP",
                        6: "INVERT", 7: "INCREMENT_AND_WRAP",
                        8: "DECREMENT_AND_WRAP"},
    "RenderPrimitiveTopology": {0: "UNKNOWN", 1: "POINT_LIST", 2: "LINE_LIST",
                                3: "LINE_STRIP", 4: "TRIANGLE_LIST",
                                5: "TRIANGLE_STRIP", 6: "TRIANGLE_FAN"},
    # Common formats BD actually targets; anything else falls back to a cast.
    "RenderFormat": {0: "UNKNOWN", 2: "R32G32B32A32_FLOAT",
                     10: "R16G16B16A16_FLOAT", 11: "R16G16B16A16_UNORM",
                     16: "R32G32_FLOAT", 20: "R8G8B8A8_UNORM",
                     24: "B8G8R8A8_UNORM", 26: "R16G16_FLOAT",
                     27: "R16G16_UNORM", 32: "D32_FLOAT", 33: "D32_FLOAT_S8_UINT",
                     34: "R32_FLOAT"},
}


def enum(kind, value):
    name = ENUMS.get(kind, {}).get(value)
    return f"plume::{kind}::{name}" if name else f"static_cast<plume::{kind}>({value})"


# (csv column, cpp field, kind, default). kind drives formatting + the
# emit-only-if-non-default check (matches PipelineState's member initializers).
FIELDS = [
    ("instancing", "instancing", "bool", 0),
    ("zEnable", "zEnable", "bool", 1),
    ("zWriteEnable", "zWriteEnable", "bool", 1),
    ("stencilEnable", "stencilEnable", "bool", 0),
    ("stencilTwoSided", "stencilTwoSided", "bool", 0),
    ("srcBlend", "srcBlend", "RenderBlend", 2),
    ("destBlend", "destBlend", "RenderBlend", 1),
    ("cullMode", "cullMode", "RenderCullMode", 1),
    ("fillMode", "fillMode", "RenderFillMode", 1),
    ("frontFace", "frontFace", "RenderFrontFace", 1),
    ("zFunc", "zFunc", "RenderComparisonFunction", 2),
    ("stencilFunc", "stencilFunc", "RenderComparisonFunction", 8),
    ("stencilFail", "stencilFail", "RenderStencilOp", 1),
    ("stencilZFail", "stencilZFail", "RenderStencilOp", 1),
    ("stencilPass", "stencilPass", "RenderStencilOp", 1),
    ("stencilFuncCCW", "stencilFuncCCW", "RenderComparisonFunction", 8),
    ("stencilFailCCW", "stencilFailCCW", "RenderStencilOp", 1),
    ("stencilZFailCCW", "stencilZFailCCW", "RenderStencilOp", 1),
    ("stencilPassCCW", "stencilPassCCW", "RenderStencilOp", 1),
    ("stencilMask", "stencilMask", "hex", 255),
    ("stencilWriteMask", "stencilWriteMask", "hex", 255),
    ("stencilRef", "stencilRef", "int", 0),
    ("alphaBlendEnable", "alphaBlendEnable", "bool", 0),
    ("blendOp", "blendOp", "RenderBlendOperation", 1),
    ("slopeScaledDepthBias", "slopeScaledDepthBias", "float", 0.0),
    ("depthBias", "depthBias", "int", 0),
    ("srcBlendAlpha", "srcBlendAlpha", "RenderBlend", 2),
    ("destBlendAlpha", "destBlendAlpha", "RenderBlend", 1),
    ("blendOpAlpha", "blendOpAlpha", "RenderBlendOperation", 1),
    ("colorWriteEnable", "colorWriteEnable", "hex", 15),
    ("primitiveTopology", "primitiveTopology", "RenderPrimitiveTopology", 4),
    ("vertexStrides", "vertexStrides", "strides", None),
    ("renderTargetFormat", "renderTargetFormat", "RenderFormat", 0),
    ("depthStencilFormat", "depthStencilFormat", "RenderFormat", 0),
    ("sampleCount", "sampleCount", "int", 1),
    ("enableAlphaToCoverage", "enableAlphaToCoverage", "bool", 0),
    ("specConstants", "specConstants", "hex", 0),
    # Hashed PS-swap variants. Absent from CSVs written before these columns
    # existed; a missing column reads as the default (false), matching the old
    # behavior of emitting only the normal-draw variant.
    ("occlusionCounting", "occlusionCounting", "bool", 0),
    ("enhancedDof", "enhancedDof", "bool", 0),
]


def emit_line(row):
    parts = [f".vertexShader = reinterpret_cast<GuestShader*>(0x{int(row['vsHash'], 16):016X})"]
    ps = int(row["psHash"], 16)
    if ps:
        parts.append(f".pixelShader = reinterpret_cast<GuestShader*>(0x{ps:016X})")
    parts.append(f".vertexDeclaration = reinterpret_cast<GuestVertexDeclaration*>(0x{int(row['declHash'], 16):016X})")

    for col, field, kind, default in FIELDS:
        raw = row.get(col)
        if raw is None or raw == "":
            continue  # column absent (CSV predates this field) -> default
        if kind == "strides":
            strides = [int(x) for x in raw.split("|")]
            last = max((i for i, v in enumerate(strides) if v), default=-1)
            if last >= 0:
                parts.append(f".{field} = {{" + ", ".join(str(v) for v in strides[: last + 1]) + "}")
        elif kind == "float":
            val = float(raw)
            if val != default:
                parts.append(f".{field} = {val}f")
        else:
            val = int(raw)
            if val == default:
                continue
            if kind == "bool":
                parts.append(f".{field} = {'true' if val else 'false'}")
            elif kind == "hex":
                parts.append(f".{field} = 0x{val:X}")
            elif kind == "int":
                parts.append(f".{field} = {val}")
            else:  # enum kind
                parts.append(f".{field} = {enum(kind, val)}")

    return "{ " + ", ".join(parts) + " },"


# Substrings marking a dead header line. New rows are filtered earlier from
# parsed CSV values; this scrubs lines already emitted into the header.
DEAD_VERTEX_SHADER = ".vertexShader = reinterpret_cast<GuestShader*>(0x0000000000000000)"
DEAD_SAMPLE_COUNT = ".sampleCount = "


def is_dead_entry_line(line):
    return DEAD_VERTEX_SHADER in line or DEAD_SAMPLE_COUNT in line


def read_existing_entries(path):
    """Return the `{ ... },` PSO entry lines already in an output header.

    Re-running accumulates coverage instead of replacing it. The emit is
    deterministic from the dedup key, so deduping at the emitted-line level is
    equivalent to deduping the source rows.
    """
    entries = set()
    try:
        with open(path, newline="") as fh:
            for line in fh:
                s = line.strip()
                if s.startswith("{") and s.endswith("},") and not is_dead_entry_line(s):
                    entries.add(s)
    except FileNotFoundError:
        pass
    return entries


def collect_csvs(inputs):
    files = []
    for raw in inputs:
        p = Path(raw)
        if p.is_dir():
            found = sorted(p.rglob("*.csv"))
            if not found:
                sys.stderr.write(f"warning: no .csv files under {p}\n")
            files.extend(found)
        elif p.is_file():
            files.append(p)
        else:
            sys.stderr.write(f"warning: skipping missing path {p}\n")
    return files


def main():
    ap = argparse.ArgumentParser(description="Aggregate PSO coverage CSVs into the static cache header fragment.")
    ap.add_argument("csv", nargs="+",
                    help="recorded pso_misses_*.csv / pso_cache.csv file(s), or "
                         "folder(s) to scan recursively for *.csv")
    ap.add_argument("-o", "--output", help="output .h fragment (default: stdout)")
    ap.add_argument("--replace", action="store_true",
                    help="regenerate from the given CSVs only, discarding any "
                         "entries already in the -o file (default: accumulate)")
    args = ap.parse_args()

    csv_files = collect_csvs(args.csv)
    if not csv_files:
        sys.stderr.write("error: no input CSV files found\n")
        sys.exit(1)

    # Dedup key: everything that identifies a pipeline (skip diagnostics).
    pso_cols = ["vsHash", "psHash", "declHash"] + [f[0] for f in FIELDS]
    seen = {}
    for path in csv_files:
        with open(path, newline="") as fh:
            for row in csv.DictReader(fh):
                if not row.get("vsHash"):
                    continue
                if int(row["vsHash"], 16) == 0:
                    continue  # unresolvable runtime hcg-HLSL shader
                sample_count = row.get("sampleCount")
                if sample_count and int(sample_count) != 1:
                    continue  # MSAA forced off at runtime; stale seed row
                seen.setdefault(tuple(row.get(c, "") for c in pso_cols), row)

    new_entries = {emit_line(r) for r in seen.values()}
    existing = (read_existing_entries(args.output)
                if args.output and not args.replace else set())
    entries = sorted(new_entries | existing)
    header = (
        f"// {len(entries)} pipeline(s)\n"
    )
    out = header + "".join(e + "\n" for e in entries)

    if args.output:
        with open(args.output, "w", newline="\n") as fh:
            fh.write(out)
        kept = len(existing)
        added = len(entries) - kept
        sys.stderr.write(
            f"read {len(csv_files)} CSV file(s); wrote {len(entries)} "
            f"pipeline(s) to {args.output} ({added} new, {kept} kept from "
            f"existing)\n")
    else:
        sys.stdout.write(out)


if __name__ == "__main__":
    main()
