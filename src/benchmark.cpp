/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "benchmark.h"
#include "numa.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

// clang-format off
const std::vector<std::string> Defaults = {
  "setoption name UCI_Chess960 value false",
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10",
  "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11",
  "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
  "rq3rk1/ppp2ppp/1bnpb3/3N2B1/3NP3/7P/PPPQ1PP1/2KR3R w - - 7 14 moves d4e6",
  "r1bq1r1k/1pp1n1pp/1p1p4/4p2Q/4Pp2/1BNP4/PPP2PPP/3R1RK1 w - - 2 14 moves g2g4",
  "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
  "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
  "r1bq1rk1/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - - 1 16",
  "4r1k1/r1q2ppp/ppp2n2/4P3/5Rb1/1N1BQ3/PPP3PP/R5K1 w - - 1 17",
  "2rqkb1r/ppp2p2/2npb1p1/1N1Nn2p/2P1PP2/8/PP2B1PP/R1BQK2R b KQ - 0 11",
  "r1bq1r1k/b1p1npp1/p2p3p/1p6/3PP3/1B2NN2/PP3PPP/R2Q1RK1 w - - 1 16",
  "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - - 6 22",
  "r1q2rk1/2p1bppp/2Pp4/p6b/Q1PNp3/4B3/PP1R1PPP/2K4R w - - 2 18",
  "4k2r/1pb2ppp/1p2p3/1R1p4/3P4/2r1PN2/P4PPP/1R4K1 b - - 3 22",
  "3q2k1/pb3p1p/4pbp1/2r5/PpN2N2/1P2P2P/5PP1/Q2R2K1 b - - 4 26",
  "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/3N4 b - - 0 1",
  "3b4/5kp1/1p1p1p1p/pP1PpP1P/P1P1P3/3KN3/8/8 w - - 0 1",
  "2K5/p7/7P/5pR1/8/5k2/r7/8 w - - 0 1 moves g5g6 f3e3 g6g5 e3f3",
  "8/6pk/1p6/8/PP3p1p/5P2/4KP1q/3Q4 w - - 0 1",
  "7k/3p2pp/4q3/8/4Q3/5Kp1/P6b/8 w - - 0 1",
  "8/2p5/8/2kPKp1p/2p4P/2P5/3P4/8 w - - 0 1",
  "8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - - 0 1",
  "8/pp2r1k1/2p1p3/3pP2p/1P1P1P1P/P5KR/8/8 w - - 0 1",
  "8/3p4/p1bk3p/Pp6/1Kp1PpPp/2P2P1P/2P5/5B2 b - - 0 1",
  "5k2/7R/4P2p/5K2/p1r2P1p/8/8/8 b - - 0 1",
  "6k1/6p1/P6p/r1N5/5p2/7P/1b3PP1/4R1K1 w - - 0 1",
  "1r3k2/4q3/2Pp3b/3Bp3/2Q2p2/1p1P2P1/1P2KP2/3N4 w - - 0 1",
  "6k1/4pp1p/3p2p1/P1pPb3/R7/1r2P1PP/3B1P2/6K1 w - - 0 1",
  "8/3p3B/5p2/5P2/p7/PP5b/k7/6K1 w - - 0 1",
  "5rk1/q6p/2p3bR/1pPp1rP1/1P1Pp3/P3B1Q1/1K3P2/R7 w - - 93 90",
  "4rrk1/1p1nq3/p7/2p1P1pp/3P2bp/3Q1Bn1/PPPB4/1K2R1NR w - - 40 21",
  "r3k2r/3nnpbp/q2pp1p1/p7/Pp1PPPP1/4BNN1/1P5P/R2Q1RK1 w kq - 0 16",
  "3Qb1k1/1r2ppb1/pN1n2q1/Pp1Pp1Pr/4P2p/4BP2/4B1R1/1R5K b - - 11 40",
  "4k3/3q1r2/1N2r1b1/3ppN2/2nPP3/1B1R2n1/2R1Q3/3K4 w - - 5 1",

  // 5-man positions
  "8/8/8/8/5kp1/P7/8/1K1N4 w - - 0 1",     // Kc2 - mate
  "8/8/8/5N2/8/p7/8/2NK3k w - - 0 1",      // Na2 - mate
  "8/3k4/8/8/8/4B3/4KB2/2B5 w - - 0 1",    // draw

  // 6-man positions
  "8/8/1P6/5pr1/8/4R3/7k/2K5 w - - 0 1",   // Re5 - mate
  "8/2p4P/8/kr6/6R1/8/8/1K6 w - - 0 1",    // Ka2 - mate
  "8/8/3P3k/8/1p6/8/1P6/1K3n2 b - - 0 1",  // Nd2 - draw

  // 7-man positions
  "8/R7/2q5/8/6k1/8/1P5p/K6R w - - 0 124", // Draw

  // Mate and stalemate positions
  "6k1/3b3r/1p1p4/p1n2p2/1PPNpP1q/P3Q1p1/1R1RB1P1/5K2 b - - 0 1",
  "r2r1n2/pp2bk2/2p1p2p/3q4/3PN1QP/2P3R1/P4PP1/5RK1 w - - 0 1",
  "8/8/8/8/8/6k1/6p1/6K1 w - -",
  "7k/7P/6K1/8/3B4/8/8/8 b - -",

  // Chess 960
  "setoption name UCI_Chess960 value true",
  "bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w HFhf - 0 1 moves g2g3 d7d5 d2d4 c8h3 c1g5 e8d6 g5e7 f7f6",
  "nqbnrkrb/pppppppp/8/8/8/8/PPPPPPPP/NQBNRKRB w KQkq - 0 1",
  "setoption name UCI_Chess960 value false"
};
// clang-format on

