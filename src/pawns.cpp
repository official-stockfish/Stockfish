/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2012 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <cassert>

#include "bitboard.h"
#include "bitcount.h"
#include "pawns.h"
#include "position.h"

namespace {

  #define V Value
  #define S(mg, eg) make_score(mg, eg)

  // Doubled pawn penalty by opposed flag and file
  const Score DoubledPawnPenalty[2][8] = {
  { S(13, 43), S(20, 48), S(23, 48), S(23, 48),
    S(23, 48), S(23, 48), S(20, 48), S(13, 43) },
  { S(13, 43), S(20, 48), S(23, 48), S(23, 48),
    S(23, 48), S(23, 48), S(20, 48), S(13, 43) }};

  // Isolated pawn penalty by opposed flag and file
  const Score IsolatedPawnPenalty[2][8] = {
  { S(37, 45), S(54, 52), S(60, 52), S(60, 52),
    S(60, 52), S(60, 52), S(54, 52), S(37, 45) },
  { S(25, 30), S(36, 35), S(40, 35), S(40, 35),
    S(40, 35), S(40, 35), S(36, 35), S(25, 30) }};

  // Backward pawn penalty by opposed flag and file
  const Score BackwardPawnPenalty[2][8] = {
  { S(30, 42), S(43, 46), S(49, 46), S(49, 46),
    S(49, 46), S(49, 46), S(43, 46), S(30, 42) },
  { S(20, 28), S(29, 31), S(33, 31), S(33, 31),
    S(33, 31), S(33, 31), S(29, 31), S(20, 28) }};

  // Pawn chain membership bonus by file
  const Score ChainBonus[8] = {
    S(11,-1), S(13,-1), S(13,-1), S(14,-1),
    S(14,-1), S(13,-1), S(13,-1), S(11,-1)
  };

  // Candidate passed pawn bonus by rank
  const Score CandidateBonus[8] = {
    S( 0, 0), S( 6, 13), S(6,13), S(14,29),
    S(34,68), S(83,166), S(0, 0), S( 0, 0)
  };

  const Score PawnStructureWeight = S(233, 201);

  // Weakness of our pawn shelter in front of the king indexed by [king pawn][rank]
  const Value ShelterWeakness[2][8] =
  { { V(141), V(0), V(38), V(102), V(128), V(141), V(141) },
    { V( 61), V(0), V(16), V( 44), V( 56), V( 61), V( 61) } };

  // Danger of enemy pawns moving toward our king indexed by [pawn blocked][rank]
  const Value StormDanger[2][8] =
  { { V(26), V(0), V(128), V(51), V(26) },
    { V(13), V(0), V( 64), V(25), V(13) } };

  // Max bonus for king safety. Corresponds to start position with all the pawns
  // in front of the king and no enemy pawn on the horizont.
  const Value MaxSafetyBonus = V(263);

  #undef S
  #undef V
}


/// PawnTable::probe() takes a position object as input, computes a PawnEntry
/// object, and returns a pointer to it. The result is also stored in a hash
/// table, so we don't have to recompute everything when the same pawn structure
/// occurs again.

PawnEntry* PawnTable::probe(const Position& pos) {

  Key key = pos.pawn_key();
  PawnEntry* e = entries[key];

  // If e->key matches the position's pawn hash key, it means that we
  // have analysed this pawn structure before, and we can simply return
  // the information we found the last time instead of recomputing it.
  if (e->key == key)
      return e;

  e->key = key;
  e->passedPawns[WHITE] = e->passedPawns[BLACK] = 0;
  e->kingSquares[WHITE] = e->kingSquares[BLACK] = SQ_NONE;
  e->halfOpenFiles[WHITE] = e->halfOpenFiles[BLACK] = 0xFF;

  Bitboard wPawns = pos.pieces(PAWN, WHITE);
  Bitboard bPawns = pos.pieces(PAWN, BLACK);
  e->pawnAttacks[WHITE] = ((wPawns & ~FileHBB) << 9) | ((wPawns & ~FileABB) << 7);
  e->pawnAttacks[BLACK] = ((bPawns & ~FileHBB) >> 7) | ((bPawns & ~FileABB) >> 9);

  e->value =  evaluate_pawns<WHITE>(pos, wPawns, bPawns, e)
            - evaluate_pawns<BLACK>(pos, bPawns, wPawns, e);

  e->value = apply_weight(e->value, PawnStructureWeight);

  return e;
}


/// PawnTable::evaluate_pawns() evaluates each pawn of the given color

