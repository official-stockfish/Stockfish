/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "nnue_misc.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>
#include <tuple>

#include "../evaluate.h"
#include "../position.h"
#include "../types.h"
#include "../uci.h"
#include "network.h"
#include "nnue_accumulator.h"

namespace Stockfish::Eval::NNUE {

constexpr std::string_view PieceToChar(" PNBRQK  pnbrqk");

// Helper function to hint common parent position, optimizing cache access.
void hint_common_parent_position(const Position& pos, const Networks& networks, AccumulatorCaches& caches) {
    auto& active_cache = Eval::use_smallnet(pos) ? caches.small : caches.big;
    auto& active_network = Eval::use_smallnet(pos) ? networks.small : networks.big;
    active_network.hint_common_access(pos, &active_cache);
}

namespace {

// Converts a value into centipawns (cp) and formats it for compact display.
void format_cp_compact(Value v, char* buffer, const Position& pos) {
    buffer[0] = (v < 0) ? '-' : (v > 0 ? '+' : ' ');
    int cp = std::abs(UCIEngine::to_cp(v, pos));

    if (cp >= 10000) {
        std::snprintf(buffer + 1, 5, "%2d.%d", cp / 10000, (cp % 10000) / 1000);
    } else if (cp >= 1000) {
        std::snprintf(buffer + 1, 5, "%d.%1d", cp / 1000, (cp % 1000) / 100);
    } else {
        std::snprintf(buffer + 1, 5, "0.%2d", cp / 100);
    }
}

// Converts value into pawns with two decimals for detailed formatting.
void format_cp_aligned_dot(Value v, std::stringstream& stream, const Position& pos) {
    const double pawns = std::abs(0.01 * UCIEngine::to_cp(v, pos));
    stream << (v < 0 ? '-' : v > 0 ? '+' : ' ')
           << std::fixed << std::setw(6) << std::setprecision(2) << pawns;
}

}  // namespace

// Traces the NNUE evaluation for a given position and returns a string
// with the evaluation breakdown by bucket (PSQT, Layers).
std::string trace(Position& pos, const Networks& networks, AccumulatorCaches& caches) {
    std::stringstream ss;
    char board[3 * 8 + 1][8 * 8 + 2] = {};
    for (int row = 0; row < 3 * 8 + 1; ++row)
        std::memset(board[row], ' ', sizeof(board[row]));

    // Helper to write a square's content on the board.
    auto write_square = [&board, &pos](File file, Rank rank, Piece pc, Value value) {
        const int x = static_cast<int>(file) * 8;
        const int y = (7 - static_cast<int>(rank)) * 3;

        for (int i = 1; i < 8; ++i) {
            board[y][x + i] = board[y + 3][x + i] = '-';
        }

        for (int i = 1; i < 3; ++i) {
            board[y + i][x] = board[y + i][x + 8] = '|';
        }

        board[y][x] = board[y][x + 8] = board[y + 3][x + 8] = board[y + 3][x] = '+';

        if (pc != NO_PIECE)
            board[y + 1][x + 4] = PieceToChar[pc];

        if (value != VALUE_NONE)
            format_cp_compact(value, &board[y + 2][x + 2], pos);
    };

    // Get base evaluation.
    auto [psqt, positional] = networks.big.evaluate(pos, &caches.big);
    Value base_eval = pos.side_to_move() == WHITE ? psqt + positional : -(psqt + positional);

    // Process the board piece by piece.
    for (File f = FILE_A; f <= FILE_H; ++f) {
        for (Rank r = RANK_1; r <= RANK_8; ++r) {
            Square sq = make_square(f, r);
            Piece pc = pos.piece_on(sq);
            Value diff_eval = VALUE_NONE;

            if (pc != NO_PIECE && type_of(pc) != KING) {
                auto st = pos.state();
                pos.remove_piece(sq);
                st->accumulatorBig.computed[WHITE] = st->accumulatorBig.computed[BLACK] = false;

                auto [new_psqt, new_positional] = networks.big.evaluate(pos, &caches.big);
                Value new_eval = pos.side_to_move() == WHITE ? new_psqt + new_positional : -(new_psqt + new_positional);
                diff_eval = base_eval - new_eval;

                pos.put_piece(pc, sq);
                st->accumulatorBig.computed[WHITE] = st->accumulatorBig.computed[BLACK] = false;
            }

            write_square(f, r, pc, diff_eval);
        }
    }

    // Output the board.
    ss << " NNUE derived piece values:\n";
    for (int row = 0; row < 3 * 8 + 1; ++row)
        ss << board[row] << '\n';
    ss << '\n';

    // Output network contributions by bucket.
    auto trace_eval = networks.big.trace_evaluate(pos, &caches.big);
    ss << " NNUE network contributions " 
       << (pos.side_to_move() == WHITE ? "(White to move)" : "(Black to move)") << "\n"
       << "+------------+------------+------------+------------+\n"
       << "|   Bucket   |  Material  | Positional |   Total    |\n"
       << "|            |   (PSQT)   |  (Layers)  |            |\n"
       << "+------------+------------+------------+------------+\n";

    for (std::size_t bucket = 0; bucket < LayerStacks; ++bucket) {
        ss << "|  " << std::setw(10) << bucket << "  |  ";
        format_cp_aligned_dot(trace_eval.psqt[bucket], ss, pos);
        ss << "  |  ";
        format_cp_aligned_dot(trace_eval.positional[bucket], ss, pos);
        ss << "  |  ";
        format_cp_aligned_dot(trace_eval.psqt[bucket] + trace_eval.positional[bucket], ss, pos);
        ss << "  |";
        if (bucket == trace_eval.correctBucket)
            ss << " <-- this bucket is used";
        ss << '\n';
    }

    ss << "+------------+------------+------------+------------+\n";

    return ss.str();
}

}  // namespace Stockfish::Eval::NNUE
