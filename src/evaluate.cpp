/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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

#include "evaluate.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

#include "attacks.h"
#include "misc.h"
#include "nnue/network.h"
#include "nnue/nnue_misc.h"
#include "position.h"
#include "types.h"
#include "uci.h"
#include "nnue/nnue_accumulator.h"

namespace Stockfish {

// ---------- उन्नत किंग हंटर बोनस (संशोधित) ----------
template<Color Us>
Value king_hunter_score(const Position& pos) {
    constexpr Color Them = ~Us;
    Square ksq = pos.square<KING>(Them);

    // राजा के आसपास के 8 स्क्वेयर + राजा का अपना स्क्वेयर
    Bitboard kingRing = Attacks::attacks_bb<KING>(ksq) | square_bb(ksq);

    int score = 0;

    // 1. हमारे मोहरों द्वारा किंग रिंग पर हमले
    Bitboard ourPieces = pos.pieces(Us);
    while (ourPieces) {
        Square s = pop_lsb(ourPieces);               // पॉइंटर नहीं, संदर्भ
        PieceType pt = type_of(pos.piece_on(s));
        Bitboard attacks = Attacks::attacks_bb(pt, s, pos.pieces());

        if (attacks & kingRing) {
            int w = 0;
            switch (pt) {
                case QUEEN:  w = 12; break;
                case ROOK:   w = 7;  break;
                case BISHOP: w = 5;  break;
                case KNIGHT: w = 5;  break;
                case PAWN:   w = 2;  break;
                default:     w = 0;  break;
            }
            // यदि सीधे राजा पर चेक हो रहा है तो अतिरिक्त बोनस
            if (attacks & square_bb(ksq))
                w += 10;
            score += w;
        }
    }

    // 2. राजा की अपनी प्यादा ढाल – कम प्यादे = अधिक खतरा
    int friendlyPawns = popcount(pos.pieces(Them, PAWN) & kingRing);
    score += (8 - friendlyPawns) * 4;

    // 3. राजा की गतिशीलता – कम वैध किंग मूव्स = अधिक बोनस
    // सही attackers_to का उपयोग (occupancy सहित)
    Bitboard kingMoves = Attacks::attacks_bb<KING>(ksq) & ~pos.pieces(Them) & ~pos.attackers_to(ksq, pos.pieces());
    int mobility = popcount(kingMoves);
    score += (8 - mobility) * 3;

    // 4. हमारे प्यादों की उन्नति (Pawn storm)
    Bitboard pawnStorm = pos.pieces(Us, PAWN) & Attacks::attacks_bb<KING>(ksq);
    score += popcount(pawnStorm) * 6;

    // अंतिम बोनस (स्केलिंग)
    return Value(score * 8);
}
// --------------------------------------------------------------

Value Eval::evaluate(const Eval::NNUE::Network&     network,
                     const Position&                pos,
                     Eval::NNUE::AccumulatorStack&  accumulators,
                     Eval::NNUE::AccumulatorCaches& caches,
                     int                            optimism) {

    assert(!pos.checkers());

    auto [psqt, positional] = network.evaluate(pos, accumulators, caches);

    Value nnue = psqt + positional;

    int nnueComplexity = std::abs(psqt - positional);
    optimism += optimism * i64(nnueComplexity) / 476;
    nnue -= nnue * i64(nnueComplexity) / 18236;

    int material = 534 * pos.count<PAWN>() + pos.non_pawn_material();
    int v        = (nnue * i64(91000 + material) + optimism * i64(7675)) / 91000;

    // ----- किंग हंटर बोनस (अब संकलन-त्रुटि-मुक्त) -----
    Value wKing = king_hunter_score<WHITE>(pos);
    Value bKing = king_hunter_score<BLACK>(pos);
    Value kingDiff = wKing - bKing;
    if (pos.side_to_move() == BLACK)
        kingDiff = -kingDiff;
    v += kingDiff;   // v पहले से ही side-to-move के अनुसार है
    // --------------------------------------------------

    // Damp down the evaluation linearly when shuffling
    v -= v * pos.rule50_count() / 199;

    // Guarantee evaluation does not hit the tablebase range
    v = std::clamp(v, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);

    return v;
}

std::string Eval::trace(Position& pos, const Eval::NNUE::Network& network) {

    if (pos.checkers())
        return "Final evaluation: none (in check)";

    auto accumulators = std::make_unique<Eval::NNUE::AccumulatorStack>();
    auto caches       = std::make_unique<Eval::NNUE::AccumulatorCaches>(network);

    std::stringstream ss;
    ss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2);
    ss << '\n' << NNUE::trace(pos, network, *caches) << '\n';

    ss << std::showpoint << std::showpos << std::fixed << std::setprecision(2) << std::setw(15);

    auto [psqt, positional] = network.evaluate(pos, *accumulators, *caches);
    Value v                 = psqt + positional;
    ss << "NNUE evaluation          " << v << " (side to move, internal units)\n";
    v = pos.side_to_move() == WHITE ? v : -v;
    ss << "NNUE evaluation        " << 0.01 * UCIEngine::to_cp(v, pos) << " (white side)\n";

    v = evaluate(network, pos, *accumulators, *caches, VALUE_ZERO);
    v = pos.side_to_move() == WHITE ? v : -v;

    ss << "Final evaluation      ";
    ss << 0.01 * UCIEngine::to_cp(v, pos) << " (white side)";
    ss << " [with scaled NNUE, ...]\n";

    return ss.str();
}

}  // namespace Stockfish
