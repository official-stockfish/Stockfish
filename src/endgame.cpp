/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2013 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <algorithm>
#include <cassert>

#include "bitboard.h"
#include "bitcount.h"
#include "endgame.h"
#include "movegen.h"

using std::string;

namespace {

  // Table used to drive the defending king towards the edge of the board
  // in KX vs K and KQ vs KR endgames.
  const int MateTable[SQUARE_NB] = {
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
  const int KBNKMateTable[SQUARE_NB] = {
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

  // Get the material key of a Position out of the given endgame key code
  // like "KBPKN". The trick here is to first forge an ad-hoc fen string
  // and then let a Position object to do the work for us. Note that the
  // fen string could correspond to an illegal position.
  Key key(const string& code, Color c) {

    assert(code.length() > 0 && code.length() < 8);
    assert(code[0] == 'K');

    string sides[] = { code.substr(code.find('K', 1)),      // Weaker
                       code.substr(0, code.find('K', 1)) }; // Stronger

    std::transform(sides[c].begin(), sides[c].end(), sides[c].begin(), tolower);

    string fen =  sides[0] + char('0' + int(8 - code.length()))
                + sides[1] + "/8/8/8/8/8/8/8 w - - 0 10";

    return Position(fen, false, NULL).material_key();
  }

  template<typename M>
  void delete_endgame(const typename M::value_type& p) { delete p.second; }

} // namespace


/// Endgames members definitions

Endgames::Endgames() {

  add<KPK>("KPK");
  add<KNNK>("KNNK");
  add<KBNK>("KBNK");
  add<KRKP>("KRKP");
  add<KRKB>("KRKB");
  add<KRKN>("KRKN");
  add<KQKP>("KQKP");
  add<KQKR>("KQKR");
  add<KBBKN>("KBBKN");

  add<KNPK>("KNPK");
  add<KNPKB>("KNPKB");
  add<KRPKR>("KRPKR");
  add<KBPKB>("KBPKB");
  add<KBPKN>("KBPKN");
  add<KBPPKB>("KBPPKB");
  add<KRPPKRP>("KRPPKRP");
}

Endgames::~Endgames() {

  for_each(m1.begin(), m1.end(), delete_endgame<M1>);
  for_each(m2.begin(), m2.end(), delete_endgame<M2>);
}

template<EndgameType E>
void Endgames::add(const string& code) {

  map((Endgame<E>*)0)[key(code, WHITE)] = new Endgame<E>(WHITE);
  map((Endgame<E>*)0)[key(code, BLACK)] = new Endgame<E>(BLACK);
}


/// Mate with KX vs K. This function is used to evaluate positions with
/// King and plenty of material vs a lone king. It simply gives the
/// attacking side a bonus for driving the defending king towards the edge
/// of the board, and for keeping the distance between the two kings small.
template<>
Value Endgame<KXK>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(weakerSide) == VALUE_ZERO);
  assert(!pos.count<PAWN>(weakerSide));

  // Stalemate detection with lone king
  if (    pos.side_to_move() == weakerSide
      && !pos.checkers()
      && !MoveList<LEGAL>(pos).size()) {
    return VALUE_DRAW;
  }

  Square winnerKSq = pos.king_square(strongerSide);
  Square loserKSq = pos.king_square(weakerSide);

  Value result =   pos.non_pawn_material(strongerSide)
                 + pos.count<PAWN>(strongerSide) * PawnValueEg
                 + MateTable[loserKSq]
                 + DistanceBonus[square_distance(winnerKSq, loserKSq)];

  if (   pos.count<QUEEN>(strongerSide)
      || pos.count<ROOK>(strongerSide)
      || pos.bishop_pair(strongerSide)) {
    result += VALUE_KNOWN_WIN;
  }

  return strongerSide == pos.side_to_move() ? result : -result;
}


/// Mate with KBN vs K. This is similar to KX vs K, but we have to drive the
/// defending king towards a corner square of the right color.
template<>
Value Endgame<KBNK>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == KnightValueMg + BishopValueMg);
  assert(pos.non_pawn_material(weakerSide) == VALUE_ZERO);
  assert(pos.count<BISHOP>(strongerSide) == 1);
  assert(pos.count<KNIGHT>(strongerSide) == 1);
  assert(pos.count<  PAWN>(strongerSide) == 0);
  assert(pos.count<  PAWN>(weakerSide  ) == 0);

  Square winnerKSq = pos.king_square(strongerSide);
  Square loserKSq = pos.king_square(weakerSide);
  Square bishopSq = pos.list<BISHOP>(strongerSide)[0];

  // kbnk_mate_table() tries to drive toward corners A1 or H8,
  // if we have a bishop that cannot reach the above squares we
  // mirror the kings so to drive enemy toward corners A8 or H1.
  if (opposite_colors(bishopSq, SQ_A1))
  {
      winnerKSq = mirror(winnerKSq);
      loserKSq = mirror(loserKSq);
  }

  Value result =  VALUE_KNOWN_WIN
                + DistanceBonus[square_distance(winnerKSq, loserKSq)]
                + KBNKMateTable[loserKSq];

  return strongerSide == pos.side_to_move() ? result : -result;
}


