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


#if !defined(DEPTH_H_INCLUDED)
#define DEPTH_H_INCLUDED

////
//// Types
////

enum Depth {
  DEPTH_ZERO = 0,
  DEPTH_MAX = 200, // 100 * OnePly;
  DEPTH_ENSURE_SIGNED = -1
};


////
//// Constants
////

const Depth OnePly = Depth(2);


////
//// Inline functions
////

inline Depth operator+ (Depth d, int i) { return Depth(int(d) + i); }
inline Depth operator+ (Depth d1, Depth d2) { return Depth(int(d1) + int(d2)); }
inline void operator+= (Depth &d, int i) { d = Depth(int(d) + i); }
inline void operator+= (Depth &d1, Depth d2) { d1 += int(d2); }
inline Depth operator- (Depth d, int i) { return Depth(int(d) - i); }
inline Depth operator- (Depth d1, Depth d2) { return Depth(int(d1) - int(d2)); }
inline void operator-= (Depth &d, int i) { d = Depth(int(d) - i); }
inline void operator-= (Depth &d1, Depth d2) { d1 -= int(d2); }
inline Depth operator* (Depth d, int i) { return Depth(int(d) * i); }
inline Depth operator* (int i, Depth d) { return Depth(int(d) * i); }
inline void operator*= (Depth &d, int i) { d = Depth(int(d) * i); }
inline Depth operator/ (Depth d, int i) { return Depth(int(d) / i); }
inline void operator/= (Depth &d, int i) { d = Depth(int(d) / i); }


#endif // !defined(DEPTH_H_INCLUDED)
