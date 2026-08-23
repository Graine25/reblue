#!/usr/bin/env python3
"""Generate cache/pso_state_templates.h for the load-time PSO predictor.

The HDB-determined part of a PSO (shader pair, decl, stride) is predicted at
model load from BD's technique table; the engine-determined part comes from
this table, keyed by (shader family, table column). Within a group, cullMode
and the engine specConstants bits are near-orthogonal material axes, so a
group stores core state vectors plus the observed cull/spec sets and the
predictor emits the cross product. renderPassId is deliberately ignored: only
3 values occur and states repeat across them, so merging passes covers
cross-pass gaps for free.

shader_table_descriptors.csv is dumped from g_shaderTableDescriptors
(0x82771D18 in IDA): tech,col,skin,vso,pso. Rows whose shader pair is not in
the technique table (post-fx, 2D/movie, hcg-HLSL) are residual territory and
are skipped.

Note: capture CSVs record only prediction misses, so a regen from captures
alone loses every core this table already covers. Merge against the existing
header rather than replacing it.

Usage:
  pso_gen_templates.py [--hashes tools/shader_cache/shader_hashes.csv]
                       [--desc tools/shader_cache/shader_table_descriptors.csv]
                       -o src/gpu/pipeline/cache/pso_state_templates.h
                       capture.csv [capture2.csv ...]
"""
import argparse
import csv
import os
import sys
from collections import defaultdict

from pso_cache_to_header import ENUMS, enum

FAMILIES = ["normal", "wind", "toon", "edge", "cloud", "water", "effect",
            "waterfall", "lightshaft", "glass", "caustics", "mirror", "sss",
            "fresco", "fur", "blur", "shadownull"]
FAMILY_OF_TECH = {
    0: "normal", 1: "toon", 2: "edge", 3: "wind", 4: "cloud", 5: "water",
    6: "effect", 7: "waterfall", 8: "lightshaft", 9: "glass", 10: "caustics",
    11: "mirror", 12: "sss", 13: "sss", 14: "fresco", 15: "fur", 16: "fur",
    17: "fur", 18: "blur", 19: "wind", 20: "wind", 21: "toon", 22: "toon",
    23: "toon", 24: "shadownull", 25: "shadownull", 26: "normal", 27: "normal",
    28: "normal", 29: "normal", 30: "normal", 31: "normal",
}

# Engine-determined core state carried per template. Excluded vs the capture
# schema: shaders/decl/strides (HDB-predicted at load), cullMode +
# specConstants (cross axes), sampleCount + enableAlphaToCoverage (config,
# masked at capture), renderPassId/builtMiss (diagnostics), occlusionCounting +
# enhancedDof (post-fx PS-swap passes outside the technique table).
CORE_FIELDS = [
    ("instancing", "bool", 0),
    ("zEnable", "bool", 1),
    ("zWriteEnable", "bool", 1),
    ("stencilEnable", "bool", 0),
    ("stencilTwoSided", "bool", 0),
    ("srcBlend", "RenderBlend", 2),
    ("destBlend", "RenderBlend", 1),
    ("frontFace", "RenderFrontFace", 1),
    ("zFunc", "RenderComparisonFunction", 2),
    ("stencilFunc", "RenderComparisonFunction", 8),
    ("stencilFail", "RenderStencilOp", 1),
    ("stencilZFail", "RenderStencilOp", 1),
    ("stencilPass", "RenderStencilOp", 1),
    ("stencilFuncCCW", "RenderComparisonFunction", 8),
    ("stencilFailCCW", "RenderStencilOp", 1),
    ("stencilZFailCCW", "RenderStencilOp", 1),
    ("stencilPassCCW", "RenderStencilOp", 1),
    ("stencilMask", "hex", 255),
    ("stencilWriteMask", "hex", 255),
    ("stencilRef", "int", 0),
    ("alphaBlendEnable", "bool", 0),
    ("blendOp", "RenderBlendOperation", 1),
    ("slopeScaledDepthBias", "float", 0.0),
    ("depthBias", "int", 0),
    ("srcBlendAlpha", "RenderBlend", 2),
    ("destBlendAlpha", "RenderBlend", 1),
    ("blendOpAlpha", "RenderBlendOperation", 1),
    ("colorWriteEnable", "hex", 15),
    ("primitiveTopology", "RenderPrimitiveTopology", 4),
    ("renderTargetFormat", "RenderFormat", 0),
    ("depthStencilFormat", "RenderFormat", 0),
]


