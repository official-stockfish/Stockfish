/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2021 The Stockfish developers (see AUTHORS file)

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
#include "endgame.h"
#include "movegen.h"

namespace Stockfish {

namespace {

  // Used to drive the king towards the edge of the board
  // in KX vs K and KQ vs KR endgames.
  // Values range from 27 (center squares) to 90 (in the corners)
  inline int push_to_edge(Square s) {
      int rd = edge_distance(rank_of(s)), fd = edge_distance(file_of(s));
      return 90 - (7 * fd * fd / 2 + 7 * rd * rd / 2);
  }

  // Used to drive the king towards A1H8 corners in KBN vs K endgames.
  // Values range from 0 on A8H1 diagonal to 7 in A1H8 corners
  inline int push_to_corner(Square s) {
      return abs(7 - rank_of(s) - file_of(s));
  }

  // Drive a piece close to or away from another piece
  inline int push_close(Square s1, Square s2) { return 140 - 20 * distance(s1, s2); }
  inline int push_away(Square s1, Square s2) { return 120 - push_close(s1, s2); }

#ifndef NDEBUG
  bool verify_material(const Position& pos, Color c, Value npm, int pawnsCnt) {
    return pos.non_pawn_material(c) == npm && pos.count<PAWN>(c) == pawnsCnt;
  }
#endif

  // Map the square as if strongSide is white and strongSide's only pawn
  // is on the left half of the board.
  Square normalize(const Position& pos, Color strongSide, Square sq) {

    assert(pos.count<PAWN>(strongSide) == 1);

    if (file_of(pos.square<PAWN>(strongSide)) >= FILE_E)
        sq = flip_file(sq);

    return strongSide == WHITE ? sq : flip_rank(sq);
  }

} // namespace


namespace Endgames {

  std::pair<Map<Value>, Map<ScaleFactor>> maps;

  void init() {

    add<KPK>("KPK");
    add<KNNK>("KNNK");
    add<KBNK>("KBNK");
    add<KRKP>("KRKP");
    add<KRKB>("KRKB");
    add<KRKN>("KRKN");
    add<KQKP>("KQKP");
    add<KQKR>("KQKR");
    add<KNNKP>("KNNKP");

    add<KRPKR>("KRPKR");
    add<KRPKB>("KRPKB");
    add<KBPKB>("KBPKB");
    add<KBPKN>("KBPKN");
    add<KBPPKB>("KBPPKB");
    add<KRPPKRP>("KRPPKRP");
  }
}


/// Mate with KX vs K. This function is used to evaluate positions with
/// king and plenty of material vs a lone king. It simply gives the
/// attacking side a bonus for driving the defending king towards the edge
/// of the board, and for keeping the distance between the two kings small.
template<>
Value Endgame<KXK>::operator()(const Position& pos) const {

  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));
  assert(!pos.checkers()); // Eval is never called when in check

  // Stalemate detection with lone king
  if (pos.side_to_move() == weakSide && !MoveList<LEGAL>(pos).size())
      return VALUE_DRAW;

  Square strongKing = pos.square<KING>(strongSide);
  Square weakKing   = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + push_to_edge(weakKing)
                + push_close(strongKing, weakKing);

  if (   pos.count<QUEEN>(strongSide)
      || pos.count<ROOK>(strongSide)
      ||(pos.count<BISHOP>(strongSide) && pos.count<KNIGHT>(strongSide))
      || (   (pos.pieces(strongSide, BISHOP) & ~DarkSquares)
          && (pos.pieces(strongSide, BISHOP) &  DarkSquares)))
      result = std::min(result + VALUE_KNOWN_WIN, VALUE_TB_WIN_IN_MAX_PLY - 1);

  return strongSide == pos.side_to_move() ? result : -result;
}


/// Mate with KBN vs K. This is similar to KX vs K, but we have to drive the
/// defending king towards a corner square that our bishop attacks.
template<>
Value Endgame<KBNK>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + BishopValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  Square strongKing   = pos.square<KING>(strongSide);
  Square strongBishop = pos.square<BISHOP>(strongSide);
  Square weakKing     = pos.square<KING>(weakSide);

  // If our bishop does not attack A1/H8, we flip the enemy king square
  // to drive to opposite corners (A8/H1).

  Value result =  (VALUE_KNOWN_WIN + 3520)
                + push_close(strongKing, weakKing)
                + 420 * push_to_corner(opposite_colors(strongBishop, SQ_A1) ? flip_file(weakKing) : weakKing);

  assert(abs(result) < VALUE_TB_WIN_IN_MAX_PLY);
  return strongSide == pos.side_to_move() ? result : -result;
}


