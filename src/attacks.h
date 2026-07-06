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

#ifndef ATTACKS_H_INCLUDED
#define ATTACKS_H_INCLUDED

#include <cassert>
#include <array>
#include <initializer_list>

#include "types.h"
#include "bitboard.h"

#ifdef __aarch64__
    #include <arm_acle.h>
    #define USE_HYPERBOLA_QUINT
#elif defined(__loongarch__) && __loongarch_grlen == 64
    #define USE_HYPERBOLA_QUINT
#elif defined(USE_AVX2)
    #include <immintrin.h>
    #define USE_DUAL_HYPERBOLA_QUINT
#endif

namespace Stockfish::Attacks {

void init();

#ifdef USE_HYPERBOLA_QUINT

inline Bitboard reverse_bb(Bitboard bb) {
    #if __has_builtin(__builtin_bitreverse64)
    return __builtin_bitreverse64(bb);
    #else
        #ifdef __aarch64__
            #if defined(__GNUC__) && !defined(__clang__) \
              && (__GNUC__ < 12 || (__GNUC__ == 12 && __GNUC_MINOR__ < 2))
    // no rbit in arm_acle.h
    Bitboard out;
    asm("rbit %0, %1" : "=r"(out) : "r"(bb));
    return out;
            #else
    return __rbitll(bb);
            #endif
        #else  // loongarch
    Bitboard out;
    asm("bitrev.d %0, %1" : "=r"(out) : "r"(bb));
    return out;
        #endif
    #endif
}

// Hyperbola quintessence implementation for ARM, thanks to the availability of an
// efficient bit reversal instruction.
// See https://www.chessprogramming.org/Hyperbola_Quintessence
struct Magic {
    // For rooks: file attacks, rank attacks. For bishops: diagonal/antidiagonal
    Bitboard mask1, mask2;

    Bitboard hyperbola(Square s, Bitboard occupied, Bitboard mask) const {
        Bitboard o   = occupied & mask;
        Bitboard fwd = o - square_bb(s);
        Bitboard rev = reverse_bb(o) - square_bb(Square(63 - int(s)));
        return (fwd ^ reverse_bb(rev)) & mask;
    }

    Bitboard attacks_bb(Square s, Bitboard occupied) const {
        return hyperbola(s, occupied, mask1) | hyperbola(s, occupied, mask2);
    }
};

const Magic& magic(Square s, PieceType pt);

#elif defined(USE_DUAL_HYPERBOLA_QUINT)

struct alignas(32) DualMagic {
    // file, diagonal, unused, antidiagonal
    Bitboard maskFile, maskDiag, maskNone, maskAntidiag;
    // Precomputed 2 * square_bb(sq), 2 * reverse(square_bb(sq))
    Bitboard r, rr;

    const u8* RESTRICT rankAttacksLookup;
    // 8 * rank_of(sq)
    int shift;

    // We always compute [bishop, rook] attacks at once, then rely on
    // compiler's DCE and CSE to eliminate unneeded re-computations or extractions.
    //
    // When using hyperbola quintessence, file, diagonal and antidiagonal attacks
    // can use a byte reversal rather than a full bit reversal (because all squares
    // reside in different bytes). Rank atttacks cannot. Thus, for rank attacks
    // only, we use a compact lookup table indexed by the 8 bits of the rank's occupancy.
    std::pair<Bitboard, Bitboard> both_attacks_bb(Bitboard occupied) const {
        // Byteswap within 64-bit elements
        const auto bswap = [](__m256i v) {
            return _mm256_shuffle_epi8(v, _mm256_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                                          13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                                          10, 11, 12, 13, 14, 15));
        };

        // Each lane contains a mask and we follow the same HQ algorithm as
        // given above in the ARM64 code path
        const __m256i mask = _mm256_load_si256(reinterpret_cast<const __m256i*>(this));
        const __m256i rs   = _mm256_set1_epi64x(r);
        const __m256i rrs  = _mm256_set1_epi64x(rr);

