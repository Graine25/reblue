#!/usr/bin/env python3
"""Audit res/embed/localization.toml against the keys the source asks for.

Every entry in the catalog is "<key>.<code>", so the ".us" lines are the set of
keys the UI may ask for. A key the source uses but English lacks renders as
"#key" in the UI, and a key English defines but nothing uses is dead weight a
translator still pays for.

One rule decides what a language owes, and both the reports and the import
answer to it: a key is owed when the language has no line for it and English
is not borrowing the word from the game. So anything a report asks for can be
handed back and taken, and anything the import turns away was never asked for.
A translation that reads the same as its English is a real answer, not a
missing one, and is stored like any other.

A key whose English is "@game:table/KEY|literal" takes Blue Dragon's own
wording in whatever language it is running, so it is localized already and no
translation owes it a line.

English rides the same road as the rest. Exporting "us" hands back the
catalog's own wording to rewrite, and importing it writes the reworded lines
over the ones already there, naming the translations that were written against
wording that has since moved.

    --missing [CODE ...]  what each language still owes, as pasteable lines.
                          No argument means the languages the catalog already
                          has rather than all ten.
    --export CODE ...     the whole file for one language, whatever it has so
                          far filled in. Works for a language with no lines
                          yet, which is how a new one starts, and for "us",
                          which is how the English gets reviewed.
    --out DIR             write either report to DIR/<code>.toml instead of
                          stdout, which is what a translator gets handed.
    --import FILE ...     reconcile a filled-in file against the catalog and
                          say what it adds, what it overwrites and what the
                          catalog no longer has a place for. Reports only
                          until --apply writes it in.

    python tools/translation_mgr.py --export po --out lang-out
    python tools/translation_mgr.py --import lang-in/po.toml
    python tools/translation_mgr.py --import lang-in/po.toml --apply

    python tools/translation_mgr.py --export us --out lang-out
    python tools/translation_mgr.py --import lang-out/us.toml --apply
"""
import argparse
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
CATALOG = ROOT / "res" / "embed" / "localization.toml"

CODES = ("jp", "us", "de", "fr", "es", "it", "kr", "tw", "cn", "po")
ENGLISH = "us"
BORROW = "@game:"

# i18n::Text("k"), i18n::Fmt("k", ...), the installer's T("k") shorthand, and
# the key-valued table fields.
CALL = re.compile(r'(?:i18n::(?:Text|Fmt)|(?<![A-Za-z0-9_])T)\(\s*"([a-z0-9_.]+)"')
FIELD = re.compile(r'\.(?:label|desc|key)\s*=\s*"([a-z0-9_.]+\.[a-z0-9_.]+)"')
# Keys that reach a lookup indirectly: positional initializers
# (ConfigLayout::kSectionKeys, kPages) and the arms of a ternary.
BARE = re.compile(
    r'"((?:settings|menu|opt|footer|title|common|locale|installer|map'
    r'|update)'
    r'\.[a-z0-9_.]+)"')
ENTRY = re.compile(r'([A-Za-z0-9_.]+)\s*=')

# LocaleKey() builds these at runtime from the bd_boot.ini codes.
DYNAMIC = {f"locale.{c}" for c in CODES}

# What the import does with a line, and what it calls it. The first four are
# taken, the rest are turned away. Only a lost placeholder is an error: the
# others say the catalog moved on from a file written against an older one.
TAKEN = ("add", "change", "same", "blank")
BROKEN = ("placeholder",)


def flatten(table, prefix=""):
    out = {}
    for k, v in table.items():
        key = f"{prefix}.{k}" if prefix else k
        if isinstance(v, dict):
            out.update(flatten(v, key))
        else:
            out[key] = v
    return out


def split_code(key, fallback=None):
    """A catalog key is "<base>.<code>", or bare when a file omits the code."""
    base, _, code = key.rpartition(".")
    if code in CODES:
        return base, code
    return key, fallback


