/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

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
#include "bitcount.h"
#include "pawns.h"
#include "position.h"

namespace {

  #define V Value
  #define S(mg, eg) make_score(mg, eg)

  // Doubled pawn penalty by file
  const Score Doubled[FILE_NB] = {
    S(13, 43), S(20, 48), S(23, 48), S(23, 48),
    S(23, 48), S(23, 48), S(20, 48), S(13, 43) };

  // Isolated pawn penalty by opposed flag and file
  const Score Isolated[2][FILE_NB] = {
  { S(37, 45), S(54, 52), S(60, 52), S(60, 52),
    S(60, 52), S(60, 52), S(54, 52), S(37, 45) },
  { S(25, 30), S(36, 35), S(40, 35), S(40, 35),
    S(40, 35), S(40, 35), S(36, 35), S(25, 30) } };

  // Backward pawn penalty by opposed flag and file
  const Score Backward[2][FILE_NB] = {
  { S(30, 42), S(43, 46), S(49, 46), S(49, 46),
    S(49, 46), S(49, 46), S(43, 46), S(30, 42) },
  { S(20, 28), S(29, 31), S(33, 31), S(33, 31),
    S(33, 31), S(33, 31), S(29, 31), S(20, 28) } };

  // Connected pawn bonus by opposed, phalanx flags and rank
  Score Connected[2][2][RANK_NB];

  // Levers bonus by rank
  const Score Lever[RANK_NB] = {
    S( 0, 0), S( 0, 0), S(0, 0), S(0, 0),
    S(20,20), S(40,40), S(0, 0), S(0, 0) };

  // Unsupported pawn penalty
  const Score UnsupportedPawnPenalty = S(20, 10);

  // Weakness of our pawn shelter in front of the king indexed by [rank]
  const Value ShelterWeakness[RANK_NB] =
  { V(100), V(0), V(27), V(73), V(92), V(101), V(101) };

  // Danger of enemy pawns moving toward our king indexed by
  // [no friendly pawn | pawn unblocked | pawn blocked][rank of enemy pawn]
  const Value StormDanger[][RANK_NB] = {
  { V( 0),  V(64), V(128), V(51), V(26) },
  { V(26),  V(32), V( 96), V(38), V(20) },
  { V( 0),  V( 0), V(160), V(25), V(13) } };

  // Max bonus for king safety. Corresponds to start position with all the pawns
  // in front of the king and no enemy pawn on the horizon.
  const Value MaxSafetyBonus = V(263);

  #undef S
  #undef V

