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

#include "index.h"

#include "../attacks.h"
#include "../bitboard.h"
#include "../types.h"

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <vector>

namespace Stockfish::ChessTB {

namespace {

constexpr std::array<std::array<uint32_t, 8>, 65> BINOMIAL = []() {
    std::array<std::array<uint32_t, 8>, 65> b{};
    for (size_t k = 0; k <= 64; ++k)
    {
        b[k][0] = 1;
        for (size_t n = 1; n <= 7; ++n)
            b[k][n] = (n > k) ? 0 : b[k - 1][n - 1] + b[k - 1][n];
    }
    return b;
}();

// White king squares of the slices: files a-d under the file mirror, the
// a1-d1-d4 triangle under all eight symmetries.
constexpr Bitboard ANCHOR_FILE_MIRROR = 0x0F0F0F0F0F0F0F0FULL;
constexpr Bitboard ANCHOR_TRIANGLE    = 0x00000000080C0E0FULL;
constexpr Bitboard MAIN_DIAG          = 0x8040201008040201ULL;
constexpr Bitboard ABOVE_DIAG         = 0x7F3F1F0F07030100ULL;

constexpr Bitboard flip_files(Bitboard b) {
    constexpr Bitboard k1 = 0x5555555555555555, k2 = 0x3333333333333333, k4 = 0x0F0F0F0F0F0F0F0F;
    b = ((b >> 1) & k1) | ((b & k1) << 1);
    b = ((b >> 2) & k2) | ((b & k2) << 2);
    return ((b >> 4) & k4) | ((b & k4) << 4);
}

// The a1-h8 mirror, by delta swaps.
constexpr Bitboard transpose(Bitboard b) {
    constexpr Bitboard k1 = 0x5500550055005500, k2 = 0x3333000033330000, k4 = 0x0F0F0F0F00000000;
    Bitboard           t = k4 & (b ^ (b << 28));
    b ^= t ^ (t >> 28);
    t = k2 & (b ^ (b << 14));
    b ^= t ^ (t >> 14);
    t = k1 & (b ^ (b << 7));
    return b ^ t ^ (t >> 7);
}

// The transform mask (Pair_Lookup::transform) on every square of `b`.
Bitboard apply_transform(Bitboard b, uint8_t t) {
    if (t & 1)
        b = flip_files(b);
    if (t & 2)
        b = Attacks::byteswap_bb(b);
    if (t & 4)
        b = transpose(b);
    return b;
}

const King_Slice_Manager& slice_mgr_for(Symmetry_Group sym) {
    static const King_Slice_Manager file_mirror(Symmetry_Group::FILE_MIRROR);
    static const King_Slice_Manager dihedral_8(Symmetry_Group::DIHEDRAL_8);
    assert(sym != Symmetry_Group::NONE);
    return sym == Symmetry_Group::DIHEDRAL_8 ? dihedral_8 : file_mirror;
}

const King_Slice_Manager& castling_slice_mgr(size_t white_rights, size_t black_rights) {
    constexpr size_t  STRIDE = Castling_Group::MAX_RIGHTS + 1;
    static const auto table  = [] {
        std::array<std::unique_ptr<const King_Slice_Manager>, STRIDE * STRIDE> t;
        for (size_t w = 0; w < STRIDE; ++w)
            for (size_t b = 0; b < STRIDE; ++b)
                if (w + b > 0)
                    t[w * STRIDE + b] = std::make_unique<King_Slice_Manager>(w, b);
        return t;
    }();
    const auto& mgr = table[white_rights * STRIDE + black_rights];
    assert(mgr != nullptr);
    return *mgr;
}

}  // namespace

const Pawn_Slice_Manager& pawn_slice_mgr(bool has_pair, size_t white_pawns, size_t black_pawns) {
    constexpr size_t  STRIDE = MAX_TOTAL_PAWNS + 1;
    static std::mutex mu;
    static std::array<std::unique_ptr<const Pawn_Slice_Manager>, 2 * STRIDE * STRIDE> table;
    assert(white_pawns < STRIDE && black_pawns < STRIDE);
    std::lock_guard<std::mutex> lk(mu);
    auto& slot = table[(size_t(has_pair) * STRIDE + white_pawns) * STRIDE + black_pawns];
    if (!slot)
        slot = std::make_unique<const Pawn_Slice_Manager>(has_pair, white_pawns, black_pawns);
    return *slot;
}

Piece_Group::Piece_Group(Piece pc, size_t count) :
    m_piece(pc),
    m_num_pieces(count),
    m_num_legal_squares(type_of(pc) == PAWN ? PAWN_SQUARE_NB : SQUARE_NB),
    m_first_square(possible_sq(pc, 0)),
    m_table_size(BINOMIAL[m_num_legal_squares][count]) {}

Piece_Group::Placement_Index Piece_Group::index_of(Bitboard b) const {
    assert(size_t(popcount(b)) == m_num_pieces);
    uint32_t rank = 0;
    for (size_t k = 1; b; ++k)
        rank += BINOMIAL[pop_lsb(b) - m_first_square][k];
    assert(rank < m_table_size);
    return rank;
}

Bitboard Piece_Group::squares(Placement_Index idx) const {
    assert(idx < m_table_size);

    size_t   pos[MAX_PIECE_GROUP_SIZE]{};
    uint32_t rank = idx;
    size_t   hi   = m_num_legal_squares;
    for (size_t k = m_num_pieces; k > 0; --k)
    {
        size_t p = hi - 1;
        while (BINOMIAL[p][k] > rank)
        {
            assert(p > 0);
            --p;
        }
        pos[k - 1] = p;
        rank -= BINOMIAL[p][k];
        hi = p;
    }

    Bitboard b = 0;
    for (size_t i = 0; i < m_num_pieces; ++i)
        b |= square_bb(Square(m_first_square + pos[i]));
    return b;
}

// Enumerates in the tables' order: the slice ids are stored.
King_Slice_Manager::King_Slice_Manager(size_t white_rights, size_t black_rights) :
    has_castling(true),
    castling_rights{white_rights, black_rights},
    m_castling_group(std::make_shared<Castling_Group>(white_rights, black_rights)) {
    for (auto& e : pair_lookup)
        e = {SLICE_NONE, 0, 0};

    m_free_king_stride = m_castling_group->both_kings_pinned() ? size_t(1) : size_t(SQUARE_NB);
    const size_t n     = m_castling_group->table_size();
    castling_slice_lookup.assign(n * m_free_king_stride, SLICE_NONE);

    for (size_t ci = 0; ci < n; ++ci)
    {
        const Castling_Group::Index cidx = static_cast<Castling_Group::Index>(ci);

        Square   pinned_king[COLOR_NB] = {SQ_NONE, SQ_NONE};
        Bitboard pinned                = Bitboard(0);
        for (const Color c : {WHITE, BLACK})
        {
            if (castling_rights[c] == 0)
                continue;
            pinned_king[c] = m_castling_group->king_square(cidx, c);
            pinned |= square_bb(pinned_king[c]) | m_castling_group->rooks(cidx, c);
        }

        auto emit = [&](size_t free_slot) {
            castling_slice_lookup[ci * m_free_king_stride + free_slot] =
              static_cast<int32_t>(num_slices++);
        };

        if (m_castling_group->both_kings_pinned())
        {
            emit(0);
            continue;
        }

        const Color  pinned_color = (castling_rights[WHITE] > 0) ? WHITE : BLACK;
        const Square ksq          = pinned_king[pinned_color];
        for (Square fk = SQ_A1; fk < SQ_NONE; ++fk)
        {
            if (pinned & fk)
                continue;
            if (Attacks::attacks_bb(KING, ksq) & fk)
                continue;
            emit(static_cast<size_t>(fk));
        }
    }
}

King_Slice_Manager::King_Slice_Manager(Symmetry_Group sym) {
    for (auto& e : pair_lookup)
        e = {SLICE_NONE, 0, 0};
    const Bitboard anchors =
      sym == Symmetry_Group::DIHEDRAL_8 ? ANCHOR_TRIANGLE : ANCHOR_FILE_MIRROR;

    for (Square wk = SQ_A1; wk < SQ_NONE; ++wk)
    {
        if (!(anchors & wk))
            continue;

        for (Square bk = SQ_A1; bk < SQ_NONE; ++bk)
        {
            if (bk == wk)
                continue;
            if (Attacks::attacks_bb(KING, wk) & bk)
                continue;

            if (sym == Symmetry_Group::DIHEDRAL_8 && (MAIN_DIAG & wk) && (ABOVE_DIAG & bk))
                continue;

            const int32_t sid = static_cast<int32_t>(num_slices++);

            const bool stab =
              (sym == Symmetry_Group::DIHEDRAL_8) && (MAIN_DIAG & wk) && (MAIN_DIAG & bk);

            pair_lookup[static_cast<int>(wk) * SQUARE_NB + static_cast<int>(bk)] = {
              sid, 0, static_cast<uint8_t>(stab)};
        }
    }

    const int n_trans = sym == Symmetry_Group::DIHEDRAL_8 ? 8 : 2;
    for (Square wk = SQ_A1; wk < SQ_NONE; ++wk)
    {
        for (Square bk = SQ_A1; bk < SQ_NONE; ++bk)
        {
            Pair_Lookup& e = pair_lookup[static_cast<int>(wk) * SQUARE_NB + static_cast<int>(bk)];
            if (e.slice_id != SLICE_NONE)
                continue;
            if (wk == bk || (Attacks::attacks_bb(KING, wk) & bk))
                continue;

            for (uint8_t t = 0; t < n_trans; ++t)
            {
                const Square wk_t = lsb(apply_transform(square_bb(wk), t));
                const Square bk_t = lsb(apply_transform(square_bb(bk), t));
                const auto&  look =
                  pair_lookup[static_cast<int>(wk_t) * SQUARE_NB + static_cast<int>(bk_t)];
                if (look.slice_id != SLICE_NONE && look.transform == 0)
                {
                    e.slice_id            = look.slice_id;
                    e.transform           = t;
                    e.has_diag_stabilizer = look.has_diag_stabilizer;
                    break;
                }
            }
        }
    }
}

Pawn_Slice_Manager::Pawn_Slice_Manager(bool has_pair, size_t white_pawns, size_t black_pawns) {
    if (has_pair)
        m_pair_group = std::make_unique<const Pair_Group>();
    if (white_pawns)
        m_white_group = std::make_unique<const Piece_Group>(W_PAWN, white_pawns);
    if (black_pawns)
        m_black_group = std::make_unique<const Piece_Group>(B_PAWN, black_pawns);
    const Pair_Group*  pair  = m_pair_group.get();
    const Piece_Group* white = m_white_group.get();
    const Piece_Group* black = m_black_group.get();

    m_has_pawns            = pair != nullptr || white != nullptr || black != nullptr;
    m_pair_table_size      = pair ? pair->table_size() : 1;
    m_white_table_size     = white ? white->table_size() : 1;
    m_black_table_size     = black ? black->table_size() : 1;
    m_num_cartesian_slices = m_pair_table_size * m_white_table_size * m_black_table_size;
    if (!pair)
    {
        // Without a pair only overlaps are cut, so lookup() ranks the slice
        // directly: no bitmap.
        m_white_free_size = BINOMIAL[SQ_H7 - SQ_A2 + 1 - black_pawns][white_pawns];
        m_num_slices      = m_black_table_size * m_white_free_size;
        return;
    }

    auto placements_of = [](const Piece_Group* g) {
        std::vector<Bitboard> out;
        for (size_t i = 0; i < (g ? g->table_size() : 1); ++i)
            out.push_back(g ? g->squares(static_cast<Piece_Group::Placement_Index>(i)) : 0);
        return out;
    };

    // The bitmap of surviving cells, built by bitsets; then each block's count
    // of survivors below it, for lookup().
    m_rank_blocks.assign((m_num_cartesian_slices + 63) / 64, Rank_Block{});
    m_num_slices = 0;
    build_pawn_slice_bits(pair, placements_of(white), placements_of(black),
                          [&](size_t cart, uint64_t bits) {
                              if (!bits)
                                  return;
                              m_num_slices += popcount(bits);
                              const size_t word = cart >> 6, shift = cart & 63;
                              m_rank_blocks[word].bits |= bits << shift;
                              if (shift && word + 1 < m_rank_blocks.size())
                                  m_rank_blocks[word + 1].bits |= bits >> (64 - shift);
                          });
    int32_t running = 0;
    for (Rank_Block& block : m_rank_blocks)
    {
        block.survivors_before = running;
        running += static_cast<int32_t>(popcount(block.bits));
    }
    assert(static_cast<size_t>(running) == m_num_slices);
}

int32_t Pawn_Slice_Manager::lookup(Square   pair_white_sq,
                                   Square   pair_black_sq,
                                   Bitboard white,
                                   Bitboard black) const {
    const Piece_Group::Placement_Index b_idx = m_black_group ? m_black_group->index_of(black) : 0;

    // Without a pair: black's placement, then white's among the squares black
    // leaves free.
    if (!m_pair_group)
    {
        const Bitboard free = ~(Rank1BB | Rank8BB) & ~black;
        size_t         rank = 0;
#ifdef USE_PEXT
        Bitboard bits = pext(white, free);
        for (size_t k = 1; bits; ++k)
            rank += BINOMIAL[pop_lsb(bits)][k];
#else
        for (size_t k = 1; white; ++k)
            rank += BINOMIAL[popcount(free & (square_bb(pop_lsb(white)) - 1))][k];
#endif
        assert(rank < m_white_free_size);
        return static_cast<int32_t>(b_idx * m_white_free_size + rank);
    }

    // With a pair: the cell's rank among the bitmap's survivors.
    const Piece_Group::Placement_Index w_idx = m_white_group ? m_white_group->index_of(white) : 0;
    const size_t cart = static_cast<size_t>(m_pair_group->index_of(pair_white_sq, pair_black_sq))
                      + static_cast<size_t>(w_idx) * m_pair_table_size
                      + static_cast<size_t>(b_idx) * m_pair_table_size * m_white_table_size;
    assert(cart < m_num_cartesian_slices);
    assert(cartesian_survives(cart));
    return storage_of_cartesian(cart);
}

// Tables exist only for materials that fit, so the size checks are asserts.
Position_Index_Config::Position_Index_Config(const Piece_Config& ps) :
    Piece_Config(ps) {
    const auto counts = piece_counts();

    [[maybe_unused]] const size_t total_pawns =
      counts[W_PAWN] + counts[B_PAWN] + (has_opposing_pair() ? 2 : 0);
    assert(total_pawns <= MAX_TOTAL_PAWNS);

    const Symmetry_Group symmetry = pick_symmetry(*this);
    assert(has_castling() == (symmetry == Symmetry_Group::NONE));
    m_king_slice_manager = has_castling()
                           ? &castling_slice_mgr(castling_rights(WHITE), castling_rights(BLACK))
                           : &slice_mgr_for(symmetry);

    // Pawns are the pawn slice manager's.
    constexpr std::pair<PieceType, Piece_Type_Class> TYPES[] = {
      {QUEEN, QUEENS}, {ROOK, ROOKS}, {BISHOP, BISHOPS}, {KNIGHT, KNIGHTS}};
    for (Color c : {WHITE, BLACK})
        for (const auto& [pt, ptcl] : TYPES)
            if (const size_t n = counts[make_piece(c, pt)])
                m_groups[make_piece_class(c, ptcl)] =
                  std::make_unique<const Piece_Group>(make_piece(c, pt), n);

    m_pawn_slice_manager = &pawn_slice_mgr(has_opposing_pair(), counts[W_PAWN], counts[B_PAWN]);
    m_num_pawn_slices    = m_pawn_slice_manager->num_slices();

    m_num_populated_classes = 0;
    size_t w                = 1;
    for (Piece_Class i = BLACK_KINGS; i < PIECE_CLASS_NB; ++i)
    {
        if (m_groups[i] == nullptr)
            continue;
        m_populated_classes[m_num_populated_classes++] = i;
        m_weights[i]                                   = w;
        w *= m_groups[i]->table_size();
        assert(uint64_t(w) <= MAX_NUM_POSITIONS);
    }
    m_within_slice_size = w;

    const size_t num_king_slices = m_king_slice_manager->num_slices;
    m_pawn_slice_stride          = uint64_t(num_king_slices) * m_within_slice_size;
    assert(m_num_pawn_slices * m_pawn_slice_stride <= MAX_NUM_POSITIONS);
}

Board_Index board_index_of_position(const Position_Index_Config& cfg,
                                    const Index_Storage_Layout&  layout,
                                    const View&                  pos) {
    const size_t       n       = cfg.num_populated_classes();
    const Piece_Class* classes = cfg.populated_classes();

    Bitboard boards[PIECE_CLASS_NB];
    for (size_t i = 0; i < n; ++i)
        boards[classes[i]] = pos.piece_bb(cfg.group(classes[i]).piece());
    Bitboard white_pawns = pos.piece_bb(W_PAWN), black_pawns = pos.piece_bb(B_PAWN);

    const Square                 wk = pos.king_square(WHITE), bk = pos.king_square(BLACK);
    int32_t                      king_slice_id;
    Piece_Group::Placement_Index idx[PIECE_CLASS_NB];
    if (!cfg.has_castling())
    {
        // The symmetry that takes the kings to their slice's squares, applied
        // to every man.
        const auto& look = cfg.king_slice_manager().lookup(wk, bk);
        king_slice_id    = look.slice_id;
        if (king_slice_id == SLICE_NONE)
            return BOARD_INDEX_NONE;
        if (look.transform)
        {
            for (size_t i = 0; i < n; ++i)
                boards[classes[i]] = apply_transform(boards[classes[i]], look.transform);
            white_pawns = apply_transform(white_pawns, look.transform);
            black_pawns = apply_transform(black_pawns, look.transform);
        }
        for (size_t i = 0; i < n; ++i)
            idx[classes[i]] = cfg.group(classes[i]).index_of(boards[classes[i]]);

        // Both kings on the diagonal: the a1-h8 mirror keeps their slice, so
        // the men take whichever side ranks lower.
        if (look.has_diag_stabilizer)
        {
            Piece_Group::Placement_Index alt[PIECE_CLASS_NB];
            size_t                       within_cur = 0, within_alt = 0;
            for (size_t i = 0; i < n; ++i)
            {
                const Piece_Class c = classes[i];
                alt[c]              = cfg.group(c).index_of(transpose(boards[c]));
                within_cur += cfg.weight(c) * idx[c];
                within_alt += cfg.weight(c) * alt[c];
            }
            if (within_alt < within_cur)
                for (size_t i = 0; i < n; ++i)
                    idx[classes[i]] = alt[classes[i]];
        }
    }
    else
    {
        // A castling rook is not in its class, which holds only the free ones.
        const Bitboard rooks[COLOR_NB] = {pos.castling_rooks(WHITE), pos.castling_rooks(BLACK)};
        for (const Color c : {WHITE, BLACK})
        {
            const Piece_Class rook_class = make_piece_class(c, ROOKS);
            if (cfg.is_populated(rook_class))
                boards[rook_class] &= ~rooks[c];
        }
        king_slice_id =
          cfg.king_slice_manager().castling_slice_of(wk, bk, rooks[WHITE], rooks[BLACK]);
        if (king_slice_id == SLICE_NONE)
            return BOARD_INDEX_NONE;
        for (size_t i = 0; i < n; ++i)
            idx[classes[i]] = cfg.group(classes[i]).index_of(boards[classes[i]]);
    }

    int32_t pawn_slice_id = 0;
    if (cfg.pawn_slice_manager().has_pawns())
    {
        Square pair_w = SQ_NONE, pair_b = SQ_NONE;
        if (cfg.has_opposing_pair())
        {
            Pair_Group::canonical_pair(white_pawns, black_pawns, pair_w, pair_b);
            white_pawns ^= pair_w;
            black_pawns ^= pair_b;
        }
        pawn_slice_id = cfg.pawn_slice_manager().lookup(pair_w, pair_b, white_pawns, black_pawns);
    }

    // Within the slice, the classes in storage order, the first varying fastest.
    size_t within = 0;
    size_t w      = 1;
    for (size_t i = 0; i < layout.n; ++i)
    {
        within += w * idx[layout.order[i]];
        w *= layout.radix[i];
    }
    return Board_Index(uint64_t(pawn_slice_id) * cfg.pawn_slice_stride()
                       + uint64_t(king_slice_id) * cfg.within_slice_size() + within);
}

bool Piece_Config::sort_pieces(Span<Piece>                         pieces,
                               const std::array<size_t, COLOR_NB>& castling_rights) {
    size_t score[COLOR_NB] = {0, 0};
    for (const Piece p : pieces)
        score[color_of(p)] += PIECE_STRENGTH_FOR_SIDE_ORDER[p];
    for (const Color c : {WHITE, BLACK})
        score[c] += castling_rights[c] * PIECE_STRENGTH_FOR_SIDE_ORDER[make_piece(c, ROOK)];

    bool do_swap = score[BLACK] > score[WHITE];
    if (score[BLACK] == score[WHITE])
    {
        std::array<int8_t, PIECE_TYPE_NB> white_key{}, black_key{};
        for (const Piece p : pieces)
            (color_of(p) == WHITE ? white_key : black_key)[type_of(p)]++;
        white_key[ROOK] += static_cast<int8_t>(castling_rights[WHITE]);
        black_key[ROOK] += static_cast<int8_t>(castling_rights[BLACK]);
        // Counts compared from KING down to PAWN.
        if (white_key != black_key)
            do_swap = std::lexicographical_compare(white_key.rbegin(), white_key.rend(),
                                                   black_key.rbegin(), black_key.rend());
        else
            // Same men on both sides, so the only thing left to order by is which
            // side's rook kept its right. Fewer white rights is the canonical
            // orientation, matching Material_Key's castle code -- KRKr, not KrKR.
            do_swap = castling_rights[WHITE] > castling_rights[BLACK];
    }

    if (do_swap)
        for (Piece& p : pieces)
            p = ~p;

    std::sort(pieces.begin(), pieces.end(),
              [](Piece a, Piece b) { return PIECE_ORDER[a] < PIECE_ORDER[b]; });

    return do_swap;
}

}  // namespace Stockfish::ChessTB