/// KP vs K. This endgame is evaluated with the help of a bitbase
template<>
Value Endgame<KPK>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, VALUE_ZERO, 1));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  // Assume strongSide is white and the pawn is on files A-D
  Square strongKing = normalize(pos, strongSide, pos.square<KING>(strongSide));
  Square strongPawn = normalize(pos, strongSide, pos.square<PAWN>(strongSide));
  Square weakKing   = normalize(pos, strongSide, pos.square<KING>(weakSide));

  Color us = strongSide == pos.side_to_move() ? WHITE : BLACK;

  if (!Bitbases::probe(strongKing, strongPawn, weakKing, us))
      return VALUE_DRAW;

  Value result = VALUE_KNOWN_WIN + PawnValueEg + Value(rank_of(strongPawn));

  return strongSide == pos.side_to_move() ? result : -result;
}


/// KR vs KP. This is a somewhat tricky endgame to evaluate precisely without
/// a bitbase. The function below returns drawish scores when the pawn is
/// far advanced with support of the king, while the attacking king is far
/// away.
template<>
Value Endgame<KRKP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 1));

  Square strongKing = pos.square<KING>(strongSide);
  Square weakKing   = pos.square<KING>(weakSide);
  Square strongRook = pos.square<ROOK>(strongSide);
  Square weakPawn   = pos.square<PAWN>(weakSide);
  Square queeningSquare = make_square(file_of(weakPawn), relative_rank(weakSide, RANK_8));
  Value result;

  // If the stronger side's king is in front of the pawn, it's a win
  if (forward_file_bb(strongSide, strongKing) & weakPawn)
      result = RookValueEg - distance(strongKing, weakPawn);

  // If the weaker side's king is too far from the pawn and the rook,
  // it's a win.
  else if (   distance(weakKing, weakPawn) >= 3 + (pos.side_to_move() == weakSide)
           && distance(weakKing, strongRook) >= 3)
      result = RookValueEg - distance(strongKing, weakPawn);

  // If the pawn is far advanced and supported by the defending king,
  // the position is drawish
  else if (   relative_rank(strongSide, weakKing) <= RANK_3
           && distance(weakKing, weakPawn) == 1
           && relative_rank(strongSide, strongKing) >= RANK_4
           && distance(strongKing, weakPawn) > 2 + (pos.side_to_move() == strongSide))
      result = Value(80) - 8 * distance(strongKing, weakPawn);

  else
      result =  Value(200) - 8 * (  distance(strongKing, weakPawn + pawn_push(weakSide))
                                  - distance(weakKing, weakPawn + pawn_push(weakSide))
                                  - distance(weakPawn, queeningSquare));

  return strongSide == pos.side_to_move() ? result : -result;
}


/// KR vs KB. This is very simple, and always returns drawish scores. The
/// score is slightly bigger when the defending king is close to the edge.
template<>
Value Endgame<KRKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg, 0));

  Value result = Value(push_to_edge(pos.square<KING>(weakSide)));
  return strongSide == pos.side_to_move() ? result : -result;
}


/// KR vs KN. The attacking side has slightly better winning chances than
/// in KR vs KB, particularly if the king and the knight are far apart.
template<>
Value Endgame<KRKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Square weakKing   = pos.square<KING>(weakSide);
  Square weakKnight = pos.square<KNIGHT>(weakSide);
  Value result = Value(push_to_edge(weakKing) + push_away(weakKing, weakKnight));
  return strongSide == pos.side_to_move() ? result : -result;
}


