/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2017 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#ifndef CLUSTER_H_INCLUDED
#define CLUSTER_H_INCLUDED

#include <algorithm>
#include <array>
#include <istream>
#include <string>

#include "tt.h"

class Thread;

namespace Cluster {

struct MoveInfo {
  int move;
  int depth;
  int score;
  int rank;
};

#ifdef USE_MPI
using KeyedTTEntry = std::pair<Key, TTEntry>;

constexpr std::size_t TTSendBufferSize = 32;
template <std::size_t N> class TTSendBuffer : public std::array<KeyedTTEntry, N> {

  struct Compare {
      inline bool operator()(const KeyedTTEntry& lhs, const KeyedTTEntry& rhs) {
          return lhs.second.depth() > rhs.second.depth();
      }
  };
  Compare compare;

public:
  bool replace(const KeyedTTEntry& value) {

      if (compare(value, this->front()))
      {
          std::pop_heap(this->begin(), this->end(), compare);
          this->back() = value;
          std::push_heap(this->begin(), this->end(), compare);
          return true;
      }
      return false;
  }
};

void init();
void finalize();
bool getline(std::istream& input, std::string& str);
int size();
int rank();
inline bool is_root() { return rank() == 0; }
void save(Thread* thread, TTEntry* tte, Key k, Value v, Bound b, Depth d, Move m, Value ev);
void pick_moves(MoveInfo& mi);
void sum(uint64_t& val);
void sync_start();
void sync_stop();

#else

inline void init() { }
inline void finalize() { }
inline bool getline(std::istream& input, std::string& str) {

  return static_cast<bool>(std::getline(input, str));
}
constexpr int size() { return 1; }
constexpr int rank() { return 0; }
constexpr bool is_root() { return true; }
inline void save(Thread* thread, TTEntry* tte, Key k, Value v, Bound b, Depth d, Move m, Value ev) {

  (void)thread;
  tte->save(k, v, b, d, m, ev);
}
inline void pick_moves(MoveInfo&) { }
void sum(uint64_t& val) { }
inline void sync_start() { }
inline void sync_stop() { }

#endif /* USE_MPI */

}

#endif // #ifndef CLUSTER_H_INCLUDED
