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

#ifndef CHESSTB_INDEX_H_INCLUDED
#define CHESSTB_INDEX_H_INCLUDED

#include "../bitboard.h"
#include "../types.h"
#include "common.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Stockfish::ChessTB {

// An opposing pawn pair ('p'): a white and a black pawn on one file, white
// below black, so neither can pass or promote. White ranks 2..6, black 3..7:
// 15 x 8 files = 120 placements; the slice manager folds the mirror on top.
struct Pair_Group {
    using Index = uint32_t;

    Pair_Group() {
        m_inverse.fill(-1);
        size_t n = 0;
        // Layout: file-major; within a file, ascending white rank then black rank.
        for (int f = 0; f < 8; ++f)
        {
            const File file = static_cast<File>(f);
            for (int wr = static_cast<int>(RANK_2); wr <= static_cast<int>(RANK_6); ++wr)
                for (int br = wr + 1; br <= static_cast<int>(RANK_7); ++br)
                {
                    const Square w           = make_square(file, static_cast<Rank>(wr));
                    const Square b           = make_square(file, static_cast<Rank>(br));
                    m_inverse[inv_key(w, b)] = static_cast<int32_t>(n);
                    m_white[n]               = w;
                    m_black[n++]             = b;
                }
        }
    }

    static constexpr size_t PLACEMENT_COUNT = 120;

    static constexpr size_t table_size() { return PLACEMENT_COUNT; }

    Square white_square(Index i) const { return m_white[i]; }
    Square black_square(Index i) const { return m_black[i]; }

    // Caller guarantees a legal pair.
    Index index_of(Square white_sq, Square black_sq) const {
        const int32_t idx = m_inverse[inv_key(white_sq, black_sq)];
        assert(idx >= 0);
        return static_cast<Index>(idx);
    }

    // Same file, white below black, both on ranks the enumeration covers.
    static bool is_opposing_pair(Square white_sq, Square black_sq) {
        if (file_of(white_sq) != file_of(black_sq))
            return false;
        const int wr = static_cast<int>(rank_of(white_sq));
        const int br = static_cast<int>(rank_of(black_sq));
        return wr >= static_cast<int>(RANK_2) && br <= static_cast<int>(RANK_7) && wr < br;
    }

    // Pair pawns look like free ones, so the pair is the minimal one by (file,
    // white rank, black rank): on the first file holding one, the lowest white
    // pawn with a black one above, then the lowest such black pawn. False if none.
    static bool
    canonical_pair(Bitboard white, Bitboard black, Square& out_white, Square& out_black) {
        for (File f = FILE_A; f <= FILE_H; ++f)
        {
            const Bitboard w = white & file_bb(f), b = black & file_bb(f);
            if (w && b && lsb(w) < msb(b))
            {
                out_white = lsb(w);
                out_black = lsb(b & (~Bitboard(0) << out_white));
                return true;
            }
        }
        return false;
    }

   private:
    static int inv_key(Square w, Square b) {
        return static_cast<int>(w) * 64 + static_cast<int>(b);
    }

    std::array<Square, PLACEMENT_COUNT> m_white;
    std::array<Square, PLACEMENT_COUNT> m_black;
    std::array<int32_t, 64 * 64>        m_inverse;
};

// Joint placement of the men standing rights pin: each unmoved king and the
// rooks it could still castle with. 56 per rights-holding side, the joint table
// their product. The enumeration order is baked into stored king-slice ids.
struct Castling_Group {
    using Index                        = int32_t;
    static constexpr Index  INDEX_NONE = -1;
    static constexpr size_t MAX_RIGHTS = 2;
    // A side's castling rooks as a mask over the files of its home rank.
    static constexpr size_t ROOK_MASKS = size_t(1) << FILE_NB;

    // One side's pinned men on its home rank: its king's file, and its castling
    // rooks as a mask of files.
    struct Side_Placement {
        File    king  = FILE_NB;
        uint8_t rooks = 0;
    };

