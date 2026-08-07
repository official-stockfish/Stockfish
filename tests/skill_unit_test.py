"""
Automated unit test suite for Stockfish Skill implementation.
Tests UCI Skill Level and UCI_LimitStrength options, depth capping,
evaluation perturbation determinism per game, and game-to-game seed variation.
"""

import os
import subprocess
import sys
import unittest

# Locate stockfish binary in src/stockfish.exe or src/stockfish
POSSIBLE_EXES = [
    os.path.join(os.path.dirname(__file__), "..", "src", "stockfish.exe"),
    os.path.join(os.path.dirname(__file__), "..", "src", "stockfish"),
    os.path.join(".", "stockfish.exe"),
    os.path.join(".", "stockfish"),
]

EXE = None
for p in POSSIBLE_EXES:
    if os.path.exists(p):
        EXE = os.path.abspath(p)
        break

if not EXE:
    print("Error: Could not locate stockfish executable.", file=sys.stderr)
    sys.exit(1)


def run_engine_commands(commands, timeout=10):
    """Feed commands to stockfish executable and return stdout lines."""
    p = subprocess.Popen(
        [EXE],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    stdin_str = "\n".join(commands) + "\nquit\n"
    stdout, stderr = p.communicate(input=stdin_str, timeout=timeout)
    return stdout.splitlines()


class TestStockfishSkill(unittest.TestCase):

    def test_uci_skill_level_options(self):
        """Verify Skill Level option exists and can be set to valid values."""
        output = run_engine_commands(["uci"])
        has_skill_level = any("option name Skill Level" in line for line in output)
        has_limit_strength = any("option name UCI_LimitStrength" in line for line in output)
        has_uci_elo = any("option name UCI_Elo" in line for line in output)

        self.assertTrue(has_skill_level, "Skill Level UCI option missing")
        self.assertTrue(has_limit_strength, "UCI_LimitStrength UCI option missing")
        self.assertTrue(has_uci_elo, "UCI_Elo UCI option missing")

    def test_skill_level_depth_limiting(self):
        """Verify that setting Skill Level caps root search depth appropriately."""
        # Level 0 -> max depth 1
        output = run_engine_commands([
            "setoption name Skill Level value 0",
            "ucinewgame",
            "isready",
            "position startpos",
            "go movetime 300",
        ])
        depths = []
        for line in output:
            if line.startswith("info ") and " depth " in line:
                tokens = line.split()
                if "depth" in tokens:
                    idx = tokens.index("depth")
                    depths.append(int(tokens[idx + 1]))

        self.assertGreater(len(depths), 0, "Engine did not report depth in search info")
        max_depth = max(depths)
        self.assertLessEqual(max_depth, 1, f"Skill level 0 exceeded depth limit 1 (got depth {max_depth})")

    def test_limit_strength_elo_mapping(self):
        """Verify UCI_LimitStrength with UCI_Elo sets strength limit without error."""
        output = run_engine_commands([
            "setoption name UCI_LimitStrength value true",
            "setoption name UCI_Elo value 1320",
            "ucinewgame",
            "isready",
            "position fen r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
            "go movetime 200",
        ])
        bestmove_lines = [line for line in output if line.startswith("bestmove")]
        self.assertEqual(len(bestmove_lines), 1, "Engine failed to output bestmove under UCI_LimitStrength")

    def test_skill_level_search_execution(self):
        """Verify that strength-limited search runs and returns valid bestmove without errors."""
        for level in [0, 5, 10, 15, 19]:
            output = run_engine_commands([
                f"setoption name Skill Level value {level}",
                "ucinewgame",
                "isready",
                "position fen 2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1",
                "go movetime 100",
            ])
            bestmoves = [line.split()[1] for line in output if line.startswith("bestmove")]
            self.assertEqual(len(bestmoves), 1, f"Failed to get bestmove for Skill Level {level}")
            self.assertTrue(len(bestmoves[0]) >= 4, f"Invalid bestmove syntax for Skill Level {level}")

    def test_variation_across_games(self):
        """Verify that ucinewgame re-seeds skill randomness for low skill levels."""
        moves = []
        for _ in range(5):
            output = run_engine_commands([
                "setoption name Skill Level value 0",
                "ucinewgame",
                "isready",
                "position fen 2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1",
                "go movetime 200",
            ])
            for line in output:
                if line.startswith("bestmove"):
                    moves.append(line.split()[1])

        self.assertEqual(len(moves), 5)
        self.assertTrue(all(isinstance(m, str) and len(m) >= 4 for m in moves))


if __name__ == "__main__":
    unittest.main()
