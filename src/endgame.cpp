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

#include "bitbase.h"
#include "bitcount.h"
#include "endgame.h"


////
//// Local definitions
////

namespace {

  // Table used to drive the defending king towards the edge of the board
  // in KX vs K and KQ vs KR endgames.
  const uint8_t MateTable[64] = {
    100, 90, 80, 70, 70, 80, 90, 100,
     90, 70, 60, 50, 50, 60, 70,  90,
     80, 60, 40, 30, 30, 40, 60,  80,
     70, 50, 30, 20, 20, 30, 50,  70,
     70, 50, 30, 20, 20, 30, 50,  70,
     80, 60, 40, 30, 30, 40, 60,  80,
     90, 70, 60, 50, 50, 60, 70,  90,
    100, 90, 80, 70, 70, 80, 90, 100,
  };

  // Table used to drive the defending king towards a corner square of the
  // right color in KBN vs K endgames.
  const uint8_t KBNKMateTable[64] = {
    200, 190, 180, 170, 160, 150, 140, 130,
    190, 180, 170, 160, 150, 140, 130, 140,
    180, 170, 155, 140, 140, 125, 140, 150,
    170, 160, 140, 120, 110, 140, 150, 160,
    160, 150, 140, 110, 120, 140, 160, 170,
    150, 140, 125, 140, 140, 155, 170, 180,
    140, 130, 140, 150, 160, 170, 180, 190,
    130, 140, 150, 160, 170, 180, 190, 200
  };

  // The attacking side is given a descending bonus based on distance between
  // the two kings in basic endgames.
  const int DistanceBonus[8] = { 0, 0, 100, 80, 60, 40, 20, 10 };

  // Bitbase for KP vs K
  uint8_t KPKBitbase[24576];

  // Penalty for big distance between king and knight for the defending king
  // and knight in KR vs KN endgames.
  const int KRKNKingKnightDistancePenalty[8] = { 0, 0, 4, 10, 20, 32, 48, 70 };

  // Various inline functions for accessing the above arrays
  inline Value mate_table(Square s) {
    return Value(MateTable[s]);
  }

  inline Value kbnk_mate_table(Square s) {
    return Value(KBNKMateTable[s]);
  }

  inline Value distance_bonus(int d) {
    return Value(DistanceBonus[d]);
  }

  inline Value krkn_king_knight_distance_penalty(int d) {
    return Value(KRKNKingKnightDistancePenalty[d]);
  }

  // Function for probing the KP vs K bitbase
  int probe_kpk(Square wksq, Square wpsq, Square bksq, Color stm);

}


////
//// Functions
////

/// Mate with KX vs K. This function is used to evaluate positions with
/// King and plenty of material vs a lone king. It simply gives the
/// attacking side a bonus for driving the defending king towards the edge
/// of the board, and for keeping the distance between the two kings small.
template<>
Value EvaluationFunction<KXK>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(weakerSide) == Value(0));
  assert(pos.piece_count(weakerSide, PAWN) == Value(0));

  Square winnerKSq = pos.king_square(strongerSide);
  Square loserKSq = pos.king_square(weakerSide);

  Value result =   pos.non_pawn_material(strongerSide)
                 + pos.piece_count(strongerSide, PAWN) * PawnValueEndgame
                 + mate_table(loserKSq)
                 + distance_bonus(square_distance(winnerKSq, loserKSq));

  if (   pos.piece_count(strongerSide, QUEEN) > 0
      || pos.piece_count(strongerSide, ROOK) > 0
      || pos.piece_count(strongerSide, BISHOP) > 1)
      // TODO: check for two equal-colored bishops!
      result += VALUE_KNOWN_WIN;

  return (strongerSide == pos.side_to_move() ? result : -result);
}


/// Mate with KBN vs K. This is similar to KX vs K, but we have to drive the
/// defending king towards a corner square of the right color.
template<>
Value EvaluationFunction<KBNK>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(weakerSide) == Value(0));
  assert(pos.piece_count(weakerSide, PAWN) == Value(0));
  assert(pos.non_pawn_material(strongerSide) == KnightValueMidgame + BishopValueMidgame);
  assert(pos.piece_count(strongerSide, BISHOP) == 1);
  assert(pos.piece_count(strongerSide, KNIGHT) == 1);
  assert(pos.piece_count(strongerSide, PAWN) == 0);

  Square winnerKSq = pos.king_square(strongerSide);
  Square loserKSq = pos.king_square(weakerSide);
  Square bishopSquare = pos.piece_list(strongerSide, BISHOP, 0);

  if (square_color(bishopSquare) == BLACK)
  {
      winnerKSq = flop_square(winnerKSq);
      loserKSq = flop_square(loserKSq);
  }

  Value result =  VALUE_KNOWN_WIN
                + distance_bonus(square_distance(winnerKSq, loserKSq))
                + kbnk_mate_table(loserKSq);

  return (strongerSide == pos.side_to_move() ? result : -result);
}