/// KP vs K. This endgame is evaluated with the help of a bitbase.
template<>
Value Endgame<KPK>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == VALUE_ZERO);
  assert(pos.non_pawn_material(weakerSide) == VALUE_ZERO);
  assert(pos.count<PAWN>(strongerSide) == 1);
  assert(pos.count<PAWN>(weakerSide  ) == 0);

  Square wksq, bksq, wpsq;
  Color us;

  if (strongerSide == WHITE)
  {
      wksq = pos.king_square(WHITE);
      bksq = pos.king_square(BLACK);
      wpsq = pos.list<PAWN>(WHITE)[0];
      us   = pos.side_to_move();
  }
  else
  {
      wksq = ~pos.king_square(BLACK);
      bksq = ~pos.king_square(WHITE);
      wpsq = ~pos.list<PAWN>(BLACK)[0];
      us   = ~pos.side_to_move();
  }

  if (file_of(wpsq) >= FILE_E)
  {
      wksq = mirror(wksq);
      bksq = mirror(bksq);
      wpsq = mirror(wpsq);
  }

  if (!Bitbases::probe_kpk(wksq, wpsq, bksq, us))
      return VALUE_DRAW;

  Value result = VALUE_KNOWN_WIN + PawnValueEg + Value(rank_of(wpsq));

  return strongerSide == pos.side_to_move() ? result : -result;
}


/// KR vs KP. This is a somewhat tricky endgame to evaluate precisely without
/// a bitbase. The function below returns drawish scores when the pawn is
/// far advanced with support of the king, while the attacking king is far
/// away.
template<>
Value Endgame<KRKP>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == RookValueMg);
  assert(pos.non_pawn_material(weakerSide) == 0);
  assert(pos.count<PAWN>(strongerSide) == 0);
  assert(pos.count<PAWN>(weakerSide  ) == 1);

  Square wksq, wrsq, bksq, bpsq;
  int tempo = (pos.side_to_move() == strongerSide);

  wksq = pos.king_square(strongerSide);
  bksq = pos.king_square(weakerSide);
  wrsq = pos.list<ROOK>(strongerSide)[0];
  bpsq = pos.list<PAWN>(weakerSide)[0];

  if (strongerSide == BLACK)
  {
      wksq = ~wksq;
      wrsq = ~wrsq;
      bksq = ~bksq;
      bpsq = ~bpsq;
  }

  Square queeningSq = file_of(bpsq) | RANK_1;
  Value result;

  // If the stronger side's king is in front of the pawn, it's a win
  if (wksq < bpsq && file_of(wksq) == file_of(bpsq))
      result = RookValueEg - Value(square_distance(wksq, bpsq));

  // If the weaker side's king is too far from the pawn and the rook,
  // it's a win
  else if (   square_distance(bksq, bpsq) - (tempo ^ 1) >= 3
           && square_distance(bksq, wrsq) >= 3)
      result = RookValueEg - Value(square_distance(wksq, bpsq));

  // If the pawn is far advanced and supported by the defending king,
  // the position is drawish
  else if (   rank_of(bksq) <= RANK_3
           && square_distance(bksq, bpsq) == 1
           && rank_of(wksq) >= RANK_4
           && square_distance(wksq, bpsq) - tempo > 2)
      result = Value(80 - square_distance(wksq, bpsq) * 8);

  else
      result =  Value(200)
              - Value(square_distance(wksq, bpsq + DELTA_S) * 8)
              + Value(square_distance(bksq, bpsq + DELTA_S) * 8)
              + Value(square_distance(bpsq, queeningSq) * 8);

  return strongerSide == pos.side_to_move() ? result : -result;
}


/// KR vs KB. This is very simple, and always returns drawish scores.  The
/// score is slightly bigger when the defending king is close to the edge.
template<>
Value Endgame<KRKB>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == RookValueMg);
  assert(pos.non_pawn_material(weakerSide  ) == BishopValueMg);
  assert(pos.count<BISHOP>(weakerSide  ) == 1);
  assert(pos.count<  PAWN>(weakerSide  ) == 0);
  assert(pos.count<  PAWN>(strongerSide) == 0);

  Value result = Value(MateTable[pos.king_square(weakerSide)]);
  return strongerSide == pos.side_to_move() ? result : -result;
}