    Castling_Group(size_t white_rights, size_t black_rights) :
        m_rights{white_rights, black_rights} {
        assert(white_rights <= MAX_RIGHTS && black_rights <= MAX_RIGHTS);
        assert(white_rights + black_rights > 0);
        for (const Color c : {WHITE, BLACK})
            m_side[c].build(m_rights[c]);
    }

    size_t rights(Color c) const { return m_rights[c]; }
    bool   both_kings_pinned() const { return m_rights[WHITE] > 0 && m_rights[BLACK] > 0; }

    size_t table_size() const {
        return std::max<size_t>(m_side[WHITE].size(), 1)
             * std::max<size_t>(m_side[BLACK].size(), 1);
    }

    // Index order: white's placement outermost, black's within it. Nothing
    // depends on the order beyond it being stable -- but it must stay stable,
    // since stored king-slice ids are built from it.
    const Side_Placement& side(Index idx, Color c) const {
        assert(idx >= 0 && static_cast<size_t>(idx) < table_size());
        assert(m_rights[c] > 0);
        const size_t b_span = std::max<size_t>(m_side[BLACK].size(), 1);
        const size_t within =
          (c == WHITE) ? static_cast<size_t>(idx) / b_span : static_cast<size_t>(idx) % b_span;
        return m_side[c].placements[within];
    }

    // The placement of each side's king file and rook mask, INDEX_NONE if they
    // are not one; a side without rights is ignored.
    Index
    index_of(File white_king, uint8_t white_rooks, File black_king, uint8_t black_rooks) const {
        const File    king[COLOR_NB]  = {white_king, black_king};
        const uint8_t rooks[COLOR_NB] = {white_rooks, black_rooks};

        size_t within[COLOR_NB] = {0, 0};
        for (const Color c : {WHITE, BLACK})
        {
            if (m_rights[c] == 0)
                continue;
            const int32_t i = m_side[c].inverse[king[c] * ROOK_MASKS + rooks[c]];
            if (i < 0)
                return INDEX_NONE;
            within[c] = static_cast<size_t>(i);
        }
        const size_t b_span = std::max<size_t>(m_side[BLACK].size(), 1);
        return static_cast<Index>(within[WHITE] * b_span + within[BLACK]);
    }

    Square king_square(Index idx, Color c) const {
        return make_square(side(idx, c).king, relative_rank(c, RANK_1));
    }

    // The castling rooks' squares.
    Bitboard rooks(Index idx, Color c) const {
        return Bitboard(side(idx, c).rooks) << (8 * relative_rank(c, RANK_1));
    }

   private:
    // One side's placements, enumerated king file first then rook files, all
    // ascending, and their index by king file * ROOK_MASKS + rook mask. Empty for a
    // side holding no rights.
    struct Side_Table {
        std::vector<Side_Placement>               placements;
        std::array<int32_t, FILE_NB * ROOK_MASKS> inverse{};

        size_t size() const { return placements.size(); }

        void build(size_t num_rights) {
            inverse.fill(INDEX_NONE);
            if (num_rights == 0)
                return;

            for (File k = FILE_A; k < FILE_NB; ++k)
            {
                if (num_rights == 1)
                {
                    for (File r = FILE_A; r < FILE_NB; ++r)
                        if (r != k)
                            add({k, uint8_t(1 << r)});
                }
                else
                {
                    for (File lo = FILE_A; lo < k; ++lo)
                        for (File hi = static_cast<File>(k + 1); hi < FILE_NB; ++hi)
                            add({k, uint8_t((1 << lo) | (1 << hi))});
                }
            }
        }

        void add(const Side_Placement& p) {
            inverse[p.king * ROOK_MASKS + p.rooks] = static_cast<int32_t>(placements.size());
            placements.push_back(p);
        }
    };

    std::array<size_t, COLOR_NB> m_rights;
    Side_Table                   m_side[COLOR_NB];
};