/// KQ vs KP. In general, this is a win for the stronger side, but there are a
/// few important exceptions. A pawn on 7th rank and on the A,C,F or H files
/// with a king positioned next to it can be a draw, so in that case, we only
/// use the distance between the kings.
template<>
Value Endgame<KQKP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, QueenValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 1));

  Square strongKing = pos.square<KING>(strongSide);
  Square weakKing   = pos.square<KING>(weakSide);
  Square weakPawn   = pos.square<PAWN>(weakSide);

  Value result = Value(push_close(strongKing, weakKing));

  if (   relative_rank(weakSide, weakPawn) != RANK_7
      || distance(weakKing, weakPawn) != 1
      || ((FileBBB | FileDBB | FileEBB | FileGBB) & weakPawn))
      result += QueenValueEg - PawnValueEg;

  return strongSide == pos.side_to_move() ? result : -result;
}


/// KQ vs KR. This is almost identical to KX vs K: we give the attacking
/// king a bonus for having the kings close together, and for forcing the
/// defending king towards the edge. If we also take care to avoid null move for
/// the defending side in the search, this is usually sufficient to win KQ vs KR.
template<>
Value Endgame<KQKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, QueenValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Square strongKing = pos.square<KING>(strongSide);
  Square weakKing   = pos.square<KING>(weakSide);

  Value result =  QueenValueEg
                - RookValueEg
                + push_to_edge(weakKing)
                + push_close(strongKing, weakKing);

  return strongSide == pos.side_to_move() ? result : -result;
}


/// KNN vs KP. Very drawish, but there are some mate opportunities if we can
/// press the weakSide King to a corner before the pawn advances too much.
template<>
Value Endgame<KNNKP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, 2 * KnightValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 1));

  Square weakKing = pos.square<KING>(weakSide);
  Square weakPawn = pos.square<PAWN>(weakSide);

  Value result =      PawnValueEg
               +  2 * push_to_edge(weakKing)
               - 10 * relative_rank(weakSide, weakPawn);

  return strongSide == pos.side_to_move() ? result : -result;
}


/// Some cases of trivial draws
template<> Value Endgame<KNNK>::operator()(const Position&) const { return VALUE_DRAW; }


/// KB and one or more pawns vs K. It checks for draws with rook pawns and
/// a bishop of the wrong color. If such a draw is detected, SCALE_FACTOR_DRAW
/// is returned. If not, the return value is SCALE_FACTOR_NONE, i.e. no scaling
/// will be used.
template<>
ScaleFactor Endgame<KBPsK>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongSide) == BishopValueMg);
  assert(pos.count<PAWN>(strongSide) >= 1);

  // No assertions about the material of weakSide, because we want draws to
  // be detected even when the weaker side has some pawns.

  Bitboard strongPawns = pos.pieces(strongSide, PAWN);
  Bitboard allPawns = pos.pieces(PAWN);

  Square strongBishop = pos.square<BISHOP>(strongSide);
  Square weakKing = pos.square<KING>(weakSide);
  Square strongKing = pos.square<KING>(strongSide);

  // All strongSide pawns are on a single rook file?
  if (!(strongPawns & ~FileABB) || !(strongPawns & ~FileHBB))
  {
      Square queeningSquare = relative_square(strongSide, make_square(file_of(lsb(strongPawns)), RANK_8));

      if (   opposite_colors(queeningSquare, strongBishop)
          && distance(queeningSquare, weakKing) <= 1)
          return SCALE_FACTOR_DRAW;
  }

  // If all the pawns are on the same B or G file, then it's potentially a draw
  if ((!(allPawns & ~FileBBB) || !(allPawns & ~FileGBB))
      && pos.non_pawn_material(weakSide) == 0
      && pos.count<PAWN>(weakSide) >= 1)
  {
      // Get the least advanced weakSide pawn
      Square weakPawn = frontmost_sq(strongSide, pos.pieces(weakSide, PAWN));

      // There's potential for a draw if our pawn is blocked on the 7th rank,
      // the bishop cannot attack it or they only have one pawn left.
      if (   relative_rank(strongSide, weakPawn) == RANK_7
          && (strongPawns & (weakPawn + pawn_push(weakSide)))
          && (opposite_colors(strongBishop, weakPawn) || !more_than_one(strongPawns)))
      {
          int strongKingDist = distance(weakPawn, strongKing);
          int weakKingDist = distance(weakPawn, weakKing);

          // It's a draw if the weak king is on its back two ranks, within 2
          // squares of the blocking pawn and the strong king is not
          // closer. (I think this rule only fails in practically
          // unreachable positions such as 5k1K/6p1/6P1/8/8/3B4/8/8 w
          // and positions where qsearch will immediately correct the
          // problem such as 8/4k1p1/6P1/1K6/3B4/8/8/8 w).
          if (   relative_rank(strongSide, weakKing) >= RANK_7
              && weakKingDist <= 2
              && weakKingDist <= strongKingDist)
              return SCALE_FACTOR_DRAW;
      }
  }

  return SCALE_FACTOR_NONE;
}


