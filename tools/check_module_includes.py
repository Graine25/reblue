#!/usr/bin/env python3
"""Module facade gate.

A file under src/<a>/ may include "<b>/..." only when a == b (its own module)
or the path is exactly "<b>/<b>.h" (that module's one public header). core/ is
exempt on both sides: it is the shared vocabulary every module builds on.

Run from anywhere; exits 1 and lists every violation on failure.
"""
import os
import re
import sys

SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'src')
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
EXEMPT = {'core'}

# Generated headers land in the build tree's generated/ include dir, not src/.
GENERATED = {'reblue_init.h', 'installer_png.h', 'installer_manifest.h',
             'installer_font.h'}


def module_of(rel):
    head, _, tail = rel.partition('/')
    return head if tail else ''


def main():
    modules = {d for d in os.listdir(SRC) if os.path.isdir(os.path.join(SRC, d))}
    violations = []

    for dirpath, _, filenames in os.walk(SRC):
        for name in sorted(filenames):
            if not name.endswith(('.h', '.cpp', '.c')):
                continue
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, SRC).replace('\\', '/')
            mine = module_of(rel)
            if mine in EXEMPT:
                continue
            with open(path, encoding='utf-8', errors='replace') as f:
                text = f.read()
            for m in INCLUDE.finditer(text):
                inc = m.group(1)
                if inc in GENERATED or not os.path.exists(os.path.join(SRC, inc)):
                    continue
                theirs = module_of(inc)
                if theirs in EXEMPT or theirs == mine or not theirs:
                    continue
                if theirs in modules and inc != '%s/%s.h' % (theirs, theirs):
                    line = text.count('\n', 0, m.start()) + 1
                    violations.append('%s:%d: %s reaches past %s/%s.h'
                                      % (rel, line, inc, theirs, theirs))

    for v in violations:
        print(v)
    if violations:
        print('\n%d module facade violation(s). A module exposes one header: '
              '<module>/<module>.h' % len(violations))
        return 1
    print('module facade gate: clean')
    return 0


if __name__ == '__main__':
    sys.exit(main())