/// KR vs KN.  The attacking side has slightly better winning chances than
/// in KR vs KB, particularly if the king and the knight are far apart.
template<>
Value Endgame<KRKN>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == RookValueMg);
  assert(pos.non_pawn_material(weakerSide  ) == KnightValueMg);
  assert(pos.count<KNIGHT>(weakerSide  ) == 1);
  assert(pos.count<  PAWN>(weakerSide  ) == 0);
  assert(pos.count<  PAWN>(strongerSide) == 0);

  const int penalty[8] = { 0, 10, 14, 20, 30, 42, 58, 80 };

  Square bksq = pos.king_square(weakerSide);
  Square bnsq = pos.list<KNIGHT>(weakerSide)[0];
  Value result = Value(MateTable[bksq] + penalty[square_distance(bksq, bnsq)]);
  return strongerSide == pos.side_to_move() ? result : -result;
}


/// KQ vs KP.  In general, a win for the stronger side, however, there are a few
/// important exceptions.  Pawn on 7th rank, A,C,F or H file, with king next can
/// be a draw, so we scale down to distance between kings only.
template<>
Value Endgame<KQKP>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == QueenValueMg);
  assert(pos.non_pawn_material(weakerSide  ) == VALUE_ZERO);
  assert(pos.count<PAWN>(strongerSide) == 0);
  assert(pos.count<PAWN>(weakerSide  ) == 1);

  Square winnerKSq = pos.king_square(strongerSide);
  Square loserKSq = pos.king_square(weakerSide);
  Square pawnSq = pos.list<PAWN>(weakerSide)[0];

  Value result =  QueenValueEg
                - PawnValueEg
                + DistanceBonus[square_distance(winnerKSq, loserKSq)];

  if (   square_distance(loserKSq, pawnSq) == 1
      && relative_rank(weakerSide, pawnSq) == RANK_7)
  {
      File f = file_of(pawnSq);

      if (f == FILE_A || f == FILE_C || f == FILE_F || f == FILE_H)
          result = Value(DistanceBonus[square_distance(winnerKSq, loserKSq)]);
  }
  return strongerSide == pos.side_to_move() ? result : -result;
}


/// KQ vs KR.  This is almost identical to KX vs K:  We give the attacking
/// king a bonus for having the kings close together, and for forcing the
/// defending king towards the edge.  If we also take care to avoid null move
/// for the defending side in the search, this is usually sufficient to be
/// able to win KQ vs KR.
template<>
Value Endgame<KQKR>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == QueenValueMg);
  assert(pos.non_pawn_material(weakerSide  ) == RookValueMg);
  assert(pos.count<PAWN>(strongerSide) == 0);
  assert(pos.count<PAWN>(weakerSide  ) == 0);

  Square winnerKSq = pos.king_square(strongerSide);
  Square loserKSq = pos.king_square(weakerSide);

  Value result =  QueenValueEg
                - RookValueEg
                + MateTable[loserKSq]
                + DistanceBonus[square_distance(winnerKSq, loserKSq)];

  return strongerSide == pos.side_to_move() ? result : -result;
}

template<>
Value Endgame<KBBKN>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == 2 * BishopValueMg);
  assert(pos.non_pawn_material(weakerSide  ) == KnightValueMg);
  assert(pos.count<BISHOP>(strongerSide) == 2);
  assert(pos.count<KNIGHT>(weakerSide  ) == 1);
  assert(!pos.pieces(PAWN));

  Value result = BishopValueEg;
  Square wksq = pos.king_square(strongerSide);
  Square bksq = pos.king_square(weakerSide);
  Square nsq = pos.list<KNIGHT>(weakerSide)[0];

  // Bonus for attacking king close to defending king
  result += Value(DistanceBonus[square_distance(wksq, bksq)]);

  // Bonus for driving the defending king and knight apart
  result += Value(square_distance(bksq, nsq) * 32);

  // Bonus for restricting the knight's mobility
  result += Value((8 - popcount<Max15>(pos.attacks_from<KNIGHT>(nsq))) * 8);

  return strongerSide == pos.side_to_move() ? result : -result;
}


/// K and two minors vs K and one or two minors or K and two knights against
/// king alone are always draw.
template<>
Value Endgame<KmmKm>::operator()(const Position&) const {
  return VALUE_DRAW;
}

template<>
Value Endgame<KNNK>::operator()(const Position&) const {
  return VALUE_DRAW;
}

