#!/usr/bin/env python3

import re
import subprocess
import sys

BANNED_RE = re.compile('\\b(size_t|ptrdiff_t|int8_t|int16_t|int32_t|int64_t|uint8_t|uint16_t|uint32_t|uint64_t|int_least8_t|int_least16_t|int_least32_t|int_least64_t|uint_least8_t|uint_least16_t|uint_least32_t|uint_least64_t|int_fast8_t|int_fast16_t|int_fast32_t|int_fast64_t|uint_fast8_t|uint_fast16_t|uint_fast32_t|uint_fast64_t|intmax_t|uintmax_t|intptr_t|uintptr_t)\\b')


def do_check(path):
    if not path.endswith(".cpp") and not path.endswith(".h"):
        return False
    return "universal" not in path


def parse_diff(diff_text):
    violations = []
    hunk_re = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@")
    path = None
    checked = False
    new_lineno = 0

    for line in diff_text.splitlines():
        if line.startswith("+++ "):
            p = line[4:]
            if p.startswith("b/"):
                p = p[2:]
            path = p
            checked = do_check(path)
            continue
        if line.startswith("--- ") or line.startswith("diff ") or line.startswith("index "):
            continue
        if line.startswith("@@"):
            m = hunk_re.match(line)
            if m:
                new_lineno = int(m.group(1))
            continue
        if not checked or line.startswith("\\"):
            continue

        if line.startswith("+"):
            content = line[1:]
            m = BANNED_RE.search(content)
            if m:
                violations.append((path, new_lineno, m.group(1), content.strip()))
            new_lineno += 1
        elif line.startswith("-"):
            pass
        else:
            new_lineno += 1

    return violations


def main(argv):
    diff_text = subprocess.run(
        ["git", "diff", "--unified=0", "--no-color", f"{argv[1]}...{argv[2]}"],
        capture_output=True, text=True, check=True,
    ).stdout

    violations = parse_diff(diff_text)

    for path, lineno, token, _line in violations:
        print(
            f"::error file={path},line={lineno}::"
            f"'{token}' is not allowed in new code, instead use the alias from misc.h"
        )

    if violations:
        return 1

    return 0

sys.exit(main(sys.argv))