/// KP vs K. This endgame is evaluated with the help of a bitbase.
template<>
Value EvaluationFunction<KPK>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == Value(0));
  assert(pos.non_pawn_material(weakerSide) == Value(0));
  assert(pos.piece_count(strongerSide, PAWN) == 1);
  assert(pos.piece_count(weakerSide, PAWN) == 0);

  Square wksq, bksq, wpsq;
  Color stm;

  if (strongerSide == WHITE)
  {
      wksq = pos.king_square(WHITE);
      bksq = pos.king_square(BLACK);
      wpsq = pos.piece_list(WHITE, PAWN, 0);
      stm = pos.side_to_move();
  }
  else
  {
      wksq = flip_square(pos.king_square(BLACK));
      bksq = flip_square(pos.king_square(WHITE));
      wpsq = flip_square(pos.piece_list(BLACK, PAWN, 0));
      stm = opposite_color(pos.side_to_move());
  }

  if (square_file(wpsq) >= FILE_E)
  {
    wksq = flop_square(wksq);
    bksq = flop_square(bksq);
    wpsq = flop_square(wpsq);
  }

  if (!probe_kpk(wksq, wpsq, bksq, stm))
      return VALUE_DRAW;

  Value result =  VALUE_KNOWN_WIN
                + PawnValueEndgame
                + Value(square_rank(wpsq));

  return (strongerSide == pos.side_to_move() ? result : -result);
}


/// KR vs KP. This is a somewhat tricky endgame to evaluate precisely without
/// a bitbase. The function below returns drawish scores when the pawn is
/// far advanced with support of the king, while the attacking king is far
/// away.
template<>
Value EvaluationFunction<KRKP>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == RookValueMidgame);
  assert(pos.piece_count(strongerSide, PAWN) == 0);
  assert(pos.non_pawn_material(weakerSide) == 0);
  assert(pos.piece_count(weakerSide, PAWN) == 1);

  Square wksq, wrsq, bksq, bpsq;
  int tempo = (pos.side_to_move() == strongerSide);

  wksq = pos.king_square(strongerSide);
  wrsq = pos.piece_list(strongerSide, ROOK, 0);
  bksq = pos.king_square(weakerSide);
  bpsq = pos.piece_list(weakerSide, PAWN, 0);

  if (strongerSide == BLACK)
  {
      wksq = flip_square(wksq);
      wrsq = flip_square(wrsq);
      bksq = flip_square(bksq);
      bpsq = flip_square(bpsq);
  }

  Square queeningSq = make_square(square_file(bpsq), RANK_1);
  Value result;

  // If the stronger side's king is in front of the pawn, it's a win
  if (wksq < bpsq && square_file(wksq) == square_file(bpsq))
      result = RookValueEndgame - Value(square_distance(wksq, bpsq));

  // If the weaker side's king is too far from the pawn and the rook,
  // it's a win
  else if (   square_distance(bksq, bpsq) - (tempo^1) >= 3
           && square_distance(bksq, wrsq) >= 3)
      result = RookValueEndgame - Value(square_distance(wksq, bpsq));

  // If the pawn is far advanced and supported by the defending king,
  // the position is drawish
  else if (   square_rank(bksq) <= RANK_3
           && square_distance(bksq, bpsq) == 1
           && square_rank(wksq) >= RANK_4
           && square_distance(wksq, bpsq) - tempo > 2)
      result = Value(80 - square_distance(wksq, bpsq) * 8);

  else
      result =  Value(200)
              - Value(square_distance(wksq, bpsq + DELTA_S) * 8)
              + Value(square_distance(bksq, bpsq + DELTA_S) * 8)
              + Value(square_distance(bpsq, queeningSq) * 8);

  return (strongerSide == pos.side_to_move() ? result : -result);
}


/// KR vs KB. This is very simple, and always returns drawish scores.  The
/// score is slightly bigger when the defending king is close to the edge.
template<>
Value EvaluationFunction<KRKB>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == RookValueMidgame);
  assert(pos.piece_count(strongerSide, PAWN) == 0);
  assert(pos.non_pawn_material(weakerSide) == BishopValueMidgame);
  assert(pos.piece_count(weakerSide, PAWN) == 0);
  assert(pos.piece_count(weakerSide, BISHOP) == 1);

  Value result = mate_table(pos.king_square(weakerSide));
  return (pos.side_to_move() == strongerSide ? result : -result);
}


