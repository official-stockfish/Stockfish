/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2022 The Stockfish developers (see AUTHORS file)

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

#include <algorithm>
#include <cassert>

#include "bitboard.h"
#include "pawns.h"
#include "position.h"
#include "thread.h"

namespace Stockfish {

namespace {

  #define V Value
  #define S(mg, eg) make_score(mg, eg)

  // Pawn penalties
  constexpr Score Backward      = S( 6, 19);
  constexpr Score Doubled       = S(11, 51);
  constexpr Score DoubledEarly  = S(17,  7);
  constexpr Score Isolated      = S( 1, 20);
  constexpr Score WeakLever     = S( 2, 57);
  constexpr Score WeakUnopposed = S(15, 18);

  // Bonus for blocked pawns at 5th or 6th rank
  constexpr Score BlockedPawn[2] = { S(-19, -8), S(-7, 3) };

  constexpr Score BlockedStorm[RANK_NB] = {
    S(0, 0), S(0, 0), S(64, 75), S(-3, 14), S(-12, 19), S(-7, 4), S(-10, 5)
  };

  // Connected pawn bonus
  constexpr int Connected[RANK_NB] = { 0, 3, 7, 7, 15, 54, 86 };

  // Strength of pawn shelter for our king by [distance from edge][rank].
  // RANK_1 = 0 is used for files where we have no pawn, or pawn is behind our king.
  constexpr Value ShelterStrength[int(FILE_NB) / 2][RANK_NB] = {
    { V(-2), V(85), V(95), V(53), V(39), V(23), V(25) },
    { V(-55), V(64), V(32), V(-55), V(-30), V(-11), V(-61) },
    { V(-11), V(75), V(19), V(-6), V(26), V(9), V(-47) },
    { V(-41), V(-11), V(-27), V(-58), V(-42), V(-66), V(-163) }
  };

  // Danger of enemy pawns moving toward our king by [distance from edge][rank].
  // RANK_1 = 0 is used for files where the enemy has no pawn, or their pawn
  // is behind our king. Note that UnblockedStorm[0][1-2] accommodate opponent pawn
  // on edge, likely blocked by our king.
  constexpr Value UnblockedStorm[int(FILE_NB) / 2][RANK_NB] = {
    { V(94), V(-280), V(-170), V(90), V(59), V(47), V(53) },
    { V(43), V(-17), V(128), V(39), V(26), V(-17), V(15) },
    { V(-9), V(62), V(170), V(34), V(-5), V(-20), V(-11) },
    { V(-27), V(-19), V(106), V(10), V(2), V(-13), V(-24) }
  };


  // KingOnFile[semi-open Us][semi-open Them] contains bonuses/penalties
  // for king when the king is on a semi-open or open file.
  constexpr Score KingOnFile[2][2] = {{ S(-18,11), S(-6,-3)  },
                                     {  S(  0, 0), S( 5,-4) }};

  #undef S
  #undef V


  /// evaluate() calculates a score for the static pawn structure of the given position.
  /// We cannot use the location of pieces or king in this function, as the evaluation
  /// of the pawn structure will be stored in a small cache for speed reasons, and will
  /// be re-used even when the pieces have moved.