/// K, bishop and one or more pawns vs K. It checks for draws with rook pawns and
/// a bishop of the wrong color. If such a draw is detected, SCALE_FACTOR_DRAW
/// is returned. If not, the return value is SCALE_FACTOR_NONE, i.e. no scaling
/// will be used.
template<>
ScaleFactor Endgame<KBPsK>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == BishopValueMg);
  assert(pos.count<BISHOP>(strongerSide) == 1);
  assert(pos.count<  PAWN>(strongerSide) >= 1);

  // No assertions about the material of weakerSide, because we want draws to
  // be detected even when the weaker side has some pawns.

  Bitboard pawns = pos.pieces(strongerSide, PAWN);
  File pawnFile = file_of(pos.list<PAWN>(strongerSide)[0]);

  // All pawns are on a single rook file ?
  if (    (pawnFile == FILE_A || pawnFile == FILE_H)
      && !(pawns & ~file_bb(pawnFile)))
  {
      Square bishopSq = pos.list<BISHOP>(strongerSide)[0];
      Square queeningSq = relative_square(strongerSide, pawnFile | RANK_8);
      Square kingSq = pos.king_square(weakerSide);

      if (   opposite_colors(queeningSq, bishopSq)
          && abs(file_of(kingSq) - pawnFile) <= 1)
      {
          // The bishop has the wrong color, and the defending king is on the
          // file of the pawn(s) or the adjacent file. Find the rank of the
          // frontmost pawn.
          Rank rank;
          if (strongerSide == WHITE)
          {
              for (rank = RANK_7; !(rank_bb(rank) & pawns); rank--) {}
              assert(rank >= RANK_2 && rank <= RANK_7);
          }
          else
          {
              for (rank = RANK_2; !(rank_bb(rank) & pawns); rank++) {}
              rank = Rank(rank ^ 7);  // HACK to get the relative rank
              assert(rank >= RANK_2 && rank <= RANK_7);
          }
          // If the defending king has distance 1 to the promotion square or
          // is placed somewhere in front of the pawn, it's a draw.
          if (   square_distance(kingSq, queeningSq) <= 1
              || relative_rank(strongerSide, kingSq) >= rank)
              return SCALE_FACTOR_DRAW;
      }
  }

  // All pawns on same B or G file? Then potential draw
  if (    (pawnFile == FILE_B || pawnFile == FILE_G)
      && !(pos.pieces(PAWN) & ~file_bb(pawnFile))
      && pos.non_pawn_material(weakerSide) == 0
      && pos.count<PAWN>(weakerSide) >= 1)
  {
      // Get weaker pawn closest to opponent's queening square
      Bitboard wkPawns = pos.pieces(weakerSide, PAWN);
      Square weakerPawnSq = strongerSide == WHITE ? msb(wkPawns) : lsb(wkPawns);

      Square strongerKingSq = pos.king_square(strongerSide);
      Square weakerKingSq = pos.king_square(weakerSide);
      Square bishopSq = pos.list<BISHOP>(strongerSide)[0];

      // Draw if weaker pawn is on rank 7, bishop can't attack the pawn, and
      // weaker king can stop opposing opponent's king from penetrating.
      if (   relative_rank(strongerSide, weakerPawnSq) == RANK_7
          && opposite_colors(bishopSq, weakerPawnSq)
          && square_distance(weakerPawnSq, weakerKingSq) <= square_distance(weakerPawnSq, strongerKingSq))
          return SCALE_FACTOR_DRAW;
  }

  return SCALE_FACTOR_NONE;
}


/// K and queen vs K, rook and one or more pawns. It tests for fortress draws with
/// a rook on the third rank defended by a pawn.
template<>
ScaleFactor Endgame<KQKRPs>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == QueenValueMg);
  assert(pos.count<QUEEN>(strongerSide) == 1);
  assert(pos.count< PAWN>(strongerSide) == 0);
  assert(pos.count< ROOK>(weakerSide  ) == 1);
  assert(pos.count< PAWN>(weakerSide  ) >= 1);

  Square kingSq = pos.king_square(weakerSide);
  if (    relative_rank(weakerSide, kingSq) <= RANK_2
      &&  relative_rank(weakerSide, pos.king_square(strongerSide)) >= RANK_4
      && (pos.pieces(weakerSide, ROOK) & rank_bb(relative_rank(weakerSide, RANK_3)))
      && (pos.pieces(weakerSide, PAWN) & rank_bb(relative_rank(weakerSide, RANK_2)))
      && (pos.attacks_from<KING>(kingSq) & pos.pieces(weakerSide, PAWN)))
  {
      Square rsq = pos.list<ROOK>(weakerSide)[0];
      if (pos.attacks_from<PAWN>(rsq, strongerSide) & pos.pieces(weakerSide, PAWN))
          return SCALE_FACTOR_DRAW;
  }
  return SCALE_FACTOR_NONE;
}