def core_initializer(row):
    parts = []
    for field, kind, default in CORE_FIELDS:
        raw = row[field]
        if kind == "float":
            val = float(raw)
            if val != default:
                parts.append(f".{field} = {val}f")
            continue
        val = int(raw)
        if val == default:
            continue
        if kind == "bool":
            parts.append(f".{field} = {'true' if val else 'false'}")
        elif kind == "hex":
            parts.append(f".{field} = 0x{val:X}")
        elif kind == "int":
            parts.append(f".{field} = {val}")
        else:
            parts.append(f".{field} = {enum(kind, val)}")
    return "{ " + ", ".join(parts) + " }," if parts else "{},"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="+")
    ap.add_argument("--hashes", default="tools/shader_cache/shader_hashes.csv")
    ap.add_argument("--desc",
                    default="tools/shader_cache/shader_table_descriptors.csv")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    hashes = {r["name"]: r["hash"] for r in csv.DictReader(open(args.hashes))}
    pair_slots = defaultdict(set)
    vs_slots = defaultdict(set)
    for d in csv.DictReader(open(args.desc)):
        vh, ph = hashes.get(d["vso"]), hashes.get(d["pso"])
        if not vh or not ph:
            sys.exit(f"descriptor shader not in {args.hashes}: {d}")
        slot = (int(d["tech"]), int(d["col"]), int(d["skin"]))
        pair_slots[(vh, ph)].add(slot)
        vs_slots[vh].add(slot)

    # (family, column) -> [{core key -> row}, culls, specs]
    groups = defaultdict(lambda: [dict(), set(), set()])
    total = skipped = 0
    for path in args.csv:
        for r in csv.DictReader(open(path)):
            total += 1
            key = (r["vsHash"], r["psHash"])
            # Depth-only draws record psHash 0 (no PS resolvable); match by VS.
            slots = pair_slots.get(key) or (
                vs_slots.get(r["vsHash"]) if r["psHash"] == "0" * 16 else None)
            if not slots:
                skipped += 1
                continue
            core_key = tuple(r[f] for f, _, _ in CORE_FIELDS)
            spec = int(r["specConstants"]) & ~1  # mask decl-derived bit 0
            for fam_col in {(FAMILY_OF_TECH[t], c) for (t, c, s) in slots}:
                g = groups[fam_col]
                g[0].setdefault(core_key, r)
                g[1].add(int(r["cullMode"]))
                g[2].add(spec)

    cores = []
    group_lines = []
    n_crossed = 0
    for (fam, col) in sorted(groups, key=lambda k: (FAMILIES.index(k[0]), k[1])):
        core_rows, culls, specs = groups[(fam, col)]
        first = len(cores)
        for ck in sorted(core_rows):
            cores.append(core_initializer(core_rows[ck]))
        culls_s = ", ".join(enum("RenderCullMode", c) for c in sorted(culls))
        specs_s = ", ".join(f"0x{s:X}" for s in sorted(specs))
        n_crossed += len(core_rows) * len(culls) * len(specs)
        group_lines.append(
            f"{{ .family = {FAMILIES.index(fam)} /*{fam}*/, .column = {col}, "
            f".coreFirst = {first}, .coreCount = {len(core_rows)}, "
            f".cullCount = {len(culls)}, .specCount = {len(specs)}, "
            f".cullModes = {{{culls_s}}}, .specs = {{{specs_s}}} }},")

    fam_of_tech = ", ".join(
        str(FAMILIES.index(FAMILY_OF_TECH[t])) for t in range(32))
    out = (
        "// Generated by tools/shader_cache/pso_gen_templates.py. Do not edit.\n"
        f"// Inputs: {len(args.csv)} PSO capture CSV(s): "
        f"{', '.join(os.path.basename(p) for p in args.csv)}\n"
        f"// {len(groups)} groups, {len(cores)} cores, {n_crossed} crossed "
        "state vectors.\n"
        "// Included by pso_predictor.cpp inside its namespace; PipelineState\n"
        "// and plume must already be visible.\n"
        "\n"
        "// Core = the engine-determined state of one template, with the\n"
        "// HDB-predicted parts (shaders/decl/strides) and the cross axes\n"
        "// (cullMode, engine specConstants bits) left at defaults for the\n"
        "// predictor to fill in.\n"
        "inline const PipelineState g_psoTemplateCores[] = {\n"
        + "".join(c + "\n" for c in cores) +
        "};\n"
        "\n"
        "struct PsoTemplateGroup {\n"
        "  uint8_t family;\n"
        "  uint8_t column;\n"
        "  uint16_t coreFirst;\n"
        "  uint16_t coreCount;\n"
        "  uint8_t cullCount;\n"
        "  uint8_t specCount;\n"
        "  plume::RenderCullMode cullModes[3];\n"
        "  uint32_t specs[2];\n"
        "};\n"
        "\n"
        "inline const PsoTemplateGroup g_psoTemplateGroups[] = {\n"
        + "".join(g + "\n" for g in group_lines) +
        "};\n"
        "\n"
        "// Technique (0..31) -> family index (comment names in FAMILIES order\n"
        f"// of the generator: {', '.join(FAMILIES)}).\n"
        f"inline constexpr uint8_t kPsoFamilyOfTech[32] = {{{fam_of_tech}}};\n")
    with open(args.output, "w", newline="\n") as fh:
        fh.write(out)
    sys.stderr.write(
        f"{args.output}: {len(groups)} groups, {len(cores)} cores, "
        f"{n_crossed} crossed states ({total} rows in, {skipped} non-table "
        "skipped)\n")


if __name__ == "__main__":
    main()
