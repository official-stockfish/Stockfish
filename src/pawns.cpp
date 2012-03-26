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

  #undef S

  const File ShelterFile[8] = { FILE_B, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_G };

  inline Value score_non_center_file(const Value v) {
		return Value(v * 7 / 16);
  }

  typedef Value V;
  // Arrays are indexed by rank.  Zeroth value is for when no pawn on that file.
  const Value PawnShelter[8] =  { V(141), V(0), V( 38), V(102), V(128), V(141), V(141), V(141) };
  const Value PawnStorm[8] =    { V( 26), V(0), V(128), V( 51), V( 26), V(  0), V(  0), V(  0) };
  // We compute shelter as a penalty for the given color, but shelter is used as a bonus, so we invert it using this as the basis.
  const Value PawnShelterBasis =  PawnShelter[0] + score_non_center_file(PawnShelter[0]) * 2;

  inline Score apply_weight(Score v, Score w) {
    return make_score((int(mg_value(v)) * mg_value(w)) / 0x100,
                      (int(eg_value(v)) * eg_value(w)) / 0x100);
  }
}


/// PawnInfoTable::pawn_info() takes a position object as input, computes
/// a PawnInfo object, and returns a pointer to it. The result is also stored
/// in an hash table, so we don't have to recompute everything when the same
/// pawn structure occurs again.

PawnInfo* PawnInfoTable::pawn_info(const Position& pos) const {

  Key key = pos.pawn_key();
  PawnInfo* pi = probe(key);

  // If pi->key matches the position's pawn hash key, it means that we
  // have analysed this pawn structure before, and we can simply return
  // the information we found the last time instead of recomputing it.
  if (pi->key == key)
      return pi;

  // Initialize PawnInfo entry
  pi->key = key;
  pi->passedPawns[WHITE] = pi->passedPawns[BLACK] = 0;
  pi->kingSquares[WHITE] = pi->kingSquares[BLACK] = SQ_NONE;
  pi->halfOpenFiles[WHITE] = pi->halfOpenFiles[BLACK] = 0xFF;

  // Calculate pawn attacks
  Bitboard wPawns = pos.pieces(PAWN, WHITE);
  Bitboard bPawns = pos.pieces(PAWN, BLACK);
  pi->pawnAttacks[WHITE] = ((wPawns << 9) & ~FileABB) | ((wPawns << 7) & ~FileHBB);
  pi->pawnAttacks[BLACK] = ((bPawns >> 7) & ~FileABB) | ((bPawns >> 9) & ~FileHBB);

  // Evaluate pawns for both colors and weight the result
  pi->value =  evaluate_pawns<WHITE>(pos, wPawns, bPawns, pi)
             - evaluate_pawns<BLACK>(pos, bPawns, wPawns, pi);

  pi->value = apply_weight(pi->value, PawnStructureWeight);

  return pi;
}


/// PawnInfoTable::evaluate_pawns() evaluates each pawn of the given color

template<Color Us>
Score PawnInfoTable::evaluate_pawns(const Position& pos, Bitboard ourPawns,
                                    Bitboard theirPawns, PawnInfo* pi) {

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
      pi->halfOpenFiles[Us] &= ~(1 << f);

      // Our rank plus previous one. Used for chain detection
      b = rank_bb(r) | rank_bb(Us == WHITE ? r - Rank(1) : r + Rank(1));

      // Flag the pawn as passed, isolated, doubled or member of a pawn
      // chain (but not the backward one).
      passed   = !(theirPawns & passed_pawn_mask(Us, s));
      doubled  =   ourPawns   & squares_in_front_of(Us, s);
      opposed  =   theirPawns & squares_in_front_of(Us, s);
      isolated = !(ourPawns   & adjacent_files_bb(f));
      chain    =   ourPawns   & adjacent_files_bb(f) & b;

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
          pi->passedPawns[Us] |= s;

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

template<Color Us>
int computePawnShelter(const Position &pos, Square ksq) {
  const Color Them          = (Us == WHITE ? BLACK : WHITE);
  const File kingFile       = ShelterFile[file_of(ksq)];
  const Bitboard ourPawns   = pos.pieces(PAWN, Us) & in_front_bb(Us, ksq);
  const Bitboard theirPawns = pos.pieces(PAWN, Them) & (RankBB[rank_of(ksq)] | in_front_bb(Us, ksq));

  int shelter = 0;

  // Compute king shelter and storm values for the file the king is on, as well as the two adjacent files.
  for (int fileOffset = -1; fileOffset <= 1; fileOffset++) {
    // Shelter takes full penalty for center file, otherwise it's half penalty
    Bitboard shelterFile  = ourPawns & FileBB[kingFile + fileOffset];
    Rank shelterClosest = shelterFile ? relative_rank<Us>(shelterFile)
                                      : RANK_1;

    shelter += fileOffset == 0 ? PawnShelter[shelterClosest]
                               : score_non_center_file(PawnShelter[shelterClosest]);

    // Storm takes full penalty, unless there is an enemy pawn blocking us
    Bitboard stormFile  = theirPawns & FileBB[kingFile + fileOffset];
    Rank stormClosest   = stormFile ? relative_rank<Us>(stormFile)
                                    : RANK_1;

    shelter += shelterClosest + 1 == stormClosest ? PawnStorm[stormClosest] / 2
                                                  : PawnStorm[stormClosest];
  }
  
  return shelter;
}

/// PawnInfo::updateShelter() calculates and caches king shelter. It is called
/// only when king square changes, about 20% of total king_shelter() calls.
template<Color Us>
Score PawnInfo::updateShelter(const Position& pos, Square ksq) {
  int shelter = 0;

  if (relative_rank(Us, ksq) <= RANK_4)
  {
    shelter = computePawnShelter<Us>(pos, ksq);
    if (pos.can_castle(Us == WHITE ? WHITE_OO : BLACK_OO))
      shelter = std::min(shelter, computePawnShelter<Us>(pos, relative_square(Us, SQ_G1)));
    if (pos.can_castle(Us == WHITE ? WHITE_OOO : BLACK_OOO))
      shelter = std::min(shelter, computePawnShelter<Us>(pos, relative_square(Us, SQ_C1)));
    shelter = PawnShelterBasis - shelter;
  }

  kingSquares[Us] = ksq;
  kingShelters[Us] = make_score(shelter, 0);
  return kingShelters[Us];
}

// Explicit template instantiation
template Score PawnInfo::updateShelter<WHITE>(const Position& pos, Square ksq);
template Score PawnInfo::updateShelter<BLACK>(const Position& pos, Square ksq);