/// KR vs KN.  The attacking side has slightly better winning chances than
/// in KR vs KB, particularly if the king and the knight are far apart.
template<>
Value EvaluationFunction<KRKN>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == RookValueMidgame);
  assert(pos.piece_count(strongerSide, PAWN) == 0);
  assert(pos.non_pawn_material(weakerSide) == KnightValueMidgame);
  assert(pos.piece_count(weakerSide, PAWN) == 0);
  assert(pos.piece_count(weakerSide, KNIGHT) == 1);

  Square defendingKSq = pos.king_square(weakerSide);
  Square nSq = pos.piece_list(weakerSide, KNIGHT, 0);

  Value result = Value(10) + mate_table(defendingKSq) +
    krkn_king_knight_distance_penalty(square_distance(defendingKSq, nSq));

  return (strongerSide == pos.side_to_move())? result : -result;
}


/// KQ vs KR.  This is almost identical to KX vs K:  We give the attacking
/// king a bonus for having the kings close together, and for forcing the
/// defending king towards the edge.  If we also take care to avoid null move
/// for the defending side in the search, this is usually sufficient to be
/// able to win KQ vs KR.
template<>
Value EvaluationFunction<KQKR>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == QueenValueMidgame);
  assert(pos.piece_count(strongerSide, PAWN) == 0);
  assert(pos.non_pawn_material(weakerSide) == RookValueMidgame);
  assert(pos.piece_count(weakerSide, PAWN) == 0);

  Square winnerKSq = pos.king_square(strongerSide);
  Square loserKSq = pos.king_square(weakerSide);

  Value result =  QueenValueEndgame
                - RookValueEndgame
                + mate_table(loserKSq)
                + distance_bonus(square_distance(winnerKSq, loserKSq));

  return (strongerSide == pos.side_to_move())? result : -result;
}

template<>
Value EvaluationFunction<KBBKN>::apply(const Position& pos) const {

  assert(pos.piece_count(strongerSide, BISHOP) == 2);
  assert(pos.non_pawn_material(strongerSide) == 2*BishopValueMidgame);
  assert(pos.piece_count(weakerSide, KNIGHT) == 1);
  assert(pos.non_pawn_material(weakerSide) == KnightValueMidgame);
  assert(pos.pieces(PAWN) == EmptyBoardBB);

  Value result = BishopValueEndgame;
  Square wksq = pos.king_square(strongerSide);
  Square bksq = pos.king_square(weakerSide);
  Square nsq = pos.piece_list(weakerSide, KNIGHT, 0);

  // Bonus for attacking king close to defending king
  result += distance_bonus(square_distance(wksq, bksq));

  // Bonus for driving the defending king and knight apart
  result += Value(square_distance(bksq, nsq) * 32);

  // Bonus for restricting the knight's mobility
  result += Value((8 - count_1s_max_15(pos.attacks_from<KNIGHT>(nsq))) * 8);

  return (strongerSide == pos.side_to_move() ? result : -result);
}


/// K and two minors vs K and one or two minors or K and two knights against
/// king alone are always draw.
template<>
Value EvaluationFunction<KmmKm>::apply(const Position&) const {
  return Value(0);
}

template<>
Value EvaluationFunction<KNNK>::apply(const Position&) const {
  return Value(0);
}

