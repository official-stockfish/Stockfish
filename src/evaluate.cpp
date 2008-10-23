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
#include <cstring>

#include "evaluate.h"
#include "material.h"
#include "pawns.h"
#include "scale.h"
#include "thread.h"
#include "ucioption.h"


////
//// Local definitions
////

namespace {

  const int Sign[2] = {1, -1};

  // Evaluation grain size, must be a power of 2.
  const int GrainSize = 4;

  // Evaluation weights
  int WeightMobilityMidgame      = 0x100;
  int WeightMobilityEndgame      = 0x100;
  int WeightPawnStructureMidgame = 0x100;
  int WeightPawnStructureEndgame = 0x100;
  int WeightPassedPawnsMidgame   = 0x100;
  int WeightPassedPawnsEndgame   = 0x100;
  int WeightKingSafety[2] = { 0x100, 0x100 };

  // Internal evaluation weights.  These are applied on top of the evaluation
  // weights read from UCI parameters.  The purpose is to be able to change
  // the evaluation weights while keeping the default values of the UCI
  // parameters at 100, which looks prettier.
  const int WeightMobilityMidgameInternal      = 0x100;
  const int WeightMobilityEndgameInternal      = 0x100;
  const int WeightPawnStructureMidgameInternal = 0x100;
  const int WeightPawnStructureEndgameInternal = 0x100;
  const int WeightPassedPawnsMidgameInternal   = 0x100;
  const int WeightPassedPawnsEndgameInternal   = 0x100;
  const int WeightKingSafetyInternal           = 0x100;
  const int WeightKingOppSafetyInternal        = 0x100;

  // Visually better to define tables constants
  typedef Value V;

  // Knight mobility bonus in middle game and endgame, indexed by the number
  // of attacked squares not occupied by friendly piecess.
  const Value MidgameKnightMobilityBonus[] = {
  //    0       1      2     3      4      5      6      7      8
    V(-30), V(-20),V(-10), V(0), V(10), V(20), V(25), V(30), V(30)
  };

  const Value EndgameKnightMobilityBonus[] = {
  //    0       1      2     3      4      5      6      7      8
    V(-30), V(-20),V(-10), V(0), V(10), V(20), V(25), V(30), V(30)
  };

  // Bishop mobility bonus in middle game and endgame, indexed by the number
  // of attacked squares not occupied by friendly pieces.  X-ray attacks through
  // queens are also included.
  const Value MidgameBishopMobilityBonus[] = {
  //    0       1      2      3      4      5      6      7
    V(-30), V(-15),  V(0), V(15), V(30), V(45), V(58), V(66),
  //    8       9     10     11     12     13     14     15
    V( 72), V( 76), V(78), V(80), V(81), V(82), V(83), V(83)
  };

  const Value EndgameBishopMobilityBonus[] = {
  //    0       1      2      3      4      5      6      7
    V(-30), V(-15),  V(0), V(15), V(30), V(45), V(58), V(66),
  //    8       9     10     11     12     13     14     15
    V( 72), V( 76), V(78), V(80), V(81), V(82), V(83), V(83)
  };

  // Rook mobility bonus in middle game and endgame, indexed by the number
  // of attacked squares not occupied by friendly pieces.  X-ray attacks through
  // queens and rooks are also included.
  const Value MidgameRookMobilityBonus[] = {
  //    0       1      2      3      4      5      6      7
    V(-18), V(-12), V(-6),  V(0),  V(6), V(12), V(16), V(21),
  //    8       9     10     11     12     13     14     15
    V( 24), V( 27), V(28), V(29), V(30), V(31), V(32), V(33)
  };

  const Value EndgameRookMobilityBonus[] = {
  //    0       1      2      3      4      5      6      7
    V(-30), V(-18), V(-6),  V(6), V(18), V(30), V(42), V(54),
  //    8       9     10     11     12     13     14     15
    V( 66), V( 74), V(78), V(80), V(81), V(82), V(83), V(83)
  };

  // Queen mobility bonus in middle game and endgame, indexed by the number
  // of attacked squares not occupied by friendly pieces.
  const Value MidgameQueenMobilityBonus[] = {
  //    0      1      2      3      4      5      6      7
    V(-10), V(-8), V(-6), V(-4), V(-2), V( 0), V( 2), V( 4),
  //    8      9     10     11     12     13     14     15
    V(  6), V( 8), V(10), V(12), V(13), V(14), V(15), V(16),
  //   16     17     18     19     20     21     22     23
    V( 16), V(16), V(16), V(16), V(16), V(16), V(16), V(16),
  //   24     25     26     27     28     29     30     31
    V( 16), V(16), V(16), V(16), V(16), V(16), V(16), V(16)
  };

  const Value EndgameQueenMobilityBonus[] = {
  //    0      1      2      3      4      5      6      7
    V(-20),V(-15),V(-10), V(-5), V( 0), V( 5), V(10), V(15),
  //    8      9     10     11     12     13     14     15
    V( 19), V(23), V(27), V(29), V(30), V(30), V(30), V(30),
  //   16     17     18     19     20     21     22     23
    V( 30), V(30), V(30), V(30), V(30), V(30), V(30), V(30),
  //   24     25     26     27     28     29     30     31
    V( 30), V(30), V(30), V(30), V(30), V(30), V(30), V(30)
  };

