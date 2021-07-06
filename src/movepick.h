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

#ifndef MOVEPICK_H_INCLUDED
#define MOVEPICK_H_INCLUDED

#include <array>
#include <limits>
#include <type_traits>

#include "history.h"
#include "movegen.h"
#include "position.h"
#include "types.h"

namespace Stockfish {

/// MovePicker::score_*() assigns a numerical value to each move in a list, used
/// for sorting. Captures are ordered by Most Valuable Victim (MVV), preferring
/// captures with a good history. Quiets moves are ordered using the histories.

template <typename T>
inline void score_captures_base(T& self) {
  for (auto& m : self)
      m.value =  int(PieceValue[MG][self.pos.piece_on(to_sq(m))]) * 6
               + (*self.captureHistory)[self.pos.moved_piece(m)][to_sq(m)][type_of(self.pos.piece_on(to_sq(m)))];
}

template <typename T>
inline void score_evasions_base(T& self) {
  for (auto& m : self)
      if (self.pos.capture(m))
          m.value =  PieceValue[MG][self.pos.piece_on(to_sq(m))]
                   - Value(type_of(self.pos.moved_piece(m)));
      else
          m.value =      (*self.mainHistory)[self.pos.side_to_move()][from_to(m)]
                   + 2 * (*self.continuationHistory[0])[self.pos.moved_piece(m)][to_sq(m)]
                   - (1 << 28);
}

template <typename T>
inline void score_quiets_base(T& self) {
  for (auto& m : self)
      m.value =      (*self.mainHistory)[self.pos.side_to_move()][from_to(m)]
               + 2 * (*self.continuationHistory[0])[self.pos.moved_piece(m)][to_sq(m)]
               +     (*self.continuationHistory[1])[self.pos.moved_piece(m)][to_sq(m)]
               +     (*self.continuationHistory[3])[self.pos.moved_piece(m)][to_sq(m)]
               +     (*self.continuationHistory[5])[self.pos.moved_piece(m)][to_sq(m)]
               + (self.ply < MAX_LPH ? std::min(4, self.depth / 3) * (*self.lowPlyHistory)[self.ply][from_to(m)] : 0);
}


/// MovePicker* classes are used to pick one pseudo-legal move at a time from the
/// current position. The most important method is next_move(), which returns a
/// new pseudo-legal move each time it is called, until there are no moves left,
/// when MOVE_NONE is returned. In order to improve the efficiency of the
/// alpha-beta algorithm, Move pickers attempts to return the moves which are most
/// likely to get a cut-off first.
/// Different move pickers are needed in different places in search, sometimes
/// excluding certain moves, or rating them differently. Each MovePicker*
/// class represents one such use case.

class MovePicker {

protected:
  enum PickType { Next, Best };

  MovePicker(const MovePicker&) = delete;
  MovePicker(MovePicker&&) = delete;
  MovePicker& operator=(const MovePicker&) = delete;
  MovePicker& operator=(MovePicker&&) = delete;

  MovePicker(const Position&, Move);

  template<PickType T, typename Pred> Move select(Pred);

  ExtMove* begin() { return cur; }
  ExtMove* end() { return endMoves; }

  const Position& pos;
  Move ttMove;
  ExtMove moves[MAX_MOVES];
  ExtMove* cur;
  ExtMove* endMoves;
};

class MovePicker_Main : public MovePicker {
private:
  enum Stages {
    MAIN_TT, CAPTURE_INIT, GOOD_CAPTURE, REFUTATION, QUIET_INIT, QUIET, BAD_CAPTURE,
    EVASION_TT, EVASION_INIT, EVASION
  };

public:
  MovePicker_Main(const Position&,
                  Move,
                  Depth,
                  const ButterflyHistory*,
                  const LowPlyHistory*,
                  const CapturePieceToHistory*,
                  const PieceToHistory**,
                  Move,
                  const Move*,
                  int);

  Move next_move(bool skipQuiets = false);

private:
  const ButterflyHistory* mainHistory;
  const LowPlyHistory* lowPlyHistory;
  const CapturePieceToHistory* captureHistory;
  const PieceToHistory** continuationHistory;
  ExtMove refutations[3];
  ExtMove* endBadCaptures;
  int stage;
  Square recaptureSquare;
  Value threshold;
  Depth depth;
  int ply;

  Stages get_start_stage(bool evastion, bool tt);

  auto score_captures() { return score_captures_base(*this); }
  auto score_evasions() { return score_evasions_base(*this); }
  auto score_quiets()   { return score_quiets_base(*this); }
  template <typename T> friend void score_captures_base(T&);
  template <typename T> friend void score_evasions_base(T&);
  template <typename T> friend void score_quiets_base  (T&);
};

class MovePicker_Quiescence : public MovePicker {
private:
  enum Stages {
    QSEARCH_TT, QCAPTURE_INIT, QCAPTURE, QCHECK_INIT, QCHECK,
    EVASION_TT, EVASION_INIT, EVASION
  };

public:
  MovePicker_Quiescence(const Position&,
             Move,
             Depth,
             const ButterflyHistory*,
             const CapturePieceToHistory*,
             const PieceToHistory**,
             Square);

  Move next_move();

private:
  const ButterflyHistory* mainHistory;
  const CapturePieceToHistory* captureHistory;
  const PieceToHistory** continuationHistory;
  ExtMove* endBadCaptures;
  int stage;
  Square recaptureSquare;
  Depth depth;

  Stages get_start_stage(bool evastion, bool tt);

  auto score_captures() { return score_captures_base(*this); }
  auto score_evasions() { return score_evasions_base(*this); }
  template <typename T> friend void score_captures_base(T&);
  template <typename T> friend void score_evasions_base(T&);
};

class MovePicker_ProbCut : public MovePicker {
private:
  enum Stages {
    PROBCUT_TT, PROBCUT_INIT, PROBCUT
  };

public:
  MovePicker_ProbCut(const Position&, Move, Value, const CapturePieceToHistory*);
  Move next_move();

private:
  const CapturePieceToHistory* captureHistory;
  ExtMove* endBadCaptures;
  int stage;
  Value threshold;

  Stages get_start_stage(bool tt);

  auto score_captures() { return score_captures_base(*this); }
  template <typename T> friend void score_captures_base(T&);
};

} // namespace Stockfish

#endif // #ifndef MOVEPICK_H_INCLUDED