  template<Color Us>
  Score evaluate(const Position& pos, Pawns::Entry* entry) {

    const Color  Them  = (Us == WHITE ? BLACK    : WHITE);
    const Square Up    = (Us == WHITE ? DELTA_N  : DELTA_S);
    const Square Right = (Us == WHITE ? DELTA_NE : DELTA_SW);
    const Square Left  = (Us == WHITE ? DELTA_NW : DELTA_SE);

    Bitboard bitboard, previous, doubled, connected;
    Square square;
    bool passed, isolated, opposed, phalanx, backward, unsupported, lever;
    Score value = SCORE_ZERO;
    const Square* pieceList = pos.list<PAWN>(Us);
    const Bitboard* pawnAttacksBB = StepAttacksBB[make_piece(Us, PAWN)];

    Bitboard ourPawns   = pos.pieces(Us  , PAWN);
    Bitboard theirPawns = pos.pieces(Them, PAWN);

    entry->passedPawns[Us] = 0;
    entry->kingSquares[Us] = SQ_NONE;
    entry->semiopenFiles[Us] = 0xFF;
    entry->pawnAttacks[Us] = shift_bb<Right>(ourPawns) | shift_bb<Left>(ourPawns);
    entry->pawnsOnSquares[Us][BLACK] = popcount<Max15>(ourPawns & DarkSquares);
    entry->pawnsOnSquares[Us][WHITE] = pos.count<PAWN>(Us) - entry->pawnsOnSquares[Us][BLACK];

    // Loop through all pawns of the current color and score each pawn
    while ((square = *pieceList++) != SQ_NONE)
    {
        assert(pos.piece_on(square) == make_piece(Us, PAWN));

        File file = file_of(square);

        // This file cannot be semi-open
        entry->semiopenFiles[Us] &= ~(1 << file);

        // Previous rank
        previous = rank_bb(square - pawn_push(Us));

        // Flag the pawn as passed, isolated, doubled,
        // unsupported or connected (but not the backward one).
        connected   =   ourPawns   & adjacent_files_bb(file) & (rank_bb(square) | previous);
        phalanx     =   connected  & rank_bb(square);
        unsupported = !(ourPawns   & adjacent_files_bb(file) & previous);
        isolated    = !(ourPawns   & adjacent_files_bb(file));
        doubled     =   ourPawns   & forward_bb(Us, square);
        opposed     =   theirPawns & forward_bb(Us, square);
        passed      = !(theirPawns & passed_pawn_mask(Us, square));
        lever       =   theirPawns & pawnAttacksBB[square];

        // Test for backward pawn.
        // If the pawn is passed, isolated, or connected it cannot be
        // backward. If there are friendly pawns behind on adjacent files
        // or if it can capture an enemy pawn it cannot be backward either.
        if (   (passed | isolated | connected)
            || (ourPawns & pawn_attack_span(Them, square))
            || (pos.attacks_from<PAWN>(square, Us) & theirPawns))
            backward = false;
        else
        {
            // We now know that there are no friendly pawns beside or behind this
            // pawn on adjacent files. We now check whether the pawn is
            // backward by looking in the forward direction on the adjacent
            // files, and picking the closest pawn there.
            bitboard = pawn_attack_span(Us, square) & (ourPawns | theirPawns);
            bitboard = pawn_attack_span(Us, square) & rank_bb(backmost_sq(Us, bitboard));

            // If we have an enemy pawn in the same or next rank, the pawn is
            // backward because it cannot advance without being captured.
            backward = (bitboard | shift_bb<Up>(bitboard)) & theirPawns;
        }

        assert(opposed | passed | (pawn_attack_span(Us, square) & theirPawns));

        // Passed pawns will be properly scored in evaluation because we need
        // full attack info to evaluate passed pawns. Only the frontmost passed
        // pawn on each file is considered a true passed pawn.
        if (passed && !doubled)
            entry->passedPawns[Us] |= square;

        // Score this pawn
        if (isolated)
            value -= Isolated[opposed][file];

        if (unsupported && !isolated)
            value -= UnsupportedPawnPenalty;

        if (doubled)
            value -= Doubled[file] / distance<Rank>(square, Square(lsb(doubled)));

        if (backward)
            value -= Backward[opposed][file];

        if (connected)
            value += Connected[opposed][phalanx][relative_rank(Us, square)];

        if (lever)
            value += Lever[relative_rank(Us, square)];
    }

    bitboard = entry->semiopenFiles[Us] ^ 0xFF;
    entry->pawnSpan[Us] = bitboard ? int(msb(bitboard) - lsb(bitboard)) : 0;

    return value;
  }

} // namespace