  // Outpost bonuses for knights and bishops, indexed by square (from white's
  // point of view).
  const Value KnightOutpostBonus[64] = {
  //  A     B     C     D     E     F     G     H
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // 1
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // 2
    V(0), V(0), V(5),V(10),V(10), V(5), V(0), V(0), // 3
    V(0), V(5),V(20),V(30),V(30),V(20), V(5), V(0), // 4
    V(0),V(10),V(30),V(40),V(40),V(30),V(10), V(0), // 5
    V(0), V(5),V(20),V(20),V(20),V(20), V(5), V(0), // 6
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // 7
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0)  // 8
  };

  const Value BishopOutpostBonus[64] = {
  //  A     B     C     D     E     F     G     H
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // 1
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // 2
    V(0), V(0), V(5), V(5), V(5), V(5), V(0), V(0), // 3
    V(0), V(5),V(10),V(10),V(10),V(10), V(5), V(0), // 4
    V(0),V(10),V(20),V(20),V(20),V(20),V(10), V(0), // 5
    V(0), V(5), V(8), V(8), V(8), V(8), V(5), V(0), // 6
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // 7
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0)  // 8
  };

  // Bonus for unstoppable passed pawns:
  const Value UnstoppablePawnValue = Value(0x500);

  // Rooks and queens on the 7th rank:
  const Value MidgameRookOn7thBonus  = Value(50);
  const Value EndgameRookOn7thBonus  = Value(100);
  const Value MidgameQueenOn7thBonus = Value(25);
  const Value EndgameQueenOn7thBonus = Value(50);

  // Rooks on open files:
  const Value RookOpenFileBonus     = Value(40);
  const Value RookHalfOpenFileBonus = Value(20);

  // Penalty for rooks trapped inside a friendly king which has lost the
  // right to castle:
  const Value TrappedRookPenalty = Value(180);

  // Penalty for a bishop on a7/h7 (a2/h2 for black) which is trapped by
  // enemy pawns:
  const Value TrappedBishopA7H7Penalty = Value(300);

  // Bitboard masks for detecting trapped bishops on a7/h7 (a2/h2 for black):
  const Bitboard MaskA7H7[2] = {
    ((1ULL << SQ_A7) | (1ULL << SQ_H7)),
    ((1ULL << SQ_A2) | (1ULL << SQ_H2))
  };

  // Penalty for a bishop on a1/h1 (a8/h8 for black) which is trapped by
  // a friendly pawn on b2/g2 (b7/g7 for black).  This can obviously only
  // happen in Chess960 games.
  const Value TrappedBishopA1H1Penalty = Value(100);

  // Bitboard masks for detecting trapped bishops on a1/h1 (a8/h8 for black):
  const Bitboard MaskA1H1[2] = {
    ((1ULL << SQ_A1) | (1ULL << SQ_H1)),
    ((1ULL << SQ_A8) | (1ULL << SQ_H8))
  };

  /// King safety constants and variables.  The king safety scores are taken
  /// from the array SafetyTable[].  Various little "meta-bonuses" measuring
  /// the strength of the attack are added up into an integer, which is used
  /// as an index to SafetyTable[].

  // Attack weights for each piece type.
  const int QueenAttackWeight  = 5;
  const int RookAttackWeight   = 3;
  const int BishopAttackWeight = 2;
  const int KnightAttackWeight = 2;

  // Bonuses for safe checks for each piece type.
  int QueenContactCheckBonus = 4;
  int RookContactCheckBonus  = 2;
  int QueenCheckBonus        = 2;
  int RookCheckBonus         = 1;
  int BishopCheckBonus       = 1;
  int KnightCheckBonus       = 1;
  int DiscoveredCheckBonus   = 3;

  // Scan for queen contact mates?
  const bool QueenContactMates = true;

  // Bonus for having a mate threat.
  int MateThreatBonus = 3;

  // InitKingDanger[] contains bonuses based on the position of the defending
  // king.
  const int InitKingDanger[64] = {
     2,  0,  2,  5,  5,  2,  0,  2,
     2,  2,  4,  8,  8,  4,  2,  2,
     7, 10, 12, 12, 12, 12, 10,  7,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15
  };

  // SafetyTable[] contains the actual king safety scores.  It is initialized
  // in init_safety().
  Value SafetyTable[100];

  // Pawn and material hash tables, indexed by the current thread id:
  PawnInfoTable *PawnTable[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  MaterialInfoTable *MaterialTable[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  // Sizes of pawn and material hash tables:
  const int PawnTableSize = 16384;
  const int MaterialTableSize = 1024;

  // Array which gives the number of nonzero bits in an 8-bit integer:
  uint8_t BitCount8Bit[256];

  // Function prototypes:
  void evaluate_knight(const Position &p, Square s, Color us, EvalInfo &ei);
  void evaluate_bishop(const Position &p, Square s, Color us, EvalInfo &ei);
  void evaluate_rook(const Position &p, Square s, Color us, EvalInfo &ei);
  void evaluate_queen(const Position &p, Square s, Color us, EvalInfo &ei);
  void evaluate_king(const Position &p, Square s, Color us, EvalInfo &ei);

  void evaluate_passed_pawns(const Position &pos, EvalInfo &ei);
  void evaluate_trapped_bishop_a7h7(const Position &pos, Square s, Color us,
                                    EvalInfo &ei);
  void evaluate_trapped_bishop_a1h1(const Position &pos, Square s, Color us,
                                    EvalInfo &ei);

  inline Value apply_weight(Value v, int w);
  Value scale_by_game_phase(Value mv, Value ev, Phase ph, const ScaleFactor sf[]);

  int count_1s_8bit(Bitboard b);

  int compute_weight(int uciWeight, int internalWeight);
  int weight_option(const std::string& opt, int weight);
  void init_safety();

}


////
//// Functions
////

/// evaluate() is the main evaluation function.  It always computes two
/// values, an endgame score and a middle game score, and interpolates
/// between them based on the remaining material.

Value evaluate(const Position &pos, EvalInfo &ei, int threadID) {

  assert(pos.is_ok());
  assert(threadID >= 0 && threadID < THREAD_MAX);

  memset(&ei, 0, sizeof(EvalInfo));

  // Initialize by reading the incrementally updated scores included in the
  // position object (material + piece square tables)
  ei.mgValue = pos.mg_value();
  ei.egValue = pos.eg_value();

  // Probe the material hash table
  ei.mi = MaterialTable[threadID]->get_material_info(pos);
  ei.mgValue += ei.mi->mg_value();
  ei.egValue += ei.mi->eg_value();

  // If we have a specialized evaluation function for the current material
  // configuration, call it and return
  if (ei.mi->specialized_eval_exists())
      return ei.mi->evaluate(pos);

  // After get_material_info() call that modifies them
  ScaleFactor factor[2];
  factor[WHITE] = ei.mi->scale_factor(pos, WHITE);
  factor[BLACK] = ei.mi->scale_factor(pos, BLACK);

  // Probe the pawn hash table
  ei.pi = PawnTable[threadID]->get_pawn_info(pos);
  ei.mgValue += apply_weight(ei.pi->mg_value(), WeightPawnStructureMidgame);
  ei.egValue += apply_weight(ei.pi->eg_value(), WeightPawnStructureEndgame);

  // Initialize king attack bitboards and king attack zones for both sides
  ei.attackedBy[WHITE][KING] = pos.piece_attacks<KING>(pos.king_square(WHITE));
  ei.attackedBy[BLACK][KING] = pos.piece_attacks<KING>(pos.king_square(BLACK));
  ei.kingZone[WHITE] = ei.attackedBy[BLACK][KING] | (ei.attackedBy[BLACK][KING] >> 8);
  ei.kingZone[BLACK] = ei.attackedBy[WHITE][KING] | (ei.attackedBy[WHITE][KING] << 8);

  // Initialize pawn attack bitboards for both sides
  ei.attackedBy[WHITE][PAWN] = ((pos.pawns(WHITE) << 9) & ~FileABB) | ((pos.pawns(WHITE) << 7) & ~FileHBB);
  ei.attackedBy[BLACK][PAWN] = ((pos.pawns(BLACK) >> 7) & ~FileABB) | ((pos.pawns(BLACK) >> 9) & ~FileHBB);
  ei.kingAttackersCount[WHITE] = count_1s_max_15(ei.attackedBy[WHITE][PAWN] & ei.attackedBy[BLACK][KING])/2;
  ei.kingAttackersCount[BLACK] = count_1s_max_15(ei.attackedBy[BLACK][PAWN] & ei.attackedBy[WHITE][KING])/2;

  // Evaluate pieces
  for (Color c = WHITE; c <= BLACK; c++)
  {
    // Knights
    for (int i = 0; i < pos.piece_count(c, KNIGHT); i++)
        evaluate_knight(pos, pos.piece_list(c, KNIGHT, i), c, ei);

    // Bishops
    for (int i = 0; i < pos.piece_count(c, BISHOP); i++)
        evaluate_bishop(pos, pos.piece_list(c, BISHOP, i), c, ei);

    // Rooks
    for (int i = 0; i < pos.piece_count(c, ROOK); i++)
        evaluate_rook(pos, pos.piece_list(c, ROOK, i), c, ei);

    // Queens
    for(int i = 0; i < pos.piece_count(c, QUEEN); i++)
        evaluate_queen(pos, pos.piece_list(c, QUEEN, i), c, ei);

    // Special pattern: trapped bishops on a7/h7/a2/h2
    Bitboard b = pos.bishops(c) & MaskA7H7[c];
    while (b)
    {
        Square s = pop_1st_bit(&b);
        evaluate_trapped_bishop_a7h7(pos, s, c, ei);
    }

    // Special pattern: trapped bishops on a1/h1/a8/h8 in Chess960:
    if (Chess960)
    {
        b = pos.bishops(c) & MaskA1H1[c];
        while (b)
        {
            Square s = pop_1st_bit(&b);
            evaluate_trapped_bishop_a1h1(pos, s, c, ei);
        }
    }

    // Sum up all attacked squares
    ei.attackedBy[c][0] =   ei.attackedBy[c][PAWN]   | ei.attackedBy[c][KNIGHT]
                          | ei.attackedBy[c][BISHOP] | ei.attackedBy[c][ROOK]
                          | ei.attackedBy[c][QUEEN]  | ei.attackedBy[c][KING];
  }

  // Kings.  Kings are evaluated after all other pieces for both sides,
  // because we need complete attack information for all pieces when computing
  // the king safety evaluation.
  for (Color c = WHITE; c <= BLACK; c++)
      evaluate_king(pos, pos.king_square(c), c, ei);

  // Evaluate passed pawns.  We evaluate passed pawns for both sides at once,
  // because we need to know which side promotes first in positions where
  // both sides have an unstoppable passed pawn.
  if (ei.pi->passed_pawns())
      evaluate_passed_pawns(pos, ei);

  Phase phase = pos.game_phase();

  // Middle-game specific evaluation terms
  if (phase > PHASE_ENDGAME)
  {
    // Pawn storms in positions with opposite castling.
    if (   square_file(pos.king_square(WHITE)) >= FILE_E
        && square_file(pos.king_square(BLACK)) <= FILE_D)

        ei.mgValue += ei.pi->queenside_storm_value(WHITE)
                    - ei.pi->kingside_storm_value(BLACK);

    else if (   square_file(pos.king_square(WHITE)) <= FILE_D
             && square_file(pos.king_square(BLACK)) >= FILE_E)

        ei.mgValue += ei.pi->kingside_storm_value(WHITE)
                    - ei.pi->queenside_storm_value(BLACK);
  }

  // Mobility
  ei.mgValue += apply_weight(ei.mgMobility, WeightMobilityMidgame);
  ei.egValue += apply_weight(ei.egMobility, WeightMobilityEndgame);

  // If we don't already have an unusual scale factor, check for opposite
  // colored bishop endgames, and use a lower scale for those:
  if (   phase < PHASE_MIDGAME
      && pos.opposite_colored_bishops()
      && (   (factor[WHITE] == SCALE_FACTOR_NORMAL && ei.egValue > Value(0))
          || (factor[BLACK] == SCALE_FACTOR_NORMAL && ei.egValue < Value(0))))
  {
      ScaleFactor sf;

      // Only the two bishops ?
      if (   pos.non_pawn_material(WHITE) == BishopValueMidgame
          && pos.non_pawn_material(BLACK) == BishopValueMidgame)
      {
          // Check for KBP vs KB with only a single pawn that is almost
          // certainly a draw or at least two pawns.
          bool one_pawn = (pos.piece_count(WHITE, PAWN) + pos.piece_count(BLACK, PAWN) == 1);
          sf = one_pawn ? ScaleFactor(8) : ScaleFactor(32);
      }
      else
          // Endgame with opposite-colored bishops, but also other pieces. Still
          // a bit drawish, but not as drawish as with only the two bishops.
           sf = ScaleFactor(50);

      if (factor[WHITE] == SCALE_FACTOR_NORMAL)
          factor[WHITE] = sf;
      if (factor[BLACK] == SCALE_FACTOR_NORMAL)
          factor[BLACK] = sf;
  }

  // Interpolate between the middle game and the endgame score, and
  // return
  Color stm = pos.side_to_move();

  Value v = Sign[stm] * scale_by_game_phase(ei.mgValue, ei.egValue, phase, factor);

  return (ei.mateThreat[stm] == MOVE_NONE ? v : 8 * QueenValueMidgame - v);
}


/// quick_evaluate() does a very approximate evaluation of the current position.
/// It currently considers only material and piece square table scores.  Perhaps
/// we should add scores from the pawn and material hash tables?

Value quick_evaluate(const Position &pos) {

  assert(pos.is_ok());

  static const
  ScaleFactor sf[2] = {SCALE_FACTOR_NORMAL, SCALE_FACTOR_NORMAL};  

  Value mgv = pos.mg_value();
  Value egv = pos.eg_value();
  Phase ph = pos.game_phase();
  Color stm = pos.side_to_move();

  return Sign[stm] * scale_by_game_phase(mgv, egv, ph, sf);
}


/// init_eval() initializes various tables used by the evaluation function.

void init_eval(int threads) {

  assert(threads <= THREAD_MAX);

  for (int i = 0; i < THREAD_MAX; i++)
  {
    if (i >= threads)
    {
        delete PawnTable[i];
        delete MaterialTable[i];
        PawnTable[i] = NULL;
        MaterialTable[i] = NULL;
        continue;
    }
    if (!PawnTable[i])
        PawnTable[i] = new PawnInfoTable(PawnTableSize);
    if (!MaterialTable[i])
        MaterialTable[i] = new MaterialInfoTable(MaterialTableSize);
  }

  for (Bitboard b = 0ULL; b < 256ULL; b++)
      BitCount8Bit[b] = count_1s(b);
}


/// quit_eval() releases heap-allocated memory at program termination.

void quit_eval() {

  for (int i = 0; i < THREAD_MAX; i++)
  {
      delete PawnTable[i];
      delete MaterialTable[i];
  }
}


/// read_weights() reads evaluation weights from the corresponding UCI
/// parameters.

void read_weights(Color us) {

  WeightMobilityMidgame      = weight_option("Mobility (Middle Game)", WeightMobilityMidgameInternal);
  WeightMobilityEndgame      = weight_option("Mobility (Endgame)", WeightMobilityEndgameInternal);
  WeightPawnStructureMidgame = weight_option("Pawn Structure (Middle Game)", WeightPawnStructureMidgameInternal);
  WeightPawnStructureEndgame = weight_option("Pawn Structure (Endgame)", WeightPawnStructureEndgameInternal);
  WeightPassedPawnsMidgame   = weight_option("Passed Pawns (Middle Game)", WeightPassedPawnsMidgameInternal);
  WeightPassedPawnsEndgame   = weight_option("Passed Pawns (Endgame)", WeightPassedPawnsEndgameInternal);

  Color them = opposite_color(us);

  WeightKingSafety[us]   = weight_option("Cowardice", WeightKingSafetyInternal);
  WeightKingSafety[them] = weight_option("Aggressiveness", WeightKingOppSafetyInternal);

  init_safety();
}


namespace {

  // evaluate_common() computes terms common to all pieces attack

  int evaluate_common(const Position&p, const Bitboard& b, Color us, EvalInfo& ei,
                       int AttackWeight, const Value* mgBonus, const Value* egBonus,
                       Square s = SQ_NONE, const Value* OutpostBonus = NULL) {

    Color them = opposite_color(us);

    // King attack
    if (b & ei.kingZone[us])
    {
        ei.kingAttackersCount[us]++;
        ei.kingAttackersWeight[us] += AttackWeight;
        Bitboard bb = (b & ei.attackedBy[them][KING]);
        if (bb)
            ei.kingAdjacentZoneAttacksCount[us] += count_1s_max_15(bb);
    }

    // Mobility
    int mob = count_1s_max_15(b & ~p.pieces_of_color(us));
    ei.mgMobility += Sign[us] * mgBonus[mob];
    ei.egMobility += Sign[us] * egBonus[mob];

    // Bishop and Knight outposts
    if (!OutpostBonus || !p.square_is_weak(s, them))
        return mob;

    // Initial bonus based on square
    Value v, bonus;
    v = bonus = OutpostBonus[relative_square(us, s)];

    // Increase bonus if supported by pawn, especially if the opponent has
    // no minor piece which can exchange the outpost piece
    if (v && (p.pawn_attacks(them, s) & p.pawns(us)))
    {
        bonus += v / 2;
        if (   p.piece_count(them, KNIGHT) == 0
            && (SquaresByColorBB[square_color(s)] & p.bishops(them)) == EmptyBoardBB)
            bonus += v;
    }
    ei.mgValue += Sign[us] * bonus;
    ei.egValue += Sign[us] * bonus;
    return mob;
  }


  // evaluate_knight() assigns bonuses and penalties to a knight of a given
  // color on a given square.

  void evaluate_knight(const Position &p, Square s, Color us, EvalInfo &ei) {

    Bitboard b = p.piece_attacks<KNIGHT>(s);
    ei.attackedBy[us][KNIGHT] |= b;

    // King attack, mobility and outposts
    evaluate_common(p, b, us, ei, KnightAttackWeight, MidgameKnightMobilityBonus,
                    EndgameKnightMobilityBonus, s, KnightOutpostBonus);
  }


  // evaluate_bishop() assigns bonuses and penalties to a bishop of a given
  // color on a given square.

  void evaluate_bishop(const Position &p, Square s, Color us, EvalInfo &ei) {

    Bitboard b = bishop_attacks_bb(s, p.occupied_squares() & ~p.queens(us));
    ei.attackedBy[us][BISHOP] |= b;

    // King attack, mobility and outposts
    evaluate_common(p, b, us, ei, BishopAttackWeight, MidgameBishopMobilityBonus,
                    EndgameBishopMobilityBonus, s, BishopOutpostBonus);
  }


  // evaluate_rook() assigns bonuses and penalties to a rook of a given
  // color on a given square.

  void evaluate_rook(const Position &p, Square s, Color us, EvalInfo &ei) {

    //Bitboard b = p.rook_attacks(s);
    Bitboard b = rook_attacks_bb(s, p.occupied_squares() & ~p.rooks_and_queens(us));
    ei.attackedBy[us][ROOK] |= b;

    // King attack and mobility
    int mob = evaluate_common(p, b, us, ei, RookAttackWeight, MidgameRookMobilityBonus,
                              EndgameRookMobilityBonus);

    // Rook on 7th rank
    Color them = opposite_color(us);

    if (   relative_rank(us, s) == RANK_7
        && relative_rank(us, p.king_square(them)) == RANK_8)
    {
        ei.mgValue += Sign[us] * MidgameRookOn7thBonus;
        ei.egValue += Sign[us] * EndgameRookOn7thBonus;
    }

    // Open and half-open files
    File f = square_file(s);
    if (ei.pi->file_is_half_open(us, f))
    {
        if (ei.pi->file_is_half_open(them, f))
        {
            ei.mgValue += Sign[us] * RookOpenFileBonus;
            ei.egValue += Sign[us] * RookOpenFileBonus;
        }
        else
        {
            ei.mgValue += Sign[us] * RookHalfOpenFileBonus;
            ei.egValue += Sign[us] * RookHalfOpenFileBonus;
        }
    }

    // Penalize rooks which are trapped inside a king. Penalize more if
    // king has lost right to castle
    if (mob > 6 || ei.pi->file_is_half_open(us, f))
        return;

    Square ksq = p.king_square(us);

    if (    square_file(ksq) >= FILE_E
        &&  square_file(s) > square_file(ksq)
        && (relative_rank(us, ksq) == RANK_1 || square_rank(ksq) == square_rank(s)))
    {
        // Is there a half-open file between the king and the edge of the board?
        if (!ei.pi->has_open_file_to_right(us, square_file(ksq)))
            ei.mgValue -= p.can_castle(us)? Sign[us] * ((TrappedRookPenalty - mob * 16) / 2)
                                          : Sign[us] *  (TrappedRookPenalty - mob * 16);
    }
    else if (    square_file(ksq) <= FILE_D
             &&  square_file(s) < square_file(ksq)
             && (relative_rank(us, ksq) == RANK_1 || square_rank(ksq) == square_rank(s)))
    {
        // Is there a half-open file between the king and the edge of the board?
        if (!ei.pi->has_open_file_to_left(us, square_file(ksq)))
            ei.mgValue -= p.can_castle(us)? Sign[us] * ((TrappedRookPenalty - mob * 16) / 2)
                                          : Sign[us] * (TrappedRookPenalty - mob * 16);
    }
  }


  // evaluate_queen() assigns bonuses and penalties to a queen of a given
  // color on a given square.

  void evaluate_queen(const Position &p, Square s, Color us, EvalInfo &ei) {

    Bitboard b = p.piece_attacks<QUEEN>(s);
    ei.attackedBy[us][QUEEN] |= b;

    // King attack and mobility
    evaluate_common(p, b, us, ei, QueenAttackWeight, MidgameQueenMobilityBonus,
                    EndgameQueenMobilityBonus);

    // Queen on 7th rank
    Color them = opposite_color(us);

    if (   relative_rank(us, s) == RANK_7
        && relative_rank(us, p.king_square(them)) == RANK_8)
    {
        ei.mgValue += Sign[us] * MidgameQueenOn7thBonus;
        ei.egValue += Sign[us] * EndgameQueenOn7thBonus;
    }
  }

  inline Bitboard shiftRowsDown(const Bitboard& b, int num) {

    return b >> (num << 3);
  }

  // evaluate_king() assigns bonuses and penalties to a king of a given
  // color on a given square.

  void evaluate_king(const Position &p, Square s, Color us, EvalInfo &ei) {

    int shelter = 0, sign = Sign[us];

    // King shelter
    if (relative_rank(us, s) <= RANK_4)
    {
        Bitboard pawns = p.pawns(us) & this_and_neighboring_files_bb(s);
        Rank r = square_rank(s);
        for (int i = 1; i < 4; i++)
            shelter += count_1s_8bit(shiftRowsDown(pawns, r+i*sign)) * (128>>i);

        ei.mgValue += sign * Value(shelter);
    }

    // King safety.  This is quite complicated, and is almost certainly far
    // from optimally tuned.
    Color them = opposite_color(us);

    if (   p.piece_count(them, QUEEN) >= 1
        && ei.kingAttackersCount[them] >= 2
        && p.non_pawn_material(them) >= QueenValueMidgame + RookValueMidgame
        && ei.kingAdjacentZoneAttacksCount[them])
    {
      // Is it the attackers turn to move?
      bool sente = (them == p.side_to_move());

      // Find the attacked squares around the king which has no defenders
      // apart from the king itself
      Bitboard undefended =
             ei.attacked_by(them)       & ~ei.attacked_by(us, PAWN)
          & ~ei.attacked_by(us, KNIGHT) & ~ei.attacked_by(us, BISHOP)
          & ~ei.attacked_by(us, ROOK)   & ~ei.attacked_by(us, QUEEN)
          & ei.attacked_by(us, KING);

      Bitboard occ = p.occupied_squares(), b, b2;

      // Initialize the 'attackUnits' variable, which is used later on as an
      // index to the SafetyTable[] array.  The initial is based on the number
      // and types of the attacking pieces, the number of attacked and
      // undefended squares around the king, the square of the king, and the
      // quality of the pawn shelter.
      int attackUnits =
            Min((ei.kingAttackersCount[them] * ei.kingAttackersWeight[them]) / 2, 25)
          + (ei.kingAdjacentZoneAttacksCount[them] + count_1s_max_15(undefended)) * 3
          + InitKingDanger[relative_square(us, s)] - shelter / 32;

      // Analyse safe queen contact checks
      b = undefended & ei.attacked_by(them, QUEEN) & ~p.pieces_of_color(them);
      if (b)
      {
        Bitboard attackedByOthers =
              ei.attacked_by(them, PAWN)   | ei.attacked_by(them, KNIGHT)
            | ei.attacked_by(them, BISHOP) | ei.attacked_by(them, ROOK);

        b &= attackedByOthers;
        if (b)
        {
          // The bitboard b now contains the squares available for safe queen
          // contact checks.
          int count = count_1s_max_15(b);
          attackUnits += QueenContactCheckBonus * count * (sente ? 2 : 1);

          // Is there a mate threat?
          if (QueenContactMates && !p.is_check())
          {
            Bitboard escapeSquares =
                p.piece_attacks<KING>(s) & ~p.pieces_of_color(us) & ~attackedByOthers;

            while (b)
            {
                Square from, to = pop_1st_bit(&b);
                if (!(escapeSquares & ~queen_attacks_bb(to, occ & ClearMaskBB[s])))
                {
                    // We have a mate, unless the queen is pinned or there
                    // is an X-ray attack through the queen.
                    for (int i = 0; i < p.piece_count(them, QUEEN); i++)
                    {
                        from = p.piece_list(them, QUEEN, i);
                        if (    bit_is_set(p.piece_attacks<QUEEN>(from), to)
                            && !bit_is_set(p.pinned_pieces(them), from)
                            && !(rook_attacks_bb(to, occ & ClearMaskBB[from]) & p.rooks_and_queens(us))
                            && !(rook_attacks_bb(to, occ & ClearMaskBB[from]) & p.rooks_and_queens(us)))
                            
                            ei.mateThreat[them] = make_move(from, to);
                    }
                }
            }
          }
        }
      }
      // Analyse safe rook contact checks:
      if (RookContactCheckBonus)
      {
          b = undefended & ei.attacked_by(them, ROOK) & ~p.pieces_of_color(them);
          if (b)
          {
              Bitboard attackedByOthers =
                    ei.attacked_by(them, PAWN)   | ei.attacked_by(them, KNIGHT)
                  | ei.attacked_by(them, BISHOP) | ei.attacked_by(them, QUEEN);

              b &= attackedByOthers;
              if (b)
              {
                  int count = count_1s_max_15(b);
                  attackUnits += (RookContactCheckBonus * count * (sente? 2 : 1));
              }
          }
      }
      // Analyse safe distance checks:
      if (QueenCheckBonus > 0 || RookCheckBonus > 0)
      {
          b = p.piece_attacks<ROOK>(s) & ~p.pieces_of_color(them) & ~ei.attacked_by(us);

          // Queen checks
          b2 = b & ei.attacked_by(them, QUEEN);
          if( b2)
              attackUnits += QueenCheckBonus * count_1s_max_15(b2);

          // Rook checks
          b2 = b & ei.attacked_by(them, ROOK);
          if (b2)
              attackUnits += RookCheckBonus * count_1s_max_15(b2);
      }
      if (QueenCheckBonus > 0 || BishopCheckBonus > 0)
      {
          b = p.piece_attacks<BISHOP>(s) & ~p.pieces_of_color(them) & ~ei.attacked_by(us);

          // Queen checks
          b2 = b & ei.attacked_by(them, QUEEN);
          if (b2)
              attackUnits += QueenCheckBonus * count_1s_max_15(b2);

          // Bishop checks
          b2 = b & ei.attacked_by(them, BISHOP);
          if (b2)
              attackUnits += BishopCheckBonus * count_1s_max_15(b2);
      }
      if (KnightCheckBonus > 0)
      {
          b = p.piece_attacks<KNIGHT>(s) & ~p.pieces_of_color(them) & ~ei.attacked_by(us);

          // Knight checks
          b2 = b & ei.attacked_by(them, KNIGHT);
          if (b2)
              attackUnits += KnightCheckBonus * count_1s_max_15(b2);
      }

      // Analyse discovered checks (only for non-pawns right now, consider
      // adding pawns later).
      if (DiscoveredCheckBonus)
      {
        b = p.discovered_check_candidates(them) & ~p.pawns();
        if (b)
          attackUnits += DiscoveredCheckBonus * count_1s_max_15(b) * (sente? 2 : 1);
      }

      // Has a mate threat been found?  We don't do anything here if the
      // side with the mating move is the side to move, because in that
      // case the mating side will get a huge bonus at the end of the main
      // evaluation function instead.
      if (ei.mateThreat[them] != MOVE_NONE)
          attackUnits += MateThreatBonus;

      // Ensure that attackUnits is between 0 and 99, in order to avoid array
      // out of bounds errors:
      if (attackUnits < 0)
          attackUnits = 0;

      if (attackUnits >= 100)
          attackUnits = 99;

      // Finally, extract the king safety score from the SafetyTable[] array.
      // Add the score to the evaluation, and also to ei.futilityMargin.  The
      // reason for adding the king safety score to the futility margin is
      // that the king safety scores can sometimes be very big, and that
      // capturing a single attacking piece can therefore result in a score
      // change far bigger than the value of the captured piece.
      Value v = apply_weight(SafetyTable[attackUnits], WeightKingSafety[us]);

      ei.mgValue -= sign * v;

      if (us == p.side_to_move())
          ei.futilityMargin += v;
    }
  }


  // evaluate_passed_pawns() evaluates the passed pawns for both sides.

  void evaluate_passed_pawns(const Position &pos, EvalInfo &ei) {
    bool hasUnstoppable[2] = {false, false};
    int movesToGo[2] = {100, 100};

    for(Color us = WHITE; us <= BLACK; us++) {
      Color them = opposite_color(us);
      Square ourKingSq = pos.king_square(us);
      Square theirKingSq = pos.king_square(them);
      Bitboard b = ei.pi->passed_pawns() & pos.pawns(us), b2, b3, b4;

      while(b) {
        Square s = pop_1st_bit(&b);
        assert(pos.piece_on(s) == pawn_of_color(us));
        assert(pos.pawn_is_passed(us, s));

        int r = int(relative_rank(us, s) - RANK_2);
        int tr = Max(0, r * (r-1));
        Square blockSq = s + pawn_push(us);

        // Base bonus based on rank:
        Value mbonus = Value(20 * tr);
        Value ebonus = Value(10 + r * r * 10);

        // Adjust bonus based on king proximity:
        ebonus -= Value(square_distance(ourKingSq, blockSq) * 3 * tr);
        ebonus -=
          Value(square_distance(ourKingSq, blockSq + pawn_push(us)) * 1 * tr);
        ebonus += Value(square_distance(theirKingSq, blockSq) * 6 * tr);

        // If the pawn is free to advance, increase bonus:
        if(pos.square_is_empty(blockSq)) {

          b2 = squares_in_front_of(us, s);
          b3 = b2 & ei.attacked_by(them);
          b4 = b2 & ei.attacked_by(us);
          if((b2 & pos.pieces_of_color(them)) == EmptyBoardBB) {
            // There are no enemy pieces in the pawn's path!  Are any of the
            // squares in the pawn's path attacked by the enemy?
            if(b3 == EmptyBoardBB)
              // No enemy attacks, huge bonus!
              ebonus += Value(tr * ((b2 == b4)? 17 : 15));
            else
              // OK, there are enemy attacks.  Are those squares which are
              // attacked by the enemy also attacked by us?  If yes, big bonus
              // (but smaller than when there are no enemy attacks), if no,
              // somewhat smaller bonus.
              ebonus += Value(tr * (((b3 & b4) == b3)? 13 : 8));
          }
          else {
            // There are some enemy pieces in the pawn's path.  While this is
            // sad, we still assign a moderate bonus if all squares in the path
            // which are either occupied by or attacked by enemy pieces are
            // also attacked by us.
            if(((b3 | (b2 & pos.pieces_of_color(them))) & ~b4) == EmptyBoardBB)
              ebonus += Value(tr * 6);
          }
          // At last, add a small bonus when there are no *friendly* pieces
          // in the pawn's path:
          if((b2 & pos.pieces_of_color(us)) == EmptyBoardBB)
            ebonus += Value(tr);
        }

        // If the pawn is supported by a friendly pawn, increase bonus.
        b2 = pos.pawns(us) & neighboring_files_bb(s);
        if(b2 & rank_bb(s))
          ebonus += Value(r * 20);
        else if(pos.pawn_attacks(them, s) & b2)
          ebonus += Value(r * 12);

        // If the other side has only a king, check whether the pawn is
        // unstoppable:
        if(pos.non_pawn_material(them) == Value(0)) {
          Square qsq;
          int d;

          qsq = relative_square(us, make_square(square_file(s), RANK_8));
          d = square_distance(s, qsq) - square_distance(theirKingSq, qsq)
            + ((us == pos.side_to_move())? 0 : 1);

          if(d < 0) {
            int mtg = RANK_8 - relative_rank(us, s);
            int blockerCount =
              count_1s_max_15(squares_in_front_of(us,s)&pos.occupied_squares());
            mtg += blockerCount;
            d += blockerCount;
            if(d < 0) {
              hasUnstoppable[us] = true;
              movesToGo[us] = Min(movesToGo[us], mtg);
            }
          }
        }
        // Rook pawns are a special case:  They are sometimes worse, and
        // sometimes better than other passed pawns.  It is difficult to find
        // good rules for determining whether they are good or bad.  For now,
        // we try the following:  Increase the value for rook pawns if the
        // other side has no pieces apart from a knight, and decrease the
        // value if the other side has a rook or queen.
        if(square_file(s) == FILE_A || square_file(s) == FILE_H) {
          if(pos.non_pawn_material(them) == KnightValueMidgame
             && pos.piece_count(them, KNIGHT) == 1)
            ebonus += ebonus / 4;
          else if(pos.rooks_and_queens(them))
            ebonus -= ebonus / 4;
        }

        // Add the scores for this pawn to the middle game and endgame eval.
        ei.mgValue += apply_weight(Sign[us] * mbonus, WeightPassedPawnsMidgame);
        ei.egValue += apply_weight(Sign[us] * ebonus, WeightPassedPawnsEndgame);
      }
    }

    // Does either side have an unstoppable passed pawn?
    if(hasUnstoppable[WHITE] && !hasUnstoppable[BLACK])
      ei.egValue += UnstoppablePawnValue - Value(0x40 * movesToGo[WHITE]);
    else if(hasUnstoppable[BLACK] && !hasUnstoppable[WHITE])
      ei.egValue -= UnstoppablePawnValue - Value(0x40 * movesToGo[BLACK]);
    else if(hasUnstoppable[BLACK] && hasUnstoppable[WHITE]) {
      // Both sides have unstoppable pawns!  Try to find out who queens
      // first.  We begin by transforming 'movesToGo' to the number of
      // plies until the pawn queens for both sides:
      movesToGo[WHITE] *= 2;
      movesToGo[BLACK] *= 2;
      movesToGo[pos.side_to_move()]--;

      // If one side queens at least three plies before the other, that
      // side wins:
      if(movesToGo[WHITE] <= movesToGo[BLACK] - 3)
        ei.egValue += UnstoppablePawnValue - Value(0x40 * (movesToGo[WHITE]/2));
      else if(movesToGo[BLACK] <= movesToGo[WHITE] - 3)
        ei.egValue -= UnstoppablePawnValue - Value(0x40 * (movesToGo[BLACK]/2));

      // We could also add some rules about the situation when one side
      // queens exactly one ply before the other:  Does the first queen
      // check the opponent's king, or attack the opponent's queening square?
      // This is slightly tricky to get right, because it is possible that
      // the opponent's king has moved somewhere before the first pawn queens.
    }
  }


  // evaluate_trapped_bishop_a7h7() determines whether a bishop on a7/h7
  // (a2/h2 for black) is trapped by enemy pawns, and assigns a penalty
  // if it is.

  void evaluate_trapped_bishop_a7h7(const Position &pos, Square s, Color us,
                                    EvalInfo &ei) {

    assert(square_is_ok(s));
    assert(pos.piece_on(s) == bishop_of_color(us));

    Square b6 = relative_square(us, (square_file(s) == FILE_A) ? SQ_B6 : SQ_G6);
    Square b8 = relative_square(us, (square_file(s) == FILE_A) ? SQ_B8 : SQ_G8);

    if (   pos.piece_on(b6) == pawn_of_color(opposite_color(us))
        && pos.see(s, b6) < 0
        && pos.see(s, b8) < 0)
    {
        ei.mgValue -= Sign[us] * TrappedBishopA7H7Penalty;
        ei.egValue -= Sign[us] * TrappedBishopA7H7Penalty;
    }
  }


  // evaluate_trapped_bishop_a1h1() determines whether a bishop on a1/h1
  // (a8/h8 for black) is trapped by a friendly pawn on b2/g2 (b7/g7 for
  // black), and assigns a penalty if it is.  This pattern can obviously
  // only occur in Chess960 games.

  void evaluate_trapped_bishop_a1h1(const Position &pos, Square s, Color us,
                                    EvalInfo &ei) {
    Piece pawn = pawn_of_color(us);
    Square b2, b3, c3;

    assert(Chess960);
    assert(square_is_ok(s));
    assert(pos.piece_on(s) == bishop_of_color(us));

    if(square_file(s) == FILE_A) {
      b2 = relative_square(us, SQ_B2);
      b3 = relative_square(us, SQ_B3);
      c3 = relative_square(us, SQ_C3);
    }
    else {
      b2 = relative_square(us, SQ_G2);
      b3 = relative_square(us, SQ_G3);
      c3 = relative_square(us, SQ_F3);
    }

    if(pos.piece_on(b2) == pawn) {
      Value penalty;

      if(!pos.square_is_empty(b3))
        penalty = 2*TrappedBishopA1H1Penalty;
      else if(pos.piece_on(c3) == pawn)
        penalty = TrappedBishopA1H1Penalty;
      else
        penalty = TrappedBishopA1H1Penalty / 2;

      ei.mgValue -= Sign[us] * penalty;
      ei.egValue -= Sign[us] * penalty;
    }

  }


  // apply_weight applies an evaluation weight to a value.

  inline Value apply_weight(Value v, int w) {
    return (v*w) / 0x100;
  }


  // scale_by_game_phase interpolates between a middle game and an endgame
  // score, based on game phase.  It also scales the return value by a
  // ScaleFactor array.

  Value scale_by_game_phase(Value mv, Value ev, Phase ph, const ScaleFactor sf[]) {

    assert(mv > -VALUE_INFINITE && mv < VALUE_INFINITE);
    assert(ev > -VALUE_INFINITE && ev < VALUE_INFINITE);
    assert(ph >= PHASE_ENDGAME && ph <= PHASE_MIDGAME);

    ev = apply_scale_factor(ev, sf[(ev > Value(0) ? WHITE : BLACK)]);

    // Linearized sigmoid interpolator
    int sph = int(ph);
    sph -= (64 - sph) / 4;
    sph = Min(PHASE_MIDGAME, Max(PHASE_ENDGAME, sph));

    Value result = Value(int((mv * sph + ev * (128 - sph)) / 128));

    return Value(int(result) & ~(GrainSize - 1));
  }


  // count_1s_8bit() counts the number of nonzero bits in the 8 least
  // significant bits of a Bitboard. This function is used by the king
  // shield evaluation.

  int count_1s_8bit(Bitboard b) {
    return int(BitCount8Bit[b & 0xFF]);
  }


  // compute_weight() computes the value of an evaluation weight, by combining
  // an UCI-configurable weight with an internal weight.

  int compute_weight(int uciWeight, int internalWeight) {
    uciWeight = (uciWeight * 0x100) / 100;
    return (uciWeight * internalWeight) / 0x100;
  }


  // helper used in read_weights()
  int weight_option(const std::string& opt, int weight) {

    return compute_weight(get_option_value_int(opt), weight);
  }


  // init_safety() initizes the king safety evaluation, based on UCI
  // parameters.  It is called from read_weights().

  void init_safety() {

    QueenContactCheckBonus = get_option_value_int("Queen Contact Check Bonus");
    RookContactCheckBonus  = get_option_value_int("Rook Contact Check Bonus");
    QueenCheckBonus        = get_option_value_int("Queen Check Bonus");
    RookCheckBonus         = get_option_value_int("Rook Check Bonus");
    BishopCheckBonus       = get_option_value_int("Bishop Check Bonus");
    KnightCheckBonus       = get_option_value_int("Knight Check Bonus");
    DiscoveredCheckBonus   = get_option_value_int("Discovered Check Bonus");
    MateThreatBonus        = get_option_value_int("Mate Threat Bonus");

    int maxSlope = get_option_value_int("King Safety Max Slope");
    int peak     = get_option_value_int("King Safety Max Value") * 256 / 100;
    double a     = get_option_value_int("King Safety Coefficient") / 100.0;
    double b     = get_option_value_int("King Safety X Intercept");
    bool quad    = (get_option_value_string("King Safety Curve") == "Quadratic");
    bool linear  = (get_option_value_string("King Safety Curve") == "Linear");

    for (int i = 0; i < 100; i++)
    {
        if (i < b)
            SafetyTable[i] = Value(0);
        else if(quad)
            SafetyTable[i] = Value((int)(a * (i - b) * (i - b)));
        else if(linear)
            SafetyTable[i] = Value((int)(100 * a * (i - b)));
    }

    for (int i = 0; i < 100; i++)
    {
        if (SafetyTable[i+1] - SafetyTable[i] > maxSlope)
            for (int j = i + 1; j < 100; j++)
                SafetyTable[j] = SafetyTable[j-1] + Value(maxSlope);

        if (SafetyTable[i]  > Value(peak))
            SafetyTable[i] = Value(peak);
    }
  }

}