// clang-format off
// human-randomly picked 5 games with <60 moves from
// https://tests.stockfishchess.org/tests/view/665c71f9fd45fb0f907c21e0
// only moves for one side
const std::vector<std::vector<std::string>> BenchmarkPositions = {
    {
        "rnbq1k1r/ppp1bppp/4pn2/8/2B5/2NP1N2/PPP2PPP/R1BQR1K1 b - - 2 8",
        "rnbq1k1r/pp2bppp/4pn2/2p5/2B2B2/2NP1N2/PPP2PPP/R2QR1K1 b - - 1 9",
        "r1bq1k1r/pp2bppp/2n1pn2/2p5/2B1NB2/3P1N2/PPP2PPP/R2QR1K1 b - - 3 10",
        "r1bq1k1r/pp2bppp/2n1p3/2p5/2B1PB2/5N2/PPP2PPP/R2QR1K1 b - - 0 11",
        "r1b2k1r/pp2bppp/2n1p3/2p5/2B1PB2/5N2/PPP2PPP/3RR1K1 b - - 0 12",
        "r1b1k2r/pp2bppp/2n1p3/2p5/2B1PB2/2P2N2/PP3PPP/3RR1K1 b - - 0 13",
        "r1b1k2r/1p2bppp/p1n1p3/2p5/4PB2/2P2N2/PP2BPPP/3RR1K1 b - - 1 14",
        "r1b1k2r/4bppp/p1n1p3/1pp5/P3PB2/2P2N2/1P2BPPP/3RR1K1 b - - 0 15",
        "r1b1k2r/4bppp/p1n1p3/1P6/2p1PB2/2P2N2/1P2BPPP/3RR1K1 b - - 0 16",
        "r1b1k2r/4bppp/2n1p3/1p6/2p1PB2/1PP2N2/4BPPP/3RR1K1 b - - 0 17",
        "r3k2r/3bbppp/2n1p3/1p6/2P1PB2/2P2N2/4BPPP/3RR1K1 b - - 0 18",
        "r3k2r/3bbppp/2n1p3/8/1pP1P3/2P2N2/3BBPPP/3RR1K1 b - - 1 19",
        "1r2k2r/3bbppp/2n1p3/8/1pPNP3/2P5/3BBPPP/3RR1K1 b - - 3 20",
        "1r2k2r/3bbppp/2n1p3/8/2PNP3/2B5/4BPPP/3RR1K1 b - - 0 21",
        "1r2k2r/3bb1pp/2n1pp2/1N6/2P1P3/2B5/4BPPP/3RR1K1 b - - 1 22",
        "1r2k2r/3b2pp/2n1pp2/1N6/1BP1P3/8/4BPPP/3RR1K1 b - - 0 23",
        "1r2k2r/3b2pp/4pp2/1N6/1nP1P3/8/3RBPPP/4R1K1 b - - 1 24",
        "1r5r/3bk1pp/4pp2/1N6/1nP1PP2/8/3RB1PP/4R1K1 b - - 0 25",
        "1r5r/3bk1pp/2n1pp2/1N6/2P1PP2/8/3RBKPP/4R3 b - - 2 26",
        "1r5r/3bk1pp/2n2p2/1N2p3/2P1PP2/6P1/3RBK1P/4R3 b - - 0 27",
        "1r1r4/3bk1pp/2n2p2/1N2p3/2P1PP2/6P1/3RBK1P/R7 b - - 2 28",
        "1r1r4/N3k1pp/2n1bp2/4p3/2P1PP2/6P1/3RBK1P/R7 b - - 4 29",
        "1r1r4/3bk1pp/2N2p2/4p3/2P1PP2/6P1/3RBK1P/R7 b - - 0 30",
        "1r1R4/4k1pp/2b2p2/4p3/2P1PP2/6P1/4BK1P/R7 b - - 0 31",
        "3r4/4k1pp/2b2p2/4P3/2P1P3/6P1/4BK1P/R7 b - - 0 32",
        "3r4/R3k1pp/2b5/4p3/2P1P3/6P1/4BK1P/8 b - - 1 33",
        "8/3rk1pp/2b5/R3p3/2P1P3/6P1/4BK1P/8 b - - 3 34",
        "8/3r2pp/2bk4/R1P1p3/4P3/6P1/4BK1P/8 b - - 0 35",
        "8/2kr2pp/2b5/R1P1p3/4P3/4K1P1/4B2P/8 b - - 2 36",
        "1k6/3r2pp/2b5/RBP1p3/4P3/4K1P1/7P/8 b - - 4 37",
        "8/1k1r2pp/2b5/R1P1p3/4P3/3BK1P1/7P/8 b - - 6 38",
        "1k6/3r2pp/2b5/2P1p3/4P3/3BK1P1/7P/R7 b - - 8 39",
        "1k6/r5pp/2b5/2P1p3/4P3/3BK1P1/7P/5R2 b - - 10 40",
        "1k3R2/6pp/2b5/2P1p3/4P3/r2BK1P1/7P/8 b - - 12 41",
        "5R2/2k3pp/2b5/2P1p3/4P3/r2B2P1/3K3P/8 b - - 14 42",
        "5R2/2k3pp/2b5/2P1p3/4P3/3BK1P1/r6P/8 b - - 16 43",
        "5R2/2k3pp/2b5/2P1p3/4P3/r2B2P1/4K2P/8 b - - 18 44",
        "5R2/2k3pp/2b5/2P1p3/4P3/3B1KP1/r6P/8 b - - 20 45",
        "8/2k2Rpp/2b5/2P1p3/4P3/r2B1KP1/7P/8 b - - 22 46",
        "3k4/5Rpp/2b5/2P1p3/4P3/r2B2P1/4K2P/8 b - - 24 47",
        "3k4/5Rpp/2b5/2P1p3/4P3/3B1KP1/r6P/8 b - - 26 48",
        "3k4/5Rpp/2b5/2P1p3/4P3/r2B2P1/4K2P/8 b - - 28 49",
        "3k4/5Rpp/2b5/2P1p3/4P3/3BK1P1/r6P/8 b - - 30 50",
        "3k4/5Rpp/2b5/2P1p3/4P3/r2B2P1/3K3P/8 b - - 32 51",
        "3k4/5Rpp/2b5/2P1p3/4P3/2KB2P1/r6P/8 b - - 34 52",
        "3k4/5Rpp/2b5/2P1p3/4P3/r2B2P1/2K4P/8 b - - 36 53",
        "3k4/5Rpp/2b5/2P1p3/4P3/1K1B2P1/r6P/8 b - - 38 54",
        "3k4/6Rp/2b5/2P1p3/4P3/1K1B2P1/7r/8 b - - 0 55",
        "3k4/8/2b3Rp/2P1p3/4P3/1K1B2P1/7r/8 b - - 1 56",
        "8/2k3R1/2b4p/2P1p3/4P3/1K1B2P1/7r/8 b - - 3 57",
        "3k4/8/2b3Rp/2P1p3/4P3/1K1B2P1/7r/8 b - - 5 58",
        "8/2k5/2b3Rp/2P1p3/1K2P3/3B2P1/7r/8 b - - 7 59",
        "8/2k5/2b3Rp/2P1p3/4P3/2KB2P1/3r4/8 b - - 9 60",
        "8/2k5/2b3Rp/2P1p3/1K2P3/3B2P1/6r1/8 b - - 11 61",
        "8/2k5/2b3Rp/2P1p3/4P3/2KB2P1/3r4/8 b - - 13 62",
        "8/2k5/2b3Rp/2P1p3/2K1P3/3B2P1/6r1/8 b - - 15 63",
        "4b3/2k3R1/7p/2P1p3/2K1P3/3B2P1/6r1/8 b - - 17 64",
    },
    {
        "r1bqkbnr/npp1pppp/p7/3P4/4pB2/2N5/PPP2PPP/R2QKBNR w KQkq - 1 6",
        "r1bqkb1r/npp1pppp/p4n2/3P4/4pB2/2N5/PPP1QPPP/R3KBNR w KQkq - 3 7",
        "r2qkb1r/npp1pppp/p4n2/3P1b2/4pB2/2N5/PPP1QPPP/2KR1BNR w kq - 5 8",
        "r2qkb1r/1pp1pppp/p4n2/1n1P1b2/4pB2/2N4P/PPP1QPP1/2KR1BNR w kq - 1 9",
        "r2qkb1r/1pp1pppp/5n2/1p1P1b2/4pB2/7P/PPP1QPP1/2KR1BNR w kq - 0 10",
        "r2qkb1r/1ppbpppp/5n2/1Q1P4/4pB2/7P/PPP2PP1/2KR1BNR w kq - 1 11",
        "3qkb1r/1Qpbpppp/5n2/3P4/4pB2/7P/rPP2PP1/2KR1BNR w k - 0 12",
        "q3kb1r/1Qpbpppp/5n2/3P4/4pB2/7P/rPP2PP1/1K1R1BNR w k - 2 13",
        "r3kb1r/2pbpppp/5n2/3P4/4pB2/7P/1PP2PP1/1K1R1BNR w k - 0 14",
        "r3kb1r/2Bb1ppp/4pn2/3P4/4p3/7P/1PP2PP1/1K1R1BNR w k - 0 15",
        "r3kb1r/2Bb2pp/4pn2/8/4p3/7P/1PP2PP1/1K1R1BNR w k - 0 16",
        "r3k2r/2Bb2pp/4pn2/2b5/4p3/7P/1PP1NPP1/1K1R1B1R w k - 2 17",
        "r6r/2Bbk1pp/4pn2/2b5/3Np3/7P/1PP2PP1/1K1R1B1R w - - 4 18",
        "r6r/b2bk1pp/4pn2/4B3/3Np3/7P/1PP2PP1/1K1R1B1R w - - 6 19",
        "r1r5/b2bk1pp/4pn2/4B3/2BNp3/7P/1PP2PP1/1K1R3R w - - 8 20",
        "r7/b2bk1pp/4pn2/2r1B3/2BNp3/1P5P/2P2PP1/1K1R3R w - - 1 21",
        "rb6/3bk1pp/4pn2/2r1B3/2BNpP2/1P5P/2P3P1/1K1R3R w - - 1 22",
        "1r6/3bk1pp/4pn2/2r5/2BNpP2/1P5P/2P3P1/1K1R3R w - - 0 23",
        "1r6/3bk1p1/4pn1p/2r5/2BNpP2/1P5P/2P3P1/2KR3R w - - 0 24",
        "8/3bk1p1/1r2pn1p/2r5/2BNpP1P/1P6/2P3P1/2KR3R w - - 1 25",
        "8/3bk3/1r2pnpp/2r5/2BNpP1P/1P6/2P3P1/2K1R2R w - - 0 26",
        "2b5/4k3/1r2pnpp/2r5/2BNpP1P/1P4P1/2P5/2K1R2R w - - 1 27",
        "8/1b2k3/1r2pnpp/2r5/2BNpP1P/1P4P1/2P5/2K1R1R1 w - - 3 28",
        "8/1b1nk3/1r2p1pp/2r5/2BNpPPP/1P6/2P5/2K1R1R1 w - - 1 29",
        "8/1b2k3/1r2p1pp/2r1nP2/2BNp1PP/1P6/2P5/2K1R1R1 w - - 1 30",
        "8/1b2k3/1r2p1p1/2r1nPp1/2BNp2P/1P6/2P5/2K1R1R1 w - - 0 31",
        "8/1b2k3/1r2p1n1/2r3p1/2BNp2P/1P6/2P5/2K1R1R1 w - - 0 32",
        "8/1b2k3/1r2p1n1/6r1/2BNp2P/1P6/2P5/2K1R3 w - - 0 33",
        "8/1b2k3/1r2p3/4n1P1/2BNp3/1P6/2P5/2K1R3 w - - 1 34",
        "8/1b2k3/1r2p3/4n1P1/2BN4/1P2p3/2P5/2K4R w - - 0 35",
        "8/1b2k3/1r2p2R/6P1/2nN4/1P2p3/2P5/2K5 w - - 0 36",
        "8/1b2k3/3rp2R/6P1/2PN4/4p3/2P5/2K5 w - - 1 37",
        "8/4k3/3rp2R/6P1/2PN4/2P1p3/6b1/2K5 w - - 1 38",
        "8/4k3/r3p2R/2P3P1/3N4/2P1p3/6b1/2K5 w - - 1 39",
        "8/3k4/r3p2R/2P2NP1/8/2P1p3/6b1/2K5 w - - 3 40",
        "8/3k4/4p2R/2P3P1/8/2P1N3/6b1/r1K5 w - - 1 41",
        "8/3k4/4p2R/2P3P1/8/2P1N3/3K2b1/6r1 w - - 3 42",
        "8/3k4/4p2R/2P3P1/8/2PKNb2/8/6r1 w - - 5 43",
        "8/4k3/4p1R1/2P3P1/8/2PKNb2/8/6r1 w - - 7 44",
        "8/4k3/4p1R1/2P3P1/3K4/2P1N3/8/6rb w - - 9 45",
        "8/3k4/4p1R1/2P1K1P1/8/2P1N3/8/6rb w - - 11 46",
        "8/3k4/4p1R1/2P3P1/5K2/2P1N3/8/4r2b w - - 13 47",
        "8/3k4/2b1p2R/2P3P1/5K2/2P1N3/8/4r3 w - - 15 48",
        "8/3k4/2b1p3/2P3P1/5K2/2P1N2R/8/6r1 w - - 17 49",
        "2k5/7R/2b1p3/2P3P1/5K2/2P1N3/8/6r1 w - - 19 50",
        "2k5/7R/4p3/2P3P1/b1P2K2/4N3/8/6r1 w - - 1 51",
        "2k5/3bR3/4p3/2P3P1/2P2K2/4N3/8/6r1 w - - 3 52",
        "3k4/3b2R1/4p3/2P3P1/2P2K2/4N3/8/6r1 w - - 5 53",
        "3kb3/6R1/4p1P1/2P5/2P2K2/4N3/8/6r1 w - - 1 54",
        "3kb3/6R1/4p1P1/2P5/2P2KN1/8/8/2r5 w - - 3 55",
        "3kb3/6R1/4p1P1/2P1N3/2P2K2/8/8/5r2 w - - 5 56",
        "3kb3/6R1/4p1P1/2P1N3/2P5/4K3/8/4r3 w - - 7 57",
    },
    {
        "rnbq1rk1/ppp1npb1/4p1p1/3P3p/3PP3/2N2N2/PP2BPPP/R1BQ1RK1 b - - 0 8",
        "rnbq1rk1/ppp1npb1/6p1/3pP2p/3P4/2N2N2/PP2BPPP/R1BQ1RK1 b - - 0 9",
        "rn1q1rk1/ppp1npb1/6p1/3pP2p/3P2b1/2N2N2/PP2BPPP/R1BQR1K1 b - - 2 10",
        "r2q1rk1/ppp1npb1/2n3p1/3pP2p/3P2bN/2N5/PP2BPPP/R1BQR1K1 b - - 4 11",
        "r4rk1/pppqnpb1/2n3p1/3pP2p/3P2bN/2N4P/PP2BPP1/R1BQR1K1 b - - 0 12",
        "r4rk1/pppqnpb1/2n3p1/3pP2p/3P3N/7P/PP2NPP1/R1BQR1K1 b - - 0 13",
        "r4rk1/pppq1pb1/2n3p1/3pPN1p/3P4/7P/PP2NPP1/R1BQR1K1 b - - 0 14",
        "r4rk1/ppp2pb1/2n3p1/3pPq1p/3P1N2/7P/PP3PP1/R1BQR1K1 b - - 1 15",
        "r4rk1/pppq1pb1/2n3p1/3pP2p/P2P1N2/7P/1P3PP1/R1BQR1K1 b - - 0 16",
        "r2n1rk1/pppq1pb1/6p1/3pP2p/P2P1N2/R6P/1P3PP1/2BQR1K1 b - - 2 17",
        "r4rk1/pppq1pb1/4N1p1/3pP2p/P2P4/R6P/1P3PP1/2BQR1K1 b - - 0 18",
        "r4rk1/ppp2pb1/4q1p1/3pP1Bp/P2P4/R6P/1P3PP1/3QR1K1 b - - 1 19",
        "r3r1k1/ppp2pb1/4q1p1/3pP1Bp/P2P1P2/R6P/1P4P1/3QR1K1 b - - 0 20",
        "r3r1k1/ppp3b1/4qpp1/3pP2p/P2P1P1B/R6P/1P4P1/3QR1K1 b - - 1 21",
        "r3r1k1/ppp3b1/4q1p1/3pP2p/P4P1B/R6P/1P4P1/3QR1K1 b - - 0 22",
        "r4rk1/ppp3b1/4q1p1/3pP1Bp/P4P2/R6P/1P4P1/3QR1K1 b - - 2 23",
        "r4rk1/pp4b1/4q1p1/2ppP1Bp/P4P2/3R3P/1P4P1/3QR1K1 b - - 1 24",
        "r4rk1/pp4b1/4q1p1/2p1P1Bp/P2p1PP1/3R3P/1P6/3QR1K1 b - - 0 25",
        "r4rk1/pp4b1/4q1p1/2p1P1B1/P2p1PP1/3R4/1P6/3QR1K1 b - - 0 26",
        "r5k1/pp3rb1/4q1p1/2p1P1B1/P2p1PP1/6R1/1P6/3QR1K1 b - - 2 27",
        "5rk1/pp3rb1/4q1p1/2p1P1B1/P2pRPP1/6R1/1P6/3Q2K1 b - - 4 28",
        "5rk1/1p3rb1/p3q1p1/P1p1P1B1/3pRPP1/6R1/1P6/3Q2K1 b - - 0 29",
        "4r1k1/1p3rb1/p3q1p1/P1p1P1B1/3pRPP1/1P4R1/8/3Q2K1 b - - 0 30",
        "4r1k1/5rb1/pP2q1p1/2p1P1B1/3pRPP1/1P4R1/8/3Q2K1 b - - 0 31",
        "4r1k1/5rb1/pq4p1/2p1P1B1/3pRPP1/1P4R1/4Q3/6K1 b - - 1 32",
        "4r1k1/1r4b1/pq4p1/2p1P1B1/3pRPP1/1P4R1/2Q5/6K1 b - - 3 33",
        "4r1k1/1r4b1/1q4p1/p1p1P1B1/3p1PP1/1P4R1/2Q5/4R1K1 b - - 1 34",
        "4r1k1/3r2b1/1q4p1/p1p1P1B1/2Qp1PP1/1P4R1/8/4R1K1 b - - 3 35",
        "4r1k1/3r2b1/4q1p1/p1p1P1B1/2Qp1PP1/1P4R1/5K2/4R3 b - - 5 36",
        "4r1k1/3r2b1/6p1/p1p1P1B1/2Pp1PP1/6R1/5K2/4R3 b - - 0 37",
        "4r1k1/3r2b1/6p1/p1p1P1B1/2P2PP1/3p2R1/5K2/3R4 b - - 1 38",
        "5rk1/3r2b1/6p1/p1p1P1B1/2P2PP1/3p2R1/8/3RK3 b - - 3 39",
        "5rk1/6b1/6p1/p1p1P1B1/2Pr1PP1/3R4/8/3RK3 b - - 0 40",
        "5rk1/3R2b1/6p1/p1p1P1B1/2r2PP1/8/8/3RK3 b - - 1 41",
        "5rk1/3R2b1/6p1/p1p1P1B1/4rPP1/8/3K4/3R4 b - - 3 42",
        "1r4k1/3R2b1/6p1/p1p1P1B1/4rPP1/2K5/8/3R4 b - - 5 43",
        "1r4k1/3R2b1/6p1/p1p1P1B1/2K2PP1/4r3/8/3R4 b - - 7 44",
        "1r3bk1/8/3R2p1/p1p1P1B1/2K2PP1/4r3/8/3R4 b - - 9 45",
        "1r3bk1/8/6R1/2p1P1B1/p1K2PP1/4r3/8/3R4 b - - 0 46",
        "1r3b2/5k2/R7/2p1P1B1/p1K2PP1/4r3/8/3R4 b - - 2 47",
        "5b2/1r3k2/R7/2p1P1B1/p1K2PP1/4r3/8/7R b - - 4 48",
        "5b2/5k2/R7/2pKP1B1/pr3PP1/4r3/8/7R b - - 6 49",
        "5b2/5k2/R1K5/2p1P1B1/p2r1PP1/4r3/8/7R b - - 8 50",
        "8/R4kb1/2K5/2p1P1B1/p2r1PP1/4r3/8/7R b - - 10 51",
        "8/R5b1/2K3k1/2p1PPB1/p2r2P1/4r3/8/7R b - - 0 52",
        "8/6R1/2K5/2p1PPk1/p2r2P1/4r3/8/7R b - - 0 53",
        "8/6R1/2K5/2p1PP2/p2r1kP1/4r3/8/5R2 b - - 2 54",
        "8/6R1/2K2P2/2p1P3/p2r2P1/4r1k1/8/5R2 b - - 0 55",
        "8/5PR1/2K5/2p1P3/p2r2P1/4r3/6k1/5R2 b - - 0 56",
    },
    {
        "rn1qkb1r/p1pbpppp/5n2/8/2pP4/2N5/1PQ1PPPP/R1B1KBNR w KQkq - 0 7",
        "r2qkb1r/p1pbpppp/2n2n2/8/2pP4/2N2N2/1PQ1PPPP/R1B1KB1R w KQkq - 2 8",
        "r2qkb1r/p1pbpppp/5n2/8/1npPP3/2N2N2/1PQ2PPP/R1B1KB1R w KQkq - 1 9",
        "r2qkb1r/p1pb1ppp/4pn2/8/1npPP3/2N2N2/1P3PPP/R1BQKB1R w KQkq - 0 10",
        "r2qk2r/p1pbbppp/4pn2/8/1nBPP3/2N2N2/1P3PPP/R1BQK2R w KQkq - 1 11",
        "r2q1rk1/p1pbbppp/4pn2/8/1nBPP3/2N2N2/1P3PPP/R1BQ1RK1 w - - 3 12",
        "r2q1rk1/2pbbppp/p3pn2/8/1nBPPB2/2N2N2/1P3PPP/R2Q1RK1 w - - 0 13",
        "r2q1rk1/2p1bppp/p3pn2/1b6/1nBPPB2/2N2N2/1P3PPP/R2QR1K1 w - - 2 14",
        "r2q1rk1/4bppp/p1p1pn2/1b6/1nBPPB2/1PN2N2/5PPP/R2QR1K1 w - - 0 15",
        "r4rk1/3qbppp/p1p1pn2/1b6/1nBPPB2/1PN2N2/3Q1PPP/R3R1K1 w - - 2 16",
        "r4rk1/1q2bppp/p1p1pn2/1b6/1nBPPB2/1PN2N1P/3Q1PP1/R3R1K1 w - - 1 17",
        "r3r1k1/1q2bppp/p1p1pn2/1b6/1nBPPB2/1PN2N1P/4QPP1/R3R1K1 w - - 3 18",
        "r3r1k1/1q1nbppp/p1p1p3/1b6/1nBPPB2/1PN2N1P/4QPP1/3RR1K1 w - - 5 19",
        "r3rbk1/1q1n1ppp/p1p1p3/1b6/1nBPPB2/1PN2N1P/3RQPP1/4R1K1 w - - 7 20",
        "r3rbk1/1q3ppp/pnp1p3/1b6/1nBPPB2/1PN2N1P/3RQPP1/4R2K w - - 9 21",
        "2r1rbk1/1q3ppp/pnp1p3/1b6/1nBPPB2/1PN2N1P/3RQPP1/1R5K w - - 11 22",
        "2r1rbk1/1q4pp/pnp1pp2/1b6/1nBPPB2/1PN2N1P/4QPP1/1R1R3K w - - 0 23",
        "2r1rbk1/5qpp/pnp1pp2/1b6/1nBPP3/1PN1BN1P/4QPP1/1R1R3K w - - 2 24",
        "2r1rbk1/5qp1/pnp1pp1p/1b6/1nBPP3/1PN1BN1P/4QPP1/1R1R2K1 w - - 0 25",
        "2r1rbk1/5qp1/pnp1pp1p/1b6/2BPP3/1P2BN1P/n3QPP1/1R1R2K1 w - - 0 26",
        "r3rbk1/5qp1/pnp1pp1p/1b6/2BPP3/1P2BN1P/Q4PP1/1R1R2K1 w - - 1 27",
        "rr3bk1/5qp1/pnp1pp1p/1b6/2BPP3/1P2BN1P/Q4PP1/R2R2K1 w - - 3 28",
        "rr2qbk1/6p1/pnp1pp1p/1b6/2BPP3/1P2BN1P/4QPP1/R2R2K1 w - - 5 29",
        "rr2qbk1/6p1/1np1pp1p/pb6/2BPP3/1P1QBN1P/5PP1/R2R2K1 w - - 0 30",
        "rr2qbk1/6p1/1n2pp1p/pp6/3PP3/1P1QBN1P/5PP1/R2R2K1 w - - 0 31",
        "rr2qbk1/6p1/1n2pp1p/1p1P4/p3P3/1P1QBN1P/5PP1/R2R2K1 w - - 0 32",
        "rr2qbk1/3n2p1/3Ppp1p/1p6/p3P3/1P1QBN1P/5PP1/R2R2K1 w - - 1 33",
        "rr3bk1/3n2p1/3Ppp1p/1p5q/pP2P3/3QBN1P/5PP1/R2R2K1 w - - 1 34",
        "rr3bk1/3n2p1/3Ppp1p/1p5q/1P2P3/p2QBN1P/5PP1/2RR2K1 w - - 0 35",
        "1r3bk1/3n2p1/r2Ppp1p/1p5q/1P2P3/pQ2BN1P/5PP1/2RR2K1 w - - 2 36",
        "1r2qbk1/2Rn2p1/r2Ppp1p/1p6/1P2P3/pQ2BN1P/5PP1/3R2K1 w - - 4 37",
        "1r2qbk1/2Rn2p1/r2Ppp1p/1pB5/1P2P3/1Q3N1P/p4PP1/3R2K1 w - - 0 38",
        "1r2q1k1/2Rn2p1/r2bpp1p/1pB5/1P2P3/1Q3N1P/p4PP1/R5K1 w - - 0 39",
        "1r2q1k1/2Rn2p1/3rpp1p/1p6/1P2P3/1Q3N1P/p4PP1/R5K1 w - - 0 40",
        "2r1q1k1/2Rn2p1/3rpp1p/1p6/1P2P3/5N1P/Q4PP1/R5K1 w - - 1 41",
        "1r2q1k1/1R1n2p1/3rpp1p/1p6/1P2P3/5N1P/Q4PP1/R5K1 w - - 3 42",
        "2r1q1k1/2Rn2p1/3rpp1p/1p6/1P2P3/5N1P/Q4PP1/R5K1 w - - 5 43",
        "1r2q1k1/1R1n2p1/3rpp1p/1p6/1P2P3/5N1P/Q4PP1/R5K1 w - - 7 44",
        "1rq3k1/R2n2p1/3rpp1p/1p6/1P2P3/5N1P/Q4PP1/R5K1 w - - 9 45",
        "2q3k1/Rr1n2p1/3rpp1p/1p6/1P2P3/5N1P/4QPP1/R5K1 w - - 11 46",
        "Rrq3k1/3n2p1/3rpp1p/1p6/1P2P3/5N1P/4QPP1/R5K1 w - - 13 47",
    },
    {
        "rn1qkb1r/1pp2ppp/p4p2/3p1b2/5P2/1P2PN2/P1PP2PP/RN1QKB1R b KQkq - 1 6",
        "r2qkb1r/1pp2ppp/p1n2p2/3p1b2/3P1P2/1P2PN2/P1P3PP/RN1QKB1R b KQkq - 0 7",
        "r2qkb1r/1pp2ppp/p4p2/3p1b2/1n1P1P2/1P1BPN2/P1P3PP/RN1QK2R b KQkq - 2 8",
        "r2qkb1r/1pp2ppp/p4p2/3p1b2/3P1P2/1P1PPN2/P5PP/RN1QK2R b KQkq - 0 9",
        "r2qk2r/1pp2ppp/p2b1p2/3p1b2/3P1P2/1PNPPN2/P5PP/R2QK2R b KQkq - 2 10",
        "r2qk2r/1p3ppp/p1pb1p2/3p1b2/3P1P2/1PNPPN2/P5PP/R2Q1RK1 b kq - 1 11",
        "r2q1rk1/1p3ppp/p1pb1p2/3p1b2/3P1P2/1PNPPN2/P2Q2PP/R4RK1 b - - 3 12",
        "r2qr1k1/1p3ppp/p1pb1p2/3p1b2/3P1P2/1P1PPN2/P2QN1PP/R4RK1 b - - 5 13",
        "r3r1k1/1p3ppp/pqpb1p2/3p1b2/3P1P2/1P1PPNN1/P2Q2PP/R4RK1 b - - 7 14",
        "r3r1k1/1p3ppp/pqp2p2/3p1b2/1b1P1P2/1P1PPNN1/P1Q3PP/R4RK1 b - - 9 15",
        "r3r1k1/1p1b1ppp/pqp2p2/3p4/1b1P1P2/1P1PPNN1/P4QPP/R4RK1 b - - 11 16",
        "2r1r1k1/1p1b1ppp/pqp2p2/3p4/1b1PPP2/1P1P1NN1/P4QPP/R4RK1 b - - 0 17",
        "2r1r1k1/1p1b1ppp/pq3p2/2pp4/1b1PPP2/PP1P1NN1/5QPP/R4RK1 b - - 0 18",
        "2r1r1k1/1p1b1ppp/pq3p2/2Pp4/4PP2/PPbP1NN1/5QPP/R4RK1 b - - 0 19",
        "2r1r1k1/1p1b1ppp/p4p2/2Pp4/4PP2/PqbP1NN1/5QPP/RR4K1 b - - 1 20",
        "2r1r1k1/1p1b1ppp/p4p2/2Pp4/q3PP2/P1bP1NN1/R4QPP/1R4K1 b - - 3 21",
        "2r1r1k1/1p3ppp/p4p2/1bPP4/q4P2/P1bP1NN1/R4QPP/1R4K1 b - - 0 22",
        "2r1r1k1/1p3ppp/p4p2/2PP4/q4P2/P1bb1NN1/R4QPP/2R3K1 b - - 1 23",
        "2r1r1k1/1p3ppp/p2P1p2/2P5/2q2P2/P1bb1NN1/R4QPP/2R3K1 b - - 0 24",
        "2rr2k1/1p3ppp/p2P1p2/2P5/2q2P2/P1bb1NN1/R4QPP/2R4K b - - 2 25",
        "2rr2k1/1p3ppp/p2P1p2/2Q5/5P2/P1bb1NN1/R5PP/2R4K b - - 0 26",
        "3r2k1/1p3ppp/p2P1p2/2r5/5P2/P1bb1N2/R3N1PP/2R4K b - - 1 27",
        "3r2k1/1p3ppp/p2P1p2/2r5/5P2/P1b2N2/4R1PP/2R4K b - - 0 28",
        "3r2k1/1p3ppp/p2P1p2/2r5/1b3P2/P4N2/4R1PP/3R3K b - - 2 29",
        "3r2k1/1p2Rppp/p2P1p2/b1r5/5P2/P4N2/6PP/3R3K b - - 4 30",
        "3r2k1/1R3ppp/p1rP1p2/b7/5P2/P4N2/6PP/3R3K b - - 0 31",
        "3r2k1/1R3ppp/p2R1p2/b7/5P2/P4N2/6PP/7K b - - 0 32",
        "6k1/1R3ppp/p2r1p2/b7/5P2/P4NP1/7P/7K b - - 0 33",
        "6k1/1R3p1p/p2r1pp1/b7/5P1P/P4NP1/8/7K b - - 0 34",
        "6k1/3R1p1p/pr3pp1/b7/5P1P/P4NP1/8/7K b - - 2 35",
        "6k1/5p2/pr3pp1/b2R3p/5P1P/P4NP1/8/7K b - - 1 36",
        "6k1/5p2/pr3pp1/7p/5P1P/P1bR1NP1/8/7K b - - 3 37",
        "6k1/5p2/p1r2pp1/7p/5P1P/P1bR1NP1/6K1/8 b - - 5 38",
        "6k1/5p2/p1r2pp1/b2R3p/5P1P/P4NP1/6K1/8 b - - 7 39",
        "6k1/5p2/p4pp1/b2R3p/5P1P/P4NPK/2r5/8 b - - 9 40",
        "6k1/2b2p2/p4pp1/7p/5P1P/P2R1NPK/2r5/8 b - - 11 41",
        "6k1/2b2p2/5pp1/p6p/3N1P1P/P2R2PK/2r5/8 b - - 1 42",
        "6k1/2b2p2/5pp1/p6p/3N1P1P/P1R3PK/r7/8 b - - 3 43",
        "6k1/5p2/1b3pp1/p6p/5P1P/P1R3PK/r1N5/8 b - - 5 44",
        "8/5pk1/1bR2pp1/p6p/5P1P/P5PK/r1N5/8 b - - 7 45",
        "3b4/5pk1/2R2pp1/p4P1p/7P/P5PK/r1N5/8 b - - 0 46",
        "8/4bpk1/2R2pp1/p4P1p/6PP/P6K/r1N5/8 b - - 0 47",
        "8/5pk1/2R2pP1/p6p/6PP/b6K/r1N5/8 b - - 0 48",
        "8/6k1/2R2pp1/p6P/7P/b6K/r1N5/8 b - - 0 49",
        "8/6k1/2R2p2/p6p/7P/b5K1/r1N5/8 b - - 1 50",
        "8/8/2R2pk1/p6p/7P/b4K2/r1N5/8 b - - 3 51",
        "8/8/2R2pk1/p6p/7P/4NK2/rb6/8 b - - 5 52",
        "2R5/8/5pk1/7p/p6P/4NK2/rb6/8 b - - 1 53",
        "6R1/8/5pk1/7p/p6P/4NK2/1b6/r7 b - - 3 54",
        "R7/5k2/5p2/7p/p6P/4NK2/1b6/r7 b - - 5 55",
        "R7/5k2/5p2/7p/7P/p3N3/1b2K3/r7 b - - 1 56",
        "8/R4k2/5p2/7p/7P/p3N3/1b2K3/7r b - - 3 57",
        "8/8/5pk1/7p/R6P/p3N3/1b2K3/7r b - - 5 58",
        "8/8/5pk1/7p/R6P/p7/4K3/2bN3r b - - 7 59",
        "8/8/5pk1/7p/R6P/p7/4KN1r/2b5 b - - 9 60",
        "8/8/5pk1/7p/R6P/p3K3/1b3N1r/8 b - - 11 61",
        "8/8/R4pk1/7p/7P/p1b1K3/5N1r/8 b - - 13 62",
        "8/8/5pk1/7p/7P/2b1K3/R4N1r/8 b - - 0 63",
        "8/8/5pk1/7p/3K3P/8/R4N1r/4b3 b - - 2 64",
    }
};
// clang-format on

}  // namespace