/// K, rook and one pawn vs K and a rook. This function knows a handful of the
/// most important classes of drawn positions, but is far from perfect. It would
/// probably be a good idea to add more knowledge in the future.
///
/// It would also be nice to rewrite the actual code for this function,
/// which is mostly copied from Glaurung 1.x, and not very pretty.
template<>
ScaleFactor Endgame<KRPKR>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == RookValueMg);
  assert(pos.non_pawn_material(weakerSide)   == RookValueMg);
  assert(pos.count<PAWN>(strongerSide) == 1);
  assert(pos.count<PAWN>(weakerSide  ) == 0);

  Square wksq = pos.king_square(strongerSide);
  Square bksq = pos.king_square(weakerSide);
  Square wrsq = pos.list<ROOK>(strongerSide)[0];
  Square wpsq = pos.list<PAWN>(strongerSide)[0];
  Square brsq = pos.list<ROOK>(weakerSide)[0];

  // Orient the board in such a way that the stronger side is white, and the
  // pawn is on the left half of the board.
  if (strongerSide == BLACK)
  {
      wksq = ~wksq;
      wrsq = ~wrsq;
      wpsq = ~wpsq;
      bksq = ~bksq;
      brsq = ~brsq;
  }
  if (file_of(wpsq) > FILE_D)
  {
      wksq = mirror(wksq);
      wrsq = mirror(wrsq);
      wpsq = mirror(wpsq);
      bksq = mirror(bksq);
      brsq = mirror(brsq);
  }

  File f = file_of(wpsq);
  Rank r = rank_of(wpsq);
  Square queeningSq = f | RANK_8;
  int tempo = (pos.side_to_move() == strongerSide);

  // If the pawn is not too far advanced and the defending king defends the
  // queening square, use the third-rank defence.
  if (   r <= RANK_5
      && square_distance(bksq, queeningSq) <= 1
      && wksq <= SQ_H5
      && (rank_of(brsq) == RANK_6 || (r <= RANK_3 && rank_of(wrsq) != RANK_6)))
      return SCALE_FACTOR_DRAW;

  // The defending side saves a draw by checking from behind in case the pawn
  // has advanced to the 6th rank with the king behind.
  if (   r == RANK_6
      && square_distance(bksq, queeningSq) <= 1
      && rank_of(wksq) + tempo <= RANK_6
      && (rank_of(brsq) == RANK_1 || (!tempo && abs(file_of(brsq) - f) >= 3)))
      return SCALE_FACTOR_DRAW;

  if (   r >= RANK_6
      && bksq == queeningSq
      && rank_of(brsq) == RANK_1
      && (!tempo || square_distance(wksq, wpsq) >= 2))
      return SCALE_FACTOR_DRAW;

  // White pawn on a7 and rook on a8 is a draw if black's king is on g7 or h7
  // and the black rook is behind the pawn.
  if (   wpsq == SQ_A7
      && wrsq == SQ_A8
      && (bksq == SQ_H7 || bksq == SQ_G7)
      && file_of(brsq) == FILE_A
      && (rank_of(brsq) <= RANK_3 || file_of(wksq) >= FILE_D || rank_of(wksq) <= RANK_5))
      return SCALE_FACTOR_DRAW;

  // If the defending king blocks the pawn and the attacking king is too far
  // away, it's a draw.
  if (   r <= RANK_5
      && bksq == wpsq + DELTA_N
      && square_distance(wksq, wpsq) - tempo >= 2
      && square_distance(wksq, brsq) - tempo >= 2)
      return SCALE_FACTOR_DRAW;

  // Pawn on the 7th rank supported by the rook from behind usually wins if the
  // attacking king is closer to the queening square than the defending king,
  // and the defending king cannot gain tempi by threatening the attacking rook.
  if (   r == RANK_7
      && f != FILE_A
      && file_of(wrsq) == f
      && wrsq != queeningSq
      && (square_distance(wksq, queeningSq) < square_distance(bksq, queeningSq) - 2 + tempo)
      && (square_distance(wksq, queeningSq) < square_distance(bksq, wrsq) + tempo))
      return ScaleFactor(SCALE_FACTOR_MAX - 2 * square_distance(wksq, queeningSq));

  // Similar to the above, but with the pawn further back
  if (   f != FILE_A
      && file_of(wrsq) == f
      && wrsq < wpsq
      && (square_distance(wksq, queeningSq) < square_distance(bksq, queeningSq) - 2 + tempo)
      && (square_distance(wksq, wpsq + DELTA_N) < square_distance(bksq, wpsq + DELTA_N) - 2 + tempo)
      && (  square_distance(bksq, wrsq) + tempo >= 3
          || (    square_distance(wksq, queeningSq) < square_distance(bksq, wrsq) + tempo
              && (square_distance(wksq, wpsq + DELTA_N) < square_distance(bksq, wrsq) + tempo))))
      return ScaleFactor(  SCALE_FACTOR_MAX
                         - 8 * square_distance(wpsq, queeningSq)
                         - 2 * square_distance(wksq, queeningSq));

  // If the pawn is not far advanced, and the defending king is somewhere in
  // the pawn's path, it's probably a draw.
  if (r <= RANK_4 && bksq > wpsq)
  {
      if (file_of(bksq) == file_of(wpsq))
          return ScaleFactor(10);
      if (   abs(file_of(bksq) - file_of(wpsq)) == 1
          && square_distance(wksq, bksq) > 2)
          return ScaleFactor(24 - 2 * square_distance(wksq, bksq));
  }
  return SCALE_FACTOR_NONE;
}