// One class per piece type per color.
enum Piece_Type_Class : int8_t {
    KINGS,
    KNIGHTS,
    BISHOPS,
    ROOKS,
    QUEENS,
    PAWNS,
    PIECE_TYPE_CLASS_NB
};

enum Piece_Class : int8_t {
    BLACK_KINGS,
    BLACK_KNIGHTS,
    BLACK_BISHOPS,
    BLACK_ROOKS,
    BLACK_QUEENS,
    BLACK_PAWNS,
    WHITE_KINGS,
    WHITE_KNIGHTS,
    WHITE_BISHOPS,
    WHITE_ROOKS,
    WHITE_QUEENS,
    WHITE_PAWNS,
    PIECE_CLASS_NB
};

constexpr Piece_Class& operator++(Piece_Class& p_class) {
    p_class = static_cast<Piece_Class>(static_cast<int>(p_class) + 1);
    return p_class;
}

constexpr Piece_Class make_piece_class(Color color, Piece_Type_Class pt_class) {
    assert(pt_class < PIECE_TYPE_CLASS_NB);
    return static_cast<Piece_Class>(int(color == WHITE ? WHITE_KINGS : BLACK_KINGS)
                                    + int(pt_class));
}

struct Piece_Config {
    // Uppercase letters are plain pieces, coloured by side order. 'p' is an
    // opposing pawn pair (one at most); 'r' a rook still holding a castling right
    // with its king (KrK is KRK with the right indexed; two per side at most).

    static constexpr std::array<int16_t, PIECE_NB> PIECE_STRENGTH_FOR_SIDE_ORDER = []() {
        std::array<int16_t, PIECE_NB> arr{};
        // Kings present on both sides — value irrelevant for side ordering.
        arr[W_KING] = arr[B_KING] = 0;
        arr[W_QUEEN] = arr[B_QUEEN] = 900;
        arr[W_ROOK] = arr[B_ROOK] = 500;
        arr[W_BISHOP] = arr[B_BISHOP] = 330;
        arr[W_KNIGHT] = arr[B_KNIGHT] = 320;
        arr[W_PAWN] = arr[B_PAWN] = 100;
        return arr;
    }();

    // Sort order within a side: K, Q, R, B, N, P (descending strength, kings first).
    static constexpr std::array<int8_t, PIECE_NB> PIECE_ORDER = []() {
        std::array<int8_t, PIECE_NB> ret{};
        int8_t                       i           = 1;
        constexpr Color              AllColors[] = {WHITE, BLACK};
        for (const Color color : AllColors)
        {
            ret[make_piece(color, KING)]   = i++;
            ret[make_piece(color, QUEEN)]  = i++;
            ret[make_piece(color, ROOK)]   = i++;
            ret[make_piece(color, BISHOP)] = i++;
            ret[make_piece(color, KNIGHT)] = i++;
            ret[make_piece(color, PAWN)]   = i++;
        }
        return ret;
    }();

    static bool sort_pieces(Span<Piece>                         pieces,
                            const std::array<size_t, COLOR_NB>& castling_rights = {0, 0});

    static bool is_constructible_from(Const_Span<Piece> pieces) {
        if (pieces.size() < 2 || pieces.size() > MAX_MAN)
            return false;

        if (std::count(pieces.begin(), pieces.end(), W_KING) != 1)
            return false;

        if (std::count(pieces.begin(), pieces.end(), B_KING) != 1)
            return false;

        return true;
    }

    using Castling_Rights_Counts = std::array<size_t, COLOR_NB>;
    static constexpr Castling_Rights_Counts NO_CASTLE_RIGHTS{0, 0};

