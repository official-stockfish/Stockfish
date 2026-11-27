/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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

#ifndef CHESSTB_COMMON_H_INCLUDED
#define CHESSTB_COMMON_H_INCLUDED

#include "../bitboard.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Stockfish::ChessTB {

// A view of contiguous elements: Span may write them, Const_Span only reads.
template<typename T>
struct Const_Span {
    constexpr Const_Span() = default;
    constexpr Const_Span(const T* b, const T* e) :
        first(b),
        last(e) {}
    constexpr Const_Span(const T* b, size_t n) :
        first(b),
        last(b + n) {}

    constexpr const T* data() const { return first; }
    constexpr const T* begin() const { return first; }
    constexpr const T* end() const { return last; }
    constexpr size_t   size() const { return size_t(last - first); }
    constexpr const T& operator[](size_t i) const {
        assert(i < size());
        return first[i];
    }

   private:
    const T* first = nullptr;
    const T* last  = nullptr;
};

template<typename T>
struct Span {
    constexpr Span() = default;
    constexpr Span(T* b, size_t n) :
        first(b),
        last(b + n) {}

    constexpr T*     data() const { return first; }
    constexpr T*     begin() const { return first; }
    constexpr T*     end() const { return last; }
    constexpr size_t size() const { return size_t(last - first); }

   private:
    T* first = nullptr;
    T* last  = nullptr;
};

template<typename T>
constexpr T ceil_div(T val, T divisor) {
    return (val + divisor - 1) / divisor;
}

inline constexpr size_t MAX_MAN = 32;

// Squares a pawn can occupy (ranks 2..7).
inline constexpr int PAWN_SQUARE_NB = 48;

// The tables' piece order: king first, then by strength.
inline constexpr Piece TABLE_PIECES[] = {W_KING, W_QUEEN, W_ROOK, W_BISHOP, W_KNIGHT, W_PAWN,
                                         B_KING, B_QUEEN, B_ROOK, B_BISHOP, B_KNIGHT, B_PAWN};

struct Material_Key {
   private:
    // Each piece's weight in the key, as table files store it (base 9).
    static constexpr std::array<uint32_t, PIECE_NB> MAT_KEY = [] {
        std::array<uint32_t, PIECE_NB> k{};
        k[W_QUEEN]  = 6561;
        k[W_ROOK]   = 729;
        k[W_BISHOP] = 81;
        k[W_KNIGHT] = 9;
        k[W_PAWN]   = 1;
        k[B_QUEEN]  = 387420489u;
        k[B_ROOK]   = 43046721;
        k[B_BISHOP] = 4782969;
        k[B_KNIGHT] = 531441;
        k[B_PAWN]   = 59049;
        return k;
    }();

   public:
    constexpr Material_Key() :
        key(0) {}

    explicit constexpr Material_Key(uint32_t k) :
        key(k) {}

    constexpr void add_piece(Piece pc) { key += MAT_KEY[pc]; }

    constexpr void add_pair() { pair_flag = true; }

    constexpr bool has_pair() const { return pair_flag; }

    constexpr void add_castling(size_t white_rights, size_t black_rights) {
        assert(white_rights <= 2 && black_rights <= 2);
        for (size_t i = 0; i < white_rights; ++i)
            add_piece(W_ROOK);
        for (size_t i = 0; i < black_rights; ++i)
            add_piece(B_ROOK);
        castling_code = static_cast<uint8_t>(white_rights * 3 + black_rights);
    }

    constexpr bool has_castling() const { return castling_code != 0; }

    constexpr uint8_t castling_index() const { return castling_code; }

    constexpr uint32_t value() const { return key; }

    constexpr friend bool operator==(Material_Key lhs, Material_Key rhs) noexcept {
        return lhs.key == rhs.key && lhs.pair_flag == rhs.pair_flag
            && lhs.castling_code == rhs.castling_code;
    }
    constexpr friend bool operator!=(Material_Key lhs, Material_Key rhs) noexcept {
        return !(lhs == rhs);
    }
    constexpr friend bool operator<(Material_Key lhs, Material_Key rhs) noexcept {
        if (lhs.key != rhs.key)
            return lhs.key < rhs.key;
        if (lhs.pair_flag != rhs.pair_flag)
            return !lhs.pair_flag;
        return lhs.castling_code < rhs.castling_code;
    }

