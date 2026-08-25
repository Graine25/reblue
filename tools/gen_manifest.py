#!/usr/bin/env python3
"""Generate the app manifest for a release, matching AppManifest in
src/platform/manifest.cpp.

Walks a directory of built artifacts, classifies each file by its filename
suffix into one of the five platform keys AppManifest::PlatformKey() returns,
and writes a TOML manifest naming the app version, the release notes URL, and
an [app.artifacts.<key>] table per file with its url, sha256 and size.

    python tools/gen_manifest.py <version> <artifacts-dir> <base-url> \\
        <notes-url> <output>

<base-url> is the directory files resolve under; each artifact's url is
<base-url>/<filename>. Pass a version-stamped path (the R2 .../app/v<version>
prefix), not an alias that later points at a different release, so the
manifest still names working downloads once a newer release exists.

Content packs never appear here. This document only points at the manifest
that lists them, which is published separately.

Fails rather than writing a manifest when the win-amd64 artifact is missing,
or when any artifact has a zero size or an all-zero sha256.
"""
import argparse
import hashlib
import pathlib
import sys

SCHEMA = 1

SUFFIX_TO_KEY = {
    "-win-amd64.zip": "win-amd64",
    "-mac-arm64.zip": "mac-arm64",
    "-mac-amd64.zip": "mac-amd64",
    "-x86_64.AppImage": "linux-amd64",
    "-aarch64.AppImage": "linux-arm64",
}

ZERO_SHA256 = "0" * 64


def classify(name):
    for suffix, key in SUFFIX_TO_KEY.items():
        if name.endswith(suffix):
            return key
    return None


def sha256_of(path):
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def collect_artifacts(artifacts_dir, base_url):
    if base_url and not base_url.startswith(("http://", "https://")):
        raise SystemExit(f"base-url must be absolute or empty, got {base_url!r}")
    artifacts = {}
    for path in sorted(artifacts_dir.iterdir()):
        if not path.is_file():
            continue
        key = classify(path.name)
        if key is None:
            continue
        size = path.stat().st_size
        sha256 = sha256_of(path)
        if size == 0:
            raise SystemExit(f"{path.name}: zero size")
        if sha256 == ZERO_SHA256:
            raise SystemExit(f"{path.name}: all-zero sha256")
        if key in artifacts:
            raise SystemExit(f"two files map to {key}: "
                              f"{artifacts[key]['name']} and {path.name}")
        artifacts[key] = {
            "name": path.name,
            "url": (f"{base_url.rstrip('/')}/{path.name}" if base_url
                    else path.name),
            "sha256": sha256,
            "size": size,
        }

    if "win-amd64" not in artifacts:
        raise SystemExit("no win-amd64 artifact found")

    return artifacts


def build_manifest(version, notes_url, content_url, artifacts):
    lines = [f"schema = {SCHEMA}"]
    if content_url:
        lines.append(f'content_url = "{content_url}"')
    lines += ["", "[app]", f'version = "{version}"',
              f'notes_url = "{notes_url}"']
    for key in sorted(artifacts):
        a = artifacts[key]
        lines += ["", f"[app.artifacts.{key}]", f'url = "{a["url"]}"',
                  f'sha256 = "{a["sha256"]}"', f'size = {a["size"]}']
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("version")
    ap.add_argument("artifacts_dir", type=pathlib.Path)
    ap.add_argument("base_url")
    ap.add_argument("notes_url")
    ap.add_argument("output", type=pathlib.Path)
    ap.add_argument("--content-url", default="",
                    help="where the content manifest lives")
    args = ap.parse_args()

    artifacts = collect_artifacts(args.artifacts_dir, args.base_url)
    text = build_manifest(args.version, args.notes_url, args.content_url,
                          artifacts)
    args.output.write_text(text, encoding="utf-8", newline="\n")

    for key in sorted(artifacts):
        a = artifacts[key]
        print(f"{key}: {a['name']} ({a['size']} bytes)")
    print(f"wrote {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
