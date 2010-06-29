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

  const int Sign[2] = { 1, -1 };

  // Evaluation grain size, must be a power of 2
  const int GrainSize = 8;

  // Evaluation weights, initialized from UCI options
  enum { Mobility, PawnStructure, PassedPawns, Space, KingDangerUs, KingDangerThem };
  Score Weights[6];

  typedef Value V;
  #define S(mg, eg) make_score(mg, eg)

  // Internal evaluation weights. These are applied on top of the evaluation
  // weights read from UCI parameters. The purpose is to be able to change
  // the evaluation weights while keeping the default values of the UCI
  // parameters at 100, which looks prettier.
  //
  // Values modified by Joona Kiiski
  const Score WeightsInternal[] = {
      S(248, 271), S(233, 201), S(252, 259), S(46, 0), S(247, 0), S(259, 0)
  };

  // Knight mobility bonus in middle game and endgame, indexed by the number
  // of attacked squares not occupied by friendly piecess.
  const Score KnightMobilityBonus[16] = {
    S(-38,-33), S(-25,-23), S(-12,-13), S( 0,-3),
    S( 12,  7), S( 25, 17), S( 31, 22), S(38, 27), S(38, 27)
  };

  // Bishop mobility bonus in middle game and endgame, indexed by the number
  // of attacked squares not occupied by friendly pieces. X-ray attacks through
  // queens are also included.
  const Score BishopMobilityBonus[16] = {
    S(-25,-30), S(-11,-16), S( 3, -2), S(17, 12),
    S( 31, 26), S( 45, 40), S(57, 52), S(65, 60),
    S( 71, 65), S( 74, 69), S(76, 71), S(78, 73),
    S( 79, 74), S( 80, 75), S(81, 76), S(81, 76)
  };

  // Rook mobility bonus in middle game and endgame, indexed by the number
  // of attacked squares not occupied by friendly pieces. X-ray attacks through
  // queens and rooks are also included.
  const Score RookMobilityBonus[16] = {
    S(-20,-36), S(-14,-19), S(-8, -3), S(-2, 13),
    S(  4, 29), S( 10, 46), S(14, 62), S(19, 79),
    S( 23, 95), S( 26,106), S(27,111), S(28,114),
    S( 29,116), S( 30,117), S(31,118), S(32,118)
  };

  // Queen mobility bonus in middle game and endgame, indexed by the number
  // of attacked squares not occupied by friendly pieces.
  const Score QueenMobilityBonus[32] = {
    S(-10,-18), S(-8,-13), S(-6, -7), S(-3, -2), S(-1,  3), S( 1,  8),
    S(  3, 13), S( 5, 19), S( 8, 23), S(10, 27), S(12, 32), S(15, 34),
    S( 16, 35), S(17, 35), S(18, 35), S(20, 35), S(20, 35), S(20, 35),
    S( 20, 35), S(20, 35), S(20, 35), S(20, 35), S(20, 35), S(20, 35),
    S( 20, 35), S(20, 35), S(20, 35), S(20, 35), S(20, 35), S(20, 35),
    S( 20, 35), S(20, 35)
  };

  // Pointers table to access mobility tables through piece type
  const Score* MobilityBonus[8] = { 0, 0, KnightMobilityBonus, BishopMobilityBonus,
                                    RookMobilityBonus, QueenMobilityBonus, 0, 0 };

  // Outpost bonuses for knights and bishops, indexed by square (from white's
  // point of view).
  const Value KnightOutpostBonus[64] = {
  //  A     B     C     D     E     F     G     H
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // 1
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // 2
    V(0), V(0), V(4), V(8), V(8), V(4), V(0), V(0), // 3
    V(0), V(4),V(17),V(26),V(26),V(17), V(4), V(0), // 4
    V(0), V(8),V(26),V(35),V(35),V(26), V(8), V(0), // 5
    V(0), V(4),V(17),V(17),V(17),V(17), V(4), V(0), // 6
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // 7
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0)  // 8
  };

  const Value BishopOutpostBonus[64] = {
  //  A     B     C     D     E     F     G     H
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // 1
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // 2
    V(0), V(0), V(5), V(5), V(5), V(5), V(0), V(0), // 3
    V(0), V(5),V(10),V(10),V(10),V(10), V(5), V(0), // 4
    V(0),V(10),V(21),V(21),V(21),V(21),V(10), V(0), // 5
    V(0), V(5), V(8), V(8), V(8), V(8), V(5), V(0), // 6
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // 7
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0)  // 8
  };

  // ThreatBonus[attacking][attacked] contains bonus according to which
  // piece type attacks which one.
  const Score ThreatBonus[8][8] = {
    {}, {},
    { S(0, 0), S( 7, 39), S( 0,  0), S(24, 49), S(41,100), S(41,100) }, // KNIGHT
    { S(0, 0), S( 7, 39), S(24, 49), S( 0,  0), S(41,100), S(41,100) }, // BISHOP
    { S(0, 0), S(-1, 29), S(15, 49), S(15, 49), S( 0,  0), S(24, 49) }, // ROOK
    { S(0, 0), S(15, 39), S(15, 39), S(15, 39), S(15, 39), S( 0,  0) }  // QUEEN
  };

  // ThreatedByPawnPenalty[] contains a penalty according to which piece
  // type is attacked by an enemy pawn.
  const Score ThreatedByPawnPenalty[8] = {
    S(0, 0), S(0, 0), S(56, 70), S(56, 70), S(76, 99), S(86, 118)
  };

  #undef S

  // Bonus for unstoppable passed pawns
  const Value UnstoppablePawnValue = Value(0x500);

  // Rooks and queens on the 7th rank (modified by Joona Kiiski)
  const Score RookOn7thBonus  = make_score(47, 98);
  const Score QueenOn7thBonus = make_score(27, 54);

  // Rooks on open files (modified by Joona Kiiski)
  const Score RookOpenFileBonus = make_score(43, 43);
  const Score RookHalfOpenFileBonus = make_score(19, 19);

  // Penalty for rooks trapped inside a friendly king which has lost the
  // right to castle.
  const Value TrappedRookPenalty = Value(180);

  // Penalty for a bishop on a7/h7 (a2/h2 for black) which is trapped by
  // enemy pawns.
  const Score TrappedBishopA7H7Penalty = make_score(300, 300);

  // Bitboard masks for detecting trapped bishops on a7/h7 (a2/h2 for black)
  const Bitboard MaskA7H7[2] = {
    ((1ULL << SQ_A7) | (1ULL << SQ_H7)),
    ((1ULL << SQ_A2) | (1ULL << SQ_H2))
  };

  // Penalty for a bishop on a1/h1 (a8/h8 for black) which is trapped by
  // a friendly pawn on b2/g2 (b7/g7 for black). This can obviously only
  // happen in Chess960 games.
  const Score TrappedBishopA1H1Penalty = make_score(100, 100);

  // Bitboard masks for detecting trapped bishops on a1/h1 (a8/h8 for black)
  const Bitboard MaskA1H1[2] = {
    ((1ULL << SQ_A1) | (1ULL << SQ_H1)),
    ((1ULL << SQ_A8) | (1ULL << SQ_H8))
  };

  // The SpaceMask[color] contains the area of the board which is considered
  // by the space evaluation. In the middle game, each side is given a bonus
  // based on how many squares inside this area are safe and available for
  // friendly minor pieces.
  const Bitboard SpaceMask[2] = {
    (1ULL<<SQ_C2) | (1ULL<<SQ_D2) | (1ULL<<SQ_E2) | (1ULL<<SQ_F2) |
    (1ULL<<SQ_C3) | (1ULL<<SQ_D3) | (1ULL<<SQ_E3) | (1ULL<<SQ_F3) |
    (1ULL<<SQ_C4) | (1ULL<<SQ_D4) | (1ULL<<SQ_E4) | (1ULL<<SQ_F4),
    (1ULL<<SQ_C7) | (1ULL<<SQ_D7) | (1ULL<<SQ_E7) | (1ULL<<SQ_F7) |
    (1ULL<<SQ_C6) | (1ULL<<SQ_D6) | (1ULL<<SQ_E6) | (1ULL<<SQ_F6) |
    (1ULL<<SQ_C5) | (1ULL<<SQ_D5) | (1ULL<<SQ_E5) | (1ULL<<SQ_F5)
  };

  /// King danger constants and variables. The king danger scores are taken
  /// from the KingDangerTable[]. Various little "meta-bonuses" measuring
  /// the strength of the enemy attack are added up into an integer, which
  /// is used as an index to KingDangerTable[].

  // KingAttackWeights[] contains king attack weights by piece type
  const int KingAttackWeights[8] = { 0, 0, 2, 2, 3, 5 };

  // Bonuses for enemy's safe checks
  const int QueenContactCheckBonus = 3;
  const int QueenCheckBonus        = 2;
  const int RookCheckBonus         = 1;
  const int BishopCheckBonus       = 1;
  const int KnightCheckBonus       = 1;

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

  // KingDangerTable[color][] contains the actual king danger weighted scores
  Score KingDangerTable[2][128];

  // Pawn and material hash tables, indexed by the current thread id.
  // Note that they will be initialized at 0 being global variables.
  MaterialInfoTable* MaterialTable[MAX_THREADS];
  PawnInfoTable* PawnTable[MAX_THREADS];

  // Sizes of pawn and material hash tables
  const int PawnTableSize = 16384;
  const int MaterialTableSize = 1024;

  // Function prototypes
  template<bool HasPopCnt>
  Value do_evaluate(const Position& pos, EvalInfo& ei);

  template<Color Us, bool HasPopCnt>
  void init_attack_tables(const Position& pos, EvalInfo& ei);

  template<Color Us, bool HasPopCnt>
  void evaluate_pieces_of_color(const Position& pos, EvalInfo& ei);

  template<Color Us, bool HasPopCnt>
  void evaluate_king(const Position& pos, EvalInfo& ei);

  template<Color Us>
  void evaluate_threats(const Position& pos, EvalInfo& ei);

  template<Color Us, bool HasPopCnt>
  int evaluate_space(const Position& pos, EvalInfo& ei);

  template<Color Us>
  void evaluate_passed_pawns(const Position& pos, EvalInfo& ei);

  void evaluate_unstoppable_pawns(const Position& pos, EvalInfo& ei);
  void evaluate_trapped_bishop_a7h7(const Position& pos, Square s, Color us, EvalInfo& ei);
  void evaluate_trapped_bishop_a1h1(const Position& pos, Square s, Color us, EvalInfo& ei);
  inline Score apply_weight(Score v, Score weight);
  Value scale_by_game_phase(const Score& v, Phase ph, const ScaleFactor sf[]);
  Score weight_option(const std::string& mgOpt, const std::string& egOpt, Score internalWeight);
  void init_safety();
}