    Piece_Config(Const_Span<Piece>             pcs,
                 const Castling_Rights_Counts& castling_rights = NO_CASTLE_RIGHTS) :
        m_num_pieces(0) {
        assert(is_constructible_from(pcs));

        std::memcpy(m_pieces, pcs.data(), pcs.size() * sizeof(Piece));

        const bool swapped = sort_pieces(Span(m_pieces, pcs.size()), castling_rights);

        m_castling_rights = castling_rights;
        if (swapped)
            std::swap(m_castling_rights[WHITE], m_castling_rights[BLACK]);

        for (const Piece p : Const_Span(m_pieces, m_pieces + pcs.size()))
        {
            m_base_mat_key.add_piece(p);
            m_mirr_mat_key.add_piece(~p);
        }
        if (has_castling())
        {
            m_base_mat_key.add_castling(m_castling_rights[WHITE], m_castling_rights[BLACK]);
            m_mirr_mat_key.add_castling(m_castling_rights[BLACK], m_castling_rights[WHITE]);
        }
        m_num_pieces = pcs.size();
    }

    auto pieces() const { return Const_Span(m_pieces, m_pieces + m_num_pieces); }

    std::string name() const {
        std::string s;
        bool        seen_white_king = false;
        // Castling rooks are not in m_pieces, so they go in where they would have
        // sorted: before the first man of their color that outranks a rook.
        std::array<size_t, COLOR_NB> written{0, 0};
        auto                         write_castling_rooks_before = [&](int8_t order) {
            for (const Color c : {WHITE, BLACK})
            {
                if (written[c] == m_castling_rights[c])
                    continue;
                if (PIECE_ORDER[make_piece(c, ROOK)] >= order)
                    continue;
                s.append(m_castling_rights[c] - written[c], 'r');
                written[c] = m_castling_rights[c];
            }
        };
        for (size_t i = 0; i < m_num_pieces; ++i)
        {
            const Piece     p  = m_pieces[i];
            const PieceType pt = type_of(p);
            write_castling_rooks_before(PIECE_ORDER[p]);
            if (pt == KING)
            {
                if (seen_white_king && m_has_opposing_pair)
                    s += 'p';
                seen_white_king = true;
            }
            s += " PNBRQK"[pt];
        }
        write_castling_rooks_before(std::numeric_limits<int8_t>::max());
        if (m_has_opposing_pair)
            s += 'p';
        return s;
    }

    const std::array<size_t, PIECE_NB> piece_counts() const {
        std::array<size_t, PIECE_NB> counts;
        std::fill(counts.begin(), counts.end(), 0);
        for (const Piece piece : pieces())
            counts[piece] += 1;
        return counts;
    }

    bool has_opposing_pair() const { return m_has_opposing_pair; }

    bool   has_castling() const { return m_castling_rights[WHITE] + m_castling_rights[BLACK] > 0; }
    size_t castling_rights(Color c) const { return m_castling_rights[c]; }
    const Castling_Rights_Counts& castling_rights() const { return m_castling_rights; }

    bool has_pawns() const {
        if (m_has_opposing_pair)
            return true;
        for (const Piece p : pieces())
            if (type_of(p) == PAWN)
                return true;
        return false;
    }

    Material_Key base_material_key() const { return m_base_mat_key; }

    std::pair<Material_Key, Material_Key> material_keys() const {
        return {m_base_mat_key, m_mirr_mat_key};
    }

    Material_Key min_material_key() const { return std::min(m_base_mat_key, m_mirr_mat_key); }

    void mark_opposing_pair() {
        m_has_opposing_pair = true;
        m_base_mat_key.add_pair();
        m_mirr_mat_key.add_pair();
    }

   private:
    Piece  m_pieces[MAX_MAN];
    size_t m_num_pieces;
    bool   m_has_opposing_pair = false;
    // Castling rights per color, whose rooks are not in m_pieces.
    Castling_Rights_Counts m_castling_rights{0, 0};
    Material_Key           m_base_mat_key;
    Material_Key           m_mirr_mat_key;
};

// Permutation index over the populated non-pawn classes, stored as a bare uint32
// (perm 0 == native order). Validity is just the FACTORIAL range bound.
inline constexpr std::array<uint32_t, 9> FACTORIAL = {1u,   1u,   2u,    6u,    24u,
                                                      120u, 720u, 5040u, 40320u};

