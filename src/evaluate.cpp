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
#include <iomanip>
#include <sstream>
#include <algorithm>

#include "bitcount.h"
#include "evaluate.h"
#include "material.h"
#include "pawns.h"
#include "thread.h"
#include "ucioption.h"

namespace {

  // Struct EvalInfo contains various information computed and collected
  // by the evaluation functions.
  struct EvalInfo {

    // Pointers to material and pawn hash table entries
    Material::Entry* mi;
    Pawns::Entry* pi;

    // attackedBy[color][piece type] is a bitboard representing all squares
    // attacked by a given color and piece type, attackedBy[color][0] contains
    // all squares attacked by the given color.
    Bitboard attackedBy[COLOR_NB][PIECE_TYPE_NB];

    // kingRing[color] is the zone around the king which is considered
    // by the king safety evaluation. This consists of the squares directly
    // adjacent to the king, and the three (or two, for a king on an edge file)
    // squares two ranks in front of the king. For instance, if black's king
    // is on g8, kingRing[BLACK] is a bitboard containing the squares f8, h8,
    // f7, g7, h7, f6, g6 and h6.
    Bitboard kingRing[COLOR_NB];

    // kingAttackersCount[color] is the number of pieces of the given color
    // which attack a square in the kingRing of the enemy king.
    int kingAttackersCount[COLOR_NB];

    // kingAttackersWeight[color] is the sum of the "weight" of the pieces of the
    // given color which attack a square in the kingRing of the enemy king. The
    // weights of the individual piece types are given by the variables
    // QueenAttackWeight, RookAttackWeight, BishopAttackWeight and
    // KnightAttackWeight in evaluate.cpp
    int kingAttackersWeight[COLOR_NB];

    // kingAdjacentZoneAttacksCount[color] is the number of attacks to squares
    // directly adjacent to the king of the given color. Pieces which attack
    // more than one square are counted multiple times. For instance, if black's
    // king is on g8 and there's a white knight on g5, this knight adds
    // 2 to kingAdjacentZoneAttacksCount[BLACK].
    int kingAdjacentZoneAttacksCount[COLOR_NB];
  };

  // Evaluation grain size, must be a power of 2
  const int GrainSize = 8;

  // Evaluation weights, initialized from UCI options
  enum { Mobility, PassedPawns, Space };
  Score Weights[3];

  typedef Value V;
  #define S(mg, eg) make_score(mg, eg)

  // Internal evaluation weights. These are applied on top of the evaluation
  // weights read from UCI parameters. The purpose is to be able to change
  // the evaluation weights while keeping the default values of the UCI
  // parameters at 100, which looks prettier.
  //
  // Values modified by Joona Kiiski
  const Score WeightsInternal[] = {
      S(252, 344), S(216, 266), S(46, 0)
  };

  // MobilityBonus[PieceType][attacked] contains mobility bonuses for middle and
  // end game, indexed by piece type and number of attacked squares not occupied
  // by friendly pieces.
  const Score MobilityBonus[][32] = {
     {}, {},
     { S(-38,-33), S(-25,-23), S(-12,-13), S( 0, -3), S(12,  7), S(25, 17), // Knights
       S( 31, 22), S( 38, 27), S( 38, 27) },
     { S(-25,-30), S(-11,-16), S(  3, -2), S(17, 12), S(31, 26), S(45, 40), // Bishops
       S( 57, 52), S( 65, 60), S( 71, 65), S(74, 69), S(76, 71), S(78, 73),
       S( 79, 74), S( 80, 75), S( 81, 76), S(81, 76) },
     { S(-20,-36), S(-14,-19), S( -8, -3), S(-2, 13), S( 4, 29), S(10, 46), // Rooks
       S( 14, 62), S( 19, 79), S( 23, 95), S(26,106), S(27,111), S(28,114),
       S( 29,116), S( 30,117), S( 31,118), S(32,118) },
     { S(-10,-18), S( -8,-13), S( -6, -7), S(-3, -2), S(-1,  3), S( 1,  8), // Queens
       S(  3, 13), S(  5, 19), S(  8, 23), S(10, 27), S(12, 32), S(15, 34),
       S( 16, 35), S( 17, 35), S( 18, 35), S(20, 35), S(20, 35), S(20, 35),
       S( 20, 35), S( 20, 35), S( 20, 35), S(20, 35), S(20, 35), S(20, 35),
       S( 20, 35), S( 20, 35), S( 20, 35), S(20, 35), S(20, 35), S(20, 35),
       S( 20, 35), S( 20, 35) }
  };

