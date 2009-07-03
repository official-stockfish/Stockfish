/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2009 Marco Costalba

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

#include "bitcount.h"
#include "pawns.h"
#include "position.h"


////
//// Local definitions
////

namespace {

  /// Constants and variables

  // Doubled pawn penalty by file, middle game.
  const Value DoubledPawnMidgamePenalty[8] = {
    Value(13), Value(20), Value(23), Value(23),
    Value(23), Value(23), Value(20), Value(13)
  };

  // Doubled pawn penalty by file, endgame.
  const Value DoubledPawnEndgamePenalty[8] = {
    Value(43), Value(48), Value(48), Value(48),
    Value(48), Value(48), Value(48), Value(43)
  };

  // Isolated pawn penalty by file, middle game.
  const Value IsolatedPawnMidgamePenalty[8] = {
    Value(25), Value(36), Value(40), Value(40),
    Value(40), Value(40), Value(36), Value(25)
  };

  // Isolated pawn penalty by file, endgame.
  const Value IsolatedPawnEndgamePenalty[8] = {
    Value(30), Value(35), Value(35), Value(35),
    Value(35), Value(35), Value(35), Value(30)
  };

  // Backward pawn penalty by file, middle game.
  const Value BackwardPawnMidgamePenalty[8] = {
    Value(20), Value(29), Value(33), Value(33),
    Value(33), Value(33), Value(29), Value(20)
  };

  // Backward pawn penalty by file, endgame.
  const Value BackwardPawnEndgamePenalty[8] = {
    Value(28), Value(31), Value(31), Value(31),
    Value(31), Value(31), Value(31), Value(28)
  };

  // Pawn chain membership bonus by file, middle game.
  const Value ChainMidgameBonus[8] = {
    Value(11), Value(13), Value(13), Value(14),
    Value(14), Value(13), Value(13), Value(11)
  };

  // Pawn chain membership bonus by file, endgame.
  const Value ChainEndgameBonus[8] = {
    Value(-1), Value(-1), Value(-1), Value(-1),
    Value(-1), Value(-1), Value(-1), Value(-1)
  };

  // Candidate passed pawn bonus by rank, middle game.
  const Value CandidateMidgameBonus[8] = {
    Value( 0), Value( 6), Value(6), Value(14),
    Value(34), Value(83), Value(0), Value( 0)
  };

  // Candidate passed pawn bonus by rank, endgame.
  const Value CandidateEndgameBonus[8] = {
    Value( 0), Value( 13), Value(13), Value(29),
    Value(68), Value(166), Value( 0), Value( 0)
  };

  // Pawn storm tables for positions with opposite castling:
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
  if (entries == NULL)
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
  // information we found the last time instead of recomputing it
  if (pi->key == key)
      return pi;

  // Clear the PawnInfo object, and set the key
  pi->clear();
  pi->key = key;

  Value mgValue[2] = {Value(0), Value(0)};
  Value egValue[2] = {Value(0), Value(0)};