template<typename Config>
bool index_permutation_config_is_valid(const Config& cfg, uint32_t perm) {
    const size_t n = cfg.num_populated_classes();
    return n <= 8 && perm < FACTORIAL[n];
}

template<typename Config>
std::array<Piece_Class, PIECE_CLASS_NB> storage_within_class_order(const Config& cfg,
                                                                   uint32_t      perm) {
    assert(index_permutation_config_is_valid(cfg, perm));
    const size_t n = cfg.num_populated_classes();

    std::array<Piece_Class, PIECE_CLASS_NB> available{};
    for (size_t i = 0; i < n; ++i)
        available[i] = cfg.populated_classes()[i];

    std::array<Piece_Class, PIECE_CLASS_NB> order{};
    uint32_t                                idx = perm;
    for (size_t i = 0; i < n; ++i)
    {
        const uint32_t f    = FACTORIAL[n - 1 - i];
        const size_t   pick = idx / f;
        idx %= f;
        assert(pick < n - i);
        order[i] = available[pick];
        for (size_t j = pick; j + 1 < n - i; ++j)
            available[j] = available[j + 1];
    }
    return order;
}

// Emits survivor bits in cartesian order (pair, white, black). `white` and
// `black` list each side's free-pawn placements (one empty board if none);
// each call gives up to 64 bits from `cart`, sharing a word across calls
// when a radix isn't word-aligned.
template<typename Emit>
void build_pawn_slice_bits(const Pair_Group*            pair,
                           const std::vector<Bitboard>& white,
                           const std::vector<Bitboard>& black,
                           Emit&&                       emit) {
    if (pair)
    {
        using Mask = std::array<uint64_t, 2>;
        static_assert(Pair_Group::PLACEMENT_COUNT == 120);
        auto prefix = [](size_t n) -> Mask {
            return {n >= 64 ? ~uint64_t{0} : (uint64_t{1} << n) - 1,
                    n <= 64 ? 0 : (uint64_t{1} << (n - 64)) - 1};
        };
        const Mask                  all = prefix(pair->table_size());
        std::array<Mask, SQUARE_NB> white_allowed{}, black_allowed{};
        for (size_t p = 0; p < pair->table_size(); ++p)
        {
            const auto   pi = static_cast<Pair_Group::Index>(p);
            const Square pw = pair->white_square(pi), pb = pair->black_square(pi);
            for (Square sq = SQ_A2; sq <= SQ_H7; ++sq)
            {
                if (sq == pw || sq == pb)
                    continue;
                Square cw = SQ_NONE, cb = SQ_NONE;
                Pair_Group::canonical_pair(square_bb(pw) | sq, square_bb(pb), cw, cb);
                if (cw == pw && cb == pb)
                    white_allowed[sq][p >> 6] |= uint64_t{1} << (p & 63);
                Pair_Group::canonical_pair(square_bb(pw), square_bb(pb) | sq, cw, cb);
                if (cw == pw && cb == pb)
                    black_allowed[sq][p >> 6] |= uint64_t{1} << (p & 63);
            }
        }
        auto candidates = [&](const std::vector<Bitboard>&       placements,
                              const std::array<Mask, SQUARE_NB>& allowed) {
            std::vector<Mask> result(placements.size(), all);
            for (size_t i = 0; i < placements.size(); ++i)
                for (Bitboard b = placements[i]; b;)
                {
                    const Square sq = pop_lsb(b);
                    for (size_t word = 0; word < 2; ++word)
                        result[i][word] &= allowed[sq][word];
                }
            return result;
        };
        const auto white_candidates = candidates(white, white_allowed);
        const auto black_candidates = candidates(black, black_allowed);
        size_t     cart             = 0;
        for (size_t b = 0; b < black.size(); ++b)
        {
            // Pair indices follow canonical ordering. The designated pair
            // must precede every pair among the additional pawns as well.
            std::array<Mask, SQUARE_NB> before;
            before.fill(all);
            for (Square ws = SQ_A2; ws <= SQ_H7; ++ws)
            {
                size_t first = pair->table_size();
                for (Bitboard t = black[b]; t;)
                {
                    const Square bs = pop_lsb(t);
                    if (Pair_Group::is_opposing_pair(ws, bs))
                        first = std::min(first, static_cast<size_t>(pair->index_of(ws, bs)));
                }
                before[ws] = prefix(first);
            }
            for (size_t w = 0; w < white.size(); ++w, cart += pair->table_size())
            {
                if (white[w] & black[b])
                    continue;
                Mask survivors;
                for (size_t word = 0; word < 2; ++word)
                {
                    survivors[word] = white_candidates[w][word] & black_candidates[b][word];
                    for (Bitboard t = white[w]; t;)
                        survivors[word] &= before[pop_lsb(t)][word];
                }
                emit(cart, survivors[0]);
                emit(cart + 64, survivors[1]);
            }
        }
    }
    else
    {
        // Each black placement excludes every white placement touching
        // any of its squares. Compute a whole word of placements at once.
        const size_t                                 words = (white.size() + 63) / 64;
        std::array<std::vector<uint64_t>, SQUARE_NB> occupied;
        for (auto& bits : occupied)
            bits.resize(words);
        for (size_t w = 0; w < white.size(); ++w)
            for (Bitboard t = white[w]; t;)
                occupied[pop_lsb(t)][w >> 6] |= uint64_t{1} << (w & 63);
        const size_t tail = white.size() & 63;
        for (size_t b = 0; b < black.size(); ++b)
            for (size_t word = 0; word < words; ++word)
            {
                uint64_t blocked = 0;
                for (Bitboard t = black[b]; t;)
                    blocked |= occupied[pop_lsb(t)][word];
                uint64_t survivors = ~blocked;
                if (word + 1 == words && tail)
                    survivors &= (uint64_t{1} << tail) - 1;
                emit(b * white.size() + word * 64, survivors);
            }
    }
}