  // OutpostBonus[PieceType][Square] contains outpost bonuses of knights and
  // bishops, indexed by piece type and square (from white's point of view).
  const Value OutpostBonus[][SQUARE_NB] = {
  {
  //  A     B     C     D     E     F     G     H
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // Knights
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
    V(0), V(0), V(4), V(8), V(8), V(4), V(0), V(0),
    V(0), V(4),V(17),V(26),V(26),V(17), V(4), V(0),
    V(0), V(8),V(26),V(35),V(35),V(26), V(8), V(0),
    V(0), V(4),V(17),V(17),V(17),V(17), V(4), V(0) },
  {
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0), // Bishops
    V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
    V(0), V(0), V(5), V(5), V(5), V(5), V(0), V(0),
    V(0), V(5),V(10),V(10),V(10),V(10), V(5), V(0),
    V(0),V(10),V(21),V(21),V(21),V(21),V(10), V(0),
    V(0), V(5), V(8), V(8), V(8), V(8), V(5), V(0) }
  };

  // ThreatBonus[attacking][attacked] contains threat bonuses according to
  // which piece type attacks which one.
  const Score ThreatBonus[][PIECE_TYPE_NB] = {
    {}, {},
    { S(0, 0), S( 7, 39), S( 0,  0), S(24, 49), S(41,100), S(41,100) }, // KNIGHT
    { S(0, 0), S( 7, 39), S(24, 49), S( 0,  0), S(41,100), S(41,100) }, // BISHOP
    { S(0, 0), S( 0, 22), S(15, 49), S(15, 49), S( 0,  0), S(24, 49) }, // ROOK
    { S(0, 0), S(15, 39), S(15, 39), S(15, 39), S(15, 39), S( 0,  0) }  // QUEEN
  };

  // ThreatenedByPawnPenalty[PieceType] contains a penalty according to which
  // piece type is attacked by an enemy pawn.
  const Score ThreatenedByPawnPenalty[] = {
    S(0, 0), S(0, 0), S(56, 70), S(56, 70), S(76, 99), S(86, 118)
  };

  #undef S

  // Bonus for having the side to move (modified by Joona Kiiski)
  const Score Tempo = make_score(24, 11);

  // Rooks and queens on the 7th rank
  const Score RookOn7thBonus  = make_score(3, 20);
  const Score QueenOn7thBonus = make_score(1,  8);

  // Rooks and queens attacking pawns on the same rank
  const Score RookOnPawnBonus  = make_score(3, 48);
  const Score QueenOnPawnBonus = make_score(1, 40);

  // Rooks on open files (modified by Joona Kiiski)
  const Score RookOpenFileBonus     = make_score(43, 21);
  const Score RookHalfOpenFileBonus = make_score(19, 10);

  // Penalty for rooks trapped inside a friendly king which has lost the
  // right to castle.
  const Value TrappedRookPenalty = Value(180);

  // Penalty for a bishop on a1/h1 (a8/h8 for black) which is trapped by
  // a friendly pawn on b2/g2 (b7/g7 for black). This can obviously only
  // happen in Chess960 games.
  const Score TrappedBishopA1H1Penalty = make_score(100, 100);

  // Penalty for an undefended bishop or knight
  const Score UndefendedMinorPenalty = make_score(25, 10);

  // The SpaceMask[Color] contains the area of the board which is considered
  // by the space evaluation. In the middle game, each side is given a bonus
  // based on how many squares inside this area are safe and available for
  // friendly minor pieces.
  const Bitboard SpaceMask[] = {
    (1ULL << SQ_C2) | (1ULL << SQ_D2) | (1ULL << SQ_E2) | (1ULL << SQ_F2) |
    (1ULL << SQ_C3) | (1ULL << SQ_D3) | (1ULL << SQ_E3) | (1ULL << SQ_F3) |
    (1ULL << SQ_C4) | (1ULL << SQ_D4) | (1ULL << SQ_E4) | (1ULL << SQ_F4),
    (1ULL << SQ_C7) | (1ULL << SQ_D7) | (1ULL << SQ_E7) | (1ULL << SQ_F7) |
    (1ULL << SQ_C6) | (1ULL << SQ_D6) | (1ULL << SQ_E6) | (1ULL << SQ_F6) |
    (1ULL << SQ_C5) | (1ULL << SQ_D5) | (1ULL << SQ_E5) | (1ULL << SQ_F5)
  };

  // King danger constants and variables. The king danger scores are taken
  // from the KingDangerTable[]. Various little "meta-bonuses" measuring
  // the strength of the enemy attack are added up into an integer, which
  // is used as an index to KingDangerTable[].
  //
  // King safety evaluation is asymmetrical and different for us (root color)
  // and for our opponent. These values are used to init KingDangerTable.
  const int KingDangerWeights[] = { 259, 247 };

  // KingAttackWeights[PieceType] contains king attack weights by piece type
  const int KingAttackWeights[] = { 0, 0, 2, 2, 3, 5 };

  // Bonuses for enemy's safe checks
  const int QueenContactCheckBonus = 6;
  const int RookContactCheckBonus  = 4;
  const int QueenCheckBonus        = 3;
  const int RookCheckBonus         = 2;
  const int BishopCheckBonus       = 1;
  const int KnightCheckBonus       = 1;

  // InitKingDanger[Square] contains penalties based on the position of the
  // defending king, indexed by king's square (from white's point of view).
  const int InitKingDanger[] = {
     2,  0,  2,  5,  5,  2,  0,  2,
     2,  2,  4,  8,  8,  4,  2,  2,
     7, 10, 12, 12, 12, 12, 10,  7,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15
  };

  // KingDangerTable[Color][attackUnits] contains the actual king danger
  // weighted scores, indexed by color and by a calculated integer number.
  Score KingDangerTable[COLOR_NB][128];

  // TracedTerms[Color][PieceType || TracedType] contains a breakdown of the
  // evaluation terms, used when tracing.
  Score TracedScores[COLOR_NB][16];
  std::stringstream TraceStream;

  enum TracedType {
    PST = 8, IMBALANCE = 9, MOBILITY = 10, THREAT = 11,
    PASSED = 12, UNSTOPPABLE = 13, SPACE = 14, TOTAL = 15
  };

  // Function prototypes
  template<bool Trace>
  Value do_evaluate(const Position& pos, Value& margin);

  template<Color Us>
  void init_eval_info(const Position& pos, EvalInfo& ei);

  template<Color Us, bool Trace>
  Score evaluate_pieces_of_color(const Position& pos, EvalInfo& ei, Score& mobility);

  template<Color Us, bool Trace>
  Score evaluate_king(const Position& pos, EvalInfo& ei, Value margins[]);

  template<Color Us>
  Score evaluate_threats(const Position& pos, EvalInfo& ei);

  template<Color Us>
  int evaluate_space(const Position& pos, EvalInfo& ei);

  template<Color Us>
  Score evaluate_passed_pawns(const Position& pos, EvalInfo& ei);

  Score evaluate_unstoppable_pawns(const Position& pos, EvalInfo& ei);

  Value interpolate(const Score& v, Phase ph, ScaleFactor sf);
  Score weight_option(const std::string& mgOpt, const std::string& egOpt, Score internalWeight);
  double to_cp(Value v);
  void trace_add(int idx, Score term_w, Score term_b = SCORE_ZERO);
  void trace_row(const char* name, int idx);
}


namespace Eval {

  /// evaluate() is the main evaluation function. It always computes two
  /// values, an endgame score and a middle game score, and interpolates
  /// between them based on the remaining material.

  Value evaluate(const Position& pos, Value& margin) {
    return do_evaluate<false>(pos, margin);
  }


  /// init() computes evaluation weights from the corresponding UCI parameters
  /// and setup king tables.