/// KBPKScalingFunction scales endgames where the stronger side has king,
/// bishop and one or more pawns. It checks for draws with rook pawns and a
/// bishop of the wrong color. If such a draw is detected, ScaleFactor(0) is
/// returned. If not, the return value is SCALE_FACTOR_NONE, i.e. no scaling
/// will be used.
template<>
ScaleFactor ScalingFunction<KBPsK>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == BishopValueMidgame);
  assert(pos.piece_count(strongerSide, BISHOP) == 1);
  assert(pos.piece_count(strongerSide, PAWN) >= 1);

  // No assertions about the material of weakerSide, because we want draws to
  // be detected even when the weaker side has some pawns.

  Bitboard pawns = pos.pieces(PAWN, strongerSide);
  File pawnFile = square_file(pos.piece_list(strongerSide, PAWN, 0));

  // All pawns are on a single rook file ?
  if (   (pawnFile == FILE_A || pawnFile == FILE_H)
      && (pawns & ~file_bb(pawnFile)) == EmptyBoardBB)
  {
      Square bishopSq = pos.piece_list(strongerSide, BISHOP, 0);
      Square queeningSq = relative_square(strongerSide, make_square(pawnFile, RANK_8));
      Square kingSq = pos.king_square(weakerSide);

      if (   square_color(queeningSq) != square_color(bishopSq)
          && file_distance(square_file(kingSq), pawnFile) <= 1)
      {
          // The bishop has the wrong color, and the defending king is on the
          // file of the pawn(s) or the neighboring file. Find the rank of the
          // frontmost pawn.
          Rank rank;
          if (strongerSide == WHITE)
          {
              for (rank = RANK_7; (rank_bb(rank) & pawns) == EmptyBoardBB; rank--) {}
              assert(rank >= RANK_2 && rank <= RANK_7);
          }
          else
          {
              for(rank = RANK_2; (rank_bb(rank) & pawns) == EmptyBoardBB; rank++) {}
              rank = Rank(rank^7);  // HACK to get the relative rank
              assert(rank >= RANK_2 && rank <= RANK_7);
          }
          // If the defending king has distance 1 to the promotion square or
          // is placed somewhere in front of the pawn, it's a draw.
          if (   square_distance(kingSq, queeningSq) <= 1
              || relative_rank(strongerSide, kingSq) >= rank)
              return ScaleFactor(0);
      }
  }
  return SCALE_FACTOR_NONE;
}


/// KQKRPScalingFunction scales endgames where the stronger side has only
/// king and queen, while the weaker side has at least a rook and a pawn.
/// It tests for fortress draws with a rook on the third rank defended by
/// a pawn.
template<>
ScaleFactor ScalingFunction<KQKRPs>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == QueenValueMidgame);
  assert(pos.piece_count(strongerSide, QUEEN) == 1);
  assert(pos.piece_count(strongerSide, PAWN) == 0);
  assert(pos.piece_count(weakerSide, ROOK) == 1);
  assert(pos.piece_count(weakerSide, PAWN) >= 1);

  Square kingSq = pos.king_square(weakerSide);
  if (   relative_rank(weakerSide, kingSq) <= RANK_2
      && relative_rank(weakerSide, pos.king_square(strongerSide)) >= RANK_4
      && (pos.pieces(ROOK, weakerSide) & relative_rank_bb(weakerSide, RANK_3))
      && (pos.pieces(PAWN, weakerSide) & relative_rank_bb(weakerSide, RANK_2))
      && (pos.attacks_from<KING>(kingSq) & pos.pieces(PAWN, weakerSide)))
  {
      Square rsq = pos.piece_list(weakerSide, ROOK, 0);
      if (pos.attacks_from<PAWN>(rsq, strongerSide) & pos.pieces(PAWN, weakerSide))
          return ScaleFactor(0);
  }
  return SCALE_FACTOR_NONE;
}