enum Board_Index : uint64_t {
    BOARD_INDEX_ZERO = 0,
    BOARD_INDEX_NONE = std::numeric_limits<uint64_t>::max()
};

enum struct Symmetry_Group : uint8_t {
    NONE,
    FILE_MIRROR,
    DIHEDRAL_8,
};

struct Piece_Group {
    static constexpr size_t MAX_PIECE_GROUP_SIZE = 7;

    using Placement_Index = uint32_t;

    Piece_Group(Piece pc, size_t count);

    // Colex rank of the pieces on `b`; the legal squares run on from the first.
    Placement_Index index_of(Bitboard b) const;
    // The pieces of placement `idx`.
    Bitboard squares(Placement_Index idx) const;

    size_t table_size() const { return m_table_size; }
    size_t size() const { return m_num_pieces; }
    Piece  piece() const { return m_piece; }

   private:
    Piece    m_piece;
    size_t   m_num_pieces;
    size_t   m_num_legal_squares;
    Square   m_first_square;
    uint32_t m_table_size;
};

inline constexpr int32_t SLICE_NONE = -1;

struct King_Slice_Manager {
    size_t num_slices = 0;

    struct Pair_Lookup {
        int32_t slice_id;
        // Bits: 1 mirrors files, 2 ranks, 4 then the a1-h8 diagonal.
        uint8_t transform;
        uint8_t has_diag_stabilizer;
    };

    std::array<Pair_Lookup, SQUARE_NB * SQUARE_NB> pair_lookup{};

    // Castling managers only. Enumerates in the tables' order -- the slice ids
    // are on disk. Both sides share Castling_Group for that reason, so only this
    // loop is duplicated, not the placement order.
    bool                         has_castling = false;
    std::array<size_t, COLOR_NB> castling_rights{0, 0};
    std::vector<int32_t>         castling_slice_lookup;