namespace Stockfish::Benchmark {

// Builds a list of UCI commands to be run by bench. There
// are five parameters: TT size in MB, number of search threads that
// should be used, the limit value spent for each position, a file name
// where to look for positions in FEN format, and the type of the limit:
// depth, perft, nodes and movetime (in milliseconds). Examples:
//
// bench                            : search default positions up to depth 13
// bench 64 1 15                    : search default positions up to depth 15 (TT = 64MB)
// bench 64 1 100000 default nodes  : search default positions for 100K nodes each
// bench 64 4 5000 current movetime : search current position with 4 threads for 5 sec
// bench 16 1 5 blah perft          : run a perft 5 on positions in file "blah"
std::vector<std::string> setup_bench(const std::string& currentFen, std::istream& is) {

    std::vector<std::string> fens, list;
    std::string              go, token;

    // Assign default values to missing arguments
    std::string ttSize    = (is >> token) ? token : "16";
    std::string threads   = (is >> token) ? token : "1";
    std::string limit     = (is >> token) ? token : "13";
    std::string fenFile   = (is >> token) ? token : "default";
    std::string limitType = (is >> token) ? token : "depth";

    go = limitType == "eval" ? "eval" : "go " + limitType + " " + limit;

    if (fenFile == "default")
        fens = Defaults;

    else if (fenFile == "current")
        fens.push_back(currentFen);

    else
    {
        std::string   fen;
        std::ifstream file(fenFile);

        if (!file.is_open())
        {
            std::cerr << "Unable to open file " << fenFile << std::endl;
            exit(EXIT_FAILURE);
        }

        while (getline(file, fen))
            if (!fen.empty())
                fens.push_back(fen);

        file.close();
    }

    list.emplace_back("setoption name Threads value " + threads);
    list.emplace_back("setoption name Hash value " + ttSize);
    list.emplace_back("ucinewgame");

    for (const std::string& fen : fens)
        if (fen.find("setoption") != std::string::npos)
            list.emplace_back(fen);
        else
        {
            list.emplace_back("position fen " + fen);
            list.emplace_back(go);
        }

    return list;
}

BenchmarkSetup setup_benchmark(std::istream& is) {
    // TT_SIZE_PER_THREAD is chosen such that roughly half of the hash is used all positions
    // for the current sequence have been searched.
    static constexpr int TT_SIZE_PER_THREAD = 128;

    static constexpr int DEFAULT_DURATION_S = 150;

    BenchmarkSetup setup{};

    // Assign default values to missing arguments
    int desiredTimeS;

    if (!(is >> setup.threads))
        setup.threads = get_hardware_concurrency();
    else
        setup.originalInvocation += std::to_string(setup.threads);

    if (!(is >> setup.ttSize))
        setup.ttSize = TT_SIZE_PER_THREAD * setup.threads;
    else
        setup.originalInvocation += " " + std::to_string(setup.ttSize);

    if (!(is >> desiredTimeS))
        desiredTimeS = DEFAULT_DURATION_S;
    else
        setup.originalInvocation += " " + std::to_string(desiredTimeS);

    setup.filledInvocation += std::to_string(setup.threads) + " " + std::to_string(setup.ttSize)
                            + " " + std::to_string(desiredTimeS);

    auto getCorrectedTime = [&](int ply) {
        // time per move is fit roughly based on LTC games
        // seconds = 50/{ply+15}
        // ms = 50000/{ply+15}
        // with this fit 10th move gets 2000ms
        // adjust for desired 10th move time
        return 50000.0 / (static_cast<double>(ply) + 15.0);
    };

    float totalTime = 0;
    for (const auto& game : BenchmarkPositions)
    {
        setup.commands.emplace_back("ucinewgame");
        int ply = 1;
        for (int i = 0; i < static_cast<int>(game.size()); ++i)
        {
            const float correctedTime = getCorrectedTime(ply);
            totalTime += correctedTime;
            ply += 1;
        }
    }

    float timeScaleFactor = static_cast<float>(desiredTimeS * 1000) / totalTime;

    for (const auto& game : BenchmarkPositions)
    {
        setup.commands.emplace_back("ucinewgame");
        int ply = 1;
        for (const std::string& fen : game)
        {
            setup.commands.emplace_back("position fen " + fen);

            const int correctedTime = static_cast<int>(getCorrectedTime(ply) * timeScaleFactor);
            setup.commands.emplace_back("go movetime " + std::to_string(correctedTime));

            ply += 1;
        }
    }

    return setup;
}

}  // namespace Stockfish