/// KRPKRScalingFunction scales KRP vs KR endgames. This function knows a
/// handful of the most important classes of drawn positions, but is far
/// from perfect. It would probably be a good idea to add more knowledge
/// in the future.
///
/// It would also be nice to rewrite the actual code for this function,
/// which is mostly copied from Glaurung 1.x, and not very pretty.
template<>
ScaleFactor ScalingFunction<KRPKR>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == RookValueMidgame);
  assert(pos.piece_count(strongerSide, PAWN) == 1);
  assert(pos.non_pawn_material(weakerSide) == RookValueMidgame);
  assert(pos.piece_count(weakerSide, PAWN) == 0);

  Square wksq = pos.king_square(strongerSide);
  Square wrsq = pos.piece_list(strongerSide, ROOK, 0);
  Square wpsq = pos.piece_list(strongerSide, PAWN, 0);
  Square bksq = pos.king_square(weakerSide);
  Square brsq = pos.piece_list(weakerSide, ROOK, 0);

  // Orient the board in such a way that the stronger side is white, and the
  // pawn is on the left half of the board.
  if (strongerSide == BLACK)
  {
      wksq = flip_square(wksq);
      wrsq = flip_square(wrsq);
      wpsq = flip_square(wpsq);
      bksq = flip_square(bksq);
      brsq = flip_square(brsq);
  }
  if (square_file(wpsq) > FILE_D)
  {
      wksq = flop_square(wksq);
      wrsq = flop_square(wrsq);
      wpsq = flop_square(wpsq);
      bksq = flop_square(bksq);
      brsq = flop_square(brsq);
  }

  File f = square_file(wpsq);
  Rank r = square_rank(wpsq);
  Square queeningSq = make_square(f, RANK_8);
  int tempo = (pos.side_to_move() == strongerSide);

  // If the pawn is not too far advanced and the defending king defends the
  // queening square, use the third-rank defence.
  if (   r <= RANK_5
      && square_distance(bksq, queeningSq) <= 1
      && wksq <= SQ_H5
      && (square_rank(brsq) == RANK_6 || (r <= RANK_3 && square_rank(wrsq) != RANK_6)))
      return ScaleFactor(0);

  // The defending side saves a draw by checking from behind in case the pawn
  // has advanced to the 6th rank with the king behind.
  if (   r == RANK_6
      && square_distance(bksq, queeningSq) <= 1
      && square_rank(wksq) + tempo <= RANK_6
      && (square_rank(brsq) == RANK_1 || (!tempo && abs(square_file(brsq) - f) >= 3)))
      return ScaleFactor(0);

  if (   r >= RANK_6
      && bksq == queeningSq
      && square_rank(brsq) == RANK_1
      && (!tempo || square_distance(wksq, wpsq) >= 2))
      return ScaleFactor(0);

  // White pawn on a7 and rook on a8 is a draw if black's king is on g7 or h7
  // and the black rook is behind the pawn.
  if (   wpsq == SQ_A7
      && wrsq == SQ_A8
      && (bksq == SQ_H7 || bksq == SQ_G7)
      && square_file(brsq) == FILE_A
      && (square_rank(brsq) <= RANK_3 || square_file(wksq) >= FILE_D || square_rank(wksq) <= RANK_5))
      return ScaleFactor(0);

  // If the defending king blocks the pawn and the attacking king is too far
  // away, it's a draw.
  if (   r <= RANK_5
      && bksq == wpsq + DELTA_N
      && square_distance(wksq, wpsq) - tempo >= 2
      && square_distance(wksq, brsq) - tempo >= 2)
      return ScaleFactor(0);

  // Pawn on the 7th rank supported by the rook from behind usually wins if the
  // attacking king is closer to the queening square than the defending king,
  // and the defending king cannot gain tempi by threatening the attacking rook.
  if (   r == RANK_7
      && f != FILE_A
      && square_file(wrsq) == f
      && wrsq != queeningSq
      && (square_distance(wksq, queeningSq) < square_distance(bksq, queeningSq) - 2 + tempo)
      && (square_distance(wksq, queeningSq) < square_distance(bksq, wrsq) + tempo))
      return ScaleFactor(SCALE_FACTOR_MAX - 2 * square_distance(wksq, queeningSq));

  // Similar to the above, but with the pawn further back
  if (   f != FILE_A
      && square_file(wrsq) == f
      && wrsq < wpsq
      && (square_distance(wksq, queeningSq) < square_distance(bksq, queeningSq) - 2 + tempo)
      && (square_distance(wksq, wpsq + DELTA_N) < square_distance(bksq, wpsq + DELTA_N) - 2 + tempo)
      && (  square_distance(bksq, wrsq) + tempo >= 3
          || (    square_distance(wksq, queeningSq) < square_distance(bksq, wrsq) + tempo
              && (square_distance(wksq, wpsq + DELTA_N) < square_distance(bksq, wrsq) + tempo))))
      return ScaleFactor(  SCALE_FACTOR_MAX
                         - (8 * square_distance(wpsq, queeningSq)
                         + 2 * square_distance(wksq, queeningSq)));

  // If the pawn is not far advanced, and the defending king is somewhere in
  // the pawn's path, it's probably a draw.
  if (r <= RANK_4 && bksq > wpsq)
  {
      if (square_file(bksq) == square_file(wpsq))
          return ScaleFactor(10);
      if (   abs(square_file(bksq) - square_file(wpsq)) == 1
          && square_distance(wksq, bksq) > 2)
          return ScaleFactor(24 - 2 * square_distance(wksq, bksq));
  }
  return SCALE_FACTOR_NONE;
}