  void init() {

    Weights[Mobility]    = weight_option("Mobility (Middle Game)", "Mobility (Endgame)", WeightsInternal[Mobility]);
    Weights[PassedPawns] = weight_option("Passed Pawns (Middle Game)", "Passed Pawns (Endgame)", WeightsInternal[PassedPawns]);
    Weights[Space]       = weight_option("Space", "Space", WeightsInternal[Space]);

    int KingDanger[] = { KingDangerWeights[0], KingDangerWeights[1] };

    // If running in analysis mode, make sure we use symmetrical king safety.
    // We do so by replacing both KingDanger weights by their average.
    if (Options["UCI_AnalyseMode"])
        KingDanger[0] = KingDanger[1] = (KingDanger[0] + KingDanger[1]) / 2;

    const int MaxSlope = 30;
    const int Peak = 1280;

    for (int t = 0, i = 1; i < 100; i++)
    {
        t = std::min(Peak, std::min(int(0.4 * i * i), t + MaxSlope));

        KingDangerTable[0][i] = apply_weight(make_score(t, 0), make_score(KingDanger[0], 0));
        KingDangerTable[1][i] = apply_weight(make_score(t, 0), make_score(KingDanger[1], 0));
    }
  }


  /// trace() is like evaluate() but instead of a value returns a string suitable
  /// to be print on stdout with the detailed descriptions and values of each
  /// evaluation term. Used mainly for debugging.

  std::string trace(const Position& pos) {

    Value margin;
    std::string totals;

    Search::RootColor = pos.side_to_move();

    TraceStream.str("");
    TraceStream << std::showpoint << std::showpos << std::fixed << std::setprecision(2);
    memset(TracedScores, 0, 2 * 16 * sizeof(Score));

    do_evaluate<true>(pos, margin);

    totals = TraceStream.str();
    TraceStream.str("");

    TraceStream << std::setw(21) << "Eval term " << "|    White    |    Black    |     Total     \n"
                <<             "                     |   MG    EG  |   MG    EG  |   MG     EG   \n"
                <<             "---------------------+-------------+-------------+---------------\n";

    trace_row("Material, PST, Tempo", PST);
    trace_row("Material imbalance", IMBALANCE);
    trace_row("Pawns", PAWN);
    trace_row("Knights", KNIGHT);
    trace_row("Bishops", BISHOP);
    trace_row("Rooks", ROOK);
    trace_row("Queens", QUEEN);
    trace_row("Mobility", MOBILITY);
    trace_row("King safety", KING);
    trace_row("Threats", THREAT);
    trace_row("Passed pawns", PASSED);
    trace_row("Unstoppable pawns", UNSTOPPABLE);
    trace_row("Space", SPACE);

    TraceStream <<             "---------------------+-------------+-------------+---------------\n";
    trace_row("Total", TOTAL);
    TraceStream << totals;

    return TraceStream.str();
  }

} // namespace Eval


namespace {

template<bool Trace>
Value do_evaluate(const Position& pos, Value& margin) {

  assert(!pos.checkers());

  EvalInfo ei;
  Value margins[COLOR_NB];
  Score score, mobilityWhite, mobilityBlack;
  Thread* th = pos.this_thread();

  // margins[] store the uncertainty estimation of position's evaluation
  // that typically is used by the search for pruning decisions.
  margins[WHITE] = margins[BLACK] = VALUE_ZERO;

  // Initialize score by reading the incrementally updated scores included
  // in the position object (material + piece square tables) and adding
  // Tempo bonus. Score is computed from the point of view of white.
  score = pos.psq_score() + (pos.side_to_move() == WHITE ? Tempo : -Tempo);

  // Probe the material hash table
  ei.mi = Material::probe(pos, th->materialTable, th->endgames);
  score += ei.mi->material_value();

  // If we have a specialized evaluation function for the current material
  // configuration, call it and return.
  if (ei.mi->specialized_eval_exists())
  {
      margin = VALUE_ZERO;
      return ei.mi->evaluate(pos);
  }

  // Probe the pawn hash table
  ei.pi = Pawns::probe(pos, th->pawnsTable);
  score += ei.pi->pawns_value();

  // Initialize attack and king safety bitboards
  init_eval_info<WHITE>(pos, ei);
  init_eval_info<BLACK>(pos, ei);

  // Evaluate pieces and mobility
  score +=  evaluate_pieces_of_color<WHITE, Trace>(pos, ei, mobilityWhite)
          - evaluate_pieces_of_color<BLACK, Trace>(pos, ei, mobilityBlack);

  score += apply_weight(mobilityWhite - mobilityBlack, Weights[Mobility]);

  // Evaluate kings after all other pieces because we need complete attack
  // information when computing the king safety evaluation.
  score +=  evaluate_king<WHITE, Trace>(pos, ei, margins)
          - evaluate_king<BLACK, Trace>(pos, ei, margins);

  // Evaluate tactical threats, we need full attack information including king
  score +=  evaluate_threats<WHITE>(pos, ei)
          - evaluate_threats<BLACK>(pos, ei);

  // Evaluate passed pawns, we need full attack information including king
  score +=  evaluate_passed_pawns<WHITE>(pos, ei)
          - evaluate_passed_pawns<BLACK>(pos, ei);

  // If one side has only a king, check whether exists any unstoppable passed pawn
  if (!pos.non_pawn_material(WHITE) || !pos.non_pawn_material(BLACK))
      score += evaluate_unstoppable_pawns(pos, ei);

  // Evaluate space for both sides, only in middle-game.
  if (ei.mi->space_weight())
  {
      int s = evaluate_space<WHITE>(pos, ei) - evaluate_space<BLACK>(pos, ei);
      score += apply_weight(make_score(s * ei.mi->space_weight(), 0), Weights[Space]);
  }

  // Scale winning side if position is more drawish that what it appears
  ScaleFactor sf = eg_value(score) > VALUE_DRAW ? ei.mi->scale_factor(pos, WHITE)
                                                : ei.mi->scale_factor(pos, BLACK);

  // If we don't already have an unusual scale factor, check for opposite
  // colored bishop endgames, and use a lower scale for those.
  if (   ei.mi->game_phase() < PHASE_MIDGAME
      && pos.opposite_bishops()
      && sf == SCALE_FACTOR_NORMAL)
  {
      // Only the two bishops ?
      if (   pos.non_pawn_material(WHITE) == BishopValueMg
          && pos.non_pawn_material(BLACK) == BishopValueMg)
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
  }

  margin = margins[pos.side_to_move()];
  Value v = interpolate(score, ei.mi->game_phase(), sf);

  // In case of tracing add all single evaluation contributions for both white and black
  if (Trace)
  {
      trace_add(PST, pos.psq_score());
      trace_add(IMBALANCE, ei.mi->material_value());
      trace_add(PAWN, ei.pi->pawns_value());
      trace_add(MOBILITY, apply_weight(mobilityWhite, Weights[Mobility]), apply_weight(mobilityBlack, Weights[Mobility]));
      trace_add(THREAT, evaluate_threats<WHITE>(pos, ei), evaluate_threats<BLACK>(pos, ei));
      trace_add(PASSED, evaluate_passed_pawns<WHITE>(pos, ei), evaluate_passed_pawns<BLACK>(pos, ei));
      trace_add(UNSTOPPABLE, evaluate_unstoppable_pawns(pos, ei));
      Score w = make_score(ei.mi->space_weight() * evaluate_space<WHITE>(pos, ei), 0);
      Score b = make_score(ei.mi->space_weight() * evaluate_space<BLACK>(pos, ei), 0);
      trace_add(SPACE, apply_weight(w, Weights[Space]), apply_weight(b, Weights[Space]));
      trace_add(TOTAL, score);
      TraceStream << "\nUncertainty margin: White: " << to_cp(margins[WHITE])
                  << ", Black: " << to_cp(margins[BLACK])
                  << "\nScaling: " << std::noshowpos
                  << std::setw(6) << 100.0 * ei.mi->game_phase() / 128.0 << "% MG, "
                  << std::setw(6) << 100.0 * (1.0 - ei.mi->game_phase() / 128.0) << "% * "
                  << std::setw(6) << (100.0 * sf) / SCALE_FACTOR_NORMAL << "% EG.\n"
                  << "Total evaluation: " << to_cp(v);
  }

  return pos.side_to_move() == WHITE ? v : -v;
}


