#!/usr/bin/env python3
"""Fails when a bd_* cvar is named outside its module's settings.cpp.

A cvar is an external input port: console, config file, launch argument. The
typed Settings object for each module owns the value, so nothing else in
reblue reads or writes one. SDK-owned cvars (anything not starting with bd_)
are exempt: rexglue has no facade for them yet, so name-based access stays
legal.

Three classes of violation are checked.

1. Naming a bd_* cvar outside the six settings files, through either the
   REXCVAR_* macros or the by-name registry calls.
2. Naming another module's bd_* cvar from inside a settings file. Skipping an
   owner wholesale would let gpu write bd_audio_gain, and would not notice a
   setting defined in two settings files at once.
3. Reaching cvar storage through an API that bypasses validation, the
   pending-restart list or the change callbacks, from anywhere but the
   documented exception. Those writes desync every mirror in the affected
   object, and no cvar name has to appear for it to happen.

No kCv* (or similarly named) constant survives past this check's own commit,
so matching string literals is sufficient today. That is a real blind spot,
not a solved problem: this script matches source text, not identifiers, so a
named constant, a concatenated string or a name threaded through a parameter
all defeat it exactly as kCvDevmode once did. Widen the allowlist or the
patterns with that in mind, because the moment any of those three reappears,
this script stops seeing the site it would flag.

Run from anywhere; exits 1 and lists every violation on failure.
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src"

SOURCE_GLOBS = ("*.cpp", "*.h", "*.hpp", "*.inl", "*.cc")

# Every file allowed to name a bd_* cvar, and only its own. One per module, no
# exceptions. No header is exempt: REXCVAR_DECLARE belongs in the .cpp by rule,
# so a header that declares one is the design breaking, not a false positive to
# allow.
OWNERS = {
    "core/settings.cpp",
    "gpu/settings.cpp",
    "audio/settings.cpp",
    "vfs/settings.cpp",
    "ui/settings.cpp",
    "engine/settings.cpp",
}

# ApplyReblueCvarDefaults walks the registry to restyle SDK defaults before the
# config file loads, which is the one thing no facade can express. It is
# allowed to reach storage directly, but the bd_* rules above still apply to
# it, so it cannot use that reach on a reblue setting.
RAW_REGISTRY_EXCEPTION = "reblue_app.cpp"

# REXCVAR_DECLARE/DEFINE_*/GET/SET/QUERY take the bare cvar name as a macro
# argument, e.g. REXCVAR_GET(bd_devmode). DECLARE and QUERY take a leading
# type argument first; \s already matches newlines, so a call wrapped across
# lines by a formatter still matches.
MACRO = re.compile(r"\bREXCVAR_(?:DECLARE|DEFINE_\w+|GET|SET|QUERY)\s*\(\s*(?:[\w:<>]+\s*,\s*)?(bd_\w+)")

# The subset that defines a setting, which is what makes a settings file its
# owner.
DEFINE = re.compile(r"\bREXCVAR_DEFINE_\w+\s*\(\s*(bd_\w+)")

# SetFlagByName/GetFlagByName/Query<T>/RegisterChangeCallback/ResetToDefault
# take the cvar name as a runtime string. RegisterChangeCallback is in this
# list because a change callback registered outside a module's own settings.cpp
# is the same leak as a stray Set/Get: it names the setting and reacts to it
# from a second place. That was real, on bd_devmode in engine/guest_debug.cpp,
# until the reaction moved behind bd::Settings::SetDevmodeApplier.
BY_NAME = re.compile(
    r"\b(?:SetFlagByName|GetFlagByName|Query\s*<[^>]*>|RegisterChangeCallback"
    r"|ResetToDefault)"
    r"\s*\(\s*\"(bd_\w+)\""
)

# Storage reached without naming anything. ResetAllToDefaults and ResetToDefault
# call the flag's setter directly and fire no change callback, so a mirror keeps
# the old value. GetRegistry returns a mutable vector and GetFlagInfo a mutable
# entry, so either one hands out a setter that also skips validation and the
# pending-restart list. None of them takes a bd_ name this script could match.
RAW_REGISTRY = re.compile(
    r"\b(?:ResetAllToDefaults|GetRegistry|GetFlagInfo)\s*\("
)


def sources():
    seen = set()
    for glob in SOURCE_GLOBS:
        for path in SRC.rglob(glob):
            rel = path.relative_to(SRC).as_posix()
            if rel not in seen:
                seen.add(rel)
                yield rel, path


def main() -> int:
    failures = []
    for rel, path in sorted(sources()):
        text = path.read_text(encoding="utf-8", errors="replace")
        owned = {m.group(1) for m in DEFINE.finditer(text)} if rel in OWNERS else set()

        hits = []
        for pattern in (MACRO, BY_NAME):
            for m in pattern.finditer(text):
                if m.group(1) in owned:
                    continue
                line = text.count("\n", 0, m.start()) + 1
                if rel in OWNERS:
                    hits.append((line, f"names {m.group(1)}, which it does not define"))
                else:
                    hits.append((line, f"names {m.group(1)} outside its Settings"))

        if Path(rel).name != RAW_REGISTRY_EXCEPTION:
            for m in RAW_REGISTRY.finditer(text):
                line = text.count("\n", 0, m.start()) + 1
                hits.append((line, f"{m.group(0).rstrip('( ')} bypasses the change callbacks"))

        for line, why in sorted(hits):
            failures.append(f"{rel}:{line}: {why}")

    for f in failures:
        print(f, file=sys.stderr)
    if failures:
        print(
            f"\n{len(failures)} cvar access(es) outside a module settings file.\n"
            "Add an accessor to that module's Settings and call it instead.",
            file=sys.stderr,
        )
        return 1
    print("cvar access check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