def sections(text):
    """Maps each key to the [header] it is written under, for the report."""
    out = {}
    header = ""
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("["):
            header = line[1:-1]
        elif (m := ENTRY.match(line)):
            base, _ = split_code(m.group(1), "")
            out[f"{header}.{base}"] = header
    return out


def line_index(lines):
    """Maps each key to the line each of its languages is written on."""
    out = {}
    header = ""
    for i, line in enumerate(lines):
        line = line.strip()
        if line.startswith("["):
            header = line[1:-1]
        elif (m := ENTRY.match(line)):
            base, code = split_code(m.group(1), "")
            out.setdefault(f"{header}.{base}", {})[code] = i
    return out


def tail_index(english):
    """Maps a key's last two parts to it, for a key that changed section.

    Rows get regrouped, so "settings.misc.save_anywhere.label" is written
    "settings.gameplay.save_anywhere.label" today. The tail still names the row
    it always did, and an unambiguous one is the same row under a new heading.
    """
    out = {}
    for key in english:
        out.setdefault(".".join(key.split(".")[-2:]), []).append(key)
    return {tail: keys[0] for tail, keys in out.items() if len(keys) == 1}


def used_keys():
    keys = set()
    for path in SRC.rglob("*"):
        if path.suffix not in (".cpp", ".h"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for pattern in (CALL, FIELD, BARE):
            keys.update(pattern.findall(text))
    return keys


def quote(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def report_lines(code, keys, english, have, header_of, whole):
    """The keys as catalog lines, the English they translate alongside.

    English is its own case: the line already holds the words being reviewed,
    so there is nothing to quote beside it and nothing to fill in.
    """
    if code == ENGLISH:
        out = [f"# {code}: {len(keys)} keys as the UI reads them today.",
               "# Reword any line you want changed and leave the rest alone.",
               "# A line left as it is imports as no change, and a line",
               "# emptied out is read the same way rather than as a blanking.",
               "# A {} is a name or a count the game substitutes and has to",
               "# survive the rewording.",
               "#",
               "# Hand it back with:",
               "#   python tools/translation_mgr.py --import <this file>"
               " --apply"]
    else:
        todo = sum(1 for k in keys if k not in have)
        out = [f"# {code}: {todo} to translate"
               + (f" of {len(keys)}." if whole else ".")
               + " Fill in each empty string,",
               "# leaving the key and the English comment as they are. An"
               " entry",
               "# left empty falls back to English rather than blanking the"
               " UI.",
               "# A word that reads the same in this language as it does in",
               "# English still gets written out, since that is an answer.",
               "# A {} in the English is a name or a count the game"
               " substitutes",
               "# and has to survive into the translation.",
               "#",
               "# Hand it back with:",
               "#   python tools/translation_mgr.py --import <this file>"]
    header = None
    for key in keys:
        if header_of.get(key) != header:
            header = header_of.get(key, "")
            out.append(f"\n[{header}]")
        name = key[len(header) + 1:] if header else key
        written = f'{name}.{code} = {quote(have.get(key, ""))}'
        if code != ENGLISH:
            written = written.ljust(48) + f'# {quote(english[key])}'
        out.append(written)
    return "\n".join(out) + "\n"


def write_report(text, code, out_dir):
    if not out_dir:
        print()
        print(text, end="")
        return
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{code}.toml"
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"{code}: wrote {path}")


def judge(base, code, value, english, entries):
    """What the import does with one line, as (verdict, note).

    The refusals are the cases a report never asked for: a key the catalog
    dropped, a key the game supplies the word for, and a value that dropped a
    placeholder and so would drop the name or count it stood for.
    """
    if code is None:
        return "no code", "no language code on the key, and no --code given"
    if code not in CODES:
        return "not a language", f"{code} is not one of {' '.join(CODES)}"
    if not value:
        return "blank", ""
    if base not in english:
        return "dropped", "the catalog no longer has this key"
    if english[base].startswith(BORROW):
        return "borrowed", "the game supplies this word in every language"
    if "{" in english[base] and "{" not in value:
        return "placeholder", "dropped the {} the English carries"
    current = entries.get(code, {}).get(base)
    if current == value:
        return "same", ""
    if current is not None:
        return "change", current
    return "add", ""


def take(sources, fallback, english, entries, tails, apply_):
    """Reconcile files of translations against the catalog, and write them in."""
    edits = {}
    failed = False
    for path in sources:
        try:
            data = flatten(tomllib.loads(path.read_text(encoding="utf-8")))
        except (OSError, tomllib.TOMLDecodeError) as e:
            print(f"{path}: {e}")
            failed = True
            continue

        counts = {}
        detail = []
        for key, value in sorted(data.items()):
            base, code = split_code(key, fallback)
            value = value.strip() if isinstance(value, str) else str(value)
            moved = ""
            if base not in english:
                if (to := tails.get(".".join(base.split(".")[-2:]))):
                    moved, base = base, to
            verdict, note = judge(base, code, value, english, entries)
            counts[verdict] = counts.get(verdict, 0) + 1

            mark = f"  (moved from {moved})" if moved else ""
            if code == ENGLISH:
                # Rewording the English leaves every translation of it
                # answering wording that is no longer there.
                stale = [c for c in CODES
                         if c != ENGLISH and base in entries.get(c, {})]
                if verdict == "change" and stale:
                    mark += "  (translated into " + " ".join(stale) + ")"
            elif value and english.get(base) == value:
                mark += "  (reads as the English does)"
            if verdict == "add":
                detail.append(f"  {'add':<9} {base}.{code} = "
                              f"{quote(value)}{mark}")
            elif verdict == "change":
                detail.append(f"  {'change':<9} {base}.{code}: {quote(note)}"
                              f" -> {quote(value)}{mark}")
            elif verdict not in TAKEN:
                detail.append(f"  {verdict:<9} {key}: {note}")
                failed = failed or verdict in BROKEN
            if verdict in ("add", "change"):
                edits[(base, code)] = value
                entries.setdefault(code, {})[base] = value

        tally = ", ".join(f"{n} {k}" for k, n in counts.items())
        print(f"{path}: {tally or 'nothing'}")
        for line in detail:
            print(line)

    if not edits:
        return failed
    if not apply_:
        print(f"\n{len(edits)} lines to write. Run again with --apply.")
        return failed

    text = CATALOG.read_text(encoding="utf-8")
    lines = text.splitlines()
    header_of = sections(text)
    where = line_index(lines)

    inserts = []
    for (base, code), value in edits.items():
        name = base[len(header_of[base]) + 1:] if header_of.get(base) else base
        written = f"{name}.{code} = {quote(value)}"
        at = where[base]
        if code in at:
            lines[at[code]] = written
            continue
        # English first and the translations alphabetically under it, so the
        # line goes after the last code that sorts before this one.
        before = [i for c, i in at.items()
                  if c == ENGLISH or (c != ENGLISH and c < code)]
        inserts.append((max(before) + 1, written))

    for at, written in sorted(inserts, reverse=True):
        lines.insert(at, written)

    with open(CATALOG, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"\nwrote {len(edits)} lines to {CATALOG.relative_to(ROOT)}")
    return failed


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--missing", nargs="*", metavar="CODE",
                    help="list the keys each language still owes, in file "
                         "order. No argument means the languages the catalog "
                         "already has.")
    ap.add_argument("--export", "--template", dest="template", nargs="+",
                    metavar="CODE",
                    help="the whole catalog as one language's file, what it "
                         "has so far filled in. Names a language the catalog "
                         "has no lines for yet to start one, or "
                         f"{ENGLISH} to review the English itself.")
    ap.add_argument("--out", metavar="DIR", type=Path,
                    help="write the report to DIR/<code>.toml instead of "
                         "stdout, one file per language.")
    ap.add_argument("--import", dest="sources", nargs="+", metavar="FILE",
                    type=Path,
                    help="reconcile files of translations against the catalog "
                         "and report what they add and change.")
    ap.add_argument("--code", metavar="CODE",
                    help="the language an imported file is in, for a file "
                         "whose keys do not carry the code themselves.")
    ap.add_argument("--apply", action="store_true",
                    help="write the imported lines into the catalog.")
    args = ap.parse_args()
    if args.out and args.missing is None and not args.template:
        ap.error("--out needs --missing or --template")
    if args.apply and not args.sources:
        ap.error("--apply needs --import")
    if args.code and not args.sources:
        ap.error("--code needs --import")
    if args.code and args.code not in CODES:
        ap.error(f"not a language code: {args.code}")

    text = CATALOG.read_text(encoding="utf-8")
    catalog = flatten(tomllib.loads(text))
    header_of = sections(text)

    # A line whose last component is not a language code cannot be reached,
    # since every lookup appends one.
    entries = {}
    failed = False
    for key, value in catalog.items():
        base, code = split_code(key)
        if code is None:
            print(f"no language code on: {key}")
            failed = True
            continue
        entries.setdefault(code, {})[base] = value

    english = entries.get(ENGLISH, {})
    used = used_keys() | DYNAMIC

    # A literal ending in a dot is a dynamic prefix the source completes at
    # runtime ("settings.action." + ToString(action)): it claims every English
    # key under it rather than naming one of its own.
    prefixes = {k for k in used if k.endswith(".")}
    used -= prefixes
    used |= {k for k in english if k.startswith(tuple(prefixes))}

    for key in sorted(used - english.keys()):
        print(f"missing from English: {key}")
        failed = True
    for key in sorted(english.keys() - used):
        print(f"unused in English: {key}")
        failed = True

    # File order is the order a translator reads, so the reports keep it.
    order = [split_code(k)[0] for k in catalog if split_code(k)[1] == ENGLISH]
    owed = [k for k in order if not str(english.get(k, "")).startswith(BORROW)]

    if args.sources:
        return 1 if take(args.sources, args.code, english, entries,
                         tail_index(english), args.apply) or failed else 0

    present = [c for c in CODES if c != ENGLISH and c in entries]
    wanted = list(args.missing or present) if args.missing is not None else []
    for code in wanted:
        if code not in CODES or code == ENGLISH:
            print(f"not a language to translate into: {code}")
            failed = True
    for code in args.template or ():
        if code not in CODES:
            print(f"not a language: {code}")
            failed = True

    for code in CODES:
        if code == ENGLISH or code not in entries:
            continue
        other = entries[code]
        for key in sorted(other.keys() - english.keys()):
            print(f"{code}: key not in English: {key}")
            failed = True
        for key in sorted(other.keys() & english.keys()):
            # A line under a borrowed key never shows, since the game's own
            # word is already in this language, and dropping a placeholder
            # drops the name or count it stood for.
            if english[key].startswith(BORROW):
                print(f"{code}: {key} sits under a borrowed key")
                failed = True
            if "{" in english[key] and "{" not in other[key]:
                print(f"{code}: {key} lost its placeholder")
                failed = True
        done = sum(1 for k in owed if k in other)
        print(f"{code}: {done}/{len(owed)} keys")

    for code in wanted:
        if code not in CODES or code == ENGLISH:
            continue
        have = entries.get(code, {})
        write_report(report_lines(code, [k for k in owed if k not in have],
                                  english, have, header_of, False),
                     code, args.out)

    for code in args.template or ():
        if code not in CODES:
            continue
        write_report(report_lines(code, owed, english, entries.get(code, {}),
                                  header_of, True),
                     code, args.out)

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
