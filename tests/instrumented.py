import argparse
import re
import sys
import subprocess
import pathlib
import os
import fnmatch

from testing import (
    EPD,
    Stockfish as Engine,
    MiniTestFramework,
    OrderedClassMembers,
    Valgrind,
    Syzygy,
)

PATH = pathlib.Path(__file__).parent.resolve()
CWD = os.getcwd()


def get_prefix(expect_failure=False):
    if args.valgrind:
        return Valgrind.get_valgrind_command(expect_failure)
    if args.valgrind_thread:
        return Valgrind.get_valgrind_thread_command()

    return []


def get_threads():
    if args.valgrind_thread or args.sanitizer_thread:
        return 4
    return 1


def get_path():
    return os.path.abspath(os.path.join(CWD, args.stockfish_path))


def postfix_check(output):
    if args.sanitizer_undefined:
        for idx, line in enumerate(output):
            if "runtime error:" in line:
                # print next possible 50 lines
                for i in range(50):
                    debug_idx = idx + i
                    if debug_idx < len(output):
                        print(output[debug_idx])
                return False

    if args.sanitizer_thread:
        for idx, line in enumerate(output):
            if "WARNING: ThreadSanitizer:" in line:
                # print next possible 50 lines
                for i in range(50):
                    debug_idx = idx + i
                    if debug_idx < len(output):
                        print(output[debug_idx])
                return False

    if args.valgrind or args.valgrind_thread:
        for line in output:
            match = re.search(r"ERROR SUMMARY:\s*(\d+) errors", line)
            if match and int(match.group(1)) > 0:
                return False

    return True


def Stockfish(*args, expect_failure=False, **kwargs):
    return Engine(get_prefix(expect_failure), get_path(), *args, expect_failure=expect_failure, **kwargs)


