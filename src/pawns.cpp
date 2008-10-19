/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008 Marco Costalba

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

#include "pawns.h"


////
//// Local definitions
////

namespace {

  /// Constants and variables

  // Doubled pawn penalty by file, middle game.
  const Value DoubledPawnMidgamePenalty[8] = {
    Value(20), Value(30), Value(34), Value(34),
    Value(34), Value(34), Value(30), Value(20)
  };

  // Doubled pawn penalty by file, endgame.
  const Value DoubledPawnEndgamePenalty[8] = {
    Value(35), Value(40), Value(40), Value(40),
    Value(40), Value(40), Value(40), Value(35)
  };

  // Isolated pawn penalty by file, middle game.
  const Value IsolatedPawnMidgamePenalty[8] = {
    Value(20), Value(30), Value(34), Value(34),
    Value(34), Value(34), Value(30), Value(20)
  };

  // Isolated pawn penalty by file, endgame.
  const Value IsolatedPawnEndgamePenalty[8] = {
    Value(35), Value(40), Value(40), Value(40),
    Value(40), Value(40), Value(40), Value(35)
  };

  // Backward pawn penalty by file, middle game.
  const Value BackwardPawnMidgamePenalty[8] = {
    Value(16), Value(24), Value(27), Value(27),
    Value(27), Value(27), Value(24), Value(16)
  };

  // Backward pawn penalty by file, endgame.
  const Value BackwardPawnEndgamePenalty[8] = {
    Value(28), Value(32), Value(32), Value(32),
    Value(32), Value(32), Value(32), Value(28)
  };

  // Pawn chain membership bonus by file, middle game. 
  const Value ChainMidgameBonus[8] = {
    Value(14), Value(16), Value(17), Value(18),
    Value(18), Value(17), Value(16), Value(14)
  };

  // Pawn chain membership bonus by file, endgame. 
  const Value ChainEndgameBonus[8] = {
    Value(16), Value(16), Value(16), Value(16),
    Value(16), Value(16), Value(16), Value(16)
  };

  // Candidate passed pawn bonus by rank, middle game.
  const Value CandidateMidgameBonus[8] = {
    Value(0), Value(12), Value(12), Value(20),
    Value(40), Value(90), Value(0), Value(0)
  };

  // Candidate passed pawn bonus by rank, endgame.
  const Value CandidateEndgameBonus[8] = {
    Value(0), Value(24), Value(24), Value(40),
    Value(80), Value(180), Value(0), Value(0)
  };

  // Evaluate pawn storms?
  const bool EvaluatePawnStorms = true;

  // Pawn storm tables for positions with opposite castling:
  const int QStormTable[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    -22, -22, -22, -13, -4, 0, 0, 0,
    -4, -9, -9, -9, -4, 0, 0, 0,
    9, 18, 22, 18, 9, 0, 0, 0,
    22, 31, 31, 22, 0, 0, 0, 0,
    31, 40, 40, 31, 0, 0, 0, 0,
    31, 40, 40, 31, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
  };
  
  const int KStormTable[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -4, -13, -22, -27, -27,
    0, 0, 0, -4, -9, -13, -18, -18,
    0, 0, 0, 0, 9, 9, 9, 9,
    0, 0, 0, 0, 9, 18, 27, 27,
    0, 0, 0, 0, 9, 27, 40, 36,
    0, 0, 0, 0, 0, 31, 40, 31,
    0, 0, 0, 0, 0, 0, 0, 0
  };

  // Pawn storm open file bonuses by file:
  const int KStormOpenFileBonus[8] = {
    45, 45, 30, 0, 0, 0, 0, 0
  };

  const int QStormOpenFileBonus[8] = {
    0, 0, 0, 0, 0, 30, 45, 30
  };

}


////
//// Functions
////

/// Constructor

PawnInfoTable::PawnInfoTable(unsigned numOfEntries) {
  size = numOfEntries;
  entries = new PawnInfo[size];
  if(entries == NULL) {
    std::cerr << "Failed to allocate " << (numOfEntries * sizeof(PawnInfo))
              << " bytes for pawn hash table." << std::endl;
    exit(EXIT_FAILURE);
  }
  this->clear();
}