  // init_eval_info() initializes king bitboards for given color adding
  // pawn attacks. To be done at the beginning of the evaluation.

  template<Color Us>
  void init_eval_info(const Position& pos, EvalInfo& ei) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    Bitboard b = ei.attackedBy[Them][KING] = pos.attacks_from<KING>(pos.king_square(Them));
    ei.attackedBy[Us][PAWN] = ei.pi->pawn_attacks(Us);

    // Init king safety tables only if we are going to use them
    if (   pos.piece_count(Us, QUEEN)
        && pos.non_pawn_material(Us) >= QueenValueMg + RookValueMg)
    {
        ei.kingRing[Them] = (b | (Us == WHITE ? b >> 8 : b << 8));
        b &= ei.attackedBy[Us][PAWN];
        ei.kingAttackersCount[Us] = b ? popcount<Max15>(b) / 2 : 0;
        ei.kingAdjacentZoneAttacksCount[Us] = ei.kingAttackersWeight[Us] = 0;
    } else
        ei.kingRing[Them] = ei.kingAttackersCount[Us] = 0;
  }


  // evaluate_outposts() evaluates bishop and knight outposts squares

  template<PieceType Piece, Color Us>
  Score evaluate_outposts(const Position& pos, EvalInfo& ei, Square s) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    assert (Piece == BISHOP || Piece == KNIGHT);

    // Initial bonus based on square
    Value bonus = OutpostBonus[Piece == BISHOP][relative_square(Us, s)];

    // Increase bonus if supported by pawn, especially if the opponent has
    // no minor piece which can exchange the outpost piece.
    if (bonus && (ei.attackedBy[Us][PAWN] & s))
    {
        if (   !pos.pieces(Them, KNIGHT)
            && !(same_color_squares(s) & pos.pieces(Them, BISHOP)))
            bonus += bonus + bonus / 2;
        else
            bonus += bonus / 2;
    }
    return make_score(bonus, bonus);
  }


  // evaluate_pieces<>() assigns bonuses and penalties to the pieces of a given color

  template<PieceType Piece, Color Us, bool Trace>
  Score evaluate_pieces(const Position& pos, EvalInfo& ei, Score& mobility, Bitboard mobilityArea) {

    Bitboard b;
    Square s, ksq;
    int mob;
    File f;
    Score score = SCORE_ZERO;

    const Color Them = (Us == WHITE ? BLACK : WHITE);
    const Square* pl = pos.piece_list(Us, Piece);

    ei.attackedBy[Us][Piece] = 0;

    while ((s = *pl++) != SQ_NONE)
    {
        // Find attacked squares, including x-ray attacks for bishops and rooks
        if (Piece == KNIGHT || Piece == QUEEN)
            b = pos.attacks_from<Piece>(s);
        else if (Piece == BISHOP)
            b = attacks_bb<BISHOP>(s, pos.pieces() ^ pos.pieces(Us, QUEEN));
        else if (Piece == ROOK)
            b = attacks_bb<ROOK>(s, pos.pieces() ^ pos.pieces(Us, ROOK, QUEEN));
        else
            assert(false);

        ei.attackedBy[Us][Piece] |= b;

        if (b & ei.kingRing[Them])
        {
            ei.kingAttackersCount[Us]++;
            ei.kingAttackersWeight[Us] += KingAttackWeights[Piece];
            Bitboard bb = (b & ei.attackedBy[Them][KING]);
            if (bb)
                ei.kingAdjacentZoneAttacksCount[Us] += popcount<Max15>(bb);
        }

        mob = (Piece != QUEEN ? popcount<Max15>(b & mobilityArea)
                              : popcount<Full >(b & mobilityArea));

        mobility += MobilityBonus[Piece][mob];

        // Add a bonus if a slider is pinning an enemy piece
        if (   (Piece == BISHOP || Piece == ROOK || Piece == QUEEN)
            && (PseudoAttacks[Piece][pos.king_square(Them)] & s))
        {
            b = BetweenBB[s][pos.king_square(Them)] & pos.pieces();

            assert(b);

            if (!more_than_one(b) && (b & pos.pieces(Them)))
                score += ThreatBonus[Piece][type_of(pos.piece_on(lsb(b)))];
        }

        // Decrease score if we are attacked by an enemy pawn. Remaining part
        // of threat evaluation must be done later when we have full attack info.
        if (ei.attackedBy[Them][PAWN] & s)
            score -= ThreatenedByPawnPenalty[Piece];

        // Bishop and knight outposts squares
        if (    (Piece == BISHOP || Piece == KNIGHT)
            && !(pos.pieces(Them, PAWN) & attack_span_mask(Us, s)))
            score += evaluate_outposts<Piece, Us>(pos, ei, s);

        if ((Piece == ROOK || Piece == QUEEN) && relative_rank(Us, s) >= RANK_5)
        {
            // Major piece on 7th rank
            if (   relative_rank(Us, s) == RANK_7
                && relative_rank(Us, pos.king_square(Them)) == RANK_8)
                score += (Piece == ROOK ? RookOn7thBonus : QueenOn7thBonus);

            // Major piece attacking pawns on the same rank
            Bitboard pawns = pos.pieces(Them, PAWN) & rank_bb(s);
            if (pawns)
                score += (Piece == ROOK ? RookOnPawnBonus
                                        : QueenOnPawnBonus) * popcount<Max15>(pawns);
        }

        // Special extra evaluation for bishops
        if (Piece == BISHOP && pos.is_chess960())
        {
            // An important Chess960 pattern: A cornered bishop blocked by
            // a friendly pawn diagonally in front of it is a very serious
            // problem, especially when that pawn is also blocked.
            if (s == relative_square(Us, SQ_A1) || s == relative_square(Us, SQ_H1))
            {
                Square d = pawn_push(Us) + (file_of(s) == FILE_A ? DELTA_E : DELTA_W);
                if (pos.piece_on(s + d) == make_piece(Us, PAWN))
                {
                    if (!pos.is_empty(s + d + pawn_push(Us)))
                        score -= 2*TrappedBishopA1H1Penalty;
                    else if (pos.piece_on(s + 2*d) == make_piece(Us, PAWN))
                        score -= TrappedBishopA1H1Penalty;
                    else
                        score -= TrappedBishopA1H1Penalty / 2;
                }
            }
        }

        // Special extra evaluation for rooks
        if (Piece == ROOK)
        {
            // Open and half-open files
            f = file_of(s);
            if (ei.pi->file_is_half_open(Us, f))
            {
                if (ei.pi->file_is_half_open(Them, f))
                    score += RookOpenFileBonus;
                else
                    score += RookHalfOpenFileBonus;
            }

            // Penalize rooks which are trapped inside a king. Penalize more if
            // king has lost right to castle.
            if (mob > 6 || ei.pi->file_is_half_open(Us, f))
                continue;

            ksq = pos.king_square(Us);

            if (    file_of(ksq) >= FILE_E
                &&  file_of(s) > file_of(ksq)
                && (relative_rank(Us, ksq) == RANK_1 || rank_of(ksq) == rank_of(s)))
            {
                // Is there a half-open file between the king and the edge of the board?
                if (!ei.pi->has_open_file_to_right(Us, file_of(ksq)))
                    score -= make_score(pos.can_castle(Us) ? (TrappedRookPenalty - mob * 16) / 2
                                                           : (TrappedRookPenalty - mob * 16), 0);
            }
            else if (    file_of(ksq) <= FILE_D
                     &&  file_of(s) < file_of(ksq)
                     && (relative_rank(Us, ksq) == RANK_1 || rank_of(ksq) == rank_of(s)))
            {
                // Is there a half-open file between the king and the edge of the board?
                if (!ei.pi->has_open_file_to_left(Us, file_of(ksq)))
                    score -= make_score(pos.can_castle(Us) ? (TrappedRookPenalty - mob * 16) / 2
                                                           : (TrappedRookPenalty - mob * 16), 0);
            }
        }
    }

    if (Trace)
        TracedScores[Us][Piece] = score;

    return score;
  }


  // evaluate_threats<>() assigns bonuses according to the type of attacking piece
  // and the type of attacked one.

  template<Color Us>
  Score evaluate_threats(const Position& pos, EvalInfo& ei) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    Bitboard b, undefendedMinors, weakEnemies;
    Score score = SCORE_ZERO;

    // Undefended minors get penalized even if not under attack
    undefendedMinors =  pos.pieces(Them)
                      & (pos.pieces(BISHOP) | pos.pieces(KNIGHT))
                      & ~ei.attackedBy[Them][0];

    if (undefendedMinors)
        score += more_than_one(undefendedMinors) ? UndefendedMinorPenalty * 2
                                                 : UndefendedMinorPenalty;

    // Enemy pieces not defended by a pawn and under our attack
    weakEnemies =  pos.pieces(Them)
                 & ~ei.attackedBy[Them][PAWN]
                 & ei.attackedBy[Us][0];

    if (!weakEnemies)
        return score;

    // Add bonus according to type of attacked enemy piece and to the
    // type of attacking piece, from knights to queens. Kings are not
    // considered because are already handled in king evaluation.
    for (PieceType pt1 = KNIGHT; pt1 < KING; pt1++)
    {
        b = ei.attackedBy[Us][pt1] & weakEnemies;
        if (b)
            for (PieceType pt2 = PAWN; pt2 < KING; pt2++)
                if (b & pos.pieces(pt2))
                    score += ThreatBonus[pt1][pt2];
    }
    return score;
  }


  // evaluate_pieces_of_color<>() assigns bonuses and penalties to all the
  // pieces of a given color.

  template<Color Us, bool Trace>
  Score evaluate_pieces_of_color(const Position& pos, EvalInfo& ei, Score& mobility) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    Score score = mobility = SCORE_ZERO;

    // Do not include in mobility squares protected by enemy pawns or occupied by our pieces
    const Bitboard mobilityArea = ~(ei.attackedBy[Them][PAWN] | pos.pieces(Us));

    score += evaluate_pieces<KNIGHT, Us, Trace>(pos, ei, mobility, mobilityArea);
    score += evaluate_pieces<BISHOP, Us, Trace>(pos, ei, mobility, mobilityArea);
    score += evaluate_pieces<ROOK,   Us, Trace>(pos, ei, mobility, mobilityArea);
    score += evaluate_pieces<QUEEN,  Us, Trace>(pos, ei, mobility, mobilityArea);

    // Sum up all attacked squares
    ei.attackedBy[Us][0] =   ei.attackedBy[Us][PAWN]   | ei.attackedBy[Us][KNIGHT]
                           | ei.attackedBy[Us][BISHOP] | ei.attackedBy[Us][ROOK]
                           | ei.attackedBy[Us][QUEEN]  | ei.attackedBy[Us][KING];
    return score;
  }


  // evaluate_king<>() assigns bonuses and penalties to a king of a given color

  template<Color Us, bool Trace>
  Score evaluate_king(const Position& pos, EvalInfo& ei, Value margins[]) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    Bitboard undefended, b, b1, b2, safe;
    int attackUnits;
    const Square ksq = pos.king_square(Us);

    // King shelter and enemy pawns storm
    Score score = ei.pi->king_safety<Us>(pos, ksq);

    // King safety. This is quite complicated, and is almost certainly far
    // from optimally tuned.
    if (   ei.kingAttackersCount[Them] >= 2
        && ei.kingAdjacentZoneAttacksCount[Them])
    {
        // Find the attacked squares around the king which has no defenders
        // apart from the king itself
        undefended = ei.attackedBy[Them][0] & ei.attackedBy[Us][KING];
        undefended &= ~(  ei.attackedBy[Us][PAWN]   | ei.attackedBy[Us][KNIGHT]
                        | ei.attackedBy[Us][BISHOP] | ei.attackedBy[Us][ROOK]
                        | ei.attackedBy[Us][QUEEN]);

        // Initialize the 'attackUnits' variable, which is used later on as an
        // index to the KingDangerTable[] array. The initial value is based on
        // the number and types of the enemy's attacking pieces, the number of
        // attacked and undefended squares around our king, the square of the
        // king, and the quality of the pawn shelter.
        attackUnits =  std::min(25, (ei.kingAttackersCount[Them] * ei.kingAttackersWeight[Them]) / 2)
                     + 3 * (ei.kingAdjacentZoneAttacksCount[Them] + popcount<Max15>(undefended))
                     + InitKingDanger[relative_square(Us, ksq)]
                     - mg_value(score) / 32;

        // Analyse enemy's safe queen contact checks. First find undefended
        // squares around the king attacked by enemy queen...
        b = undefended & ei.attackedBy[Them][QUEEN] & ~pos.pieces(Them);
        if (b)
        {
            // ...then remove squares not supported by another enemy piece
            b &= (  ei.attackedBy[Them][PAWN]   | ei.attackedBy[Them][KNIGHT]
                  | ei.attackedBy[Them][BISHOP] | ei.attackedBy[Them][ROOK]);
            if (b)
                attackUnits +=  QueenContactCheckBonus
                              * popcount<Max15>(b)
                              * (Them == pos.side_to_move() ? 2 : 1);
        }

        // Analyse enemy's safe rook contact checks. First find undefended
        // squares around the king attacked by enemy rooks...
        b = undefended & ei.attackedBy[Them][ROOK] & ~pos.pieces(Them);

        // Consider only squares where the enemy rook gives check
        b &= PseudoAttacks[ROOK][ksq];

        if (b)
        {
            // ...then remove squares not supported by another enemy piece
            b &= (  ei.attackedBy[Them][PAWN]   | ei.attackedBy[Them][KNIGHT]
                  | ei.attackedBy[Them][BISHOP] | ei.attackedBy[Them][QUEEN]);
            if (b)
                attackUnits +=  RookContactCheckBonus
                              * popcount<Max15>(b)
                              * (Them == pos.side_to_move() ? 2 : 1);
        }

        // Analyse enemy's safe distance checks for sliders and knights
        safe = ~(pos.pieces(Them) | ei.attackedBy[Us][0]);

        b1 = pos.attacks_from<ROOK>(ksq) & safe;
        b2 = pos.attacks_from<BISHOP>(ksq) & safe;

        // Enemy queen safe checks
        b = (b1 | b2) & ei.attackedBy[Them][QUEEN];
        if (b)
            attackUnits += QueenCheckBonus * popcount<Max15>(b);

        // Enemy rooks safe checks
        b = b1 & ei.attackedBy[Them][ROOK];
        if (b)
            attackUnits += RookCheckBonus * popcount<Max15>(b);

        // Enemy bishops safe checks
        b = b2 & ei.attackedBy[Them][BISHOP];
        if (b)
            attackUnits += BishopCheckBonus * popcount<Max15>(b);

        // Enemy knights safe checks
        b = pos.attacks_from<KNIGHT>(ksq) & ei.attackedBy[Them][KNIGHT] & safe;
        if (b)
            attackUnits += KnightCheckBonus * popcount<Max15>(b);

        // To index KingDangerTable[] attackUnits must be in [0, 99] range
        attackUnits = std::min(99, std::max(0, attackUnits));

        // Finally, extract the king danger score from the KingDangerTable[]
        // array and subtract the score from evaluation. Set also margins[]
        // value that will be used for pruning because this value can sometimes
        // be very big, and so capturing a single attacking piece can therefore
        // result in a score change far bigger than the value of the captured piece.
        score -= KingDangerTable[Us == Search::RootColor][attackUnits];
        margins[Us] += mg_value(KingDangerTable[Us == Search::RootColor][attackUnits]);
    }

    if (Trace)
        TracedScores[Us][KING] = score;

    return score;
  }


  // evaluate_passed_pawns<>() evaluates the passed pawns of the given color

  template<Color Us>
  Score evaluate_passed_pawns(const Position& pos, EvalInfo& ei) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    Bitboard b, squaresToQueen, defendedSquares, unsafeSquares, supportingPawns;
    Score score = SCORE_ZERO;

    b = ei.pi->passed_pawns(Us);

    if (!b)
        return SCORE_ZERO;

    do {
        Square s = pop_lsb(&b);

        assert(pos.pawn_is_passed(Us, s));

        int r = int(relative_rank(Us, s) - RANK_2);
        int rr = r * (r - 1);

        // Base bonus based on rank
        Value mbonus = Value(20 * rr);
        Value ebonus = Value(10 * (rr + r + 1));

        if (rr)
        {
            Square blockSq = s + pawn_push(Us);

            // Adjust bonus based on kings proximity
            ebonus += Value(square_distance(pos.king_square(Them), blockSq) * 5 * rr);
            ebonus -= Value(square_distance(pos.king_square(Us), blockSq) * 2 * rr);

            // If blockSq is not the queening square then consider also a second push
            if (rank_of(blockSq) != (Us == WHITE ? RANK_8 : RANK_1))
                ebonus -= Value(square_distance(pos.king_square(Us), blockSq + pawn_push(Us)) * rr);

            // If the pawn is free to advance, increase bonus
            if (pos.is_empty(blockSq))
            {
                squaresToQueen = forward_bb(Us, s);
                defendedSquares = squaresToQueen & ei.attackedBy[Us][0];

                // If there is an enemy rook or queen attacking the pawn from behind,
                // add all X-ray attacks by the rook or queen. Otherwise consider only
                // the squares in the pawn's path attacked or occupied by the enemy.
                if (   (forward_bb(Them, s) & pos.pieces(Them, ROOK, QUEEN))
                    && (forward_bb(Them, s) & pos.pieces(Them, ROOK, QUEEN) & pos.attacks_from<ROOK>(s)))
                    unsafeSquares = squaresToQueen;
                else
                    unsafeSquares = squaresToQueen & (ei.attackedBy[Them][0] | pos.pieces(Them));

                // If there aren't enemy attacks or pieces along the path to queen give
                // huge bonus. Even bigger if we protect the pawn's path.
                if (!unsafeSquares)
                    ebonus += Value(rr * (squaresToQueen == defendedSquares ? 17 : 15));
                else
                    // OK, there are enemy attacks or pieces (but not pawns). Are those
                    // squares which are attacked by the enemy also attacked by us ?
                    // If yes, big bonus (but smaller than when there are no enemy attacks),
                    // if no, somewhat smaller bonus.
                    ebonus += Value(rr * ((unsafeSquares & defendedSquares) == unsafeSquares ? 13 : 8));
            }
        } // rr != 0

        // Increase the bonus if the passed pawn is supported by a friendly pawn
        // on the same rank and a bit smaller if it's on the previous rank.
        supportingPawns = pos.pieces(Us, PAWN) & adjacent_files_bb(file_of(s));
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
        if (file_of(s) == FILE_A || file_of(s) == FILE_H)
        {
            if (pos.non_pawn_material(Them) <= KnightValueMg)
                ebonus += ebonus / 4;
            else if (pos.pieces(Them, ROOK, QUEEN))
                ebonus -= ebonus / 4;
        }
        score += make_score(mbonus, ebonus);

    } while (b);

    // Add the scores to the middle game and endgame eval
    return apply_weight(score, Weights[PassedPawns]);
  }


  // evaluate_unstoppable_pawns() evaluates the unstoppable passed pawns for both sides, this is quite
  // conservative and returns a winning score only when we are very sure that the pawn is winning.

  Score evaluate_unstoppable_pawns(const Position& pos, EvalInfo& ei) {

    Bitboard b, b2, blockers, supporters, queeningPath, candidates;
    Square s, blockSq, queeningSquare;
    Color c, winnerSide, loserSide;
    bool pathDefended, opposed;
    int pliesToGo, movesToGo, oppMovesToGo, sacptg, blockersCount, minKingDist, kingptg, d;
    int pliesToQueen[] = { 256, 256 };

    // Step 1. Hunt for unstoppable passed pawns. If we find at least one,
    // record how many plies are required for promotion.
    for (c = WHITE; c <= BLACK; c++)
    {
        // Skip if other side has non-pawn pieces
        if (pos.non_pawn_material(~c))
            continue;

        b = ei.pi->passed_pawns(c);

        while (b)
        {
            s = pop_lsb(&b);
            queeningSquare = relative_square(c, file_of(s) | RANK_8);
            queeningPath = forward_bb(c, s);

            // Compute plies to queening and check direct advancement
            movesToGo = rank_distance(s, queeningSquare) - int(relative_rank(c, s) == RANK_2);
            oppMovesToGo = square_distance(pos.king_square(~c), queeningSquare) - int(c != pos.side_to_move());
            pathDefended = ((ei.attackedBy[c][0] & queeningPath) == queeningPath);

            if (movesToGo >= oppMovesToGo && !pathDefended)
                continue;

            // Opponent king cannot block because path is defended and position
            // is not in check. So only friendly pieces can be blockers.
            assert(!pos.checkers());
            assert((queeningPath & pos.pieces()) == (queeningPath & pos.pieces(c)));

            // Add moves needed to free the path from friendly pieces and retest condition
            movesToGo += popcount<Max15>(queeningPath & pos.pieces(c));

            if (movesToGo >= oppMovesToGo && !pathDefended)
                continue;

            pliesToGo = 2 * movesToGo - int(c == pos.side_to_move());
            pliesToQueen[c] = std::min(pliesToQueen[c], pliesToGo);
        }
    }

    // Step 2. If either side cannot promote at least three plies before the other side then situation
    // becomes too complex and we give up. Otherwise we determine the possibly "winning side"
    if (abs(pliesToQueen[WHITE] - pliesToQueen[BLACK]) < 3)
        return SCORE_ZERO;

    winnerSide = (pliesToQueen[WHITE] < pliesToQueen[BLACK] ? WHITE : BLACK);
    loserSide = ~winnerSide;

    // Step 3. Can the losing side possibly create a new passed pawn and thus prevent the loss?
    b = candidates = pos.pieces(loserSide, PAWN);

    while (b)
    {
        s = pop_lsb(&b);

        // Compute plies from queening
        queeningSquare = relative_square(loserSide, file_of(s) | RANK_8);
        movesToGo = rank_distance(s, queeningSquare) - int(relative_rank(loserSide, s) == RANK_2);
        pliesToGo = 2 * movesToGo - int(loserSide == pos.side_to_move());

        // Check if (without even considering any obstacles) we're too far away or doubled
        if (   pliesToQueen[winnerSide] + 3 <= pliesToGo
            || (forward_bb(loserSide, s) & pos.pieces(loserSide, PAWN)))
            candidates ^= s;
    }

    // If any candidate is already a passed pawn it _may_ promote in time. We give up.
    if (candidates & ei.pi->passed_pawns(loserSide))
        return SCORE_ZERO;

    // Step 4. Check new passed pawn creation through king capturing and pawn sacrifices
    b = candidates;

    while (b)
    {
        s = pop_lsb(&b);
        sacptg = blockersCount = 0;
        minKingDist = kingptg = 256;

        // Compute plies from queening
        queeningSquare = relative_square(loserSide, file_of(s) | RANK_8);
        movesToGo = rank_distance(s, queeningSquare) - int(relative_rank(loserSide, s) == RANK_2);
        pliesToGo = 2 * movesToGo - int(loserSide == pos.side_to_move());

        // Generate list of blocking pawns and supporters
        supporters = adjacent_files_bb(file_of(s)) & candidates;
        opposed = forward_bb(loserSide, s) & pos.pieces(winnerSide, PAWN);
        blockers = passed_pawn_mask(loserSide, s) & pos.pieces(winnerSide, PAWN);

        assert(blockers);

        // How many plies does it take to remove all the blocking pawns?
        while (blockers)
        {
            blockSq = pop_lsb(&blockers);
            movesToGo = 256;

            // Check pawns that can give support to overcome obstacle, for instance
            // black pawns: a4, b4 white: b2 then pawn in b4 is giving support.
            if (!opposed)
            {
                b2 = supporters & in_front_bb(winnerSide, blockSq + pawn_push(winnerSide));

                while (b2) // This while-loop could be replaced with LSB/MSB (depending on color)
                {
                    d = square_distance(blockSq, pop_lsb(&b2)) - 2;
                    movesToGo = std::min(movesToGo, d);
                }
            }

            // Check pawns that can be sacrificed against the blocking pawn
            b2 = attack_span_mask(winnerSide, blockSq) & candidates & ~(1ULL << s);

            while (b2) // This while-loop could be replaced with LSB/MSB (depending on color)
            {
                d = square_distance(blockSq, pop_lsb(&b2)) - 2;
                movesToGo = std::min(movesToGo, d);
            }

            // If obstacle can be destroyed with an immediate pawn exchange / sacrifice,
            // it's not a real obstacle and we have nothing to add to pliesToGo.
            if (movesToGo <= 0)
                continue;

            // Plies needed to sacrifice against all the blocking pawns
            sacptg += movesToGo * 2;
            blockersCount++;

            // Plies needed for the king to capture all the blocking pawns
            d = square_distance(pos.king_square(loserSide), blockSq);
            minKingDist = std::min(minKingDist, d);
            kingptg = (minKingDist + blockersCount) * 2;
        }

        // Check if pawn sacrifice plan _may_ save the day
        if (pliesToQueen[winnerSide] + 3 > pliesToGo + sacptg)
            return SCORE_ZERO;

        // Check if king capture plan _may_ save the day (contains some false positives)
        if (pliesToQueen[winnerSide] + 3 > pliesToGo + kingptg)
            return SCORE_ZERO;
    }

    // Winning pawn is unstoppable and will promote as first, return big score
    Score score = make_score(0, (Value) 1280 - 32 * pliesToQueen[winnerSide]);
    return winnerSide == WHITE ? score : -score;
  }


  // evaluate_space() computes the space evaluation for a given side. The
  // space evaluation is a simple bonus based on the number of safe squares
  // available for minor pieces on the central four files on ranks 2--4. Safe
  // squares one, two or three squares behind a friendly pawn are counted
  // twice. Finally, the space bonus is scaled by a weight taken from the
  // material hash table. The aim is to improve play on game opening.
  template<Color Us>
  int evaluate_space(const Position& pos, EvalInfo& ei) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    // Find the safe squares for our pieces inside the area defined by
    // SpaceMask[]. A square is unsafe if it is attacked by an enemy
    // pawn, or if it is undefended and attacked by an enemy piece.
    Bitboard safe =   SpaceMask[Us]
                   & ~pos.pieces(Us, PAWN)
                   & ~ei.attackedBy[Them][PAWN]
                   & (ei.attackedBy[Us][0] | ~ei.attackedBy[Them][0]);

    // Find all squares which are at most three squares behind some friendly pawn
    Bitboard behind = pos.pieces(Us, PAWN);
    behind |= (Us == WHITE ? behind >>  8 : behind <<  8);
    behind |= (Us == WHITE ? behind >> 16 : behind << 16);

    // Since SpaceMask[Us] is fully on our half of the board
    assert(unsigned(safe >> (Us == WHITE ? 32 : 0)) == 0);

    // Count safe + (behind & safe) with a single popcount
    return popcount<Full>((Us == WHITE ? safe << 32 : safe >> 32) | (behind & safe));
  }


  // interpolate() interpolates between a middle game and an endgame score,
  // based on game phase. It also scales the return value by a ScaleFactor array.

  Value interpolate(const Score& v, Phase ph, ScaleFactor sf) {

    assert(mg_value(v) > -VALUE_INFINITE && mg_value(v) < VALUE_INFINITE);
    assert(eg_value(v) > -VALUE_INFINITE && eg_value(v) < VALUE_INFINITE);
    assert(ph >= PHASE_ENDGAME && ph <= PHASE_MIDGAME);

    int ev = (eg_value(v) * int(sf)) / SCALE_FACTOR_NORMAL;
    int result = (mg_value(v) * int(ph) + ev * int(128 - ph)) / 128;
    return Value((result + GrainSize / 2) & ~(GrainSize - 1));
  }


  // weight_option() computes the value of an evaluation weight, by combining
  // two UCI-configurable weights (midgame and endgame) with an internal weight.

  Score weight_option(const std::string& mgOpt, const std::string& egOpt, Score internalWeight) {

    // Scale option value from 100 to 256
    int mg = Options[mgOpt] * 256 / 100;
    int eg = Options[egOpt] * 256 / 100;

    return apply_weight(make_score(mg, eg), internalWeight);
  }


  // A couple of little helpers used by tracing code, to_cp() converts a value to
  // a double in centipawns scale, trace_add() stores white and black scores.

  double to_cp(Value v) { return double(v) / double(PawnValueMg); }

  void trace_add(int idx, Score wScore, Score bScore) {

    TracedScores[WHITE][idx] = wScore;
    TracedScores[BLACK][idx] = bScore;
  }


  // trace_row() is an helper function used by tracing code to register the
  // values of a single evaluation term.

  void trace_row(const char* name, int idx) {

    Score wScore = TracedScores[WHITE][idx];
    Score bScore = TracedScores[BLACK][idx];

    switch (idx) {
    case PST: case IMBALANCE: case PAWN: case UNSTOPPABLE: case TOTAL:
        TraceStream << std::setw(20) << name << " |   ---   --- |   ---   --- | "
                    << std::setw(6)  << to_cp(mg_value(wScore)) << " "
                    << std::setw(6)  << to_cp(eg_value(wScore)) << " \n";
        break;
    default:
        TraceStream << std::setw(20) << name << " | " << std::noshowpos
                    << std::setw(5)  << to_cp(mg_value(wScore)) << " "
                    << std::setw(5)  << to_cp(eg_value(wScore)) << " | "
                    << std::setw(5)  << to_cp(mg_value(bScore)) << " "
                    << std::setw(5)  << to_cp(eg_value(bScore)) << " | "
                    << std::showpos
                    << std::setw(6)  << to_cp(mg_value(wScore - bScore)) << " "
                    << std::setw(6)  << to_cp(eg_value(wScore - bScore)) << " \n";
    }
  }
}