class TestCLI(metaclass=OrderedClassMembers):
    def beforeAll(self):
        pass

    def afterAll(self):
        pass

    def beforeEach(self):
        self.stockfish = None

    def afterEach(self):
        assert postfix_check(self.stockfish.get_output()) == True
        self.stockfish.clear_output()

    def test_eval(self):
        self.stockfish = Stockfish("eval".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_go_nodes_1000(self):
        self.stockfish = Stockfish("go nodes 1000".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_go_depth_10(self):
        self.stockfish = Stockfish("go depth 10".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_go_perft_4(self):
        self.stockfish = Stockfish("go perft 4".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_go_movetime_1000(self):
        self.stockfish = Stockfish("go movetime 1000".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_go_wtime_8000_btime_8000_winc_500_binc_500(self):
        self.stockfish = Stockfish(
            "go wtime 8000 btime 8000 winc 500 binc 500".split(" "),
            True,
        )
        assert self.stockfish.process.returncode == 0

    def test_go_wtime_1000_btime_1000_winc_0_binc_0(self):
        self.stockfish = Stockfish(
            "go wtime 1000 btime 1000 winc 0 binc 0".split(" "),
            True,
        )
        assert self.stockfish.process.returncode == 0

    def test_go_wtime_1000_btime_1000_winc_0_binc_0_movestogo_5(self):
        self.stockfish = Stockfish(
            "go wtime 1000 btime 1000 winc 0 binc 0 movestogo 5".split(" "),
            True,
        )
        assert self.stockfish.process.returncode == 0

    def test_go_movetime_200(self):
        self.stockfish = Stockfish("go movetime 200".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_go_nodes_20000_searchmoves_e2e4_d2d4(self):
        self.stockfish = Stockfish(
            "go nodes 20000 searchmoves e2e4 d2d4".split(" "), True
        )
        assert self.stockfish.process.returncode == 0

    def test_bench_128_threads_8_default_depth(self):
        self.stockfish = Stockfish(
            f"bench 128 {get_threads()} 8 default depth".split(" "),
            True,
        )
        assert self.stockfish.process.returncode == 0

    def test_bench_128_threads_3_bench_tmp_epd_depth(self):
        self.stockfish = Stockfish(
            f"bench 128 {get_threads()} 3 {os.path.join(PATH, 'bench_tmp.epd')} depth".split(
                " "
            ),
            True,
        )
        assert self.stockfish.process.returncode == 0

    def test_d(self):
        self.stockfish = Stockfish("d".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_compiler(self):
        self.stockfish = Stockfish("compiler".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_license(self):
        self.stockfish = Stockfish("license".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_uci(self):
        self.stockfish = Stockfish("uci".split(" "), True)
        assert self.stockfish.process.returncode == 0

    def test_export_net_verify_nnue(self):
        current_path = os.path.abspath(os.getcwd())
        self.stockfish = Stockfish(
            f"export_net {os.path.join(current_path, 'verify.nnue')}".split(" "), True
        )
        assert self.stockfish.process.returncode == 0

    # verify the generated net equals the base net

    def test_network_equals_base(self):
        self.stockfish = Stockfish(
            ["uci"],
            True,
        )

        output = self.stockfish.process.stdout

        # find line
        for line in output.split("\n"):
            if "option name EvalFile type string default" in line:
                network = line.split(" ")[-1]
                break

        # find network file in src dir
        network = os.path.join(PATH.parent.resolve(), "src", network)

        if not os.path.exists(network):
            print(
                f"Network file {network} not found, please download the network file over the make command."
            )
            assert False

        diff = subprocess.run(["diff", network, f"verify.nnue"])

        assert diff.returncode == 0


class TestInteractive(metaclass=OrderedClassMembers):
    def beforeAll(self):
        self.stockfish = Stockfish()

    def afterAll(self):
        self.stockfish.quit()
        assert self.stockfish.close() == 0

    def afterEach(self):
        assert postfix_check(self.stockfish.get_output()) == True
        self.stockfish.clear_output()

    def test_startup_output(self):
        self.stockfish.starts_with("Stockfish")

    def test_uci_command(self):
        self.stockfish.send_command("uci")
        self.stockfish.equals("uciok")

    def test_set_threads_option(self):
        self.stockfish.send_command(f"setoption name Threads value {get_threads()}")

    def test_ucinewgame_and_startpos_nodes_1000(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position startpos")
        self.stockfish.send_command("go nodes 1000")
        self.stockfish.starts_with("bestmove")

    def test_ucinewgame_and_startpos_moves(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position startpos moves e2e4 e7e6")
        self.stockfish.send_command("go nodes 1000")
        self.stockfish.starts_with("bestmove")

    def test_fen_position_1(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position fen 5rk1/1K4p1/8/8/3B4/8/8/8 b - - 0 1")
        self.stockfish.send_command("go nodes 1000")
        self.stockfish.starts_with("bestmove")

    def test_fen_position_2_flip(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position fen 5rk1/1K4p1/8/8/3B4/8/8/8 b - - 0 1")
        self.stockfish.send_command("flip")
        self.stockfish.send_command("go nodes 1000")
        self.stockfish.starts_with("bestmove")

    def test_depth_5_with_callback(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position startpos")
        self.stockfish.send_command("go depth 5")

        def callback(output):
            regex = r"info depth \d+ seldepth \d+ multipv \d+ score cp -?\d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv"
            if output.startswith("info depth") and not re.match(regex, output):
                assert False
            if output.startswith("bestmove"):
                return True
            return False

        self.stockfish.check_output(callback)

    def test_ucinewgame_and_go_depth_9(self):
        target_depth = 9

        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("setoption name UCI_ShowWDL value true")
        self.stockfish.send_command("position startpos")
        self.stockfish.send_command(f"go depth {target_depth}")

        depth = 0
        repeated = False

        def callback(output):
            nonlocal depth, repeated

            if output.startswith("info depth"):
                # ignore mid-search updates for deep searches
                if "currmove" in output or "upperbound" in output or "lowerbound" in output:
                    return False

                match = re.match(r"info depth (\d+) ", output)
                assert match, output
                new_depth = int(match.group(1))

                regex = rf"info depth {new_depth} seldepth \d+ multipv \d+ score cp -?\d+ wdl \d+ \d+ \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv"
                assert re.match(regex, output), output

                # The final depth may repeat once, if its PV was extended by
                # the ponder move that was extracted from the TT.
                if not repeated and new_depth == depth == target_depth:
                    repeated = True
                else:
                    assert new_depth == depth + 1, output
                    depth = new_depth

            if output.startswith("bestmove"):
                assert depth == target_depth
                return True

            return False

        self.stockfish.check_output(callback)
    def test_go_depth_3_with_mismatched_clock(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position startpos")
        self.stockfish.send_command("go depth 3 btime 1000")

        max_depth = 0

        def callback(output):
            nonlocal max_depth
            if output.startswith("info depth"):
                match = re.search(r"info depth (\d+)", output)
                if match:
                    max_depth = max(max_depth, int(match.group(1)))

            if output.startswith("bestmove"):
                assert max_depth == 3
                return True

            return False

        self.stockfish.check_output(callback)

    def test_clear_hash(self):
        self.stockfish.send_command("setoption name Clear Hash")

    def test_fen_position_mate_1(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 5K2/8/2qk4/2nPp3/3r4/6B1/B7/3R4 w - e6"
        )
        self.stockfish.send_command("go depth 18")

        self.stockfish.expect("* score mate 1 * pv d5e6")
        self.stockfish.equals("bestmove d5e6")

    def test_fen_position_mate_minus_1(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 2brrb2/8/p7/Q7/1p1kpPp1/1P1pN1K1/3P4/8 b - -"
        )
        self.stockfish.send_command("go depth 18")
        self.stockfish.expect("* score mate -1 *")
        self.stockfish.starts_with("bestmove")

    def test_fen_position_fixed_node(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 5K2/8/2P1P1Pk/6pP/3p2P1/1P6/3P4/8 w - - 0 1"
        )
        self.stockfish.send_command("go nodes 500000")
        self.stockfish.starts_with("bestmove")

    def test_fen_position_with_mate_go_depth(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -"
        )
        self.stockfish.send_command("go depth 18 searchmoves c6d7")
        self.stockfish.expect("* score mate 2 * pv c6d7 * f7f5")

        self.stockfish.starts_with("bestmove")

    def test_fen_position_with_mate_go_mate(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -"
        )
        self.stockfish.send_command("go mate 2 searchmoves c6d7")
        self.stockfish.expect("* score mate 2 * pv c6d7 *")

        self.stockfish.starts_with("bestmove")

    def test_fen_position_with_mate_go_nodes(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -"
        )
        self.stockfish.send_command("go nodes 500000 searchmoves c6d7")
        self.stockfish.expect("* score mate 2 * pv c6d7 * f7f5")

        self.stockfish.starts_with("bestmove")

    def test_fen_position_depth_27(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen r1b2r1k/pp1p2pp/2p5/2B1q3/8/8/P1PN2PP/R4RK1 w - - 0 18"
        )
        self.stockfish.send_command("go")
        self.stockfish.contains("score mate 1")

        self.stockfish.starts_with("bestmove")

    def test_fen_position_with_mate_go_depth_and_promotion(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - - moves c6d7 f2f1q"
        )
        self.stockfish.send_command("go depth 18")
        self.stockfish.expect("* score mate 1 * pv f7f5")
        self.stockfish.starts_with("bestmove f7f5")

    def test_fen_position_with_mate_go_depth_and_searchmoves(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -"
        )
        self.stockfish.send_command("go depth 18 searchmoves c6d7")
        self.stockfish.expect("* score mate 2 * pv c6d7 * f7f5")

        self.stockfish.starts_with("bestmove c6d7")

    def test_fen_position_with_moves_with_mate_go_depth_and_searchmoves(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - - moves c6d7"
        )
        self.stockfish.send_command("go depth 18 searchmoves e3e2")
        self.stockfish.expect("* score mate -1 * pv e3e2 f7f5")
        self.stockfish.starts_with("bestmove e3e2")

    def test_verify_nnue_network(self):
        current_path = os.path.abspath(os.getcwd())
        Stockfish(
            f"export_net {os.path.join(current_path, 'verify.nnue')}".split(" "), True
        )

        self.stockfish.send_command("setoption name EvalFile value verify.nnue")
        self.stockfish.send_command("position startpos")
        self.stockfish.send_command("go depth 5")
        self.stockfish.starts_with("bestmove")

    def test_multipv_setting(self):
        self.stockfish.send_command("setoption name MultiPV value 4")
        self.stockfish.send_command("position startpos")
        self.stockfish.send_command("go depth 5")
        self.stockfish.starts_with("bestmove")

    def test_fen_position_with_skill_level(self):
        self.stockfish.send_command("setoption name Skill Level value 10")
        self.stockfish.send_command("position startpos")
        self.stockfish.send_command("go depth 5")
        self.stockfish.starts_with("bestmove")

        self.stockfish.send_command("setoption name Skill Level value 20")


class TestSyzygy(metaclass=OrderedClassMembers):
    def beforeAll(self):
        self.stockfish = Stockfish()

    def afterAll(self):
        self.stockfish.quit()
        assert self.stockfish.close() == 0

    def afterEach(self):
        assert postfix_check(self.stockfish.get_output()) == True
        self.stockfish.clear_output()

    def test_syzygy_setup(self):
        self.stockfish.starts_with("Stockfish")
        self.stockfish.send_command("uci")
        self.stockfish.send_command(
            f"setoption name SyzygyPath value {os.path.join(PATH, 'syzygy')}"
        )
        self.stockfish.expect(
            "info string Found 35 WDL and 35 DTZ tablebase files (up to 4-man)."
        )

    def test_syzygy_bench(self):
        self.stockfish.send_command("bench 128 1 8 default depth")
        self.stockfish.expect("Nodes searched  :*")

    def test_syzygy_position(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position fen 4k3/PP6/8/8/8/8/8/4K3 w - - 0 1")
        self.stockfish.send_command("go depth 5")

        def check_output(output):
            if "score cp 20000" in output or "score mate" in output:
                return True

        self.stockfish.check_output(check_output)
        self.stockfish.expect("bestmove *")

    def test_syzygy_position_2(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position fen 8/1P6/2B5/8/4K3/8/6k1/8 w - - 0 1")
        self.stockfish.send_command("go depth 5")

        def check_output(output):
            if "score cp 20000" in output or "score mate" in output:
                return True

        self.stockfish.check_output(check_output)
        self.stockfish.expect("bestmove *")

    def test_syzygy_position_3(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position fen 8/1P6/2B5/8/4K3/8/6k1/8 b - - 0 1")
        self.stockfish.send_command("go depth 5")

        def check_output(output):
            if "score cp -20000" in output or "score mate -" in output:
                return True

        self.stockfish.check_output(check_output)
        self.stockfish.expect("bestmove *")

    def test_syzygy_position_4(self):
        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("position fen 8/8/7B/3B3P/7k/8/5K2/3r4 w - - 0 1")
        self.stockfish.send_command("go depth 1")
        self.stockfish.expect("bestmove *")

    # The StateInfo ring buffer keeps MAX_HIST (512) states. With a longer history of
    # reversible plies the repetition scan used to wrap around and read the state it
    # was filling in, which left a stale "repeated" flag on every state past ply 512.
    # A position that merely occurred a second time then looked like a threefold
    # repetition, so with Syzygy50MoveRule off a tablebase loss was scored as a draw.
    #
    # The history below is 528 plies of king and queen moves, with no capture, no pawn
    # move and no position occurring twice, so every repetition scan runs through the
    # whole ring. In the final position b3c3 returns to a position that occurred once,
    # 4 plies earlier. That is a second occurrence, not a draw by threefold repetition,
    # and Black is lost whatever he plays.
    LONG_REVERSIBLE_FEN = "8/8/8/3k4/8/8/2KQ4/8 b - - 0 1"
    LONG_REVERSIBLE_MOVES = """
        d5c6 c2d3 c6b7 d2d1 b7a6 d1c2 a6b7 c2a4 b7c8 a4a2 c8d8 a2g8
        d8d7 d3d4 d7c6 d4c3 c6c7 g8g4 c7d6 c3d3 d6c6 g4b4 c6d7 b4b8
        d7e7 d3d4 e7e6 b8g8 e6f5 g8f8 f5g6 f8d6 g6g5 d6f6 g5g4 f6f1
        g4g3 f1c4 g3g4 c4c2 g4h4 c2h7 h4g5 h7d7 g5f6 d7a7 f6f5 a7b7
        f5f4 b7c7 f4f5 c7c3 f5g5 c3e1 g5f6 e1e3 f6f5 e3c1 f5e6 d4c5
        e6e7 c1f1 e7e6 f1g2 e6d7 c5b5 d7e8 g2g4 e8d8 g4g8 d8d7 g8b3
        d7e8 b3f3 e8d8 f3e2 d8d7 e2f3 d7c8 b5a6 c8d7 f3c3 d7e8 c3c7
        e8f8 c7d7 f8g8 d7d2 g8g7 d2h2 g7g8 h2h6 g8f7 h6b6 f7g7 b6b4
        g7h6 b4h4 h6g6 h4a4 g6h5 a4a2 h5h4 a2a1 h4h3 a6a5 h3g3 a1b1
        g3g2 a5a4 g2h2 b1b2 h2g3 b2c3 g3f2 c3c7 f2e3 c7d7 e3e2 d7a7
        e2f3 a4a5 f3e2 a5b4 e2d3 a7a5 d3e3 a5a3 e3d4 a3c3 d4e4 b4c4
        e4f5 c3a1 f5e6 a1h8 e6f7 h8h5 f7e7 h5f7 e7d8 c4d3 d8c8 f7g7
        c8d8 g7g2 d8c7 g2f2 c7d7 f2f1 d7e7 f1b1 e7e6 d3c2 e6f5 b1h1
        f5e5 h1f1 e5e6 f1f6 e6d7 c2c1 d7e8 f6g7 e8d8 g7f7 d8c8 f7c4
        c8d7 c4f1 d7e6 f1e2 e6d7 e2f3 d7c7 f3a3 c7c8 a3a4 c8c7 a4e8
        c7b7 e8a4 b7b6 a4e8 b6a7 c1b2 a7b6 e8a4 b6c5 a4g4 c5b5 g4g2
        b5c4 g2c2 c4b5 c2d2 b5c6 d2d6 c6b5 d6e7 b5b6 b2a2 b6a5 e7f7
        a5a4 f7g8 a4a5 g8g6 a5b5 g6f7 b5a6 f7d5 a6a7 a2a1 a7b8 d5d6
        b8c8 d6e7 c8b8 e7e3 b8b7 e3e4 b7b6 e4d5 b6c7 d5a2 c7d8 a2h2
        d8c8 a1b2 c8b7 h2g2 b7a7 g2e2 a7b8 b2b1 b8c8 e2d2 c8b7 d2a5
        b7b8 a5h5 b8b7 h5h3 b7c6 h3d3 c6c7 d3a6 c7d7 a6c4 d7d6 c4g4
        d6e7 g4g2 e7f7 b1c2 f7f8 g2f1 f8g8 c2d1 g8g7 d1d2 g7g6 d2c3
        g6h7 f1b1 h7g8 c3d2 g8g7 d2e1 g7h8 b1b5 h8g8 e1d2 g8g7 b5b8
        g7f6 b8b2 f6g6 d2e1 g6g5 b2c3 g5h4 c3c4 h4h5 c4g4 h5h6 g4g3
        h6h7 g3f4 h7g7 f4e5 g7h6 e5e6 h6h7 e6c8 h7h6 c8f5 h6g7 f5g5
        g7f7 g5a5 f7f6 a5b6 f6g7 b6c5 g7f7 c5e5 f7f8 e5e3 f8f7 e3d3
        f7g8 d3c4 g8h7 c4c5 h7h6 c5c1 h6g6 e1f2 g6f7 f2g2 f7e7 c1c7
        e7e6 c7b6 e6d5 b6b2 d5c4 b2b4 c4d5 b4a5 d5d6 g2h3 d6e7 a5b6
        e7d7 b6b7 d7e8 b7b1 e8e7 h3g4 e7f8 b1a1 f8g8 g4h4 g8f7 h4h3
        f7e6 a1h1 e6e5 h1d5 e5f6 d5b5 f6e7 b5f1 e7d7 f1d1 d7e8 d1d3
        e8f8 d3d4 f8e7 h3g2 e7f8 g2g1 f8e7 g1f1 e7e6 d4c3 e6e7 c3d2
        e7f7 d2a5 f7e8 a5b6 e8f8 b6b5 f8g7 b5d3 g7f8 d3d7 f8g8 d7b5
        g8f8 b5b2 f8f7 b2f6 f7e8 f1g1 e8d7 f6f7 d7d6 g1f1 d6e5 f7f8
        e5d4 f8f6 d4d3 f6g7 d3e3 g7f8 e3e4 f8f4 e4d5 f4f8 d5c4 f8b8
        c4d4 b8h2 d4d3 h2b8 d3d2 b8f8 d2c1 f8f3 c1b1 f1g1 b1a2 f3g2
        a2a1 g1f1 a1b1 f1f2 b1b2 g2f3 b2a2 f2e1 a2a1 f3g4 a1b1 g4f3
        b1b2 f3f2 b2a3 f2a7 a3b2 a7c5 b2b1 e1e2 b1a2 c5b5 a2a3 b5a4
        a3b2 e2f3 b2b1 a4f4 b1a2 f4f7 a2b2 f7h7 b2a2 h7h2 a2a1 h2h8
        a1b1 h8g7 b1a2 g7f6 a2b1 f6h6 b1b2 h6a6 b2b1 a6c4 b1a1 c4f7
        a1b1 f3g2 b1c1 f7f3 c1b2 f3f2 b2c3 g2f3 c3b4 f2e3 b4c4 e3a7
        c4c3 f3f4 c3d3 a7f2 d3c4 f2d4 c4b3 d4h8 b3a2 h8a1 a2b3 a1d1
        b3c3 f4f5 c3b4 d1b3 b4a5 b3f7 a5a4 f7f8 a4b5 f8e7 b5a4 e7f7
        a4b5 f5e5 b5c5 e5f5 c5b4 f5f4 b4b5 f7f6 b5c5 f6c6 c5b4 c6g2
        b4c4 g2b7 c4d4 f4f3 d4c4 b7c7 c4d3 c7f4 d3c3 f3f2 c3b3 f2f3
    """.split()

    def test_syzygy_long_reversible_history(self):
        assert len(self.LONG_REVERSIBLE_MOVES) > 512

        self.stockfish.send_command("ucinewgame")
        self.stockfish.send_command("setoption name Syzygy50MoveRule value false")
        self.stockfish.send_command(
            f"position fen {self.LONG_REVERSIBLE_FEN} moves {' '.join(self.LONG_REVERSIBLE_MOVES)}"
        )
        self.stockfish.send_command("go depth 1")

        # The side to move is lost in the tablebase, whatever the history.
        for line in self.stockfish.readline():
            if line.startswith("info depth"):
                assert "score cp -20000" in line or "score mate -" in line, line
                break

        self.stockfish.expect("bestmove *")
        self.stockfish.send_command("setoption name Syzygy50MoveRule value true")

class TestEnPassantSanitization(metaclass=OrderedClassMembers):
    def beforeAll(self):
        self.stockfish = Stockfish()

    def afterAll(self):
        self.stockfish.quit()
        assert self.stockfish.close() == 0

    def afterEach(self):
        assert postfix_check(self.stockfish.get_output()) == True
        self.stockfish.clear_output()

    def test_position_1(self):
        self.stockfish.send_command("position fen rnbqkbnr/ppp1p1pp/5p2/3pP3/8/8/PPPP1PPP/RNBQKBNR w kq d6 0 3")
        self.stockfish.send_command("d")

        self.stockfish.expect_for_line_matching("Fen*", "*rnbqkbnr/ppp1p1pp/5p2/3pP3/8/8/PPPP1PPP/RNBQKBNR w kq d6 0 3*")

    def test_position_2(self):
        self.stockfish.send_command("position fen k7/8/8/1pP5/2K5/8/8/8 w - b6 0 1")
        self.stockfish.send_command("d")

        self.stockfish.expect_for_line_matching("Fen*", "*k7/8/8/1pP5/2K5/8/8/8 w - b6 0 1*")

    def test_position_3(self):
        self.stockfish.send_command("position fen k1r5/8/8/1pP5/2K5/8/8/8 w - b6 0 1")
        self.stockfish.send_command("d")

        self.stockfish.expect_for_line_matching("Fen*", "*k1r5/8/8/1pP5/2K5/8/8/8 w - - 0 1*")

    def test_position_4(self):
        self.stockfish.send_command("position fen k1r5/8/8/1pP5/8/2K5/8/8 w - b6 0 1")
        self.stockfish.send_command("d")

        self.stockfish.expect_for_line_matching("Fen*", "*k1r5/8/8/1pP5/8/2K5/8/8 w - - 0 1*")

    def test_position_5(self):
        self.stockfish.send_command("position fen k1r5/8/8/PpP5/8/2K5/8/8 w - b6 0 1")
        self.stockfish.send_command("d")

        self.stockfish.expect_for_line_matching("Fen*", "*k1r5/8/8/PpP5/8/2K5/8/8 w - b6 0 1*")

    def test_position_6(self):
        self.stockfish.send_command("position fen k1r5/8/8/PpP5/2K5/8/8/8 w - b6 0 1")
        self.stockfish.send_command("d")

        self.stockfish.expect_for_line_matching("Fen*", "*k1r5/8/8/PpP5/2K5/8/8/8 w - b6 0 1*")

    def test_position_7(self):
        self.stockfish.send_command("position fen k7/4b3/8/PpP5/1K6/8/8/8 w - b6 0 1")
        self.stockfish.send_command("d")

        self.stockfish.expect_for_line_matching("Fen*", "*k7/4b3/8/PpP5/1K6/8/8/8 w - b6 0 1*")

    def test_position_8(self):
        self.stockfish.send_command("position fen k7/b5b1/8/2PpP3/3K4/8/8/8 w - d6 0 1")
        self.stockfish.send_command("d")

        self.stockfish.expect_for_line_matching("Fen*", "*k7/b5b1/8/2PpP3/3K4/8/8/8 w - - 0 1*")

    def test_position_9(self):
        self.stockfish.send_command("position fen k7/8/8/r2pPK2/8/8/8/8 w - d6 0 1")
        self.stockfish.send_command("d")

        self.stockfish.expect_for_line_matching("Fen*", "*k7/8/8/r2pPK2/8/8/8/8 w - - 0 1*")

    def test_position_10(self):
        self.stockfish.send_command("position fen k7/8/8/r1PpPK2/8/8/8/8 w - d6 0 1")
        self.stockfish.send_command("d")

        self.stockfish.expect_for_line_matching("Fen*", "*k7/8/8/r1PpPK2/8/8/8/8 w - d6 0 1*")

    def test_position_11(self):
        self.stockfish.send_command("position fen kb6/8/8/3pP3/5K2/8/8/8 w - d6 0 1")
        self.stockfish.send_command("d")

        self.stockfish.expect_for_line_matching("Fen*", "*kb6/8/8/3pP3/5K2/8/8/8 w - d6 0 1*")

    def test_position_find_draw(self):
        self.stockfish.send_command("position fen q4kb1/3Q2nq/8/r3PpK1/2n5/7q/8/q7 w - f6 0 1 moves d7c8 f8f7 c8d7 f7f8 d7d8 f8f7")
        self.stockfish.send_command("go nodes 10000")

        def check_output(output):
            if fnmatch.fnmatch(output, "* score cp 0 * pv d8d7*"):
                return True

        self.stockfish.check_output(check_output)
        self.stockfish.expect("bestmove d8d7*")

class TestInvalidFEN(metaclass=OrderedClassMembers):
    def beforeEach(self):
        self.stockfish = None

    def afterEach(self):
        assert postfix_check(self.stockfish.get_output()) == True
        self.stockfish.clear_output()

    def _expect_critical(self, fen):
        self.stockfish = Stockfish(f"position fen {fen}".split(" "), True, expect_failure=True)
        assert self.stockfish.process.returncode != 0
        assert "CRITICAL ERROR" in self.stockfish.process.stdout

    def test_no_kings(self):
        self._expect_critical("8/8/8/8/8/8/8/8 w - - 0 1")

    def test_invalid_piece(self):
        self._expect_critical("rnbqkbnr/pppXpppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")

    def test_invalid_side_to_move(self):
        self._expect_critical("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1")

    def test_pawns_on_back_rank(self):
        self._expect_critical("pppppppp/8/8/8/8/8/8/4K2k w - - 0 1")

    def test_invalid_skip_count(self):
        self._expect_critical("9/8/8/8/8/8/8/8 w - - 0 1")


class TestInvalidOptions(metaclass=OrderedClassMembers):
    def beforeAll(self):
        self.stockfish = Stockfish()

    def afterAll(self):
        self.stockfish.quit()
        assert self.stockfish.close() == 0

    def afterEach(self):
        assert postfix_check(self.stockfish.get_output()) == True
        self.stockfish.clear_output()

    # Ignore bogus spin values
    def test_spin_non_numeric(self):
        self.stockfish.send_command("setoption name Threads value abc")
        self.stockfish.send_command("isready")
        self.stockfish.equals("readyok")

    def test_spin_out_of_range(self):
        self.stockfish.send_command("setoption name Threads value 999999999999")
        self.stockfish.send_command("isready")
        self.stockfish.equals("readyok")

    def test_spin_negative(self):
        self.stockfish.send_command("setoption name Threads value -5")
        self.stockfish.send_command("isready")
        self.stockfish.equals("readyok")

    # Warn on bogus NUMA configs
    def test_numa_garbage(self):
        self.stockfish.send_command("setoption name NumaPolicy value zzz")
        self.stockfish.expect("*NumaPolicy: invalid value 'zzz', keeping previous config.*")

    def test_numa_malformed_range(self):
        self.stockfish.send_command("setoption name NumaPolicy value 0-")
        self.stockfish.expect("*NumaPolicy: invalid value '0-', keeping previous config.*")

    def test_numa_overflow(self):
        self.stockfish.send_command(
            "setoption name NumaPolicy value 99999999999999999999999"
        )
        self.stockfish.expect("*NumaPolicy: invalid value*keeping previous config.*")
        self.stockfish.send_command("isready")
        self.stockfish.equals("readyok")


class TestBenchFile(metaclass=OrderedClassMembers):
    def beforeEach(self):
        self.stockfish = None

    def afterEach(self):
        assert postfix_check(self.stockfish.get_output()) == True
        self.stockfish.clear_output()

    def _bench(self, name, content, expect_failure=False):
        with open(name, "w") as f:
            f.write(content)
        self.stockfish = Stockfish(f"bench 16 1 4 {name} depth".split(" "), True, expect_failure=expect_failure)

    def test_valid_file(self):
        self._bench("good.epd", "4k3/8/4K3/8/8/8/8/8 w - - 0 1\n")
        assert self.stockfish.process.returncode == 0
        assert "Nodes searched" in self.stockfish.process.stderr

    def test_empty_file(self):
        self._bench("empty.epd", "")
        assert self.stockfish.process.returncode == 0

    def test_malformed_fen(self):
        self._bench("bad.epd", "not a valid fen\n", expect_failure=True)
        assert self.stockfish.process.returncode != 0
        assert "CRITICAL ERROR" in self.stockfish.process.stdout

    def test_missing_file(self):
        self.stockfish = Stockfish(
            "bench 16 1 4 does_not_exist.epd depth".split(" "), True, expect_failure=True
        )
        assert self.stockfish.process.returncode != 0


def parse_args():
    parser = argparse.ArgumentParser(description="Run Stockfish with testing options")
    parser.add_argument("--valgrind", action="store_true", help="Run valgrind testing")
    parser.add_argument(
        "--valgrind-thread", action="store_true", help="Run valgrind-thread testing"
    )
    parser.add_argument(
        "--sanitizer-undefined",
        action="store_true",
        help="Run sanitizer-undefined testing",
    )
    parser.add_argument(
        "--sanitizer-thread", action="store_true", help="Run sanitizer-thread testing"
    )

    parser.add_argument(
        "--none", action="store_true", help="Run without any testing options"
    )
    parser.add_argument("stockfish_path", type=str, help="Path to Stockfish binary")

    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()

    EPD.create_bench_epd()
    Syzygy.download_syzygy()

    framework = MiniTestFramework()

    # Each test suite will be run inside a temporary directory
    framework.run(
        [
            TestCLI,
            TestInteractive,
            TestSyzygy,
            TestEnPassantSanitization,
            TestInvalidFEN,
            TestInvalidOptions,
            TestBenchFile,
        ]
    )

    EPD.delete_bench_epd()

    if framework.has_failed():
        sys.exit(1)

    sys.exit(0)