    explicit King_Slice_Manager(Symmetry_Group s);
    // Castling: no symmetry.
    King_Slice_Manager(size_t white_rights, size_t black_rights);

    const Pair_Lookup& lookup(Square wk, Square bk) const {
        assert(!has_castling);
        return pair_lookup[static_cast<int>(wk) * SQUARE_NB + static_cast<int>(bk)];
    }

    // wk/bk and each side's castling rooks; SLICE_NONE if they are not on
    // their home rank or not a placement of the rights.
    int32_t
    castling_slice_of(Square wk, Square bk, Bitboard white_rooks, Bitboard black_rooks) const {
        assert(has_castling);
        const Square   king_sq[COLOR_NB] = {wk, bk};
        const Bitboard rooks[COLOR_NB]   = {white_rooks, black_rooks};

        File    king_file[COLOR_NB] = {FILE_NB, FILE_NB};
        uint8_t rook_mask[COLOR_NB] = {0, 0};
        for (const Color c : {WHITE, BLACK})
        {
            if (castling_rights[c] == 0)
            {
                if (rooks[c])
                    return SLICE_NONE;
                continue;
            }
            const Rank home = relative_rank(c, RANK_1);
            if (rank_of(king_sq[c]) != home || (rooks[c] & ~rank_bb(home)))
                return SLICE_NONE;
            king_file[c] = file_of(king_sq[c]);
            rook_mask[c] = uint8_t(rooks[c] >> (8 * home));
        }

        const Castling_Group::Index ci = m_castling_group->index_of(
          king_file[WHITE], rook_mask[WHITE], king_file[BLACK], rook_mask[BLACK]);
        if (ci == Castling_Group::INDEX_NONE)
            return SLICE_NONE;

        const size_t free_slot =
          m_castling_group->both_kings_pinned()
            ? size_t(0)
            : static_cast<size_t>(king_sq[castling_rights[WHITE] == 0 ? WHITE : BLACK]);
        return castling_slice_lookup[static_cast<size_t>(ci) * m_free_king_stride + free_slot];
    }

   private:
    std::shared_ptr<Castling_Group> m_castling_group;
    size_t                          m_free_king_stride = 1;
};

inline constexpr size_t MAX_TOTAL_PAWNS = 6;

// Depends on the pawns only: the opposing pair and each side's other pawns.
struct Pawn_Slice_Manager {
    Pawn_Slice_Manager(bool has_pair, size_t white_pawns, size_t black_pawns);

    Pawn_Slice_Manager(const Pawn_Slice_Manager&)            = delete;
    Pawn_Slice_Manager& operator=(const Pawn_Slice_Manager&) = delete;

    size_t num_slices() const { return m_num_slices; }
    bool   has_pawns() const { return m_has_pawns; }

    // The pair's squares (unused without one) and each side's other pawns.
    int32_t
    lookup(Square pair_white_sq, Square pair_black_sq, Bitboard white, Bitboard black) const;

   private:
    struct Rank_Block {
        int32_t  survivors_before = 0;
        uint64_t bits             = 0;
    };

    // Storage id of a cartesian cell: the number of surviving cells below it.
    int32_t storage_of_cartesian(size_t cart) const {
        const Rank_Block& blk   = m_rank_blocks[cart >> 6];
        const uint64_t    below = (uint64_t{1} << (cart & 63)) - 1;
        return blk.survivors_before + static_cast<int32_t>(popcount(blk.bits & below));
    }
    bool cartesian_survives(size_t cart) const {
        return (m_rank_blocks[cart >> 6].bits & (uint64_t{1} << (cart & 63))) != 0;
    }

    bool   m_has_pawns            = false;
    size_t m_num_slices           = 1;
    size_t m_num_cartesian_slices = 1;
    size_t m_pair_table_size      = 1;
    size_t m_white_table_size     = 1;
    size_t m_black_table_size     = 1;
    size_t m_white_free_size      = 1;  // without a pair: white's placements per black one