/// K, rook and two pawns vs K, rook and one pawn. There is only a single
/// pattern: If the stronger side has no passed pawns and the defending king
/// is actively placed, the position is drawish.
template<>
ScaleFactor Endgame<KRPPKRP>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == RookValueMg);
  assert(pos.non_pawn_material(weakerSide)   == RookValueMg);
  assert(pos.count<PAWN>(strongerSide) == 2);
  assert(pos.count<PAWN>(weakerSide  ) == 1);

  Square wpsq1 = pos.list<PAWN>(strongerSide)[0];
  Square wpsq2 = pos.list<PAWN>(strongerSide)[1];
  Square bksq = pos.king_square(weakerSide);

  // Does the stronger side have a passed pawn?
  if (   pos.pawn_is_passed(strongerSide, wpsq1)
      || pos.pawn_is_passed(strongerSide, wpsq2))
      return SCALE_FACTOR_NONE;

  Rank r = std::max(relative_rank(strongerSide, wpsq1), relative_rank(strongerSide, wpsq2));

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


/// K and two or more pawns vs K. There is just a single rule here: If all pawns
/// are on the same rook file and are blocked by the defending king, it's a draw.
template<>
ScaleFactor Endgame<KPsK>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == VALUE_ZERO);
  assert(pos.non_pawn_material(weakerSide)   == VALUE_ZERO);
  assert(pos.count<PAWN>(strongerSide) >= 2);
  assert(pos.count<PAWN>(weakerSide  ) == 0);

  Square ksq = pos.king_square(weakerSide);
  Bitboard pawns = pos.pieces(strongerSide, PAWN);

  // Are all pawns on the 'a' file?
  if (!(pawns & ~FileABB))
  {
      // Does the defending king block the pawns?
      if (   square_distance(ksq, relative_square(strongerSide, SQ_A8)) <= 1
          || (    file_of(ksq) == FILE_A
              && !(in_front_bb(strongerSide, ksq) & pawns)))
          return SCALE_FACTOR_DRAW;
  }
  // Are all pawns on the 'h' file?
  else if (!(pawns & ~FileHBB))
  {
    // Does the defending king block the pawns?
    if (   square_distance(ksq, relative_square(strongerSide, SQ_H8)) <= 1
        || (    file_of(ksq) == FILE_H
            && !(in_front_bb(strongerSide, ksq) & pawns)))
        return SCALE_FACTOR_DRAW;
  }
  return SCALE_FACTOR_NONE;
}