/// KQ vs KR and one or more pawns. It tests for fortress draws with a rook on
/// the third rank defended by a pawn.
template<>
ScaleFactor Endgame<KQKRPs>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, QueenValueMg, 0));
  assert(pos.count<ROOK>(weakSide) == 1);
  assert(pos.count<PAWN>(weakSide) >= 1);

  Square strongKing = pos.square<KING>(strongSide);
  Square weakKing   = pos.square<KING>(weakSide);
  Square weakRook   = pos.square<ROOK>(weakSide);

  if (    relative_rank(weakSide,   weakKing) <= RANK_2
      &&  relative_rank(weakSide, strongKing) >= RANK_4
      &&  relative_rank(weakSide,   weakRook) == RANK_3
      && (  pos.pieces(weakSide, PAWN)
          & attacks_bb<KING>(weakKing)
          & pawn_attacks_bb(strongSide, weakRook)))
          return SCALE_FACTOR_DRAW;

  return SCALE_FACTOR_NONE;
}


/// KRP vs KR. This function knows a handful of the most important classes of
/// drawn positions, but is far from perfect. It would probably be a good idea
/// to add more knowledge in the future.
///
/// It would also be nice to rewrite the actual code for this function,
/// which is mostly copied from Glaurung 1.x, and isn't very pretty.
template<>
ScaleFactor Endgame<KRPKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 1));
  assert(verify_material(pos, weakSide,   RookValueMg, 0));

  // Assume strongSide is white and the pawn is on files A-D
  Square strongKing = normalize(pos, strongSide, pos.square<KING>(strongSide));
  Square strongRook = normalize(pos, strongSide, pos.square<ROOK>(strongSide));
  Square strongPawn = normalize(pos, strongSide, pos.square<PAWN>(strongSide));
  Square weakKing = normalize(pos, strongSide, pos.square<KING>(weakSide));
  Square weakRook = normalize(pos, strongSide, pos.square<ROOK>(weakSide));

  File pawnFile = file_of(strongPawn);
  Rank pawnRank = rank_of(strongPawn);
  Square queeningSquare = make_square(pawnFile, RANK_8);
  int tempo = (pos.side_to_move() == strongSide);

  // If the pawn is not too far advanced and the defending king defends the
  // queening square, use the third-rank defence.
  if (   pawnRank <= RANK_5
      && distance(weakKing, queeningSquare) <= 1
      && strongKing <= SQ_H5
      && (rank_of(weakRook) == RANK_6 || (pawnRank <= RANK_3 && rank_of(strongRook) != RANK_6)))
      return SCALE_FACTOR_DRAW;

  // The defending side saves a draw by checking from behind in case the pawn
  // has advanced to the 6th rank with the king behind.
  if (   pawnRank == RANK_6
      && distance(weakKing, queeningSquare) <= 1
      && rank_of(strongKing) + tempo <= RANK_6
      && (rank_of(weakRook) == RANK_1 || (!tempo && distance<File>(weakRook, strongPawn) >= 3)))
      return SCALE_FACTOR_DRAW;

  if (   pawnRank >= RANK_6
      && weakKing == queeningSquare
      && rank_of(weakRook) == RANK_1
      && (!tempo || distance(strongKing, strongPawn) >= 2))
      return SCALE_FACTOR_DRAW;

  // White pawn on a7 and rook on a8 is a draw if black's king is on g7 or h7
  // and the black rook is behind the pawn.
  if (   strongPawn == SQ_A7
      && strongRook == SQ_A8
      && (weakKing == SQ_H7 || weakKing == SQ_G7)
      && file_of(weakRook) == FILE_A
      && (rank_of(weakRook) <= RANK_3 || file_of(strongKing) >= FILE_D || rank_of(strongKing) <= RANK_5))
      return SCALE_FACTOR_DRAW;

  // If the defending king blocks the pawn and the attacking king is too far
  // away, it's a draw.
  if (   pawnRank <= RANK_5
      && weakKing == strongPawn + NORTH
      && distance(strongKing, strongPawn) - tempo >= 2
      && distance(strongKing, weakRook) - tempo >= 2)
      return SCALE_FACTOR_DRAW;

  // Pawn on the 7th rank supported by the rook from behind usually wins if the
  // attacking king is closer to the queening square than the defending king,
  // and the defending king cannot gain tempi by threatening the attacking rook.
  if (   pawnRank == RANK_7
      && pawnFile != FILE_A
      && file_of(strongRook) == pawnFile
      && strongRook != queeningSquare
      && (distance(strongKing, queeningSquare) < distance(weakKing, queeningSquare) - 2 + tempo)
      && (distance(strongKing, queeningSquare) < distance(weakKing, strongRook) + tempo))
      return ScaleFactor(SCALE_FACTOR_MAX - 2 * distance(strongKing, queeningSquare));

  // Similar to the above, but with the pawn further back
  if (   pawnFile != FILE_A
      && file_of(strongRook) == pawnFile
      && strongRook < strongPawn
      && (distance(strongKing, queeningSquare) < distance(weakKing, queeningSquare) - 2 + tempo)
      && (distance(strongKing, strongPawn + NORTH) < distance(weakKing, strongPawn + NORTH) - 2 + tempo)
      && (  distance(weakKing, strongRook) + tempo >= 3
          || (    distance(strongKing, queeningSquare) < distance(weakKing, strongRook) + tempo
              && (distance(strongKing, strongPawn + NORTH) < distance(weakKing, strongPawn) + tempo))))
      return ScaleFactor(  SCALE_FACTOR_MAX
                         - 8 * distance(strongPawn, queeningSquare)
                         - 2 * distance(strongKing, queeningSquare));

  // If the pawn is not far advanced and the defending king is somewhere in
  // the pawn's path, it's probably a draw.
  if (pawnRank <= RANK_4 && weakKing > strongPawn)
  {
      if (file_of(weakKing) == file_of(strongPawn))
          return ScaleFactor(10);
      if (   distance<File>(weakKing, strongPawn) == 1
          && distance(strongKing, weakKing) > 2)
          return ScaleFactor(24 - 2 * distance(strongKing, weakKing));
  }
  return SCALE_FACTOR_NONE;
}