/// Destructor

PawnInfoTable::~PawnInfoTable() {
  delete [] entries;
}


/// PawnInfoTable::clear() clears the pawn hash table by setting all
/// entries to 0.

void PawnInfoTable::clear() {
  memset(entries, 0, size * sizeof(PawnInfo));
}


/// PawnInfoTable::get_pawn_info() takes a position object as input, computes
/// a PawnInfo object, and returns a pointer to it.  The result is also 
/// stored in a hash table, so we don't have to recompute everything when
/// the same pawn structure occurs again.

PawnInfo *PawnInfoTable::get_pawn_info(const Position &pos) {
  assert(pos.is_ok());

  Key key = pos.get_pawn_key();
  int index = int(key & (size - 1));
  PawnInfo *pi = entries + index;

  // If pi->key matches the position's pawn hash key, it means that we 
  // have analysed this pawn structure before, and we can simply return the
  // information we found the last time instead of recomputing it:
  if(pi->key == key)
    return pi;

  // Clear the PawnInfo object, and set the key:
  pi->clear();
  pi->key = key;

  Value mgValue[2] = {Value(0), Value(0)};
  Value egValue[2] = {Value(0), Value(0)};

  // Loop through the pawns for both colors:
  for(Color us = WHITE; us <= BLACK; us++) {
    Color them = opposite_color(us);
    Bitboard ourPawns = pos.pawns(us);
    Bitboard theirPawns = pos.pawns(them);
    Bitboard pawns = ourPawns;

    // Initialize pawn storm scores by giving bonuses for open files:
    if(EvaluatePawnStorms)
      for(File f = FILE_A; f <= FILE_H; f++)
        if(pos.file_is_half_open(us, f)) {
          pi->ksStormValue[us] += KStormOpenFileBonus[f];
          pi->qsStormValue[us] += QStormOpenFileBonus[f];
        }

    // Loop through all pawns of the current color and score each pawn:
    while(pawns) {
      Square s = pop_1st_bit(&pawns);
      File f = square_file(s);
      Rank r = square_rank(s);
      bool passed, doubled, isolated, backward, chain, candidate;
      int bonus;

      assert(pos.piece_on(s) == pawn_of_color(us));

      // The file containing the pawn is not half open:
      pi->halfOpenFiles[us] &= ~(1 << f);

      // Passed, isolated or doubled pawn?
      passed = pos.pawn_is_passed(us, s);
      isolated = pos.pawn_is_isolated(us, s);
      doubled = pos.pawn_is_doubled(us, s);

      if(EvaluatePawnStorms) {
        // We calculate kingside and queenside pawn storm
        // scores for both colors.  These are used when evaluating
        // middle game positions with opposite side castling.
        //
        // Each pawn is given a base score given by a piece square table
        // (KStormTable[] or QStormTable[]).  This score is increased if
        // there are enemy pawns on adjacent files in front of the pawn.
        // This is because we want to be able to open files against the
        // enemy king, and to avoid blocking the pawn structure (e.g. white
        // pawns on h6, g5, black pawns on h7, g6, f7).
        
        // Kingside pawn storms:
        bonus = KStormTable[relative_square(us, s)];
        if(bonus > 0 && outpost_mask(us, s) & theirPawns) {
          switch(f) {

          case FILE_F:
            bonus += bonus / 4;
            break;

          case FILE_G:
            bonus += bonus / 2 + bonus / 4;
            break;

          case FILE_H:
            bonus += bonus / 2;
            break;

          default:
            break;
          }
        }
        pi->ksStormValue[us] += bonus;

        // Queenside pawn storms:
        bonus = QStormTable[relative_square(us, s)];
        if(bonus > 0 && passed_pawn_mask(us, s) & theirPawns) {
          switch(f) {

          case FILE_A:
            bonus += bonus / 2;
            break;

          case FILE_B:
            bonus += bonus / 2 + bonus / 4;
            break;

          case FILE_C:
            bonus += bonus / 2;
            break;

          default:
            break;
          }
        }
        pi->qsStormValue[us] += bonus;
      }
      
      // Member of a pawn chain?  We could speed up the test a little by 
      // introducing an array of masks indexed by color and square for doing
      // the test, but because everything is hashed, it probably won't make
      // any noticable difference.
      chain = (us == WHITE)?
        (ourPawns & neighboring_files_bb(f) & (rank_bb(r) | rank_bb(r-1))) :
        (ourPawns & neighboring_files_bb(f) & (rank_bb(r) | rank_bb(r+1)));


      // Test for backward pawn.

      // If the pawn is isolated, passed, or member of a pawn chain, it cannot
      // be backward:
      if(passed || isolated || chain)
        backward = false;
      // If the pawn can capture an enemy pawn, it's not backward:
      else if(pos.pawn_attacks(us, s) & theirPawns)
        backward = false;
      // Check for friendly pawns behind on neighboring files:
      else if(ourPawns & in_front_bb(them, r) & neighboring_files_bb(f))
        backward = false;
      else {
        // We now know that there is no friendly pawns beside or behind this
        // pawn on neighboring files.  We now check whether the pawn is
        // backward by looking in the forward direction on the neighboring
        // files, and seeing whether we meet a friendly or an enemy pawn first.
        Bitboard b;
        if(us == WHITE) {
          for(b=pos.pawn_attacks(us, s); !(b&(ourPawns|theirPawns)); b<<=8);
          backward = (b | (b << 8)) & theirPawns;
        }
        else {
          for(b=pos.pawn_attacks(us, s); !(b&(ourPawns|theirPawns)); b>>=8);
          backward = (b | (b >> 8)) & theirPawns;
        }
      }

      // Test for candidate passed pawn.
      candidate =
        (!passed && pos.file_is_half_open(them, f) &&
         count_1s_max_15(neighboring_files_bb(f)
                         & (in_front_bb(them, r) | rank_bb(r))
                         & ourPawns)
         - count_1s_max_15(neighboring_files_bb(f) & in_front_bb(us, r)
                           & theirPawns)
         >= 0);

      // In order to prevent doubled passed pawns from receiving a too big
      // bonus, only the frontmost passed pawn on each file is considered as
      // a true passed pawn.
      if(passed && (ourPawns & squares_in_front_of(us, s))) {
        // candidate = true;
        passed = false;
      }

      // Score this pawn:
      Value mv = Value(0), ev = Value(0);
      if(isolated) {
        mv -= IsolatedPawnMidgamePenalty[f];
        ev -= IsolatedPawnEndgamePenalty[f];
        if(pos.file_is_half_open(them, f)) {
          mv -= IsolatedPawnMidgamePenalty[f] / 2;
          ev -= IsolatedPawnEndgamePenalty[f] / 2;
        }
      }
      if(doubled)  {
        mv -= DoubledPawnMidgamePenalty[f];
        ev -= DoubledPawnEndgamePenalty[f];
      }
      if(backward)  {
        mv -= BackwardPawnMidgamePenalty[f];
        ev -= BackwardPawnEndgamePenalty[f];
        if(pos.file_is_half_open(them, f)) {
          mv -= BackwardPawnMidgamePenalty[f] / 2;
          ev -= BackwardPawnEndgamePenalty[f] / 2;
        }
      }
      if(chain) {
        mv += ChainMidgameBonus[f];
        ev += ChainEndgameBonus[f];
      }
      if(candidate) {
        mv += CandidateMidgameBonus[relative_rank(us, s)];
        ev += CandidateEndgameBonus[relative_rank(us, s)];
      }

      mgValue[us] += mv;
      egValue[us] += ev;
        
      // If the pawn is passed, set the square of the pawn in the passedPawns
      // bitboard:
      if(passed)
        set_bit(&(pi->passedPawns), s);
    }
  }

  pi->mgValue = int16_t(mgValue[WHITE] - mgValue[BLACK]);
  pi->egValue = int16_t(egValue[WHITE] - egValue[BLACK]);

  return pi;
}
