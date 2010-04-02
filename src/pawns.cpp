/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2010 Marco Costalba, Joona Kiiski, Tord Romstad

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


////
//// Includes
////

#include <cassert>
#include <cstring>

#include "bitcount.h"
#include "pawns.h"
#include "position.h"


////
//// Local definitions
////

namespace {

  /// Constants and variables

  #define S(mg, eg) make_score(mg, eg)

  // Doubled pawn penalty by file
  const Score DoubledPawnPenalty[8] = {
    S(13, 43), S(20, 48), S(23, 48), S(23, 48),
    S(23, 48), S(23, 48), S(20, 48), S(13, 43)
  };

  // Isolated pawn penalty by file
  const Score IsolatedPawnPenalty[8] = {
    S(25, 30), S(36, 35), S(40, 35), S(40, 35),
    S(40, 35), S(40, 35), S(36, 35), S(25, 30)
  };

  // Backward pawn penalty by file
  const Score BackwardPawnPenalty[8] = {
    S(20, 28), S(29, 31), S(33, 31), S(33, 31),
    S(33, 31), S(33, 31), S(29, 31), S(20, 28)
  };

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

  // Pawn storm tables for positions with opposite castling
  const int QStormTable[64] = {
    0,  0,  0,  0, 0, 0, 0, 0,
  -22,-22,-22,-14,-6, 0, 0, 0,
   -6,-10,-10,-10,-6, 0, 0, 0,
    4, 12, 16, 12, 4, 0, 0, 0,
   16, 23, 23, 16, 0, 0, 0, 0,
   23, 31, 31, 23, 0, 0, 0, 0,
   23, 31, 31, 23, 0, 0, 0, 0,
    0,  0,  0,  0, 0, 0, 0, 0
  };

  const int KStormTable[64] = {
    0, 0, 0,  0,  0,  0,  0,  0,
    0, 0, 0,-10,-19,-28,-33,-33,
    0, 0, 0,-10,-15,-19,-24,-24,
    0, 0, 0,  0,  1,  1,  1,  1,
    0, 0, 0,  0,  1, 10, 19, 19,
    0, 0, 0,  0,  1, 19, 31, 27,
    0, 0, 0,  0,  0, 22, 31, 22,
    0, 0, 0,  0,  0,  0,  0,  0
  };

  // Pawn storm open file bonuses by file
  const int16_t KStormOpenFileBonus[8] = { 31, 31, 18, 0, 0, 0, 0, 0 };
  const int16_t QStormOpenFileBonus[8] = { 0, 0, 0, 0, 0, 26, 42, 26 };

  // Pawn storm lever bonuses by file
  const int StormLeverBonus[8] = { -8, -8, -13, 0, 0, -13, -8, -8 };

}


////
//// Functions
////

/// Constructor

PawnInfoTable::PawnInfoTable(unsigned numOfEntries) {

  size = numOfEntries;
  entries = new PawnInfo[size];
  if (!entries)
  {
      std::cerr << "Failed to allocate " << (numOfEntries * sizeof(PawnInfo))
                << " bytes for pawn hash table." << std::endl;
      Application::exit_with_failure();
  }
}


/// Destructor

PawnInfoTable::~PawnInfoTable() {
  delete [] entries;
}


/// PawnInfo::clear() resets to zero the PawnInfo entry. Note that
/// kingSquares[] is initialized to SQ_NONE instead.

void PawnInfo::clear() {

  memset(this, 0, sizeof(PawnInfo));
  kingSquares[WHITE] = kingSquares[BLACK] = SQ_NONE;
}


/// PawnInfoTable::get_pawn_info() takes a position object as input, computes
/// a PawnInfo object, and returns a pointer to it.  The result is also
/// stored in a hash table, so we don't have to recompute everything when
/// the same pawn structure occurs again.