namespace Pawns {

/// init() initializes some tables used by evaluation. Instead of hard-coded
/// tables, when makes sense, we prefer to calculate them with a formula to
/// reduce independent parameters and to allow easier tuning and better insight.

void init()
{
  static const int Seed[RANK_NB] = { 0, 6, 15, 10, 57, 75, 135, 258 };

  for (int opposed = 0; opposed <= 1; ++opposed)
      for (int phalanx = 0; phalanx <= 1; ++phalanx)
          for (Rank rank = RANK_2; rank < RANK_8; ++rank)
          {
              int bonus = Seed[rank] + (phalanx ? (Seed[rank + 1] - Seed[rank]) / 2 : 0);
              Connected[opposed][phalanx][rank] = make_score(bonus / 2, bonus >> opposed);
          }
}


/// probe() takes a position as input, computes a Entry object, and returns a
/// pointer to it. The result is also stored in a hash table, so we don't have
/// to recompute everything when the same pawn structure occurs again.

Entry* probe(const Position& pos, Table& entries) {

  Key key = pos.pawn_key();
  Entry* entry = entries[key];

  if (entry->key == key)
      return entry;

  entry->key = key;
  entry->value = evaluate<WHITE>(pos, entry) - evaluate<BLACK>(pos, entry);
  return entry;
}


/// Entry::shelter_storm() calculates shelter and storm penalties for the file
/// the king is on, as well as the two adjacent files.

template<Color Us>
Value Entry::shelter_storm(const Position& pos, Square kingSquare) {

  const Color Them = (Us == WHITE ? BLACK : WHITE);
  const Bitboard Edges = (FileABB | FileHBB) & (Rank2BB | Rank3BB);

  Bitboard bitboard = pos.pieces(PAWN) & (in_front_bb(Us, rank_of(kingSquare)) | rank_bb(kingSquare));
  Bitboard ourPawns = bitboard & pos.pieces(Us);
  Bitboard theirPawns = bitboard & pos.pieces(Them);
  Value safety = MaxSafetyBonus;
  File kingFile = std::max(FILE_B, std::min(FILE_G, file_of(kingSquare)));

  for (File file = kingFile - File(1); file <= kingFile + File(1); ++file)
  {
      bitboard = ourPawns & file_bb(file);
      Rank rankUs = bitboard ? relative_rank(Us, backmost_sq(Us, bitboard)) : RANK_1;

      bitboard  = theirPawns & file_bb(file);
      Rank rankThem = bitboard ? relative_rank(Us, frontmost_sq(Them, bitboard)) : RANK_1;

      if (   (Edges & make_square(file, rankThem))
          && file_of(kingSquare) == file
          && relative_rank(Us, kingSquare) == rankThem - 1)
          safety += 200;
      else
          safety -=  ShelterWeakness[rankUs]
                   + StormDanger[rankUs   == RANK_1   ? 0 :
                                 rankThem != rankUs + 1 ? 1 : 2][rankThem];
  }

  return safety;
}


/// Entry::do_king_safety() calculates a bonus for king safety. It is called only
/// when king square changes, which is about 20% of total king_safety() calls.

template<Color Us>
Score Entry::do_king_safety(const Position& pos, Square kingSquare) {

  kingSquares[Us] = kingSquare;
  castlingRights[Us] = pos.can_castle(Us);
  minKingPawnDistance[Us] = 0;

  Bitboard pawns = pos.pieces(Us, PAWN);
  if (pawns)
      while (!(DistanceRingsBB[kingSquare][minKingPawnDistance[Us]++] & pawns)) {}

  if (relative_rank(Us, kingSquare) > RANK_4)
      return make_score(0, -16 * minKingPawnDistance[Us]);

  Value bonus = shelter_storm<Us>(pos, kingSquare);

  // If we can castle use the bonus after the castling if it is bigger
  if (pos.can_castle(MakeCastling<Us, KING_SIDE>::right))
      bonus = std::max(bonus, shelter_storm<Us>(pos, relative_square(Us, SQ_G1)));

  if (pos.can_castle(MakeCastling<Us, QUEEN_SIDE>::right))
      bonus = std::max(bonus, shelter_storm<Us>(pos, relative_square(Us, SQ_C1)));

  return make_score(bonus, -16 * minKingPawnDistance[Us]);
}

// Explicit template instantiation
template Score Entry::do_king_safety<WHITE>(const Position& pos, Square kingSquare);
template Score Entry::do_king_safety<BLACK>(const Position& pos, Square kingSquare);

} // namespace Pawns