////
//// Functions
////

/// evaluate() is the main evaluation function. It always computes two
/// values, an endgame score and a middle game score, and interpolates
/// between them based on the remaining material.
Value evaluate(const Position& pos, EvalInfo& ei) {

    return CpuHasPOPCNT ? do_evaluate<true>(pos, ei)
                        : do_evaluate<false>(pos, ei);
}

namespace {

template<bool HasPopCnt>
Value do_evaluate(const Position& pos, EvalInfo& ei) {

  ScaleFactor factor[2];

  assert(pos.is_ok());
  assert(pos.thread() >= 0 && pos.thread() < MAX_THREADS);
  assert(!pos.is_check());

  memset(&ei, 0, sizeof(EvalInfo));

  // Initialize by reading the incrementally updated scores included in the
  // position object (material + piece square tables)
  ei.value = pos.value();

  // Probe the material hash table
  ei.mi = MaterialTable[pos.thread()]->get_material_info(pos);
  ei.value += ei.mi->material_value();

  // If we have a specialized evaluation function for the current material
  // configuration, call it and return
  if (ei.mi->specialized_eval_exists())
      return ei.mi->evaluate(pos);

  // After get_material_info() call that modifies them
  factor[WHITE] = ei.mi->scale_factor(pos, WHITE);
  factor[BLACK] = ei.mi->scale_factor(pos, BLACK);

  // Probe the pawn hash table
  ei.pi = PawnTable[pos.thread()]->get_pawn_info(pos);
  ei.value += apply_weight(ei.pi->pawns_value(), Weights[PawnStructure]);

  // Initialize attack bitboards with pawns evaluation
  init_attack_tables<WHITE, HasPopCnt>(pos, ei);
  init_attack_tables<BLACK, HasPopCnt>(pos, ei);

  // Evaluate pieces
  evaluate_pieces_of_color<WHITE, HasPopCnt>(pos, ei);
  evaluate_pieces_of_color<BLACK, HasPopCnt>(pos, ei);

  // Kings. Kings are evaluated after all other pieces for both sides,
  // because we need complete attack information for all pieces when computing
  // the king safety evaluation.
  evaluate_king<WHITE, HasPopCnt>(pos, ei);
  evaluate_king<BLACK, HasPopCnt>(pos, ei);

  // Evaluate tactical threats, we need full attack info including king
  evaluate_threats<WHITE>(pos, ei);
  evaluate_threats<BLACK>(pos, ei);

  // Evaluate passed pawns, we need full attack info including king
  evaluate_passed_pawns<WHITE>(pos, ei);
  evaluate_passed_pawns<BLACK>(pos, ei);

  // If one side has only a king, check whether exsists any unstoppable passed pawn
  if (!pos.non_pawn_material(WHITE) || !pos.non_pawn_material(BLACK))
      evaluate_unstoppable_pawns(pos, ei);

  Phase phase = ei.mi->game_phase();

  // Middle-game specific evaluation terms
  if (phase > PHASE_ENDGAME)
  {
      // Pawn storms in positions with opposite castling
      if (   square_file(pos.king_square(WHITE)) >= FILE_E
          && square_file(pos.king_square(BLACK)) <= FILE_D)

          ei.value += make_score(ei.pi->queenside_storm_value(WHITE) - ei.pi->kingside_storm_value(BLACK), 0);

      else if (   square_file(pos.king_square(WHITE)) <= FILE_D
               && square_file(pos.king_square(BLACK)) >= FILE_E)

          ei.value += make_score(ei.pi->kingside_storm_value(WHITE) - ei.pi->queenside_storm_value(BLACK), 0);

      // Evaluate space for both sides
      if (ei.mi->space_weight() > 0)
      {
          int s = evaluate_space<WHITE, HasPopCnt>(pos, ei) - evaluate_space<BLACK, HasPopCnt>(pos, ei);
          ei.value += apply_weight(make_score(s * ei.mi->space_weight(), 0), Weights[Space]);
      }
  }

  // Mobility
  ei.value += apply_weight(ei.mobility, Weights[Mobility]);

  // If we don't already have an unusual scale factor, check for opposite
  // colored bishop endgames, and use a lower scale for those
  if (   phase < PHASE_MIDGAME
      && pos.opposite_colored_bishops()
      && (   (factor[WHITE] == SCALE_FACTOR_NORMAL && eg_value(ei.value) > Value(0))
          || (factor[BLACK] == SCALE_FACTOR_NORMAL && eg_value(ei.value) < Value(0))))
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

  // Interpolate between the middle game and the endgame score
  return Sign[pos.side_to_move()] * scale_by_game_phase(ei.value, phase, factor);
}

} // namespace