    std::vector<Rank_Block> m_rank_blocks;

    std::unique_ptr<const Pair_Group>  m_pair_group;
    std::unique_ptr<const Piece_Group> m_white_group;
    std::unique_ptr<const Piece_Group> m_black_group;
};

// The manager for these pawns, built once and shared by every material that
// has them.
const Pawn_Slice_Manager& pawn_slice_mgr(bool has_pair, size_t white_pawns, size_t black_pawns);

struct Index_Storage_Layout {
    size_t                                  n = 0;
    std::array<Piece_Class, PIECE_CLASS_NB> order{};
    std::array<size_t, PIECE_CLASS_NB>      radix{};
};

template<typename Config>
Index_Storage_Layout make_index_storage_layout(const Config& cfg, uint32_t perm) {
    assert(index_permutation_config_is_valid(cfg, perm));

    Index_Storage_Layout layout;
    layout.n         = cfg.num_populated_classes();
    const auto order = storage_within_class_order(cfg, perm);
    for (size_t i = 0; i < layout.n; ++i)
    {
        layout.order[i] = order[i];
        layout.radix[i] = cfg.group(order[i]).table_size();
    }
    return layout;
}

struct Position_Index_Config: public Piece_Config {
   private:
    static constexpr uint64_t MAX_NUM_POSITIONS = 0xffffffffffffull;

    static Symmetry_Group pick_symmetry(const Piece_Config& ps) {
        if (ps.has_castling())
            return Symmetry_Group::NONE;
        const auto counts    = ps.piece_counts();
        const bool has_pawns = counts[W_PAWN] > 0 || counts[B_PAWN] > 0 || ps.has_opposing_pair();
        return has_pawns ? Symmetry_Group::FILE_MIRROR : Symmetry_Group::DIHEDRAL_8;
    }

   public:
    explicit Position_Index_Config(const Piece_Config& ps);

    Position_Index_Config(const Position_Index_Config&)            = delete;
    Position_Index_Config& operator=(const Position_Index_Config&) = delete;
    Position_Index_Config(Position_Index_Config&&)                 = default;
    Position_Index_Config& operator=(Position_Index_Config&&)      = default;

    const King_Slice_Manager& king_slice_manager() const { return *m_king_slice_manager; }
    const Pawn_Slice_Manager& pawn_slice_manager() const { return *m_pawn_slice_manager; }

    const Piece_Group& group(Piece_Class c) const {
        assert(m_groups[c] != nullptr);
        return *m_groups[c];
    }

    bool               is_populated(Piece_Class c) const { return m_groups[c] != nullptr; }
    const Piece_Class* populated_classes() const { return m_populated_classes; }
    size_t             num_populated_classes() const { return m_num_populated_classes; }
    size_t             weight(Piece_Class c) const { return m_weights[c]; }

    uint64_t pawn_slice_stride() const { return m_pawn_slice_stride; }
    size_t   within_slice_size() const { return m_within_slice_size; }

   private:
    const King_Slice_Manager* m_king_slice_manager = nullptr;
    const Pawn_Slice_Manager* m_pawn_slice_manager = nullptr;

    size_t   m_num_pawn_slices   = 1;
    uint64_t m_pawn_slice_stride = 0;
    size_t   m_within_slice_size = 0;

    size_t      m_num_populated_classes = 0;
    Piece_Class m_populated_classes[PIECE_CLASS_NB]{};

    std::unique_ptr<const Piece_Group> m_groups[PIECE_CLASS_NB];
    size_t                             m_weights[PIECE_CLASS_NB]{};
};

Board_Index board_index_of_position(const Position_Index_Config& cfg,
                                    const Index_Storage_Layout&  layout,
                                    const View&                  pos);

}  // namespace Stockfish::ChessTB

#endif  // CHESSTB_INDEX_H_INCLUDED