PawnInfo* PawnInfoTable::get_pawn_info(const Position& pos) {

  assert(pos.is_ok());

  Key key = pos.get_pawn_key();
  int index = int(key & (size - 1));
  PawnInfo* pi = entries + index;

  // If pi->key matches the position's pawn hash key, it means that we
  // have analysed this pawn structure before, and we can simply return
  // the information we found the last time instead of recomputing it.
  if (pi->key == key)
      return pi;

  // Clear the PawnInfo object, and set the key
  pi->clear();
  pi->key = key;

  // Calculate pawn attacks
  Bitboard whitePawns = pos.pieces(PAWN, WHITE);
  Bitboard blackPawns = pos.pieces(PAWN, BLACK);
  pi->pawnAttacks[WHITE] = ((whitePawns << 9) & ~FileABB) | ((whitePawns << 7) & ~FileHBB);
  pi->pawnAttacks[BLACK] = ((blackPawns >> 7) & ~FileABB) | ((blackPawns >> 9) & ~FileHBB);

  // Evaluate pawns for both colors
  pi->value =  evaluate_pawns<WHITE>(pos, whitePawns, blackPawns, pi)
             - evaluate_pawns<BLACK>(pos, blackPawns, whitePawns, pi);
  return pi;
}


/// PawnInfoTable::evaluate_pawns() evaluates each pawn of the given color