template<Color Us>
Score PawnTable::evaluate_pawns(const Position& pos, Bitboard ourPawns,
                                Bitboard theirPawns, PawnEntry* e) {

  const Color Them = (Us == WHITE ? BLACK : WHITE);

  Bitboard b;
  Square s;
  File f;
  Rank r;
  bool passed, isolated, doubled, opposed, chain, backward, candidate;
  Score value = SCORE_ZERO;
  const Square* pl = pos.piece_list(Us, PAWN);

  // Loop through all pawns of the current color and score each pawn
  while ((s = *pl++) != SQ_NONE)
  {
      assert(pos.piece_on(s) == make_piece(Us, PAWN));

      f = file_of(s);
      r = rank_of(s);

      // This file cannot be half open
      e->halfOpenFiles[Us] &= ~(1 << f);

      // Our rank plus previous one. Used for chain detection
      b = rank_bb(r) | rank_bb(Us == WHITE ? r - Rank(1) : r + Rank(1));

      // Flag the pawn as passed, isolated, doubled or member of a pawn
      // chain (but not the backward one).
      chain    =   ourPawns   & adjacent_files_bb(f) & b;
      isolated = !(ourPawns   & adjacent_files_bb(f));
      doubled  =   ourPawns   & squares_in_front_of(Us, s);
      opposed  =   theirPawns & squares_in_front_of(Us, s);
      passed   = !(theirPawns & passed_pawn_mask(Us, s));

      // Test for backward pawn
      backward = false;

      // If the pawn is passed, isolated, or member of a pawn chain it cannot
      // be backward. If there are friendly pawns behind on adjacent files
      // or if can capture an enemy pawn it cannot be backward either.
      if (   !(passed | isolated | chain)
          && !(ourPawns & attack_span_mask(Them, s))
          && !(pos.attacks_from<PAWN>(s, Us) & theirPawns))
      {
          // We now know that there are no friendly pawns beside or behind this
          // pawn on adjacent files. We now check whether the pawn is
          // backward by looking in the forward direction on the adjacent
          // files, and seeing whether we meet a friendly or an enemy pawn first.
          b = pos.attacks_from<PAWN>(s, Us);

          // Note that we are sure to find something because pawn is not passed
          // nor isolated, so loop is potentially infinite, but it isn't.
          while (!(b & (ourPawns | theirPawns)))
              Us == WHITE ? b <<= 8 : b >>= 8;

          // The friendly pawn needs to be at least two ranks closer than the
          // enemy pawn in order to help the potentially backward pawn advance.
          backward = (b | (Us == WHITE ? b << 8 : b >> 8)) & theirPawns;
      }

      assert(opposed | passed | (attack_span_mask(Us, s) & theirPawns));

      // A not passed pawn is a candidate to become passed if it is free to
      // advance and if the number of friendly pawns beside or behind this
      // pawn on adjacent files is higher or equal than the number of
      // enemy pawns in the forward direction on the adjacent files.
      candidate =   !(opposed | passed | backward | isolated)
                 && (b = attack_span_mask(Them, s + pawn_push(Us)) & ourPawns) != 0
                 &&  popcount<Max15>(b) >= popcount<Max15>(attack_span_mask(Us, s) & theirPawns);

      // Passed pawns will be properly scored in evaluation because we need
      // full attack info to evaluate passed pawns. Only the frontmost passed
      // pawn on each file is considered a true passed pawn.
      if (passed && !doubled)
          e->passedPawns[Us] |= s;

      // Score this pawn
      if (isolated)
          value -= IsolatedPawnPenalty[opposed][f];

      if (doubled)
          value -= DoubledPawnPenalty[opposed][f];

      if (backward)
          value -= BackwardPawnPenalty[opposed][f];

      if (chain)
          value += ChainBonus[f];

      if (candidate)
          value += CandidateBonus[relative_rank(Us, s)];
  }

  return value;
}


/// PawnEntry::shelter_storm() calculates shelter and storm penalties for the file
/// the king is on, as well as the two adjacent files.

template<Color Us>
Value PawnEntry::shelter_storm(const Position& pos, Square ksq) {

  const Color Them = (Us == WHITE ? BLACK : WHITE);

  Value safety = MaxSafetyBonus;
  Bitboard b = pos.pieces(PAWN) & (in_front_bb(Us, ksq) | rank_bb(ksq));
  Bitboard ourPawns = b & pos.pieces(Us) & ~rank_bb(ksq);
  Bitboard theirPawns = b & pos.pieces(Them);
  Rank rkUs, rkThem;
  File kf = file_of(ksq);

  kf = (kf == FILE_A) ? kf++ : (kf == FILE_H) ? kf-- : kf;

  for (int f = kf - 1; f <= kf + 1; f++)
  {
      // Shelter penalty is higher for the pawn in front of the king
      b = ourPawns & FileBB[f];
      rkUs = b ? rank_of(Us == WHITE ? first_1(b) : ~last_1(b)) : RANK_1;
      safety -= ShelterWeakness[f != kf][rkUs];

      // Storm danger is smaller if enemy pawn is blocked
      b  = theirPawns & FileBB[f];
      rkThem = b ? rank_of(Us == WHITE ? first_1(b) : ~last_1(b)) : RANK_1;
      safety -= StormDanger[rkThem == rkUs + 1][rkThem];
  }

  return safety;
}


/// PawnEntry::update_safety() calculates and caches a bonus for king safety. It is
/// called only when king square changes, about 20% of total king_safety() calls.

template<Color Us>
Score PawnEntry::update_safety(const Position& pos, Square ksq) {

  kingSquares[Us] = ksq;

  if (relative_rank(Us, ksq) > RANK_4)
      return kingSafety[Us] = SCORE_ZERO;

  Value bonus = shelter_storm<Us>(pos, ksq);

  // If we can castle use the bonus after the castle if is bigger
  if (pos.can_castle(make_castle_right(Us, KING_SIDE)))
      bonus = std::max(bonus, shelter_storm<Us>(pos, relative_square(Us, SQ_G1)));

  if (pos.can_castle(make_castle_right(Us, QUEEN_SIDE)))
      bonus = std::max(bonus, shelter_storm<Us>(pos, relative_square(Us, SQ_C1)));

  return kingSafety[Us] = make_score(bonus, 0);
}

// Explicit template instantiation
template Score PawnEntry::update_safety<WHITE>(const Position& pos, Square ksq);
template Score PawnEntry::update_safety<BLACK>(const Position& pos, Square ksq);