/// KRPPKRPScalingFunction scales KRPP vs KRP endgames. There is only a
/// single pattern: If the stronger side has no pawns and the defending king
/// is actively placed, the position is drawish.
template<>
ScaleFactor ScalingFunction<KRPPKRP>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == RookValueMidgame);
  assert(pos.piece_count(strongerSide, PAWN) == 2);
  assert(pos.non_pawn_material(weakerSide) == RookValueMidgame);
  assert(pos.piece_count(weakerSide, PAWN) == 1);

  Square wpsq1 = pos.piece_list(strongerSide, PAWN, 0);
  Square wpsq2 = pos.piece_list(strongerSide, PAWN, 1);
  Square bksq = pos.king_square(weakerSide);

  // Does the stronger side have a passed pawn?
  if (   pos.pawn_is_passed(strongerSide, wpsq1)
      || pos.pawn_is_passed(strongerSide, wpsq2))
      return SCALE_FACTOR_NONE;

  Rank r = Max(relative_rank(strongerSide, wpsq1), relative_rank(strongerSide, wpsq2));

  if (   file_distance(bksq, wpsq1) <= 1
      && file_distance(bksq, wpsq2) <= 1
      && relative_rank(strongerSide, bksq) > r)
  {
      switch (r) {
      case RANK_2: return ScaleFactor(10);
      case RANK_3: return ScaleFactor(10);
      case RANK_4: return ScaleFactor(15);
      case RANK_5: return ScaleFactor(20);
      case RANK_6: return ScaleFactor(40);
      default: assert(false);
      }
  }
  return SCALE_FACTOR_NONE;
}


/// KPsKScalingFunction scales endgames with king and two or more pawns
/// against king. There is just a single rule here: If all pawns are on
/// the same rook file and are blocked by the defending king, it's a draw.
template<>
ScaleFactor ScalingFunction<KPsK>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == Value(0));
  assert(pos.piece_count(strongerSide, PAWN) >= 2);
  assert(pos.non_pawn_material(weakerSide) == Value(0));
  assert(pos.piece_count(weakerSide, PAWN) == 0);

  Bitboard pawns = pos.pieces(PAWN, strongerSide);

  // Are all pawns on the 'a' file?
  if ((pawns & ~FileABB) == EmptyBoardBB)
  {
      // Does the defending king block the pawns?
      Square ksq = pos.king_square(weakerSide);
      if (square_distance(ksq, relative_square(strongerSide, SQ_A8)) <= 1)
          return ScaleFactor(0);
      else if(   square_file(ksq) == FILE_A
              && (in_front_bb(strongerSide, ksq) & pawns) == EmptyBoardBB)
          return ScaleFactor(0);
      else
          return SCALE_FACTOR_NONE;
  }
  // Are all pawns on the 'h' file?
  else if ((pawns & ~FileHBB) == EmptyBoardBB)
  {
    // Does the defending king block the pawns?
    Square ksq = pos.king_square(weakerSide);
    if (square_distance(ksq, relative_square(strongerSide, SQ_H8)) <= 1)
        return ScaleFactor(0);
    else if (   square_file(ksq) == FILE_H
             && (in_front_bb(strongerSide, ksq) & pawns) == EmptyBoardBB)
        return ScaleFactor(0);
    else
        return SCALE_FACTOR_NONE;
  }
  else
      return SCALE_FACTOR_NONE;
}


/// KBPKBScalingFunction scales KBP vs KB endgames. There are two rules:
/// If the defending king is somewhere along the path of the pawn, and the
/// square of the king is not of the same color as the stronger side's bishop,
/// it's a draw. If the two bishops have opposite color, it's almost always
/// a draw.
template<>
ScaleFactor ScalingFunction<KBPKB>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == BishopValueMidgame);
  assert(pos.piece_count(strongerSide, BISHOP) == 1);
  assert(pos.piece_count(strongerSide, PAWN) == 1);
  assert(pos.non_pawn_material(weakerSide) == BishopValueMidgame);
  assert(pos.piece_count(weakerSide, BISHOP) == 1);
  assert(pos.piece_count(weakerSide, PAWN) == 0);

  Square pawnSq = pos.piece_list(strongerSide, PAWN, 0);
  Square strongerBishopSq = pos.piece_list(strongerSide, BISHOP, 0);
  Square weakerBishopSq = pos.piece_list(weakerSide, BISHOP, 0);
  Square weakerKingSq = pos.king_square(weakerSide);

  // Case 1: Defending king blocks the pawn, and cannot be driven away
  if (   square_file(weakerKingSq) == square_file(pawnSq)
      && relative_rank(strongerSide, pawnSq) < relative_rank(strongerSide, weakerKingSq)
      && (   square_color(weakerKingSq) != square_color(strongerBishopSq)
          || relative_rank(strongerSide, weakerKingSq) <= RANK_6))
      return ScaleFactor(0);

  // Case 2: Opposite colored bishops
  if (square_color(strongerBishopSq) != square_color(weakerBishopSq))
  {
      // We assume that the position is drawn in the following three situations:
      //
      //   a. The pawn is on rank 5 or further back.
      //   b. The defending king is somewhere in the pawn's path.
      //   c. The defending bishop attacks some square along the pawn's path,
      //      and is at least three squares away from the pawn.
      //
      // These rules are probably not perfect, but in practice they work
      // reasonably well.

      if (relative_rank(strongerSide, pawnSq) <= RANK_5)
          return ScaleFactor(0);
      else
      {
          Bitboard ray = ray_bb(pawnSq, (strongerSide == WHITE)? SIGNED_DIR_N : SIGNED_DIR_S);
          if (ray & pos.pieces(KING, weakerSide))
              return ScaleFactor(0);
          if(  (pos.attacks_from<BISHOP>(weakerBishopSq) & ray)
             && square_distance(weakerBishopSq, pawnSq) >= 3)
              return ScaleFactor(0);
      }
  }
  return SCALE_FACTOR_NONE;
}