template<Color Us>
Score PawnInfoTable::evaluate_pawns(const Position& pos, Bitboard ourPawns,
                                    Bitboard theirPawns, PawnInfo* pi) {
  Square s;
  File f;
  Rank r;
  bool passed, isolated, doubled, chain, backward, candidate;
  int bonus;
  Score value = make_score(0, 0);
  const Square* ptr = pos.piece_list_begin(Us, PAWN);

  // Initialize pawn storm scores by giving bonuses for open files
  for (f = FILE_A; f <= FILE_H; f++)
      if (!(ourPawns & file_bb(f)))
      {
          pi->ksStormValue[Us] += KStormOpenFileBonus[f];
          pi->qsStormValue[Us] += QStormOpenFileBonus[f];
          pi->halfOpenFiles[Us] |= (1 << f);
      }

  // Loop through all pawns of the current color and score each pawn
  while ((s = *ptr++) != SQ_NONE)
  {
      f = square_file(s);
      r = square_rank(s);

      assert(pos.piece_on(s) == piece_of_color_and_type(Us, PAWN));

      // Passed, isolated or doubled pawn?
      passed   = Position::pawn_is_passed(theirPawns, Us, s);
      isolated = Position::pawn_is_isolated(ourPawns, s);
      doubled  = Position::pawn_is_doubled(ourPawns, Us, s);

      // We calculate kingside and queenside pawn storm
      // scores for both colors. These are used when evaluating
      // middle game positions with opposite side castling.
      //
      // Each pawn is given a base score given by a piece square table
      // (KStormTable[] or QStormTable[]). Pawns which seem to have good
      // chances of creating an open file by exchanging itself against an
      // enemy pawn on an adjacent file gets an additional bonus.

      // Kingside pawn storms
      bonus = KStormTable[relative_square(Us, s)];
      if (f >= FILE_F)
      {
          Bitboard b = outpost_mask(Us, s) & theirPawns & (FileFBB | FileGBB | FileHBB);
          while (b)
          {
              // Give a bonus according to the distance of the nearest enemy pawn
              Square s2 = pop_1st_bit(&b);
              int v = StormLeverBonus[f] - 2 * square_distance(s, s2);

              // If enemy pawn has no pawn beside itself is particularly vulnerable.
              // Big bonus, especially against a weakness on the rook file
              if (!(theirPawns & neighboring_files_bb(s2) & rank_bb(s2)))
                  v *= (square_file(s2) == FILE_H ? 4 : 2);

              bonus += v;
          }
      }
      pi->ksStormValue[Us] += bonus;

      // Queenside pawn storms
      bonus = QStormTable[relative_square(Us, s)];
      if (f <= FILE_C)
      {
          Bitboard b = outpost_mask(Us, s) & theirPawns & (FileABB | FileBBB | FileCBB);
          while (b)
          {
              // Give a bonus according to the distance of the nearest enemy pawn
              Square s2 = pop_1st_bit(&b);
              int v = StormLeverBonus[f] - 4 * square_distance(s, s2);

              // If enemy pawn has no pawn beside itself is particularly vulnerable.
              // Big bonus, especially against a weakness on the rook file
              if (!(theirPawns & neighboring_files_bb(s2) & rank_bb(s2)))
                  v *= (square_file(s2) == FILE_A ? 4 : 2);

              bonus += v;
          }
      }
      pi->qsStormValue[Us] += bonus;

      // Member of a pawn chain (but not the backward one)? We could speed up
      // the test a little by introducing an array of masks indexed by color
      // and square for doing the test, but because everything is hashed,
      // it probably won't make any noticable difference.
      chain =  ourPawns
             & neighboring_files_bb(f)
             & (rank_bb(r) | rank_bb(r - (Us == WHITE ? 1 : -1)));

      // Test for backward pawn
      //
      // If the pawn is passed, isolated, or member of a pawn chain
      // it cannot be backward. If can capture an enemy pawn or if
      // there are friendly pawns behind on neighboring files it cannot
      // be backward either.
      if (   (passed | isolated | chain)
          || (ourPawns & behind_bb(Us, r) & neighboring_files_bb(f))
          || (pos.attacks_from<PAWN>(s, Us) & theirPawns))
          backward = false;
      else
      {
          // We now know that there are no friendly pawns beside or behind this
          // pawn on neighboring files. We now check whether the pawn is
          // backward by looking in the forward direction on the neighboring
          // files, and seeing whether we meet a friendly or an enemy pawn first.
          Bitboard b = pos.attacks_from<PAWN>(s, Us);

          // Note that we are sure to find something because pawn is not passed
          // nor isolated, so loop is potentially infinite, but it isn't.
          while (!(b & (ourPawns | theirPawns)))
              Us == WHITE ? b <<= 8 : b >>= 8;

          // The friendly pawn needs to be at least two ranks closer than the enemy
          // pawn in order to help the potentially backward pawn advance.
          backward = (b | (Us == WHITE ? b << 8 : b >> 8)) & theirPawns;
      }

      // Test for candidate passed pawn
      candidate =   !passed
                 && !(theirPawns & file_bb(f))
                 && (  count_1s_max_15(neighboring_files_bb(f) & (behind_bb(Us, r) | rank_bb(r)) & ourPawns)
                     - count_1s_max_15(neighboring_files_bb(f) & in_front_bb(Us, r)              & theirPawns)
                     >= 0);

      // In order to prevent doubled passed pawns from receiving a too big
      // bonus, only the frontmost passed pawn on each file is considered as
      // a true passed pawn.
      if (passed && (ourPawns & squares_in_front_of(Us, s)))
          passed = false;

      // Score this pawn
      if (passed)
          set_bit(&(pi->passedPawns), s);

      if (isolated)
      {
          value -= IsolatedPawnPenalty[f];
          if (!(theirPawns & file_bb(f)))
              value -= IsolatedPawnPenalty[f] / 2;
      }
      if (doubled)
          value -= DoubledPawnPenalty[f];

      if (backward)
      {
          value -= BackwardPawnPenalty[f];
          if (!(theirPawns & file_bb(f)))
              value -= BackwardPawnPenalty[f] / 2;
      }
      if (chain)
          value += ChainBonus[f];

      if (candidate)
          value += CandidateBonus[relative_rank(Us, s)];
  }

  return value;
}


/// PawnInfo::updateShelter calculates and caches king shelter. It is called
/// only when king square changes, about 20% of total get_king_shelter() calls.
int PawnInfo::updateShelter(const Position& pos, Color c, Square ksq) {

  unsigned shelter = 0;
  Bitboard pawns = pos.pieces(PAWN, c) & this_and_neighboring_files_bb(ksq);
  unsigned r = ksq & (7 << 3);
  for (int i = 1, k = (c ? -8 : 8); i < 4; i++)
  {
      r += k;
      shelter += BitCount8Bit[(pawns >> r) & 0xFF] * (128 >> i);
  }
  kingSquares[c] = ksq;
  kingShelters[c] = shelter;
  return shelter;
}