/// init_eval() initializes various tables used by the evaluation function

void init_eval(int threads) {

  assert(threads <= MAX_THREADS);

  for (int i = 0; i < MAX_THREADS; i++)
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
}


/// quit_eval() releases heap-allocated memory at program termination

void quit_eval() {

  for (int i = 0; i < MAX_THREADS; i++)
  {
      delete PawnTable[i];
      delete MaterialTable[i];
      PawnTable[i] = NULL;
      MaterialTable[i] = NULL;
  }
}


/// read_weights() reads evaluation weights from the corresponding UCI parameters

void read_weights(Color us) {

  // King safety is asymmetrical. Our king danger level is weighted by
  // "Cowardice" UCI parameter, instead the opponent one by "Aggressiveness".
  const int kingDangerUs   = (us == WHITE ? KingDangerUs   : KingDangerThem);
  const int kingDangerThem = (us == WHITE ? KingDangerThem : KingDangerUs);

  Weights[Mobility]       = weight_option("Mobility (Middle Game)", "Mobility (Endgame)", WeightsInternal[Mobility]);
  Weights[PawnStructure]  = weight_option("Pawn Structure (Middle Game)", "Pawn Structure (Endgame)", WeightsInternal[PawnStructure]);
  Weights[PassedPawns]    = weight_option("Passed Pawns (Middle Game)", "Passed Pawns (Endgame)", WeightsInternal[PassedPawns]);
  Weights[Space]          = weight_option("Space", "Space", WeightsInternal[Space]);
  Weights[kingDangerUs]   = weight_option("Cowardice", "Cowardice", WeightsInternal[KingDangerUs]);
  Weights[kingDangerThem] = weight_option("Aggressiveness", "Aggressiveness", WeightsInternal[KingDangerThem]);

  // If running in analysis mode, make sure we use symmetrical king safety. We do this
  // by replacing both Weights[kingDangerUs] and Weights[kingDangerThem] by their average.
  if (get_option_value_bool("UCI_AnalyseMode"))
      Weights[kingDangerUs] = Weights[kingDangerThem] = (Weights[kingDangerUs] + Weights[kingDangerThem]) / 2;

  init_safety();
}