/// K, bishop and a pawn vs K and a bishop. There are two rules: If the defending
/// king is somewhere along the path of the pawn, and the square of the king is
/// not of the same color as the stronger side's bishop, it's a draw. If the two
/// bishops have opposite color, it's almost always a draw.
template<>
ScaleFactor Endgame<KBPKB>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == BishopValueMg);
  assert(pos.non_pawn_material(weakerSide  ) == BishopValueMg);
  assert(pos.count<BISHOP>(strongerSide) == 1);
  assert(pos.count<BISHOP>(weakerSide  ) == 1);
  assert(pos.count<  PAWN>(strongerSide) == 1);
  assert(pos.count<  PAWN>(weakerSide  ) == 0);

  Square pawnSq = pos.list<PAWN>(strongerSide)[0];
  Square strongerBishopSq = pos.list<BISHOP>(strongerSide)[0];
  Square weakerBishopSq = pos.list<BISHOP>(weakerSide)[0];
  Square weakerKingSq = pos.king_square(weakerSide);

  // Case 1: Defending king blocks the pawn, and cannot be driven away
  if (   file_of(weakerKingSq) == file_of(pawnSq)
      && relative_rank(strongerSide, pawnSq) < relative_rank(strongerSide, weakerKingSq)
      && (   opposite_colors(weakerKingSq, strongerBishopSq)
          || relative_rank(strongerSide, weakerKingSq) <= RANK_6))
      return SCALE_FACTOR_DRAW;

  // Case 2: Opposite colored bishops
  if (opposite_colors(strongerBishopSq, weakerBishopSq))
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
          return SCALE_FACTOR_DRAW;
      else
      {
          Bitboard path = forward_bb(strongerSide, pawnSq);

          if (path & pos.pieces(weakerSide, KING))
              return SCALE_FACTOR_DRAW;

          if (  (pos.attacks_from<BISHOP>(weakerBishopSq) & path)
              && square_distance(weakerBishopSq, pawnSq) >= 3)
              return SCALE_FACTOR_DRAW;
      }
  }
  return SCALE_FACTOR_NONE;
}


/// K, bishop and two pawns vs K and bishop. It detects a few basic draws with
/// opposite-colored bishops.
template<>
ScaleFactor Endgame<KBPPKB>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == BishopValueMg);
  assert(pos.non_pawn_material(weakerSide  ) == BishopValueMg);
  assert(pos.count<BISHOP>(strongerSide) == 1);
  assert(pos.count<BISHOP>(weakerSide  ) == 1);
  assert(pos.count<  PAWN>(strongerSide) == 2);
  assert(pos.count<  PAWN>(weakerSide  ) == 0);

  Square wbsq = pos.list<BISHOP>(strongerSide)[0];
  Square bbsq = pos.list<BISHOP>(weakerSide)[0];

  if (!opposite_colors(wbsq, bbsq))
      return SCALE_FACTOR_NONE;

  Square ksq = pos.king_square(weakerSide);
  Square psq1 = pos.list<PAWN>(strongerSide)[0];
  Square psq2 = pos.list<PAWN>(strongerSide)[1];
  Rank r1 = rank_of(psq1);
  Rank r2 = rank_of(psq2);
  Square blockSq1, blockSq2;

  if (relative_rank(strongerSide, psq1) > relative_rank(strongerSide, psq2))
  {
      blockSq1 = psq1 + pawn_push(strongerSide);
      blockSq2 = file_of(psq2) | rank_of(psq1);
  }
  else
  {
      blockSq1 = psq2 + pawn_push(strongerSide);
      blockSq2 = file_of(psq1) | rank_of(psq2);
  }

  switch (file_distance(psq1, psq2))
  {
  case 0:
    // Both pawns are on the same file. Easy draw if defender firmly controls
    // some square in the frontmost pawn's path.
    if (   file_of(ksq) == file_of(blockSq1)
        && relative_rank(strongerSide, ksq) >= relative_rank(strongerSide, blockSq1)
        && opposite_colors(ksq, wbsq))
        return SCALE_FACTOR_DRAW;
    else
        return SCALE_FACTOR_NONE;

  case 1:
    // Pawns on adjacent files. Draw if defender firmly controls the square
    // in front of the frontmost pawn's path, and the square diagonally behind
    // this square on the file of the other pawn.
    if (   ksq == blockSq1
        && opposite_colors(ksq, wbsq)
        && (   bbsq == blockSq2
            || (pos.attacks_from<BISHOP>(blockSq2) & pos.pieces(weakerSide, BISHOP))
            || abs(r1 - r2) >= 2))
        return SCALE_FACTOR_DRAW;

    else if (   ksq == blockSq2
             && opposite_colors(ksq, wbsq)
             && (   bbsq == blockSq1
                 || (pos.attacks_from<BISHOP>(blockSq1) & pos.pieces(weakerSide, BISHOP))))
        return SCALE_FACTOR_DRAW;
    else
        return SCALE_FACTOR_NONE;

  default:
    // The pawns are not on the same file or adjacent files. No scaling.
    return SCALE_FACTOR_NONE;
  }
}