  template<Color Us>
  Score evaluate(const Position& pos, Pawns::Entry* e) {

    constexpr Color     Them = ~Us;
    constexpr Direction Up   = pawn_push(Us);
    constexpr Direction Down = -Up;

    Bitboard neighbours, stoppers, support, phalanx, opposed;
    Bitboard lever, leverPush, blocked;
    Square s;
    bool backward, passed, doubled;
    Score score = SCORE_ZERO;
    Bitboard b = pos.pieces(Us, PAWN);

    Bitboard ourPawns   = pos.pieces(  Us, PAWN);
    Bitboard theirPawns = pos.pieces(Them, PAWN);

    Bitboard doubleAttackThem = pawn_double_attacks_bb<Them>(theirPawns);

    e->passedPawns[Us] = 0;
    e->kingSquares[Us] = SQ_NONE;
    e->pawnAttacks[Us] = e->pawnAttacksSpan[Us] = pawn_attacks_bb<Us>(ourPawns);
    e->blockedCount += popcount(shift<Up>(ourPawns) & (theirPawns | doubleAttackThem));

    // Loop through all pawns of the current color and score each pawn
    while (b)
    {
        s = pop_lsb(b);

        assert(pos.piece_on(s) == make_piece(Us, PAWN));

        Rank r = relative_rank(Us, s);

        // Flag the pawn
        opposed    = theirPawns & forward_file_bb(Us, s);
        blocked    = theirPawns & (s + Up);
        stoppers   = theirPawns & passed_pawn_span(Us, s);
        lever      = theirPawns & pawn_attacks_bb(Us, s);
        leverPush  = theirPawns & pawn_attacks_bb(Us, s + Up);
        doubled    = ourPawns   & (s - Up);
        neighbours = ourPawns   & adjacent_files_bb(s);
        phalanx    = neighbours & rank_bb(s);
        support    = neighbours & rank_bb(s - Up);

        if (doubled)
        {
            // Additional doubled penalty if none of their pawns is fixed
            if (!(ourPawns & shift<Down>(theirPawns | pawn_attacks_bb<Them>(theirPawns))))
                score -= DoubledEarly;
        }

        // A pawn is backward when it is behind all pawns of the same color on
        // the adjacent files and cannot safely advance.
        backward =  !(neighbours & forward_ranks_bb(Them, s + Up))
                  && (leverPush | blocked);

        // Compute additional span if pawn is not backward nor blocked
        if (!backward && !blocked)
            e->pawnAttacksSpan[Us] |= pawn_attack_span(Us, s);

        // A pawn is passed if one of the three following conditions is true:
        // (a) there is no stoppers except some levers
        // (b) the only stoppers are the leverPush, but we outnumber them
        // (c) there is only one front stopper which can be levered.
        //     (Refined in Evaluation::passed)
        passed =   !(stoppers ^ lever)
                || (   !(stoppers ^ leverPush)
                    && popcount(phalanx) >= popcount(leverPush))
                || (   stoppers == blocked && r >= RANK_5
                    && (shift<Up>(support) & ~(theirPawns | doubleAttackThem)));

        passed &= !(forward_file_bb(Us, s) & ourPawns);

        // Passed pawns will be properly scored later in evaluation when we have
        // full attack info.
        if (passed)
            e->passedPawns[Us] |= s;

        // Score this pawn
        if (support | phalanx)
        {
            int v =  Connected[r] * (2 + bool(phalanx) - bool(opposed))
                   + 22 * popcount(support);

            score += make_score(v, v * (r - 2) / 4);
        }

        else if (!neighbours)
        {
            if (     opposed
                &&  (ourPawns & forward_file_bb(Them, s))
                && !(theirPawns & adjacent_files_bb(s)))
                score -= Doubled;
            else
                score -=  Isolated
                        + WeakUnopposed * !opposed;
        }

        else if (backward)
            score -=  Backward
                    + WeakUnopposed * !opposed * bool(~(FileABB | FileHBB) & s);

        if (!support)
            score -=  Doubled * doubled
                    + WeakLever * more_than_one(lever);

        if (blocked && r >= RANK_5)
            score += BlockedPawn[r - RANK_5];
    }

    return score;
  }

} // namespace

namespace Pawns {


/// Pawns::probe() looks up the current position's pawns configuration in
/// the pawns hash table. It returns a pointer to the Entry if the position
/// is found. Otherwise a new Entry is computed and stored there, so we don't
/// have to recompute all when the same pawns configuration occurs again.

Entry* probe(const Position& pos) {

  Key key = pos.pawn_key();
  Entry* e = pos.this_thread()->pawnsTable[key];

  if (e->key == key)
      return e;

  e->key = key;
  e->blockedCount = 0;
  e->scores[WHITE] = evaluate<WHITE>(pos, e);
  e->scores[BLACK] = evaluate<BLACK>(pos, e);

  return e;
}


/// Entry::evaluate_shelter() calculates the shelter bonus and the storm
/// penalty for a king, looking at the king file and the two closest files.

template<Color Us>
Score Entry::evaluate_shelter(const Position& pos, Square ksq) const {

  constexpr Color Them = ~Us;

  Bitboard b = pos.pieces(PAWN) & ~forward_ranks_bb(Them, ksq);
  Bitboard ourPawns = b & pos.pieces(Us) & ~pawnAttacks[Them];
  Bitboard theirPawns = b & pos.pieces(Them);

  Score bonus = make_score(5, 5);

  File center = std::clamp(file_of(ksq), FILE_B, FILE_G);
  for (File f = File(center - 1); f <= File(center + 1); ++f)
  {
      b = ourPawns & file_bb(f);
      int ourRank = b ? relative_rank(Us, frontmost_sq(Them, b)) : 0;

      b = theirPawns & file_bb(f);
      int theirRank = b ? relative_rank(Us, frontmost_sq(Them, b)) : 0;

      int d = edge_distance(f);
      bonus += make_score(ShelterStrength[d][ourRank], 0);

      if (ourRank && (ourRank == theirRank - 1))
          bonus -= BlockedStorm[theirRank];
      else
          bonus -= make_score(UnblockedStorm[d][theirRank], 0);
  }

  // King On File
  bonus -= KingOnFile[pos.is_on_semiopen_file(Us, ksq)][pos.is_on_semiopen_file(Them, ksq)];

  return bonus;
}


/// Entry::do_king_safety() calculates a bonus for king safety. It is called only
/// when king square changes, which is about 20% of total king_safety() calls.

template<Color Us>
Score Entry::do_king_safety(const Position& pos) {

  Square ksq = pos.square<KING>(Us);
  kingSquares[Us] = ksq;
  castlingRights[Us] = pos.castling_rights(Us);
  auto compare = [](Score a, Score b) { return mg_value(a) < mg_value(b); };

  Score shelter = evaluate_shelter<Us>(pos, ksq);

  // If we can castle use the bonus after castling if it is bigger

  if (pos.can_castle(Us & KING_SIDE))
      shelter = std::max(shelter, evaluate_shelter<Us>(pos, relative_square(Us, SQ_G1)), compare);

  if (pos.can_castle(Us & QUEEN_SIDE))
      shelter = std::max(shelter, evaluate_shelter<Us>(pos, relative_square(Us, SQ_C1)), compare);

  // In endgame we like to bring our king near our closest pawn
  Bitboard pawns = pos.pieces(Us, PAWN);
  int minPawnDist = 6;

  if (pawns & attacks_bb<KING>(ksq))
      minPawnDist = 1;
  else while (pawns)
      minPawnDist = std::min(minPawnDist, distance(ksq, pop_lsb(pawns)));

  return shelter - make_score(0, 16 * minPawnDist);
}

// Explicit template instantiation
template Score Entry::do_king_safety<WHITE>(const Position& pos);
template Score Entry::do_king_safety<BLACK>(const Position& pos);

} // namespace Pawns

} // namespace Stockfish
