#!/usr/bin/env python3
"""Regression test for intentional Syzygy reinitialization on UCI game/hash reset.

Syzygy recognizes tablebase files by name at init time and maps them lazily.
The empty placeholder below MUST NOT be probed; this test only checks the
init/clear lifecycle and does not verify actual WDL or DTZ values.
"""

import pathlib
import re
import subprocess
import sys
import tempfile


FOUND = re.compile(
    r"(?m)^info string Found (\d+) WDL and (\d+) DTZ "
    r"tablebase files \(up to (\d+)-man\)\.\s*$"
)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} /path/to/stockfish", file=sys.stderr)
        return 2

    engine = pathlib.Path(sys.argv[1]).resolve()
    if not engine.is_file():
        print(f"Stockfish binary not found: {engine}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="stockfish-syzygy-") as tmp:
        root = pathlib.Path(tmp)
        with_table = root / "with_table"
        without_table = root / "without_table"
        with_table.mkdir()
        without_table.mkdir()
        (with_table / "KQvK.rtbw").touch()

        commands = [
            "uci",
            f"setoption name SyzygyPath value {with_table}",
            "ucinewgame",
            "setoption name Clear Hash",
            f"setoption name SyzygyPath value {without_table}",
            "ucinewgame",
            f"setoption name SyzygyPath value {with_table}",
            "ucinewgame",
            "quit",
        ]
        try:
            result = subprocess.run(
                [str(engine)],
                input="\n".join(commands) + "\n",
                capture_output=True,
                text=True,
                timeout=90,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print("Stockfish timed out", file=sys.stderr)
            return 1

        expected = [(1, 0, 3)] * 3 + [(0, 0, 0)] * 2 + [(1, 0, 3)] * 2
        found = [tuple(map(int, m)) for m in FOUND.findall(result.stdout)]

        if result.returncode != 0 or found != expected:
            print("Syzygy reset regression FAILED", file=sys.stderr)
            print(f"Expected tablebase discoveries: {expected}", file=sys.stderr)
            print(f"Actual tablebase discoveries:   {found}", file=sys.stderr)
            print("STDOUT:\n" + result.stdout, file=sys.stderr)
            print("STDERR:\n" + result.stderr, file=sys.stderr)
            return 1

    print("Syzygy lifecycle OK: new game / Clear Hash / path changes reinitialize tables")
    return 0


if __name__ == "__main__":
    sys.exit(main())