/// KBPPKBScalingFunction scales KBPP vs KB endgames. It detects a few basic
/// draws with opposite-colored bishops.
template<>
ScaleFactor ScalingFunction<KBPPKB>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == BishopValueMidgame);
  assert(pos.piece_count(strongerSide, BISHOP) == 1);
  assert(pos.piece_count(strongerSide, PAWN) == 2);
  assert(pos.non_pawn_material(weakerSide) == BishopValueMidgame);
  assert(pos.piece_count(weakerSide, BISHOP) == 1);
  assert(pos.piece_count(weakerSide, PAWN) == 0);

  Square wbsq = pos.piece_list(strongerSide, BISHOP, 0);
  Square bbsq = pos.piece_list(weakerSide, BISHOP, 0);

  if (square_color(wbsq) == square_color(bbsq))
      // Not opposite-colored bishops, no scaling
      return SCALE_FACTOR_NONE;

  Square ksq = pos.king_square(weakerSide);
  Square psq1 = pos.piece_list(strongerSide, PAWN, 0);
  Square psq2 = pos.piece_list(strongerSide, PAWN, 1);
  Rank r1 = square_rank(psq1);
  Rank r2 = square_rank(psq2);
  Square blockSq1, blockSq2;

  if (relative_rank(strongerSide, psq1) > relative_rank(strongerSide, psq2))
  {
      blockSq1 = psq1 + pawn_push(strongerSide);
      blockSq2 = make_square(square_file(psq2), square_rank(psq1));
  }
  else
  {
      blockSq1 = psq2 + pawn_push(strongerSide);
      blockSq2 = make_square(square_file(psq1), square_rank(psq2));
  }

  switch (file_distance(psq1, psq2))
  {
  case 0:
    // Both pawns are on the same file. Easy draw if defender firmly controls
    // some square in the frontmost pawn's path.
    if (   square_file(ksq) == square_file(blockSq1)
        && relative_rank(strongerSide, ksq) >= relative_rank(strongerSide, blockSq1)
        && square_color(ksq) != square_color(wbsq))
        return ScaleFactor(0);
    else
        return SCALE_FACTOR_NONE;

  case 1:
    // Pawns on neighboring files. Draw if defender firmly controls the square
    // in front of the frontmost pawn's path, and the square diagonally behind
    // this square on the file of the other pawn.
    if (   ksq == blockSq1
        && square_color(ksq) != square_color(wbsq)
        && (   bbsq == blockSq2
            || (pos.attacks_from<BISHOP>(blockSq2) & pos.pieces(BISHOP, weakerSide))
            || rank_distance(r1, r2) >= 2))
        return ScaleFactor(0);
    else if (   ksq == blockSq2
             && square_color(ksq) != square_color(wbsq)
             && (   bbsq == blockSq1
                 || (pos.attacks_from<BISHOP>(blockSq1) & pos.pieces(BISHOP, weakerSide))))
        return ScaleFactor(0);
    else
        return SCALE_FACTOR_NONE;

  default:
    // The pawns are not on the same file or adjacent files. No scaling.
    return SCALE_FACTOR_NONE;
  }
}


/// KBPKNScalingFunction scales KBP vs KN endgames. There is a single rule:
/// If the defending king is somewhere along the path of the pawn, and the
/// square of the king is not of the same color as the stronger side's bishop,
/// it's a draw.
template<>
ScaleFactor ScalingFunction<KBPKN>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == BishopValueMidgame);
  assert(pos.piece_count(strongerSide, BISHOP) == 1);
  assert(pos.piece_count(strongerSide, PAWN) == 1);
  assert(pos.non_pawn_material(weakerSide) == KnightValueMidgame);
  assert(pos.piece_count(weakerSide, KNIGHT) == 1);
  assert(pos.piece_count(weakerSide, PAWN) == 0);

  Square pawnSq = pos.piece_list(strongerSide, PAWN, 0);
  Square strongerBishopSq = pos.piece_list(strongerSide, BISHOP, 0);
  Square weakerKingSq = pos.king_square(weakerSide);

  if (   square_file(weakerKingSq) == square_file(pawnSq)
      && relative_rank(strongerSide, pawnSq) < relative_rank(strongerSide, weakerKingSq)
      && (   square_color(weakerKingSq) != square_color(strongerBishopSq)
          || relative_rank(strongerSide, weakerKingSq) <= RANK_6))
      return ScaleFactor(0);

  return SCALE_FACTOR_NONE;
}