        __m256i o      = _mm256_and_si256(mask, _mm256_set1_epi64x(occupied));
        __m256i fwd    = _mm256_sub_epi64(o, rs);
        __m256i rev    = bswap(_mm256_sub_epi64(bswap(o), rrs));
        __m256i result = _mm256_and_si256(_mm256_xor_si256(fwd, rev), mask);

        // Lane 0: rook attacks (file only); lane 1: bishop attacks
        __m128i rookBishop =
          _mm_or_si128(_mm256_extracti128_si256(result, 1), _mm256_castsi256_si128(result));

        Bitboard rowOccupancy = rankAttacksLookup[(occupied >> shift) & 0xff];
        Bitboard rankAttacks  = rowOccupancy << shift;

        // [bishop, rook]
        return {_mm_extract_epi64(rookBishop, 1), _mm_cvtsi128_si64(rookBishop) + rankAttacks};
    }
};

const DualMagic& dual_magic(Square s);

#else
// Magic holds all magic bitboards relevant data for a single square
struct Magic {
    Bitboard  mask;
    Bitboard* attacks;
    Bitboard  magic;
    unsigned  shift;

    // Compute the attack's index using the 'magic bitboards' approach
    unsigned index(Bitboard occupied) const {
        if (Is64Bit)
            return unsigned(((occupied & mask) * magic) >> shift);

        unsigned lo = unsigned(occupied) & unsigned(mask);
        unsigned hi = unsigned(occupied >> 32) & unsigned(mask >> 32);
        return (lo * unsigned(magic) ^ hi * unsigned(magic >> 32)) >> shift;
    }

    Bitboard attacks_bb([[maybe_unused]] Square s, Bitboard occupied) const {
        return attacks[index(occupied)];
    }
};

const Magic& magic(Square s, PieceType pt);

#endif

Bitboard line_bb(Square s1, Square s2);
Bitboard between_bb(Square s1, Square s2);
Bitboard ray_pass_bb(Square s1, Square s2);

// Returns the bitboard of target square for the given step
// from the given square. If the step is off the board, returns empty bitboard.
constexpr Bitboard safe_destination(Square s, int step) {
    constexpr auto abs = [](int v) { return v < 0 ? -v : v; };
    Square         to  = Square(s + step);
    return is_ok(to) && abs(file_of(s) - file_of(to)) <= 2 ? square_bb(to) : Bitboard(0);
}

constexpr Bitboard sliding_attack(PieceType pt, Square sq, Bitboard occupied) {
    Bitboard            attacks = 0, dest = 0;
    constexpr Direction RookDirections[4]   = {NORTH, SOUTH, EAST, WEST};
    constexpr Direction BishopDirections[4] = {NORTH_EAST, SOUTH_EAST, SOUTH_WEST, NORTH_WEST};

    for (Direction d : (pt == ROOK ? RookDirections : BishopDirections))
    {
        Square s = sq;
        while ((dest = safe_destination(s, d)))
        {
            attacks |= dest;
            s += d;
            if (occupied & dest)
            {
                break;
            }
        }
    }

    return attacks;
}

constexpr Bitboard knight_attack(Square sq) {
    Bitboard b = {};
    for (int step : {-17, -15, -10, -6, 6, 10, 15, 17})
        b |= safe_destination(sq, step);
    return b;
}

constexpr Bitboard king_attack(Square sq) {
    Bitboard b = {};
    for (int step : {-9, -8, -7, -1, 1, 7, 8, 9})
        b |= safe_destination(sq, step);
    return b;
}

constexpr Bitboard pseudo_attacks(PieceType pt, Square sq) {
    switch (pt)
    {
    case PieceType::ROOK :
    case PieceType::BISHOP :
        return sliding_attack(pt, sq, 0);
    case PieceType::QUEEN :
        return sliding_attack(PieceType::ROOK, sq, 0) | sliding_attack(PieceType::BISHOP, sq, 0);
    case PieceType::KNIGHT :
        return knight_attack(sq);
    case PieceType::KING :
        return king_attack(sq);
    default :
        assert(false);
        return 0;
    }
}