   private:
    uint32_t key;
    bool     pair_flag     = false;
    uint8_t  castling_code = 0;
};

struct Material_Key_Hash {
    size_t operator()(Material_Key k) const noexcept {
        return (static_cast<size_t>(k.value()) * 1099511628211ull + k.has_pair()) * 9
             + k.castling_index();
    }
};

constexpr Square possible_sq(Piece piece, size_t index) {
    if (type_of(piece) == PAWN)
        return Square(SQ_A2 + index);
    return Square(index);
}

// A Stockfish position as the tables read it, optionally mirrored: colours
// swapped and ranks flipped, which is how a position is read from the frame of
// the other colour. No en passant square: that is the caller's.
class View {
   public:
    explicit View(const Position& p, bool mirror = false) :
        pos(&p),
        mirrored(mirror) {}

    View mirror() const { return View(*pos, !mirrored); }

    Color turn() const { return Color(pos->side_to_move() ^ int(mirrored)); }

    Bitboard piece_bb(Piece pc) const {
        const Bitboard b = pos->pieces(Color(color_of(pc) ^ int(mirrored)), type_of(pc));
        return mirrored ? Attacks::byteswap_bb(b) : b;
    }

    Square king_square(Color c) const { return lsb(piece_bb(make_piece(c, KING))); }

    // The rooks of `c` still holding a castling right.
    Bitboard castling_rooks(Color c) const {
        Bitboard b = 0;
        for (const bool h_side : {false, true})
            if (pos->can_castle(right(c, h_side)))
                b |= square_bb(pos->castling_rook_square(right(c, h_side)));
        return mirrored ? Attacks::byteswap_bb(b) : b;
    }

   private:
    CastlingRights right(Color c, bool h_side) const {
        return Color(c ^ int(mirrored)) & (h_side ? KING_SIDE : QUEEN_SIDE);
    }

    const Position* pos;
    bool            mirrored;
};

// Semantic 5-class outcome for the side to move.
enum struct WDL_Entry : uint8_t {
    LOSE         = 0,
    BLESSED_LOSS = 1,
    DRAW         = 2,
    CURSED_WIN   = 3,
    WIN          = 4,
    ILLEGAL      = 7,
};

// On-disk 4-bit code: the five classes share WDL_Entry's values, plus two
// markers for a WIN/LOSE at the 50mr edge. Only the dropped-frame derive reads
// the markers; everything else turns a stored code into a class via
// wdl_from_storage(). The distinct type keeps a marker out of semantic code.
enum struct WDL_Stored : uint8_t {
    LOSE          = 0,
    BLESSED_LOSS  = 1,
    DRAW          = 2,
    CURSED_WIN    = 3,
    WIN           = 4,
    BOUNDARY_LOSS = 5,
    BOUNDARY_WIN  = 6,
    ILLEGAL       = 7,
};

constexpr WDL_Entry wdl_from_storage(WDL_Stored s) {
    if (s == WDL_Stored::BOUNDARY_WIN)
        return WDL_Entry::WIN;
    if (s == WDL_Stored::BOUNDARY_LOSS)
        return WDL_Entry::LOSE;
    return static_cast<WDL_Entry>(s);
}

constexpr unsigned IGNORE_50MR        = ~0u;  // no 50-move clock: DTM50's unclocked layer
constexpr uint16_t DRAWN_AT_LAYER     = 0xFFFF;
constexpr uint16_t DTZ_MAX_NON_CURSED = 100;
constexpr unsigned DTM50_HMC_COUNT    = 100;  // clocks 0..99 have a DTM50 layer

// Magic values used by probe-visible table files.
enum struct EGTB_Magic : uint64_t {
    WDL_MAGIC   = 0x9bd1e3a6,
    DTZ_MAGIC   = 0x2ec8b161,
    DTC_MAGIC   = 0x2ec8b17e,
    DTM_MAGIC   = 0xab57c134,
    DTM50_MAGIC = 0xab57c151,
};

inline constexpr size_t WDL_ENTRY_PACK_RATIO = 2;
inline constexpr size_t WDL_ENTRY_BITS       = 4;

}  // namespace Stockfish::ChessTB

#endif  // CHESSTB_COMMON_H_INCLUDED