/// KNPKScalingFunction scales KNP vs K endgames. There is a single rule:
/// If the pawn is a rook pawn on the 7th rank and the defending king prevents
/// the pawn from advancing, the position is drawn.
template<>
ScaleFactor ScalingFunction<KNPK>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == KnightValueMidgame);
  assert(pos.piece_count(strongerSide, KNIGHT) == 1);
  assert(pos.piece_count(strongerSide, PAWN) == 1);
  assert(pos.non_pawn_material(weakerSide) == Value(0));
  assert(pos.piece_count(weakerSide, PAWN) == 0);

  Square pawnSq = pos.piece_list(strongerSide, PAWN, 0);
  Square weakerKingSq = pos.king_square(weakerSide);

  if (   pawnSq == relative_square(strongerSide, SQ_A7)
      && square_distance(weakerKingSq, relative_square(strongerSide, SQ_A8)) <= 1)
      return ScaleFactor(0);

  if (   pawnSq == relative_square(strongerSide, SQ_H7)
      && square_distance(weakerKingSq, relative_square(strongerSide, SQ_H8)) <= 1)
      return ScaleFactor(0);

  return SCALE_FACTOR_NONE;
}


/// KPKPScalingFunction scales KP vs KP endgames. This is done by removing
/// the weakest side's pawn and probing the KP vs K bitbase: If the weakest
/// side has a draw without the pawn, she probably has at least a draw with
/// the pawn as well. The exception is when the stronger side's pawn is far
/// advanced and not on a rook file; in this case it is often possible to win
/// (e.g. 8/4k3/3p4/3P4/6K1/8/8/8 w - - 0 1).
template<>
ScaleFactor ScalingFunction<KPKP>::apply(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == Value(0));
  assert(pos.non_pawn_material(weakerSide) == Value(0));
  assert(pos.piece_count(WHITE, PAWN) == 1);
  assert(pos.piece_count(BLACK, PAWN) == 1);

  Square wksq, bksq, wpsq;
  Color stm;

  if (strongerSide == WHITE)
  {
      wksq = pos.king_square(WHITE);
      bksq = pos.king_square(BLACK);
      wpsq = pos.piece_list(WHITE, PAWN, 0);
      stm = pos.side_to_move();
  }
  else
  {
      wksq = flip_square(pos.king_square(BLACK));
      bksq = flip_square(pos.king_square(WHITE));
      wpsq = flip_square(pos.piece_list(BLACK, PAWN, 0));
      stm = opposite_color(pos.side_to_move());
  }

  if (square_file(wpsq) >= FILE_E)
  {
      wksq = flop_square(wksq);
      bksq = flop_square(bksq);
      wpsq = flop_square(wpsq);
  }

  // If the pawn has advanced to the fifth rank or further, and is not a
  // rook pawn, it's too dangerous to assume that it's at least a draw.
  if (   square_rank(wpsq) >= RANK_5
      && square_file(wpsq) != FILE_A)
      return SCALE_FACTOR_NONE;

  // Probe the KPK bitbase with the weakest side's pawn removed. If it's a
  // draw, it's probably at least a draw even with the pawn.
  if (probe_kpk(wksq, wpsq, bksq, stm))
      return SCALE_FACTOR_NONE;
  else
      return ScaleFactor(0);
}


/// init_bitbases() is called during program initialization, and simply loads
/// bitbases from disk into memory.  At the moment, there is only the bitbase
/// for KP vs K, but we may decide to add other bitbases later.

void init_bitbases() {
  generate_kpk_bitbase(KPKBitbase);
}


namespace {

  // Probe the KP vs K bitbase:

  int probe_kpk(Square wksq, Square wpsq, Square bksq, Color stm) {

    int wp = int(square_file(wpsq)) + (int(square_rank(wpsq)) - 1) * 4;
    int index = int(stm) + 2*int(bksq) + 128*int(wksq) + 8192*wp;

    assert(index >= 0 && index < 24576*8);
    return KPKBitbase[index/8] & (1 << (index&7));
  }
}