inline constexpr auto PseudoAttacks = []() constexpr {
    std::array<std::array<Bitboard, SQUARE_NB>, PIECE_TYPE_NB> attacks{};

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
    {
        attacks[WHITE][s1] = pawn_attacks_bb<WHITE>(square_bb(s1));
        attacks[BLACK][s1] = pawn_attacks_bb<BLACK>(square_bb(s1));

        attacks[KING][s1]   = pseudo_attacks(KING, s1);
        attacks[KNIGHT][s1] = pseudo_attacks(KNIGHT, s1);
        attacks[QUEEN][s1] = attacks[BISHOP][s1] = pseudo_attacks(BISHOP, s1);
        attacks[QUEEN][s1] |= attacks[ROOK][s1]  = pseudo_attacks(ROOK, s1);
    }

    return attacks;
}();

inline constexpr auto PawnPushOrAttacks = []() constexpr {
    std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> attacks{};

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
    {
        attacks[WHITE][s1] = pawn_single_push_bb(WHITE, square_bb(s1)) | PseudoAttacks[WHITE][s1];
        attacks[BLACK][s1] = pawn_single_push_bb(BLACK, square_bb(s1)) | PseudoAttacks[BLACK][s1];
    }

    return attacks;
}();

// Returns the pseudo attacks of the given piece type
// assuming an empty board.
template<PieceType Pt>
inline Bitboard attacks_bb(Square s, Color c = COLOR_NB) {

    assert((Pt != PAWN || c < COLOR_NB) && is_ok(s));
    return Pt == PAWN ? PseudoAttacks[c][s] : PseudoAttacks[Pt][s];
}

// Returns the attacks by the given piece
// assuming the board is occupied according to the passed Bitboard.
// Sliding piece attacks do not continue passed an occupied square.
template<PieceType Pt>
inline Bitboard attacks_bb(Square s, Bitboard occupied) {

    assert(Pt != PAWN && is_ok(s));

#ifdef USE_DUAL_HYPERBOLA_QUINT
    const auto [bishop, rook] = dual_magic(s).both_attacks_bb(occupied);

    switch (Pt)
    {
    case BISHOP :
        return bishop;
    case ROOK :
        return rook;
    case QUEEN :
        return bishop | rook;
    default :
        return PseudoAttacks[Pt][s];
    }
#else
    switch (Pt)
    {
    case BISHOP :
    case ROOK :
        return magic(s, Pt).attacks_bb(s, occupied);
    case QUEEN :
        return attacks_bb<BISHOP>(s, occupied) | attacks_bb<ROOK>(s, occupied);
    default :
        return PseudoAttacks[Pt][s];
    }
#endif
}

// Returns the attacks by the given piece
// assuming the board is occupied according to the passed Bitboard.
// Sliding piece attacks do not continue passed an occupied square.
inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occupied) {

    assert(pt != PAWN && is_ok(s));

    switch (pt)
    {
    case BISHOP :
        return attacks_bb<BISHOP>(s, occupied);
    case ROOK :
        return attacks_bb<ROOK>(s, occupied);
    case QUEEN :
        return attacks_bb<BISHOP>(s, occupied) | attacks_bb<ROOK>(s, occupied);
    default :
        return PseudoAttacks[pt][s];
    }
}

inline Bitboard attacks_bb(Piece pc, Square s, Bitboard occupied) {
    return type_of(pc) == PAWN ? PseudoAttacks[color_of(pc)][s]
                               : attacks_bb(type_of(pc), s, occupied);
}

}  // namespace Stockfish::Attacks

#endif  // #ifndef ATTACKS_H_INCLUDED