  // Loop through the pawns for both colors
  for (Color us = WHITE; us <= BLACK; us++)
  {
    Color them = opposite_color(us);
    Bitboard ourPawns = pos.pawns(us);
    Bitboard theirPawns = pos.pawns(them);
    Bitboard pawns = ourPawns;
    int bonus;

    // Initialize pawn storm scores by giving bonuses for open files
    for (File f = FILE_A; f <= FILE_H; f++)
        if (Position::file_is_half_open(ourPawns, f))
        {
            pi->ksStormValue[us] += KStormOpenFileBonus[f];
            pi->qsStormValue[us] += QStormOpenFileBonus[f];
        }

    // Loop through all pawns of the current color and score each pawn
    while (pawns)
    {
        bool passed, doubled, isolated, backward, chain, candidate;
        Square s = pop_1st_bit(&pawns);
        File f = square_file(s);
        Rank r = square_rank(s);

        assert(pos.piece_on(s) == piece_of_color_and_type(us, PAWN));

        // The file containing the pawn is not half open
        pi->halfOpenFiles[us] &= ~(1 << f);

        // Passed, isolated or doubled pawn?
        passed = Position::pawn_is_passed(theirPawns, us, s);
        isolated = Position::pawn_is_isolated(ourPawns, s);
        doubled = Position::pawn_is_doubled(ourPawns, us, s);

        // We calculate kingside and queenside pawn storm
        // scores for both colors. These are used when evaluating
        // middle game positions with opposite side castling.
        //
        // Each pawn is given a base score given by a piece square table
        // (KStormTable[] or QStormTable[]). Pawns which seem to have good
        // chances of creating an open file by exchanging itself against an
        // enemy pawn on an adjacent file gets an additional bonus.

        // Kingside pawn storms
        bonus = KStormTable[relative_square(us, s)];
        if (f >= FILE_F)
        {
            Bitboard b = outpost_mask(us, s) & theirPawns & (FileFBB | FileGBB | FileHBB);
            while (b)
            {
                Square s2 = pop_1st_bit(&b);
                if (!(theirPawns & neighboring_files_bb(s2) & rank_bb(s2)))
                {
                    // The enemy pawn has no pawn beside itself, which makes it
                    // particularly vulnerable. Big bonus, especially against a
                    // weakness on the rook file.
                    if (square_file(s2) == FILE_H)
                        bonus += 4*StormLeverBonus[f] - 8*square_distance(s, s2);
                    else
                        bonus += 2*StormLeverBonus[f] - 4*square_distance(s, s2);
                } else
                    // There is at least one enemy pawn beside the enemy pawn we look
                    // at, which means that the pawn has somewhat better chances of
                    // defending itself by advancing. Smaller bonus.
                    bonus += StormLeverBonus[f] - 2*square_distance(s, s2);
            }
        }
        pi->ksStormValue[us] += bonus;

        // Queenside pawn storms
        bonus = QStormTable[relative_square(us, s)];
        if (f <= FILE_C)
        {
            Bitboard b = outpost_mask(us, s) & theirPawns & (FileABB | FileBBB | FileCBB);
            while (b)
            {
                Square s2 = pop_1st_bit(&b);
                if (!(theirPawns & neighboring_files_bb(s2) & rank_bb(s2)))
                {
                    // The enemy pawn has no pawn beside itself, which makes it
                    // particularly vulnerable. Big bonus, especially against a
                    // weakness on the rook file.
                    if (square_file(s2) == FILE_A)
                        bonus += 4*StormLeverBonus[f] - 16*square_distance(s, s2);
                    else
                        bonus += 2*StormLeverBonus[f] - 8*square_distance(s, s2);
                } else
                    // There is at least one enemy pawn beside the enemy pawn we look
                    // at, which means that the pawn has somewhat better chances of
                    // defending itself by advancing. Smaller bonus.
                    bonus += StormLeverBonus[f] - 4*square_distance(s, s2);
            }
        }
        pi->qsStormValue[us] += bonus;

        // Member of a pawn chain (but not the backward one)? We could speed up
        // the test a little by introducing an array of masks indexed by color
        // and square for doing the test, but because everything is hashed,
        // it probably won't make any noticable difference.
        chain =  ourPawns
               & neighboring_files_bb(f)
               & (rank_bb(r) | rank_bb(r - (us == WHITE ? 1 : -1)));

        // Test for backward pawn
        //
        // If the pawn is passed, isolated, or member of a pawn chain
        // it cannot be backward. If can capture an enemy pawn or if
        // there are friendly pawns behind on neighboring files it cannot
        // be backward either.
        if (   passed
            || isolated
            || chain
            || (pos.pawn_attacks(us, s) & theirPawns)
            || (ourPawns & behind_bb(us, r) & neighboring_files_bb(f)))
            backward = false;
        else
        {
            // We now know that there are no friendly pawns beside or behind this
            // pawn on neighboring files. We now check whether the pawn is
            // backward by looking in the forward direction on the neighboring
            // files, and seeing whether we meet a friendly or an enemy pawn first.
            Bitboard b;
            if (us == WHITE)
            {
                for (b = pos.pawn_attacks(us, s); !(b & (ourPawns | theirPawns)); b <<= 8);
                backward = (b | (b << 8)) & theirPawns;
            }
            else
            {
                for (b = pos.pawn_attacks(us, s); !(b & (ourPawns | theirPawns)); b >>= 8);
                backward = (b | (b >> 8)) & theirPawns;
            }
        }

        // Test for candidate passed pawn
        candidate =    !passed
                     && Position::file_is_half_open(theirPawns, f)
                     && (  count_1s_max_15(neighboring_files_bb(f) & (behind_bb(us, r) | rank_bb(r)) & ourPawns)
                         - count_1s_max_15(neighboring_files_bb(f) & in_front_bb(us, r)              & theirPawns)
                         >= 0);

        // In order to prevent doubled passed pawns from receiving a too big
        // bonus, only the frontmost passed pawn on each file is considered as
        // a true passed pawn.
        if (passed && (ourPawns & squares_in_front_of(us, s)))
        {
            // candidate = true;
            passed = false;
        }

        // Score this pawn
        Value mv = Value(0), ev = Value(0);
        if (isolated)
        {
            mv -= IsolatedPawnMidgamePenalty[f];
            ev -= IsolatedPawnEndgamePenalty[f];
            if (Position::file_is_half_open(theirPawns, f))
            {
                mv -= IsolatedPawnMidgamePenalty[f] / 2;
                ev -= IsolatedPawnEndgamePenalty[f] / 2;
            }
        }
        if (doubled)
        {
            mv -= DoubledPawnMidgamePenalty[f];
            ev -= DoubledPawnEndgamePenalty[f];
        }
        if (backward)
        {
            mv -= BackwardPawnMidgamePenalty[f];
            ev -= BackwardPawnEndgamePenalty[f];
            if (Position::file_is_half_open(theirPawns, f))
            {
                mv -= BackwardPawnMidgamePenalty[f] / 2;
                ev -= BackwardPawnEndgamePenalty[f] / 2;
            }
        }
        if (chain)
        {
            mv += ChainMidgameBonus[f];
            ev += ChainEndgameBonus[f];
        }
        if (candidate)
        {
            mv += CandidateMidgameBonus[relative_rank(us, s)];
            ev += CandidateEndgameBonus[relative_rank(us, s)];
        }

        mgValue[us] += mv;
        egValue[us] += ev;

        // If the pawn is passed, set the square of the pawn in the passedPawns
        // bitboard
        if (passed)
            set_bit(&(pi->passedPawns), s);
    } // while(pawns)
  } // for(colors)

  pi->mgValue = int16_t(mgValue[WHITE] - mgValue[BLACK]);
  pi->egValue = int16_t(egValue[WHITE] - egValue[BLACK]);
  return pi;
}