template<>
ScaleFactor Endgame<KRPKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 1));
  assert(verify_material(pos, weakSide, BishopValueMg, 0));

  // Test for a rook pawn
  if (pos.pieces(PAWN) & (FileABB | FileHBB))
  {
      Square weakKing = pos.square<KING>(weakSide);
      Square weakBishop = pos.square<BISHOP>(weakSide);
      Square strongKing = pos.square<KING>(strongSide);
      Square strongPawn = pos.square<PAWN>(strongSide);
      Rank pawnRank = relative_rank(strongSide, strongPawn);
      Direction push = pawn_push(strongSide);

      // If the pawn is on the 5th rank and the pawn (currently) is on
      // the same color square as the bishop then there is a chance of
      // a fortress. Depending on the king position give a moderate
      // reduction or a stronger one if the defending king is near the
      // corner but not trapped there.
      if (pawnRank == RANK_5 && !opposite_colors(weakBishop, strongPawn))
      {
          int d = distance(strongPawn + 3 * push, weakKing);

          if (d <= 2 && !(d == 0 && weakKing == strongKing + 2 * push))
              return ScaleFactor(24);
          else
              return ScaleFactor(48);
      }

      // When the pawn has moved to the 6th rank we can be fairly sure
      // it's drawn if the bishop attacks the square in front of the
      // pawn from a reasonable distance and the defending king is near
      // the corner
      if (   pawnRank == RANK_6
          && distance(strongPawn + 2 * push, weakKing) <= 1
          && (attacks_bb<BISHOP>(weakBishop) & (strongPawn + push))
          && distance<File>(weakBishop, strongPawn) >= 2)
          return ScaleFactor(8);
  }

  return SCALE_FACTOR_NONE;
}