namespace {

  // init_attack_tables() initializes king bitboards for both sides adding
  // pawn attacks. To be done before other evaluations.

  template<Color Us, bool HasPopCnt>
  void init_attack_tables(const Position& pos, EvalInfo& ei) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    Bitboard b = ei.attackedBy[Them][KING] = pos.attacks_from<KING>(pos.king_square(Them));
    ei.kingZone[Us] = (b | (Us == WHITE ? b >> 8 : b << 8));
    ei.attackedBy[Us][PAWN] = ei.pi->pawn_attacks(Us);
    b &= ei.attackedBy[Us][PAWN];
    if (b)
        ei.kingAttackersCount[Us] = count_1s_max_15<HasPopCnt>(b) / 2;
  }


  // evaluate_outposts() evaluates bishop and knight outposts squares

  template<PieceType Piece, Color Us>
  void evaluate_outposts(const Position& pos, EvalInfo& ei, Square s) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    // Initial bonus based on square
    Value bonus = (Piece == BISHOP ? BishopOutpostBonus[relative_square(Us, s)]
                                   : KnightOutpostBonus[relative_square(Us, s)]);

    // Increase bonus if supported by pawn, especially if the opponent has
    // no minor piece which can exchange the outpost piece
    if (bonus && bit_is_set(ei.attackedBy[Us][PAWN], s))
    {
        if (    pos.pieces(KNIGHT, Them) == EmptyBoardBB
            && (SquaresByColorBB[square_color(s)] & pos.pieces(BISHOP, Them)) == EmptyBoardBB)
            bonus += bonus + bonus / 2;
        else
            bonus += bonus / 2;
    }
    ei.value += Sign[Us] * make_score(bonus, bonus);
  }


  // evaluate_pieces<>() assigns bonuses and penalties to the pieces of a given color

  template<PieceType Piece, Color Us, bool HasPopCnt>
  void evaluate_pieces(const Position& pos, EvalInfo& ei, Bitboard no_mob_area) {

    Bitboard b;
    Square s, ksq;
    int mob;
    File f;

    const Color Them = (Us == WHITE ? BLACK : WHITE);
    const Square* ptr = pos.piece_list_begin(Us, Piece);

    while ((s = *ptr++) != SQ_NONE)
    {
        // Find attacked squares, including x-ray attacks for bishops and rooks
        if (Piece == KNIGHT || Piece == QUEEN)
            b = pos.attacks_from<Piece>(s);
        else if (Piece == BISHOP)
            b = bishop_attacks_bb(s, pos.occupied_squares() & ~pos.pieces(QUEEN, Us));
        else if (Piece == ROOK)
            b = rook_attacks_bb(s, pos.occupied_squares() & ~pos.pieces(ROOK, QUEEN, Us));
        else
            assert(false);

        // Update attack info
        ei.attackedBy[Us][Piece] |= b;

        // King attacks
        if (b & ei.kingZone[Us])
        {
            ei.kingAttackersCount[Us]++;
            ei.kingAttackersWeight[Us] += KingAttackWeights[Piece];
            Bitboard bb = (b & ei.attackedBy[Them][KING]);
            if (bb)
                ei.kingAdjacentZoneAttacksCount[Us] += count_1s_max_15<HasPopCnt>(bb);
        }

        // Mobility
        mob = (Piece != QUEEN ? count_1s_max_15<HasPopCnt>(b & no_mob_area)
                              : count_1s<HasPopCnt>(b & no_mob_area));

        ei.mobility += Sign[Us] * MobilityBonus[Piece][mob];

        // Decrease score if we are attacked by an enemy pawn. Remaining part
        // of threat evaluation must be done later when we have full attack info.
        if (bit_is_set(ei.attackedBy[Them][PAWN], s))
            ei.value -= Sign[Us] * ThreatedByPawnPenalty[Piece];

        // Bishop and knight outposts squares
        if ((Piece == BISHOP || Piece == KNIGHT) && pos.square_is_weak(s, Us))
            evaluate_outposts<Piece, Us>(pos, ei, s);

        // Special patterns: trapped bishops on a7/h7/a2/h2
        // and trapped bishops on a1/h1/a8/h8 in Chess960.
        if (Piece == BISHOP)
        {
            if (bit_is_set(MaskA7H7[Us], s))
                evaluate_trapped_bishop_a7h7(pos, s, Us, ei);

            if (Chess960 && bit_is_set(MaskA1H1[Us], s))
                evaluate_trapped_bishop_a1h1(pos, s, Us, ei);
        }

        // Queen or rook on 7th rank
        if (  (Piece == ROOK || Piece == QUEEN)
            && relative_rank(Us, s) == RANK_7
            && relative_rank(Us, pos.king_square(Them)) == RANK_8)
        {
            ei.value += Sign[Us] * (Piece == ROOK ? RookOn7thBonus : QueenOn7thBonus);
        }

        // Special extra evaluation for rooks
        if (Piece == ROOK)
        {
            // Open and half-open files
            f = square_file(s);
            if (ei.pi->file_is_half_open(Us, f))
            {
                if (ei.pi->file_is_half_open(Them, f))
                    ei.value += Sign[Us] * RookOpenFileBonus;
                else
                    ei.value += Sign[Us] * RookHalfOpenFileBonus;
            }

            // Penalize rooks which are trapped inside a king. Penalize more if
            // king has lost right to castle.
            if (mob > 6 || ei.pi->file_is_half_open(Us, f))
                continue;

            ksq = pos.king_square(Us);

            if (    square_file(ksq) >= FILE_E
                &&  square_file(s) > square_file(ksq)
                && (relative_rank(Us, ksq) == RANK_1 || square_rank(ksq) == square_rank(s)))
            {
                // Is there a half-open file between the king and the edge of the board?
                if (!ei.pi->has_open_file_to_right(Us, square_file(ksq)))
                    ei.value -= Sign[Us] * make_score(pos.can_castle(Us) ? (TrappedRookPenalty - mob * 16) / 2
                                                                         : (TrappedRookPenalty - mob * 16), 0);
            }
            else if (    square_file(ksq) <= FILE_D
                     &&  square_file(s) < square_file(ksq)
                     && (relative_rank(Us, ksq) == RANK_1 || square_rank(ksq) == square_rank(s)))
            {
                // Is there a half-open file between the king and the edge of the board?
                if (!ei.pi->has_open_file_to_left(Us, square_file(ksq)))
                    ei.value -= Sign[Us] * make_score(pos.can_castle(Us) ? (TrappedRookPenalty - mob * 16) / 2
                                                                         : (TrappedRookPenalty - mob * 16), 0);
            }
        }
    }
  }


  // evaluate_threats<>() assigns bonuses according to the type of attacking piece
  // and the type of attacked one.

  template<Color Us>
  void evaluate_threats(const Position& pos, EvalInfo& ei) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    Bitboard b;
    Score bonus = make_score(0, 0);

    // Enemy pieces not defended by a pawn and under our attack
    Bitboard weakEnemies =  pos.pieces_of_color(Them)
                          & ~ei.attackedBy[Them][PAWN]
                          & ei.attackedBy[Us][0];
    if (!weakEnemies)
        return;

    // Add bonus according to type of attacked enemy pieces and to the
    // type of attacking piece, from knights to queens. Kings are not
    // considered because are already special handled in king evaluation.
    for (PieceType pt1 = KNIGHT; pt1 < KING; pt1++)
    {
        b = ei.attackedBy[Us][pt1] & weakEnemies;
        if (b)
            for (PieceType pt2 = PAWN; pt2 < KING; pt2++)
                if (b & pos.pieces(pt2))
                    bonus += ThreatBonus[pt1][pt2];
    }
    ei.value += Sign[Us] * bonus;
  }


  // evaluate_pieces_of_color<>() assigns bonuses and penalties to all the
  // pieces of a given color.

  template<Color Us, bool HasPopCnt>
  void evaluate_pieces_of_color(const Position& pos, EvalInfo& ei) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    // Do not include in mobility squares protected by enemy pawns or occupied by our pieces
    const Bitboard no_mob_area = ~(ei.attackedBy[Them][PAWN] | pos.pieces_of_color(Us));

    evaluate_pieces<KNIGHT, Us, HasPopCnt>(pos, ei, no_mob_area);
    evaluate_pieces<BISHOP, Us, HasPopCnt>(pos, ei, no_mob_area);
    evaluate_pieces<ROOK,   Us, HasPopCnt>(pos, ei, no_mob_area);
    evaluate_pieces<QUEEN,  Us, HasPopCnt>(pos, ei, no_mob_area);

    // Sum up all attacked squares
    ei.attackedBy[Us][0] =   ei.attackedBy[Us][PAWN]   | ei.attackedBy[Us][KNIGHT]
                           | ei.attackedBy[Us][BISHOP] | ei.attackedBy[Us][ROOK]
                           | ei.attackedBy[Us][QUEEN]  | ei.attackedBy[Us][KING];
  }


  // evaluate_king<>() assigns bonuses and penalties to a king of a given color

  template<Color Us, bool HasPopCnt>
  void evaluate_king(const Position& pos, EvalInfo& ei) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    Bitboard undefended, b, b1, b2, safe;
    bool sente;
    int attackUnits, shelter = 0;
    const Square ksq = pos.king_square(Us);

    // King shelter
    if (relative_rank(Us, ksq) <= RANK_4)
    {
        shelter = ei.pi->get_king_shelter(pos, Us, ksq);
        ei.value += Sign[Us] * make_score(shelter, 0);
    }

    // King safety. This is quite complicated, and is almost certainly far
    // from optimally tuned.
    if (   pos.piece_count(Them, QUEEN) >= 1
        && ei.kingAttackersCount[Them]  >= 2
        && pos.non_pawn_material(Them)  >= QueenValueMidgame + RookValueMidgame
        && ei.kingAdjacentZoneAttacksCount[Them])
    {
        // Is it the attackers turn to move?
        sente = (Them == pos.side_to_move());

        // Find the attacked squares around the king which has no defenders
        // apart from the king itself
        undefended = ei.attacked_by(Them) & ei.attacked_by(Us, KING);
        undefended &= ~(  ei.attacked_by(Us, PAWN)   | ei.attacked_by(Us, KNIGHT)
                        | ei.attacked_by(Us, BISHOP) | ei.attacked_by(Us, ROOK)
                        | ei.attacked_by(Us, QUEEN));

        // Initialize the 'attackUnits' variable, which is used later on as an
        // index to the KingDangerTable[] array. The initial value is based on
        // the number and types of the enemy's attacking pieces, the number of
        // attacked and undefended squares around our king, the square of the
        // king, and the quality of the pawn shelter.
        attackUnits =  Min(25, (ei.kingAttackersCount[Them] * ei.kingAttackersWeight[Them]) / 2)
                     + 3 * (ei.kingAdjacentZoneAttacksCount[Them] + count_1s_max_15<HasPopCnt>(undefended))
                     + InitKingDanger[relative_square(Us, ksq)]
                     - shelter / 32;

        // Analyse enemy's safe queen contact checks. First find undefended
        // squares around the king attacked by enemy queen...
        b = undefended & ei.attacked_by(Them, QUEEN) & ~pos.pieces_of_color(Them);
        if (b)
        {
            // ...then remove squares not supported by another enemy piece
            b &= (  ei.attacked_by(Them, PAWN)   | ei.attacked_by(Them, KNIGHT)
                  | ei.attacked_by(Them, BISHOP) | ei.attacked_by(Them, ROOK));
            if (b)
                attackUnits += QueenContactCheckBonus * count_1s_max_15<HasPopCnt>(b) * (sente ? 2 : 1);
        }

        // Analyse enemy's safe distance checks for sliders and knights
        safe = ~(pos.pieces_of_color(Them) | ei.attacked_by(Us));

        b1 = pos.attacks_from<ROOK>(ksq) & safe;
        b2 = pos.attacks_from<BISHOP>(ksq) & safe;

        // Enemy queen safe checks
        b = (b1 | b2) & ei.attacked_by(Them, QUEEN);
        if (b)
            attackUnits += QueenCheckBonus * count_1s_max_15<HasPopCnt>(b);

        // Enemy rooks safe checks
        b = b1 & ei.attacked_by(Them, ROOK);
        if (b)
            attackUnits += RookCheckBonus * count_1s_max_15<HasPopCnt>(b);

        // Enemy bishops safe checks
        b = b2 & ei.attacked_by(Them, BISHOP);
        if (b)
            attackUnits += BishopCheckBonus * count_1s_max_15<HasPopCnt>(b);

        // Enemy knights safe checks
        b = pos.attacks_from<KNIGHT>(ksq) & ei.attacked_by(Them, KNIGHT) & safe;
        if (b)
            attackUnits += KnightCheckBonus * count_1s_max_15<HasPopCnt>(b);

        // To index KingDangerTable[] attackUnits must be in [0, 99] range
        attackUnits = Min(99, Max(0, attackUnits));

        // Finally, extract the king danger score from the KingDangerTable[]
        // array and subtract the score from evaluation. Set also ei.kingDanger[]
        // value that will be used for pruning because this value can sometimes
        // be very big, and so capturing a single attacking piece can therefore
        // result in a score change far bigger than the value of the captured piece.
        ei.value -= Sign[Us] * KingDangerTable[Us][attackUnits];
        ei.kingDanger[Us] = mg_value(KingDangerTable[Us][attackUnits]);
    }
  }


  // evaluate_passed_pawns<>() evaluates the passed pawns of the given color

  template<Color Us>
  void evaluate_passed_pawns(const Position& pos, EvalInfo& ei) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    Bitboard squaresToQueen, defendedSquares, unsafeSquares, supportingPawns;
    Bitboard b = ei.pi->passed_pawns() & pos.pieces_of_color(Us);

    while (b)
    {
        Square s = pop_1st_bit(&b);

        assert(pos.pawn_is_passed(Us, s));

        int r = int(relative_rank(Us, s) - RANK_2);
        int tr = r * (r - 1);

        // Base bonus based on rank
        Value mbonus = Value(20 * tr);
        Value ebonus = Value(10 + r * r * 10);

        if (tr)
        {
            Square blockSq = s + pawn_push(Us);

            // Adjust bonus based on kings proximity
            ebonus -= Value(square_distance(pos.king_square(Us), blockSq) * 3 * tr);
            ebonus -= Value(square_distance(pos.king_square(Us), blockSq + pawn_push(Us)) * 1 * tr);
            ebonus += Value(square_distance(pos.king_square(Them), blockSq) * 6 * tr);

            // If the pawn is free to advance, increase bonus
            if (pos.square_is_empty(blockSq))
            {
                squaresToQueen = squares_in_front_of(Us, s);
                defendedSquares = squaresToQueen & ei.attacked_by(Us);

                // If there is an enemy rook or queen attacking the pawn from behind,
                // add all X-ray attacks by the rook or queen. Otherwise consider only
                // the squares in the pawn's path attacked or occupied by the enemy.
                if (   (squares_behind(Us, s) & pos.pieces(ROOK, QUEEN, Them))
                    && (squares_behind(Us, s) & pos.pieces(ROOK, QUEEN, Them) & pos.attacks_from<QUEEN>(s)))
                    unsafeSquares = squaresToQueen;
                else
                    unsafeSquares = squaresToQueen & (ei.attacked_by(Them) | pos.pieces_of_color(Them));

                // If there aren't enemy attacks or pieces along the path to queen give
                // huge bonus. Even bigger if we protect the pawn's path.
                if (!unsafeSquares)
                    ebonus += Value(tr * (squaresToQueen == defendedSquares ? 17 : 15));
                else
                    // OK, there are enemy attacks or pieces (but not pawns). Are those
                    // squares which are attacked by the enemy also attacked by us ?
                    // If yes, big bonus (but smaller than when there are no enemy attacks),
                    // if no, somewhat smaller bonus.
                    ebonus += Value(tr * ((unsafeSquares & defendedSquares) == unsafeSquares ? 13 : 8));

                // At last, add a small bonus when there are no *friendly* pieces
                // in the pawn's path.
                if (!(squaresToQueen & pos.pieces_of_color(Us)))
                    ebonus += Value(tr);
            }
        } // tr != 0

        // Increase the bonus if the passed pawn is supported by a friendly pawn
        // on the same rank and a bit smaller if it's on the previous rank.
        supportingPawns = pos.pieces(PAWN, Us) & neighboring_files_bb(s);
        if (supportingPawns & rank_bb(s))
            ebonus += Value(r * 20);
        else if (supportingPawns & rank_bb(s - pawn_push(Us)))
            ebonus += Value(r * 12);

        // Rook pawns are a special case: They are sometimes worse, and
        // sometimes better than other passed pawns. It is difficult to find
        // good rules for determining whether they are good or bad. For now,
        // we try the following: Increase the value for rook pawns if the
        // other side has no pieces apart from a knight, and decrease the
        // value if the other side has a rook or queen.
        if (square_file(s) == FILE_A || square_file(s) == FILE_H)
        {
            if (pos.non_pawn_material(Them) <= KnightValueMidgame)
                ebonus += ebonus / 4;
            else if (pos.pieces(ROOK, QUEEN, Them))
                ebonus -= ebonus / 4;
        }

        // Add the scores for this pawn to the middle game and endgame eval
        ei.value += Sign[Us] * apply_weight(make_score(mbonus, ebonus), Weights[PassedPawns]);

    } // while
  }


  // evaluate_unstoppable_pawns() evaluates the unstoppable passed pawns for both sides

  void evaluate_unstoppable_pawns(const Position& pos, EvalInfo& ei) {

    int movesToGo[2] = {0, 0};
    Square pawnToGo[2] = {SQ_NONE, SQ_NONE};

    for (Color c = WHITE; c <= BLACK; c++)
    {
        // Skip evaluation if other side has non-pawn pieces
        if (pos.non_pawn_material(opposite_color(c)))
            continue;

        Bitboard b = ei.pi->passed_pawns() & pos.pieces_of_color(c);

        while (b)
        {
            Square s = pop_1st_bit(&b);
            Square queeningSquare = relative_square(c, make_square(square_file(s), RANK_8));
            int d =  square_distance(s, queeningSquare)
                   - (relative_rank(c, s) == RANK_2) // Double pawn push
                   - square_distance(pos.king_square(opposite_color(c)), queeningSquare)
                   + int(c != pos.side_to_move());

            // Do we protect the path to queening ?
            bool pathDefended = (ei.attacked_by(c) & squares_in_front_of(c, s)) == squares_in_front_of(c, s);

            if (d < 0 || pathDefended)
            {
                int mtg = RANK_8 - relative_rank(c, s);
                int blockerCount = count_1s_max_15(squares_in_front_of(c, s) & pos.occupied_squares());
                mtg += blockerCount;
                d += blockerCount;
                if ((d < 0 || pathDefended) && (!movesToGo[c] || movesToGo[c] > mtg))
                {
                    movesToGo[c] = mtg;
                    pawnToGo[c] = s;
                }
            }
        }
    }

    // Neither side has an unstoppable passed pawn?
    if (!(movesToGo[WHITE] | movesToGo[BLACK]))
        return;

    // Does only one side have an unstoppable passed pawn?
    if (!movesToGo[WHITE] || !movesToGo[BLACK])
    {
        Color winnerSide = movesToGo[WHITE] ? WHITE : BLACK;
        ei.value += make_score(0, Sign[winnerSide] * (UnstoppablePawnValue - Value(0x40 * movesToGo[winnerSide])));
    }
    else
    {   // Both sides have unstoppable pawns! Try to find out who queens
        // first. We begin by transforming 'movesToGo' to the number of
        // plies until the pawn queens for both sides.
        movesToGo[WHITE] *= 2;
        movesToGo[BLACK] *= 2;
        movesToGo[pos.side_to_move()]--;

        Color winnerSide = movesToGo[WHITE] < movesToGo[BLACK] ? WHITE : BLACK;
        Color loserSide = opposite_color(winnerSide);

        // If one side queens at least three plies before the other, that side wins
        if (movesToGo[winnerSide] <= movesToGo[loserSide] - 3)
            ei.value += Sign[winnerSide] * make_score(0, UnstoppablePawnValue - Value(0x40 * (movesToGo[winnerSide]/2)));

        // If one side queens one ply before the other and checks the king or attacks
        // the undefended opponent's queening square, that side wins. To avoid cases
        // where the opponent's king could move somewhere before first pawn queens we
        // consider only free paths to queen for both pawns.
        else if (   !(squares_in_front_of(WHITE, pawnToGo[WHITE]) & pos.occupied_squares())
                 && !(squares_in_front_of(BLACK, pawnToGo[BLACK]) & pos.occupied_squares()))
        {
            assert(movesToGo[loserSide] - movesToGo[winnerSide] == 1);

            Square winnerQSq = relative_square(winnerSide, make_square(square_file(pawnToGo[winnerSide]), RANK_8));
            Square loserQSq = relative_square(loserSide, make_square(square_file(pawnToGo[loserSide]), RANK_8));

            Bitboard b = pos.occupied_squares();
            clear_bit(&b, pawnToGo[winnerSide]);
            clear_bit(&b, pawnToGo[loserSide]);
            b = queen_attacks_bb(winnerQSq, b);

            if (  (b & pos.pieces(KING, loserSide))
                ||(bit_is_set(b, loserQSq) && !bit_is_set(ei.attacked_by(loserSide), loserQSq)))
                ei.value += Sign[winnerSide] * make_score(0, UnstoppablePawnValue - Value(0x40 * (movesToGo[winnerSide]/2)));
        }
    }
  }


  // evaluate_trapped_bishop_a7h7() determines whether a bishop on a7/h7
  // (a2/h2 for black) is trapped by enemy pawns, and assigns a penalty
  // if it is.

  void evaluate_trapped_bishop_a7h7(const Position& pos, Square s, Color us, EvalInfo &ei) {

    assert(square_is_ok(s));
    assert(pos.piece_on(s) == piece_of_color_and_type(us, BISHOP));

    Square b6 = relative_square(us, (square_file(s) == FILE_A) ? SQ_B6 : SQ_G6);
    Square b8 = relative_square(us, (square_file(s) == FILE_A) ? SQ_B8 : SQ_G8);

    if (   pos.piece_on(b6) == piece_of_color_and_type(opposite_color(us), PAWN)
        && pos.see(s, b6) < 0
        && pos.see(s, b8) < 0)
    {
        ei.value -= Sign[us] * TrappedBishopA7H7Penalty;
    }
  }


  // evaluate_trapped_bishop_a1h1() determines whether a bishop on a1/h1
  // (a8/h8 for black) is trapped by a friendly pawn on b2/g2 (b7/g7 for
  // black), and assigns a penalty if it is. This pattern can obviously
  // only occur in Chess960 games.

  void evaluate_trapped_bishop_a1h1(const Position& pos, Square s, Color us, EvalInfo& ei) {

    Piece pawn = piece_of_color_and_type(us, PAWN);
    Square b2, b3, c3;

    assert(Chess960);
    assert(square_is_ok(s));
    assert(pos.piece_on(s) == piece_of_color_and_type(us, BISHOP));

    if (square_file(s) == FILE_A)
    {
        b2 = relative_square(us, SQ_B2);
        b3 = relative_square(us, SQ_B3);
        c3 = relative_square(us, SQ_C3);
    }
    else
    {
        b2 = relative_square(us, SQ_G2);
        b3 = relative_square(us, SQ_G3);
        c3 = relative_square(us, SQ_F3);
    }

    if (pos.piece_on(b2) == pawn)
    {
        Score penalty;

        if (!pos.square_is_empty(b3))
            penalty = 2 * TrappedBishopA1H1Penalty;
        else if (pos.piece_on(c3) == pawn)
            penalty = TrappedBishopA1H1Penalty;
        else
            penalty = TrappedBishopA1H1Penalty / 2;

        ei.value -= Sign[us] * penalty;
    }
  }


  // evaluate_space() computes the space evaluation for a given side. The
  // space evaluation is a simple bonus based on the number of safe squares
  // available for minor pieces on the central four files on ranks 2--4. Safe
  // squares one, two or three squares behind a friendly pawn are counted
  // twice. Finally, the space bonus is scaled by a weight taken from the
  // material hash table.
  template<Color Us, bool HasPopCnt>
  int evaluate_space(const Position& pos, EvalInfo& ei) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    // Find the safe squares for our pieces inside the area defined by
    // SpaceMask[us]. A square is unsafe if it is attacked by an enemy
    // pawn, or if it is undefended and attacked by an enemy piece.
    Bitboard safe =   SpaceMask[Us]
                   & ~pos.pieces(PAWN, Us)
                   & ~ei.attacked_by(Them, PAWN)
                   & (ei.attacked_by(Us) | ~ei.attacked_by(Them));

    // Find all squares which are at most three squares behind some friendly pawn
    Bitboard behind = pos.pieces(PAWN, Us);
    behind |= (Us == WHITE ? behind >>  8 : behind <<  8);
    behind |= (Us == WHITE ? behind >> 16 : behind << 16);

    return count_1s_max_15<HasPopCnt>(safe) + count_1s_max_15<HasPopCnt>(behind & safe);
  }


  // apply_weight() applies an evaluation weight to a value trying to prevent overflow

  inline Score apply_weight(Score v, Score w) {
      return make_score((int(mg_value(v)) * mg_value(w)) / 0x100, (int(eg_value(v)) * eg_value(w)) / 0x100);
  }


  // scale_by_game_phase() interpolates between a middle game and an endgame
  // score, based on game phase.  It also scales the return value by a
  // ScaleFactor array.

  Value scale_by_game_phase(const Score& v, Phase ph, const ScaleFactor sf[]) {

    assert(mg_value(v) > -VALUE_INFINITE && mg_value(v) < VALUE_INFINITE);
    assert(eg_value(v) > -VALUE_INFINITE && eg_value(v) < VALUE_INFINITE);
    assert(ph >= PHASE_ENDGAME && ph <= PHASE_MIDGAME);

    Value ev = apply_scale_factor(eg_value(v), sf[(eg_value(v) > Value(0) ? WHITE : BLACK)]);

    int result = (mg_value(v) * ph + ev * (128 - ph)) / 128;
    return Value(result & ~(GrainSize - 1));
  }


  // weight_option() computes the value of an evaluation weight, by combining
  // two UCI-configurable weights (midgame and endgame) with an internal weight.

  Score weight_option(const std::string& mgOpt, const std::string& egOpt, Score internalWeight) {

    // Scale option value from 100 to 256
    int mg = get_option_value_int(mgOpt) * 256 / 100;
    int eg = get_option_value_int(egOpt) * 256 / 100;

    return apply_weight(make_score(mg, eg), internalWeight);
  }

  // init_safety() initizes the king safety evaluation, based on UCI
  // parameters. It is called from read_weights().

  void init_safety() {

    const Value MaxSlope = Value(30);
    const Value Peak = Value(1280);
    Value t[100];

    // First setup the base table
    for (int i = 0; i < 100; i++)
    {
        t[i] = Value(int(0.4 * i * i));

        if (i > 0)
            t[i] = Min(t[i], t[i - 1] + MaxSlope);

        t[i] = Min(t[i], Peak);
    }

    // Then apply the weights and get the final KingDangerTable[] array
    for (Color c = WHITE; c <= BLACK; c++)
        for (int i = 0; i < 100; i++)
            KingDangerTable[c][i] = apply_weight(make_score(t[i], 0), Weights[KingDangerUs + c]);
  }
}