/// K, bisop and a pawn vs K and knight. There is a single rule: If the defending
/// king is somewhere along the path of the pawn, and the square of the king is
/// not of the same color as the stronger side's bishop, it's a draw.
template<>
ScaleFactor Endgame<KBPKN>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == BishopValueMg);
  assert(pos.non_pawn_material(weakerSide  ) == KnightValueMg);
  assert(pos.count<BISHOP>(strongerSide) == 1);
  assert(pos.count<KNIGHT>(weakerSide  ) == 1);
  assert(pos.count<  PAWN>(strongerSide) == 1);
  assert(pos.count<  PAWN>(weakerSide  ) == 0);

  Square pawnSq = pos.list<PAWN>(strongerSide)[0];
  Square strongerBishopSq = pos.list<BISHOP>(strongerSide)[0];
  Square weakerKingSq = pos.king_square(weakerSide);

  if (   file_of(weakerKingSq) == file_of(pawnSq)
      && relative_rank(strongerSide, pawnSq) < relative_rank(strongerSide, weakerKingSq)
      && (   opposite_colors(weakerKingSq, strongerBishopSq)
          || relative_rank(strongerSide, weakerKingSq) <= RANK_6))
      return SCALE_FACTOR_DRAW;

  return SCALE_FACTOR_NONE;
}


/// K, knight and a pawn vs K. There is a single rule: If the pawn is a rook pawn
/// on the 7th rank and the defending king prevents the pawn from advancing, the
/// position is drawn.
template<>
ScaleFactor Endgame<KNPK>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == KnightValueMg);
  assert(pos.non_pawn_material(weakerSide  ) == VALUE_ZERO);
  assert(pos.count<KNIGHT>(strongerSide) == 1);
  assert(pos.count<  PAWN>(strongerSide) == 1);
  assert(pos.count<  PAWN>(weakerSide  ) == 0);

  Square pawnSq = pos.list<PAWN>(strongerSide)[0];
  Square weakerKingSq = pos.king_square(weakerSide);

  if (   pawnSq == relative_square(strongerSide, SQ_A7)
      && square_distance(weakerKingSq, relative_square(strongerSide, SQ_A8)) <= 1)
      return SCALE_FACTOR_DRAW;

  if (   pawnSq == relative_square(strongerSide, SQ_H7)
      && square_distance(weakerKingSq, relative_square(strongerSide, SQ_H8)) <= 1)
      return SCALE_FACTOR_DRAW;

  return SCALE_FACTOR_NONE;
}


/// K, knight and a pawn vs K and bishop. If knight can block bishop from taking
/// pawn, it's a win. Otherwise, drawn.
template<>
ScaleFactor Endgame<KNPKB>::operator()(const Position& pos) const {

  Square pawnSq = pos.list<PAWN>(strongerSide)[0];
  Square bishopSq = pos.list<BISHOP>(weakerSide)[0];
  Square weakerKingSq = pos.king_square(weakerSide);

  // King needs to get close to promoting pawn to prevent knight from blocking.
  // Rules for this are very tricky, so just approximate.
  if (forward_bb(strongerSide, pawnSq) & pos.attacks_from<BISHOP>(bishopSq))
      return ScaleFactor(square_distance(weakerKingSq, pawnSq));

  return SCALE_FACTOR_NONE;
}


/// K and a pawn vs K and a pawn. This is done by removing the weakest side's
/// pawn and probing the KP vs K bitbase: If the weakest side has a draw without
/// the pawn, she probably has at least a draw with the pawn as well. The exception
/// is when the stronger side's pawn is far advanced and not on a rook file; in
/// this case it is often possible to win (e.g. 8/4k3/3p4/3P4/6K1/8/8/8 w - - 0 1).
template<>
ScaleFactor Endgame<KPKP>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongerSide) == VALUE_ZERO);
  assert(pos.non_pawn_material(weakerSide  ) == VALUE_ZERO);
  assert(pos.count<PAWN>(WHITE) == 1);
  assert(pos.count<PAWN>(BLACK) == 1);

  Square wksq = pos.king_square(strongerSide);
  Square bksq = pos.king_square(weakerSide);
  Square wpsq = pos.list<PAWN>(strongerSide)[0];
  Color us = pos.side_to_move();

  if (strongerSide == BLACK)
  {
      wksq = ~wksq;
      bksq = ~bksq;
      wpsq = ~wpsq;
      us   = ~us;
  }

  if (file_of(wpsq) >= FILE_E)
  {
      wksq = mirror(wksq);
      bksq = mirror(bksq);
      wpsq = mirror(wpsq);
  }

  // If the pawn has advanced to the fifth rank or further, and is not a
  // rook pawn, it's too dangerous to assume that it's at least a draw.
  if (   rank_of(wpsq) >= RANK_5
      && file_of(wpsq) != FILE_A)
      return SCALE_FACTOR_NONE;

  // Probe the KPK bitbase with the weakest side's pawn removed. If it's a draw,
  // it's probably at least a draw even with the pawn.
  return Bitbases::probe_kpk(wksq, wpsq, bksq, us) ? SCALE_FACTOR_NONE : SCALE_FACTOR_DRAW;
}