/// KRPP vs KRP. There is just a single rule: if the stronger side has no passed
/// pawns and the defending king is actively placed, the position is drawish.
template<>
ScaleFactor Endgame<KRPPKRP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 2));
  assert(verify_material(pos, weakSide,   RookValueMg, 1));

  Square strongPawn1 = lsb(pos.pieces(strongSide, PAWN));
  Square strongPawn2 = msb(pos.pieces(strongSide, PAWN));
  Square weakKing = pos.square<KING>(weakSide);

  // Does the stronger side have a passed pawn?
  if (pos.pawn_passed(strongSide, strongPawn1) || pos.pawn_passed(strongSide, strongPawn2))
      return SCALE_FACTOR_NONE;

  Rank pawnRank = std::max(relative_rank(strongSide, strongPawn1), relative_rank(strongSide, strongPawn2));

  if (   distance<File>(weakKing, strongPawn1) <= 1
      && distance<File>(weakKing, strongPawn2) <= 1
      && relative_rank(strongSide, weakKing) > pawnRank)
  {
      assert(pawnRank > RANK_1 && pawnRank < RANK_7);
      return ScaleFactor(7 * pawnRank);
  }
  return SCALE_FACTOR_NONE;
}


/// K and two or more pawns vs K. There is just a single rule here: if all pawns
/// are on the same rook file and are blocked by the defending king, it's a draw.
template<>
ScaleFactor Endgame<KPsK>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongSide) == VALUE_ZERO);
  assert(pos.count<PAWN>(strongSide) >= 2);
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  Square weakKing = pos.square<KING>(weakSide);
  Bitboard strongPawns = pos.pieces(strongSide, PAWN);

  // If all pawns are ahead of the king on a single rook file, it's a draw.
  if (   !(strongPawns & ~(FileABB | FileHBB))
      && !(strongPawns & ~passed_pawn_span(weakSide, weakKing)))
      return SCALE_FACTOR_DRAW;

  return SCALE_FACTOR_NONE;
}


/// KBP vs KB. There are two rules: if the defending king is somewhere along the
/// path of the pawn, and the square of the king is not of the same color as the
/// stronger side's bishop, it's a draw. If the two bishops have opposite color,
/// it's almost always a draw.
template<>
ScaleFactor Endgame<KBPKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg, 1));
  assert(verify_material(pos, weakSide,   BishopValueMg, 0));

  Square strongPawn = pos.square<PAWN>(strongSide);
  Square strongBishop = pos.square<BISHOP>(strongSide);
  Square weakBishop = pos.square<BISHOP>(weakSide);
  Square weakKing = pos.square<KING>(weakSide);

  // Case 1: Defending king blocks the pawn, and cannot be driven away
  if (   (forward_file_bb(strongSide, strongPawn) & weakKing)
      && (   opposite_colors(weakKing, strongBishop)
          || relative_rank(strongSide, weakKing) <= RANK_6))
      return SCALE_FACTOR_DRAW;

  // Case 2: Opposite colored bishops
  if (opposite_colors(strongBishop, weakBishop))
      return SCALE_FACTOR_DRAW;

  return SCALE_FACTOR_NONE;
}


/// KBPP vs KB. It detects a few basic draws with opposite-colored bishops
template<>
ScaleFactor Endgame<KBPPKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg, 2));
  assert(verify_material(pos, weakSide,   BishopValueMg, 0));

  Square strongBishop = pos.square<BISHOP>(strongSide);
  Square weakBishop   = pos.square<BISHOP>(weakSide);

  if (!opposite_colors(strongBishop, weakBishop))
      return SCALE_FACTOR_NONE;

  Square weakKing = pos.square<KING>(weakSide);
  Square strongPawn1 = lsb(pos.pieces(strongSide, PAWN));
  Square strongPawn2 = msb(pos.pieces(strongSide, PAWN));
  Square blockSq1, blockSq2;

  if (relative_rank(strongSide, strongPawn1) > relative_rank(strongSide, strongPawn2))
  {
      blockSq1 = strongPawn1 + pawn_push(strongSide);
      blockSq2 = make_square(file_of(strongPawn2), rank_of(strongPawn1));
  }
  else
  {
      blockSq1 = strongPawn2 + pawn_push(strongSide);
      blockSq2 = make_square(file_of(strongPawn1), rank_of(strongPawn2));
  }

  switch (distance<File>(strongPawn1, strongPawn2))
  {
  case 0:
    // Both pawns are on the same file. It's an easy draw if the defender firmly
    // controls some square in the frontmost pawn's path.
    if (   file_of(weakKing) == file_of(blockSq1)
        && relative_rank(strongSide, weakKing) >= relative_rank(strongSide, blockSq1)
        && opposite_colors(weakKing, strongBishop))
        return SCALE_FACTOR_DRAW;
    else
        return SCALE_FACTOR_NONE;

  case 1:
    // Pawns on adjacent files. It's a draw if the defender firmly controls the
    // square in front of the frontmost pawn's path, and the square diagonally
    // behind this square on the file of the other pawn.
    if (   weakKing == blockSq1
        && opposite_colors(weakKing, strongBishop)
        && (   weakBishop == blockSq2
            || (attacks_bb<BISHOP>(blockSq2, pos.pieces()) & pos.pieces(weakSide, BISHOP))
            || distance<Rank>(strongPawn1, strongPawn2) >= 2))
        return SCALE_FACTOR_DRAW;

    else if (   weakKing == blockSq2
             && opposite_colors(weakKing, strongBishop)
             && (   weakBishop == blockSq1
                 || (attacks_bb<BISHOP>(blockSq1, pos.pieces()) & pos.pieces(weakSide, BISHOP))))
        return SCALE_FACTOR_DRAW;
    else
        return SCALE_FACTOR_NONE;

  default:
    // The pawns are not on the same file or adjacent files. No scaling.
    return SCALE_FACTOR_NONE;
  }
}


/// KBP vs KN. There is a single rule: if the defending king is somewhere along
/// the path of the pawn, and the square of the king is not of the same color as
/// the stronger side's bishop, it's a draw.
template<>
ScaleFactor Endgame<KBPKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg, 1));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Square strongPawn = pos.square<PAWN>(strongSide);
  Square strongBishop = pos.square<BISHOP>(strongSide);
  Square weakKing = pos.square<KING>(weakSide);

  if (   file_of(weakKing) == file_of(strongPawn)
      && relative_rank(strongSide, strongPawn) < relative_rank(strongSide, weakKing)
      && (   opposite_colors(weakKing, strongBishop)
          || relative_rank(strongSide, weakKing) <= RANK_6))
      return SCALE_FACTOR_DRAW;

  return SCALE_FACTOR_NONE;
}


/// KP vs KP. This is done by removing the weakest side's pawn and probing the
/// KP vs K bitbase: if the weakest side has a draw without the pawn, it probably
/// has at least a draw with the pawn as well. The exception is when the stronger
/// side's pawn is far advanced and not on a rook file; in this case it is often
/// possible to win (e.g. 8/4k3/3p4/3P4/6K1/8/8/8 w - - 0 1).
template<>
ScaleFactor Endgame<KPKP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, VALUE_ZERO, 1));
  assert(verify_material(pos, weakSide,   VALUE_ZERO, 1));

  // Assume strongSide is white and the pawn is on files A-D
  Square strongKing = normalize(pos, strongSide, pos.square<KING>(strongSide));
  Square weakKing   = normalize(pos, strongSide, pos.square<KING>(weakSide));
  Square strongPawn = normalize(pos, strongSide, pos.square<PAWN>(strongSide));

  Color us = strongSide == pos.side_to_move() ? WHITE : BLACK;

  // If the pawn has advanced to the fifth rank or further, and is not a
  // rook pawn, it's too dangerous to assume that it's at least a draw.
  if (rank_of(strongPawn) >= RANK_5 && file_of(strongPawn) != FILE_A)
      return SCALE_FACTOR_NONE;

  // Probe the KPK bitbase with the weakest side's pawn removed. If it's a draw,
  // it's probably at least a draw even with the pawn.
  return Bitbases::probe(strongKing, strongPawn, weakKing, us) ? SCALE_FACTOR_NONE : SCALE_FACTOR_DRAW;
}

} // namespace Stockfish
