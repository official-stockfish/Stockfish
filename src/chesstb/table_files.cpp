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

#include "table_files.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <vector>

#include "../bitboard.h"
#include "../types.h"
#include "decompress.h"

namespace Stockfish::ChessTB {

struct Serial_Memory_Reader {
    Serial_Memory_Reader(Const_Span<uint8_t> span) :
        m_caret(span.begin()),
        m_begin(span.begin()),
        m_end(span.end()) {}

    template<typename T>
    T read() {
        assert(m_caret + sizeof(T) <= m_end);
        const T val = read_le<T>(m_caret);
        m_caret += sizeof(T);
        return val;
    }

    void advance(size_t size) {
        assert(m_caret + size <= m_end);
        m_caret += size;
    }

    void align(size_t alignment) {
        const size_t misalignment = (m_caret - m_begin) % alignment;
        if (misalignment == 0)
            return;

        assert(m_caret + (alignment - misalignment) <= m_end);
        m_caret += alignment - misalignment;
    }

    const uint8_t* caret() const { return m_caret; }

   private:
    const uint8_t* m_caret;
    const uint8_t* m_begin;
    const uint8_t* m_end;
};

namespace {

constexpr uint8_t SINGULAR_FLAG  = 0x80;
constexpr uint8_t DROPPED_FLAG   = 0x40;
constexpr uint8_t LOSS_ONLY_FLAG = 0x20;
constexpr uint8_t RELAXED_FLAG   = 0x10;

}  // namespace

template<typename Traits>
bool Table_File<Traits>::load(const Piece_Config& ps, const std::filesystem::path& path) {
    if (!mapped.open_readonly(path))
        return false;

    const Const_Span<uint8_t> input = mapped.data_span();
    if ((input.size() & 63) != 8)
        return false;

    Serial_Memory_Reader reader(input);

    const uint32_t magic = reader.read<uint32_t>();
    if (magic != static_cast<uint32_t>(Traits::MAGIC))
        return false;

    const uint32_t key_and_table_num = reader.read<uint32_t>();
    const uint32_t key               = key_and_table_num >> 2u;
    if (key != (ps.min_material_key().value() & 0x3FFFFFFFu))
        return false;
    index_cfg = std::make_unique<Position_Index_Config>(ps);

    // Frames stored: WHITE, and BLACK unless the material is symmetric (1).
    const size_t table_num = key_and_table_num & 3;
    if (table_num == 0 || table_num > COLOR_NB)
        return false;

    for (size_t c = 0; c < table_num; ++c)
    {
        const Color   i    = Color(c);
        const uint8_t flag = reader.read<uint8_t>();
        is_loss_only[i]    = (flag & LOSS_ONLY_FLAG) != 0;
        is_relaxed[i]      = (flag & RELAXED_FLAG) != 0;
        if (flag & SINGULAR_FLAG)
        {
            is_singular[i] = true;
            if (!Traits::on_singular(reader, per_color[i]))
                return false;
        }
        else if (flag & DROPPED_FLAG)
        {
            is_dropped[i] = true;
        }
        else
        {
            if (!Traits::parse_header(reader, per_color[i], *index_cfg))
                return false;
        }
    }
    if (table_num == 1)
    {
        // Symmetric material: BLACK is WHITE mirrored, flag byte and all.
        is_dropped[BLACK]   = true;
        is_loss_only[BLACK] = is_loss_only[WHITE];
        is_relaxed[BLACK]   = is_relaxed[WHITE];
    }

    Traits::finalize(reader, per_color, is_singular, is_dropped);
    return true;
}

bool LZMA_Rank_Loader::on_singular(Serial_Memory_Reader& reader, Per_Color& pc) {
    pc.single_val = reader.read<uint8_t>();
    return true;
}

bool LZMA_Rank_Loader::parse_header(Serial_Memory_Reader&        reader,
                                    Per_Color&                   pc,
                                    const Position_Index_Config& index_cfg) {
    const uint32_t perm = reader.read<uint32_t>();
    if (!index_permutation_config_is_valid(index_cfg, perm))
        return false;
    pc.layout = make_index_storage_layout(index_cfg, perm);

    pc.entry_bytes = reader.read<uint8_t>();
    if (pc.entry_bytes != 1 && pc.entry_bytes != sizeof(uint16_t))
        return false;
    pc.tail_size  = reader.read<uint32_t>();
    pc.block_size = reader.read<uint32_t>();
    pc.block_cnt  = reader.read<uint64_t>();
    pc.data_size  = reader.read<uint64_t>();

    const size_t num_ranks = reader.read<uint16_t>();
    pc.rank_to_value.resize(num_ranks);
    for (size_t r = 0; r < num_ranks; ++r)
        pc.rank_to_value[r] = reader.read<uint16_t>();
    return true;
}

void LZMA_Rank_Loader::finalize(Serial_Memory_Reader& reader,
                                Per_Color (&per_color)[COLOR_NB],
                                const bool (&is_singular)[COLOR_NB],
                                const bool (&is_dropped)[COLOR_NB]) {
    for (Color i : {WHITE, BLACK})
    {
        if (is_singular[i] || is_dropped[i])
            continue;
        Per_Color&    pc           = per_color[i];
        const uint8_t log2_bu      = reader.read<uint8_t>();
        const uint8_t sample_width = reader.read<uint8_t>();
        const uint8_t offset_width = reader.read<uint8_t>();
        reader.advance(1);  // usz_width
        const uint8_t* mono_ptr = reader.caret();
        const size_t   mono_bytes =
          Mono_Uint_Vec::on_disk_bytes(pc.block_cnt + 1, log2_bu, sample_width, offset_width);
        reader.advance(mono_bytes);
        pc.offsets = Mono_Uint_Vec(mono_ptr, pc.block_cnt + 1, log2_bu, sample_width, offset_width);
    }
    for (Color i : {WHITE, BLACK})
    {
        if (is_singular[i] || is_dropped[i])
            continue;
        reader.align(64);
        per_color[i].compressed_data = reader.caret();
        reader.advance(per_color[i].data_size);
    }
}

bool Layered_Rank_Loader::on_singular(Serial_Memory_Reader& reader, Per_Color&) {
    return reader.read<uint8_t>() == 0;  // the singular value must be DRAW
}

bool Layered_Rank_Loader::parse_header(Serial_Memory_Reader&        reader,
                                       Per_Color&                   pc,
                                       const Position_Index_Config& index_cfg) {
    const uint32_t perm = reader.read<uint32_t>();
    if (!index_permutation_config_is_valid(index_cfg, perm))
        return false;
    pc.layout = make_index_storage_layout(index_cfg, perm);

    pc.entry_bytes = reader.read<uint8_t>();
    if (pc.entry_bytes != 1 && pc.entry_bytes != sizeof(uint16_t))
        return false;
    pc.block_positions = reader.read<uint32_t>();
    pc.block_cnt       = reader.read<uint64_t>();
    pc.tail_positions  = reader.read<uint32_t>();
    pc.data_size       = reader.read<uint64_t>();

    const size_t num_ranks = reader.read<uint16_t>();
    pc.rank_to_value.resize(num_ranks);
    for (size_t r = 0; r < num_ranks; ++r)
        pc.rank_to_value[r] = reader.read<uint16_t>();
    return true;
}

void Layered_Rank_Loader::finalize(Serial_Memory_Reader& reader,
                                   Per_Color (&per_color)[COLOR_NB],
                                   const bool (&is_singular)[COLOR_NB],
                                   const bool (&is_dropped)[COLOR_NB]) {
    for (Color i : {WHITE, BLACK})
    {
        if (is_singular[i] || is_dropped[i])
            continue;
        Per_Color&     pc           = per_color[i];
        const uint8_t  log2_bu      = reader.read<uint8_t>();
        const uint8_t  sample_width = reader.read<uint8_t>();
        const uint8_t  offset_width = reader.read<uint8_t>();
        const uint8_t  usz_width    = reader.read<uint8_t>();
        const uint8_t* mono_ptr     = reader.caret();
        const size_t   mono_bytes =
          Mono_Uint_Vec::on_disk_bytes(pc.block_cnt + 1, log2_bu, sample_width, offset_width);
        reader.advance(mono_bytes);
        const uint8_t* usz_ptr   = reader.caret();
        const size_t   usz_bytes = Min0_Uint_Vec::on_disk_bytes(pc.block_cnt, usz_width);
        reader.advance(usz_bytes);
        pc.offsets = Mono_Uint_Vec(mono_ptr, pc.block_cnt + 1, log2_bu, sample_width, offset_width);
        pc.usizes  = Min0_Uint_Vec(usz_ptr, usz_width);
    }
    for (Color i : {WHITE, BLACK})
    {
        if (is_singular[i] || is_dropped[i])
            continue;
        reader.align(64);
        per_color[i].compressed_data = reader.caret();
        reader.advance(per_color[i].data_size);
    }
}

namespace {

// The value of the rank stored at p, in entry_bytes.
template<typename Per_Color>
uint16_t rank_value(const Per_Color& pc, const uint8_t* p) {
    if (pc.entry_bytes == 1)
        return pc.rank_to_value[*p];
    return pc.rank_to_value[read_le<uint16_t>(p)];
}

Block_Ptr lzma_rank_get_block(LZMA_Rank_Per_Color& pc, size_t block_id) {
    const size_t decode_sz =
      (block_id == pc.block_cnt - 1 && pc.tail_size != 0) ? pc.tail_size : pc.block_size;

    const auto   pair = pc.offsets.get2(block_id);
    const size_t doff = pair[0];
    const size_t dsz  = pair[1] - pair[0];
    assert(dsz != 0);

    auto buf = std::make_shared<std::vector<uint8_t>>(decode_sz, 0);

    [[maybe_unused]] const bool ok =
      lzma_decode(buf->data(), decode_sz, pc.compressed_data + doff, dsz);
    assert(ok);
    return buf;
}

constexpr size_t layered_bitmap_bytes(size_t layers) { return ((layers + 31) / 32) * 4; }

constexpr size_t DTM50_MULTI_BITMAP_BYTES = layered_bitmap_bytes(DTM50_PACK_LAYERS);
constexpr size_t DTC_MULTI_BITMAP_BYTES   = layered_bitmap_bytes(DTC_PACK_LAYERS);

size_t popcount_bit_range(const uint8_t* bm, size_t lo, size_t hi) {
    if (lo >= hi)
        return 0;
    size_t       pc        = 0;
    size_t       byte      = lo >> 3;
    size_t       remaining = hi - lo;
    const size_t head      = lo & 7;  // bits to skip in the first byte
    if (head)
    {
        const uint8_t b     = static_cast<uint8_t>(bm[byte] >> head);
        const size_t  avail = 8 - head;
        if (remaining <= avail)
            return popcount(static_cast<uint8_t>(b & ((1u << remaining) - 1u)));
        pc += popcount(b);
        remaining -= avail;
        ++byte;
    }
    while (remaining >= 64)
    {
        pc += popcount(read_le<uint64_t>(bm + byte));
        byte += 8;
        remaining -= 64;
    }
    if (remaining)  // 1..63 bits, byte-aligned
    {
        const uint64_t w = read_le<uint64_t>(bm + byte, (remaining + 7) / 8);
        pc += popcount(w & ((uint64_t{1} << remaining) - 1));
    }
    return pc;
}

constexpr size_t LAYERED_PREFIX_STRIDE = 256;

struct Layered_Prefix_Entry {
    uint32_t n_const;
    uint32_t n_single;
    uint32_t n_double;
    uint32_t n_multi;
    uint32_t n_single_short;  // popcount(single_hints, [0, n_single))
    uint32_t n_double_short;  // popcount(double_hints, [0, n_double))
};
static_assert(sizeof(Layered_Prefix_Entry) == 24);

struct Layered_Cached_Block {
    uint32_t state_bits_off;
    uint32_t const_stream_off;
    uint32_t single_hints_off;
    uint32_t single_stream_off;
    uint32_t double_hints_off;
    uint32_t double_stream_off;
    uint32_t multi_dir_off;
    uint32_t multi_data_off;
    uint32_t prefix_off;
};
static_assert(sizeof(Layered_Cached_Block) % 4 == 0);

// Position p's state is bits 2p and 2p+1 of the state vector: 00 CONST,
// 01 SINGLE, 10 DOUBLE, 11 MULTI. NONCONST counts all but 00; CONST is the span
// less NONCONST, as masked-out positions read 00.
enum class State_Class : uint8_t {
    SINGLE,
    DOUBLE,
    MULTI,
    NONCONST
};

template<State_Class CLS>
uint32_t tally_state_word(uint64_t w) {
    constexpr uint64_t EVEN_MASK = 0x5555555555555555ull;
    const uint64_t     lo        = w & EVEN_MASK;
    const uint64_t     hi        = (w >> 1) & EVEN_MASK;
    if constexpr (CLS == State_Class::SINGLE)
        return popcount(lo & ~hi);  // 01
    else if constexpr (CLS == State_Class::DOUBLE)
        return popcount(hi & ~lo);  // 10
    else if constexpr (CLS == State_Class::MULTI)
        return popcount(lo & hi);  // 11
    else
        return popcount(lo | hi);  // !=00
}

// f on each 64-bit word of the states in [bit_lo, bit_hi), the last one masked
// to the range.
template<typename F>
void for_state_words(const uint8_t* sb, size_t bit_lo, size_t bit_hi, F&& f) {
    size_t bit = bit_lo;
    for (; bit + 64 <= bit_hi; bit += 64)
        f(read_le<uint64_t>(sb + bit / 8));
    if (bit < bit_hi)
    {
        const size_t rem = bit_hi - bit;
        f(read_le<uint64_t>(sb + bit / 8, (rem + 7) / 8) & ((uint64_t{1} << rem) - 1));
    }
}

template<State_Class CLS>
uint32_t popcount_state_class(const uint8_t* sb, size_t bit_lo, size_t bit_hi) {
    uint32_t count = 0;
    for_state_words(sb, bit_lo, bit_hi, [&](uint64_t w) { count += tally_state_word<CLS>(w); });
    return count;
}

void build_layered_prefix_index(const uint8_t*        state_bits,
                                const uint8_t*        single_hints,
                                const uint8_t*        double_hints,
                                size_t                num_positions,
                                Layered_Prefix_Entry* out,
                                size_t                n_strides) {
    uint32_t nc = 0, ns = 0, nd = 0, nm = 0;
    uint32_t ns_short = 0, nd_short = 0;  // popcount(*_hints, [0, ns)/[0, nd))
    for (size_t s = 0; s < n_strides; ++s)
    {
        out[s]                   = {nc, ns, nd, nm, ns_short, nd_short};
        const size_t   p_lo      = s * LAYERED_PREFIX_STRIDE;
        const size_t   p_hi      = std::min(p_lo + LAYERED_PREFIX_STRIDE, num_positions);
        const uint32_t ns_before = ns;
        const uint32_t nd_before = nd;
        const uint32_t nm_before = nm;
        for_state_words(state_bits, p_lo * 2, p_hi * 2, [&](uint64_t w) {
            ns += tally_state_word<State_Class::SINGLE>(w);
            nd += tally_state_word<State_Class::DOUBLE>(w);
            nm += tally_state_word<State_Class::MULTI>(w);
        });
        const uint32_t added_nonc = (ns - ns_before) + (nd - nd_before) + (nm - nm_before);
        nc += static_cast<uint32_t>(p_hi - p_lo) - added_nonc;
        // Only the hints this stride added.
        ns_short += static_cast<uint32_t>(popcount_bit_range(single_hints, ns_before, ns));
        nd_short += static_cast<uint32_t>(popcount_bit_range(double_hints, nd_before, nd));
    }
}

// Cached buffer: [Layered_Cached_Block][payload][prefix][single_hints][double_hints].
template<typename Per_Color>
Block_Ptr layered_get_block(Per_Color& pc, size_t block_id) {
    const auto   pair = pc.offsets.get2(block_id);
    const size_t doff = pair[0];
    const size_t dsz  = pair[1] - pair[0];
    const size_t usz  = pc.usizes.get(block_id);
    assert(dsz != 0);
    assert((usz & 3) == 0);

    const size_t num_positions = (block_id == pc.block_cnt - 1 && pc.tail_positions != 0)
                                 ? pc.tail_positions
                                 : pc.block_positions;

    const size_t payload_off  = sizeof(Layered_Cached_Block);
    const size_t n_strides    = (num_positions + LAYERED_PREFIX_STRIDE - 1) / LAYERED_PREFIX_STRIDE;
    const size_t prefix_bytes = n_strides * sizeof(Layered_Prefix_Entry);
    const size_t prefix_off   = payload_off + usz;
    const size_t hints_off    = prefix_off + prefix_bytes;

    auto buf = std::make_shared<std::vector<uint8_t>>(hints_off, 0);

    [[maybe_unused]] const bool ok =
      lzma_decode(buf->data() + payload_off, usz, pc.compressed_data + doff, dsz);
    assert(ok);

    const uint8_t* payload             = buf->data() + payload_off;
    const uint32_t num_single          = read_le<uint32_t>(payload + 4);
    const uint32_t num_double          = read_le<uint32_t>(payload + 8);
    const uint32_t num_multi           = read_le<uint32_t>(payload + 12);
    const uint32_t single_stream_bytes = read_le<uint32_t>(payload + 16);
    const uint32_t double_stream_bytes = read_le<uint32_t>(payload + 20);
    assert(read_le<uint32_t>(payload) == num_positions);

    const size_t eb        = pc.entry_bytes;
    const size_t num_const = num_positions - num_single - num_double - num_multi;
    const size_t sb_bytes  = (num_positions * 2 + 7) / 8;
    const size_t sh_bytes  = (num_single + 7) / 8;
    const size_t dh_bytes  = (num_double + 7) / 8;

    Layered_Cached_Block meta{};
    size_t               p = 24;
    meta.state_bits_off    = static_cast<uint32_t>(p);
    p += sb_bytes;
    meta.const_stream_off = static_cast<uint32_t>(p);
    p += num_const * eb;
    meta.single_stream_off = static_cast<uint32_t>(p);
    p += single_stream_bytes;
    meta.double_stream_off = static_cast<uint32_t>(p);
    p += double_stream_bytes;
    p += (4 - (p & 3)) & 3;
    meta.multi_dir_off = static_cast<uint32_t>(p);
    p += (num_multi + 1) * 4;
    meta.multi_data_off   = static_cast<uint32_t>(p);
    meta.prefix_off       = static_cast<uint32_t>(prefix_off);
    meta.single_hints_off = static_cast<uint32_t>(hints_off - payload_off);
    meta.double_hints_off = static_cast<uint32_t>(hints_off - payload_off + sh_bytes);

    const size_t cached_size = hints_off + sh_bytes + dh_bytes;
    // Avoid geometric growth leaving nearly a second payload's worth of
    // uncounted capacity in the block cache.
    buf->reserve(cached_size);
    buf->resize(cached_size, 0);
    payload = buf->data() + payload_off;
    std::memcpy(buf->data(), &meta, sizeof(meta));

    uint8_t*       single_hints = buf->data() + payload_off + meta.single_hints_off;
    uint8_t*       double_hints = buf->data() + payload_off + meta.double_hints_off;
    const uint8_t* e            = payload + meta.single_stream_off;
    for (uint32_t j = 0; j < num_single; ++j)
    {
        // A draw end drops one stored value from the record.
        const bool draw_end = e[0] & 0x80u;
        if (draw_end)
            single_hints[j >> 3] |= static_cast<uint8_t>(1u << (j & 7));
        e += draw_end ? 1 + eb : 1 + 2 * eb;
    }
    e = payload + meta.double_stream_off;
    for (uint32_t j = 0; j < num_double; ++j)
    {
        const bool draw_end = e[1] & 0x80u;
        if (draw_end)
            double_hints[j >> 3] |= static_cast<uint8_t>(1u << (j & 7));
        e += draw_end ? 2 + 2 * eb : 2 + 3 * eb;
    }

    auto* prefix = reinterpret_cast<Layered_Prefix_Entry*>(buf->data() + meta.prefix_off);
    build_layered_prefix_index(payload + meta.state_bits_off, single_hints, double_hints,
                               num_positions, prefix, n_strides);

    return buf;
}

// A position's packed record. `entry` is the rank cell for CONST.
struct Layered_Entry_Ref {
    const uint8_t* entry;
    uint8_t        state;  // 0 CONST, 1 SINGLE, 2 DOUBLE, 3 MULTI
    bool           draw_end;
};

template<typename Traits>
bool layered_locate_entry(typename Traits::Per_Color& pc, Board_Index pos, Layered_Entry_Ref& out) {
    const size_t ppb          = pc.block_positions;
    const size_t block_id     = size_t(pos / ppb);
    const size_t pos_in_block = size_t(pos % ppb);

    const auto pair_skip = pc.offsets.get2(block_id);
    if (pair_skip[0] == pair_skip[1])
        return false;

    const uint8_t* buf_data =
      fetch_block_cached<Traits>(pc, block_id, layered_get_block<typename Traits::Per_Color>);

    const auto*    meta    = reinterpret_cast<const Layered_Cached_Block*>(buf_data);
    const uint8_t* payload = buf_data + sizeof(Layered_Cached_Block);
    const auto* prefix = reinterpret_cast<const Layered_Prefix_Entry*>(buf_data + meta->prefix_off);
    const size_t eb    = pc.entry_bytes;

    // The position's state, and its index among its state's entries: the
    // stride's count plus those of its state since the stride began.
    const uint8_t*              sb      = payload + meta->state_bits_off;
    const size_t                bit_pos = pos_in_block * 2;
    const uint8_t               state   = (sb[bit_pos / 8] >> (bit_pos % 8)) & 3u;
    const size_t                stride  = pos_in_block / LAYERED_PREFIX_STRIDE;
    const Layered_Prefix_Entry& snap    = prefix[stride];
    const size_t                bit_lo  = stride * LAYERED_PREFIX_STRIDE * 2;

    out.state = state;
    switch (state)
    {
    case 0 : {
        const size_t idx = snap.n_const + (bit_pos - bit_lo) / 2
                         - popcount_state_class<State_Class::NONCONST>(sb, bit_lo, bit_pos);
        out.entry    = payload + meta->const_stream_off + idx * eb;
        out.draw_end = false;
        break;
    }
    case 1 : {
        const size_t idx =
          snap.n_single + popcount_state_class<State_Class::SINGLE>(sb, bit_lo, bit_pos);
        // short = draw-end variant: [h, r0] against [h, r0, r1].
        const uint8_t* hints  = payload + meta->single_hints_off;
        const size_t n_short  = snap.n_single_short + popcount_bit_range(hints, snap.n_single, idx);
        const size_t byte_off = n_short * (1 + eb) + (idx - n_short) * (1 + 2 * eb);
        out.entry             = payload + meta->single_stream_off + byte_off;
        out.draw_end          = ((hints[idx >> 3] >> (idx & 7)) & 1u) != 0;
        break;
    }
    case 2 : {
        const size_t idx =
          snap.n_double + popcount_state_class<State_Class::DOUBLE>(sb, bit_lo, bit_pos);
        const uint8_t* hints  = payload + meta->double_hints_off;
        const size_t n_short  = snap.n_double_short + popcount_bit_range(hints, snap.n_double, idx);
        const size_t byte_off = n_short * (2 + 2 * eb) + (idx - n_short) * (2 + 3 * eb);
        out.entry             = payload + meta->double_stream_off + byte_off;
        out.draw_end          = ((hints[idx >> 3] >> (idx & 7)) & 1u) != 0;
        break;
    }
    default : {
        const size_t idx =
          snap.n_multi + popcount_state_class<State_Class::MULTI>(sb, bit_lo, bit_pos);
        const uint8_t* dir = payload + meta->multi_dir_off;
        out.entry          = payload + meta->multi_data_off + read_le<uint32_t>(dir + 4 * idx);
        out.draw_end       = (out.entry[0] & 0x80u) != 0;
        break;
    }
    }
    return true;
}

// A MULTI record's changepoint bitmap, the BM_BYTES following the k byte, in
// words: word w bit b is layer 64 * w + b. A width under a word is zero-extended.
template<size_t BM_BYTES>
std::array<uint64_t, (BM_BYTES + 7) / 8> layered_multi_bitmap(const uint8_t* entry) {
    std::array<uint64_t, (BM_BYTES + 7) / 8> bm;
    for (size_t w = 0; w < bm.size(); ++w)
        bm[w] = read_le<uint64_t>(entry + 1 + 8 * w, std::min<size_t>(8, BM_BYTES - 8 * w));
    return bm;
}

// Index of a MULTI record's highest set changepoint bit.
template<size_t BM_BYTES>
uint16_t layered_multi_last_changepoint(const uint8_t* entry) {
    const auto bm = layered_multi_bitmap<BM_BYTES>(entry);
    for (size_t w = bm.size(); w-- > 0;)
        if (bm[w] != 0)
            return static_cast<uint16_t>(w * 64 + int(msb(bm[w])));
    return 0;
}

// Rank slot selected at `layer`: one less than the number of changepoints at or
// below it.
template<size_t BM_BYTES>
size_t layered_multi_rank_slot(const uint8_t* entry, uint16_t layer) {
    const auto bm = layered_multi_bitmap<BM_BYTES>(entry);
    size_t     pc = 0;
    for (size_t w = 0; w < bm.size() && layer >= w * 64; ++w)
    {
        const size_t   bits = layer - w * 64 + 1;  // bits of this word at or below layer
        const uint64_t mask = (bits >= 64) ? ~uint64_t{0} : ((uint64_t{1} << bits) - 1);
        pc += static_cast<size_t>(popcount(bm[w] & mask));
    }
    return pc - 1;
}

}  // namespace

namespace {

constexpr WDL_Stored get_wdl_value(uint8_t packed, size_t pos) {
    assert(pos < WDL_ENTRY_PACK_RATIO);
    return static_cast<WDL_Stored>((packed >> (pos * WDL_ENTRY_BITS)) & 0xF);
}

Block_Ptr wdl_get_block(WDL_Per_Color& pc, size_t block_id) {
    const auto   pair = pc.offsets.get2(block_id);
    const size_t out_sz =
      (block_id == pc.block_cnt - 1 && pc.tail_size != 0) ? pc.tail_size : pc.block_size;

    auto index = std::make_shared<std::vector<uint8_t>>(lz4_point_index_bytes(out_sz));
    lz4_build_point_index(Const_Span<uint8_t>(pc.compressed_data + pair[0], pair[1] - pair[0]),
                          Span<uint8_t>(index->data(), index->size()));
    return index;
}

}  // namespace

bool WDL_Traits::on_singular(Serial_Memory_Reader& reader, Per_Color& pc) {
    pc.single_val = static_cast<WDL_Stored>(reader.read<uint8_t>());
    return true;
}

bool WDL_Traits::parse_header(Serial_Memory_Reader&        reader,
                              Per_Color&                   pc,
                              const Position_Index_Config& index_cfg) {
    const uint32_t perm = reader.read<uint32_t>();
    if (!index_permutation_config_is_valid(index_cfg, perm))
        return false;
    pc.layout = make_index_storage_layout(index_cfg, perm);

    pc.tail_size  = reader.read<uint16_t>();
    pc.block_size = reader.read<uint32_t>();
    pc.block_cnt  = reader.read<uint64_t>();
    pc.data_size  = reader.read<uint64_t>();
    return true;
}

void WDL_Traits::finalize(Serial_Memory_Reader& reader,
                          Per_Color (&per_color)[COLOR_NB],
                          const bool (&is_singular)[COLOR_NB],
                          const bool (&is_dropped)[COLOR_NB]) {
    for (Color i : {WHITE, BLACK})
    {
        if (is_singular[i] || is_dropped[i])
            continue;
        const size_t dict_size = reader.read<uint16_t>();
        per_color[i].dict      = Const_Span<uint8_t>(reader.caret(), dict_size);
        reader.advance(dict_size);
    }

    for (Color i : {WHITE, BLACK})
    {
        if (is_singular[i] || is_dropped[i])
            continue;
        Per_Color&    pc           = per_color[i];
        const uint8_t log2_bu      = reader.read<uint8_t>();
        const uint8_t sample_width = reader.read<uint8_t>();
        const uint8_t offset_width = reader.read<uint8_t>();
        reader.advance(1);  // usz_width
        const uint8_t* mono_ptr = reader.caret();
        const size_t   mono_bytes =
          Mono_Uint_Vec::on_disk_bytes(pc.block_cnt + 1, log2_bu, sample_width, offset_width);
        reader.advance(mono_bytes);
        pc.offsets = Mono_Uint_Vec(mono_ptr, pc.block_cnt + 1, log2_bu, sample_width, offset_width);
    }

    for (Color i : {WHITE, BLACK})
    {
        if (is_singular[i] || is_dropped[i])
            continue;
        Per_Color& pc = per_color[i];
        reader.align(64);
        pc.compressed_data = reader.caret();
        reader.advance(pc.data_size);
    }
}

WDL_Stored WDL_Traits::read(Per_Color& pc, bool is_singular, Board_Index pos) {
    if (is_singular)
        return pc.single_val;

    const size_t packed_byte = size_t(pos / WDL_ENTRY_PACK_RATIO);
    const size_t block_id    = packed_byte / pc.block_size;
    const size_t in_block    = packed_byte % pc.block_size;

    const auto pair = pc.offsets.get2(block_id);
    if (pair[0] == pair[1])
        return WDL_Stored::ILLEGAL;

    const uint8_t* point_index = fetch_block_cached<WDL_Traits, true>(pc, block_id, wdl_get_block);
    const uint8_t  entry =
      lz4_point_read(Const_Span<uint8_t>(pc.compressed_data + pair[0], pair[1] - pair[0]), pc.dict,
                     point_index, in_block);
    return get_wdl_value(entry, size_t(pos % WDL_ENTRY_PACK_RATIO));
}

template struct Table_File<WDL_Traits>;

namespace {

constexpr DTZ_Value dtz_value_from_storage(uint16_t stored, WDL_Entry w, size_t entry_bytes) {
    if (w == WDL_Entry::DRAW)
        return DTZ_Value{0, false};
    if (entry_bytes == 1 && (w == WDL_Entry::CURSED_WIN || w == WDL_Entry::BLESSED_LOSS))
        return DTZ_Value{static_cast<uint16_t>((stored << 1) - 1), true};
    return DTZ_Value{stored, false};
}

}  // namespace

DTZ_Value DTZ_Traits::read(Per_Color& pc, bool is_singular, Board_Index pos, WDL_Entry wdl) {
    if (is_singular)
        return dtz_value_from_storage(pc.single_val, wdl, 1);

    const size_t positions_per_block = pc.block_size / pc.entry_bytes;
    const size_t block_id            = size_t(pos / positions_per_block);
    const size_t in_block_pos        = size_t(pos % positions_per_block);

    // Skip-block: uniform DRAW/ILLEGAL. The WDL class has already filtered
    // those out; W/L would have forced a non-zero cell.
    const auto pair_skip = pc.offsets.get2(block_id);
    if (pair_skip[0] == pair_skip[1])
        return DTZ_Value{0, false};

    const uint8_t* data = fetch_block_cached<DTZ_Traits>(pc, block_id, lzma_rank_get_block);
    return dtz_value_from_storage(rank_value(pc, data + in_block_pos * pc.entry_bytes), wdl,
                                  pc.entry_bytes);
}

template struct Table_File<DTZ_Traits>;

namespace {

struct Budget_Segments {
    static constexpr size_t MAX = DTC_PACK_LAYERS;

    uint16_t start[MAX];  // first pack index the segment covers
    uint16_t value[MAX];
    size_t   count    = 0;
    bool     draw_end = false;
};

void decode_segments(const Layered_Rank_Per_Color& pc,
                     const Layered_Entry_Ref&      ref,
                     Budget_Segments&              out) {
    const size_t eb = pc.entry_bytes;
    out.draw_end    = ref.draw_end;
    switch (ref.state)
    {
    case 0 :
        out.start[0] = 0;
        out.value[0] = rank_value(pc, ref.entry);
        out.count    = 1;
        break;
    case 1 :
        out.start[0] = 0;
        out.value[0] = rank_value(pc, ref.entry + 1);
        out.start[1] = static_cast<uint16_t>(ref.entry[0] & 0x7Fu);
        out.count    = 2;
        if (!ref.draw_end)
            out.value[1] = rank_value(pc, ref.entry + 1 + eb);
        break;
    case 2 :
        out.start[0] = 0;
        out.value[0] = rank_value(pc, ref.entry + 2);
        out.start[1] = ref.entry[0];
        out.value[1] = rank_value(pc, ref.entry + 2 + eb);
        out.start[2] = static_cast<uint16_t>(ref.entry[1] & 0x7Fu);
        out.count    = 3;
        if (!ref.draw_end)
            out.value[2] = rank_value(pc, ref.entry + 2 + 2 * eb);
        break;
    default : {
        const size_t   k     = ref.entry[0] & 0x7Fu;
        const auto     words = layered_multi_bitmap<DTC_MULTI_BITMAP_BYTES>(ref.entry);
        const uint8_t* ranks = ref.entry + 1 + DTC_MULTI_BITMAP_BYTES;
        size_t         n     = 0;
        for (size_t w = 0; w < words.size(); ++w)
        {
            uint64_t bits = words[w];
            while (bits != 0)
            {
                const size_t b = static_cast<size_t>(lsb(bits));
                bits &= bits - 1;
                assert(n < k);
                out.start[n] = static_cast<uint16_t>(w * 64 + b);
                // The hint omits the last rank, DRAW standing in for it.
                if (!(ref.draw_end && n == k - 1))
                    out.value[n] = rank_value(pc, ranks + n * eb);
                ++n;
            }
        }
        assert(n == k);
        out.count = k;
        break;
    }
    }
}

// False where nothing is stored, which reads as drawn at every budget.
bool decode_position(Layered_Rank_Per_Color& pc,
                     bool                    is_singular,
                     Board_Index             pos,
                     Budget_Segments&        out) {
    if (is_singular)
        return false;

    Layered_Entry_Ref ref;
    if (!layered_locate_entry<DTC_Traits>(pc, pos, ref))
        return false;
    decode_segments(pc, ref, out);
    return true;
}

size_t budget_of_pack_index(size_t j) { return DTC_BUDGET_LAYERS - j; }

}  // namespace

DTC_Cell
DTC_Traits::read(Per_Color& pc, bool is_singular, Board_Index pos, WDL_Entry wdl, unsigned rule50) {
    Budget_Segments seg;
    if (!decode_position(pc, is_singular, pos, seg))
        return {};

    DTC_Cell out;
    out.dtz = seg.value[0];
    if (wdl == WDL_Entry::CURSED_WIN || wdl == WDL_Entry::BLESSED_LOSS)
    {
        out.dtz_rounded = (out.dtz & 1u) != 0;
        return out;
    }

    const unsigned budget_plies = dtc_budget_plies(rule50);

    const size_t decisive = seg.draw_end ? seg.count - 1 : seg.count;
    for (size_t s = decisive; s-- > 0;)
    {
        if (seg.value[s] > budget_plies)
            continue;
        const size_t last_index =
          (s + 1 < seg.count) ? static_cast<size_t>(seg.start[s + 1]) - 1 : DTC_PACK_LAYERS - 1;
        out.order = static_cast<uint16_t>(budget_of_pack_index(last_index));
        out.value = seg.value[s];
        break;
    }
    return out;
}

void DTC_Traits::read_curve(
  Per_Color& pc, bool is_singular, Board_Index pos, WDL_Entry wdl, DTC_Curve& curve) {
    for (uint16_t& v : curve.value)
        v = DRAWN_AT_LAYER;

    if (wdl == WDL_Entry::CURSED_WIN || wdl == WDL_Entry::BLESSED_LOSS)
        return;

    Budget_Segments seg;
    if (!decode_position(pc, is_singular, pos, seg))
        return;

    const size_t decisive = seg.draw_end ? seg.count - 1 : seg.count;
    for (size_t s = 0; s < decisive; ++s)
    {
        const size_t end = (s + 1 < seg.count) ? seg.start[s + 1] : DTC_PACK_LAYERS;
        for (size_t j = seg.start[s]; j < end; ++j)
            curve.value[budget_of_pack_index(j)] = seg.value[s];
    }
}

template struct Table_File<DTC_Traits>;

namespace {

constexpr uint16_t dtm_value_from_storage(uint16_t stored, WDL_Entry w) {
    switch (w)
    {
    case WDL_Entry::WIN :
    case WDL_Entry::CURSED_WIN :
        return static_cast<uint16_t>((stored << 1) | 1u);
    case WDL_Entry::LOSE :
    case WDL_Entry::BLESSED_LOSS :
        return static_cast<uint16_t>(stored << 1);
    case WDL_Entry::DRAW :
    case WDL_Entry::ILLEGAL :
    default :
        return 0;
    }
}

}  // namespace

uint16_t DTM_Traits::read(Per_Color& pc, bool is_singular, Board_Index pos, WDL_Entry wdl) {
    if (is_singular)
        return dtm_value_from_storage(pc.single_val, wdl);

    const size_t positions_per_block = pc.block_size / pc.entry_bytes;
    const size_t block_id            = size_t(pos / positions_per_block);
    const size_t in_block_pos        = size_t(pos % positions_per_block);

    // Skip-block: uniform DRAW/ILLEGAL. The WDL class has already filtered
    // those out; W/L would have forced a non-zero cell.
    const auto pair_skip = pc.offsets.get2(block_id);
    if (pair_skip[0] == pair_skip[1])
        return 0;

    const uint8_t* data = fetch_block_cached<DTM_Traits>(pc, block_id, lzma_rank_get_block);
    return dtm_value_from_storage(rank_value(pc, data + in_block_pos * pc.entry_bytes), wdl);
}

template struct Table_File<DTM_Traits>;

namespace {

constexpr uint16_t dtm50_value_from_storage(uint16_t stored, WDL_Entry w) {
    switch (w)
    {
    case WDL_Entry::WIN :
        return static_cast<uint16_t>((stored << 1) | 1u);
    case WDL_Entry::LOSE :
        return static_cast<uint16_t>(stored << 1);
    case WDL_Entry::CURSED_WIN :
    case WDL_Entry::BLESSED_LOSS :
    case WDL_Entry::DRAW :
    case WDL_Entry::ILLEGAL :
    default :
        return 0;
    }
}

}  // namespace

// The value at `hmc`, and the layer where the cell turns DRAW; the terminal rank
// decides both. Only the draw-end hint says DRAW; a stored 0 is a mate.
DTM50_Cell
DTM50_Traits::read(Per_Color& pc, bool is_singular, Board_Index pos, WDL_Entry wdl, unsigned hmc) {
    const bool flat = (hmc == IGNORE_50MR);
    if (!flat && (wdl == WDL_Entry::CURSED_WIN || wdl == WDL_Entry::BLESSED_LOSS))
        return {};
    if (is_singular)
        return {};

    Layered_Entry_Ref ref;
    if (!layered_locate_entry<DTM50_Traits>(pc, pos, ref))
        return {};

    const size_t eb = pc.entry_bytes;

    const uint16_t layer = flat ? 0u : static_cast<uint16_t>(hmc + 1);
    uint16_t       stored;
    uint16_t       draw_flip = 0;
    switch (ref.state)
    {
    case 0 :
        // CONST: one rank for all hmc.
        stored = rank_value(pc, ref.entry);
        break;
    case 1 : {
        // SINGLE: one transition at h.
        const uint16_t h = ref.entry[0] & 0x7Fu;
        if (ref.draw_end)
            draw_flip = h;
        if (layer < h)
            stored = rank_value(pc, ref.entry + 1);
        else if (ref.draw_end)
            return {DRAWN_AT_LAYER, draw_flip};
        else
            stored = rank_value(pc, ref.entry + 1 + eb);
        break;
    }
    case 2 : {
        // DOUBLE: transitions at h1 < h2.
        const uint16_t h1 = ref.entry[0];
        const uint16_t h2 = ref.entry[1] & 0x7Fu;
        if (ref.draw_end)
            draw_flip = h2;
        if (layer < h1)
            stored = rank_value(pc, ref.entry + 2);
        else if (layer < h2)
            stored = rank_value(pc, ref.entry + 2 + eb);
        else if (ref.draw_end)
            return {DRAWN_AT_LAYER, draw_flip};
        else
            stored = rank_value(pc, ref.entry + 2 + 2 * eb);
        break;
    }
    default : {
        // MULTI: changepoint bitmap; rsel = popcount(bits ≤ layer) - 1.
        const size_t     k    = ref.entry[0] & 0x7Fu;
        constexpr size_t bm   = DTM50_MULTI_BITMAP_BYTES;
        const size_t     rsel = layered_multi_rank_slot<bm>(ref.entry, layer);
        if (ref.draw_end)
            draw_flip = layered_multi_last_changepoint<bm>(ref.entry);
        if (rsel == k - 1 && ref.draw_end)
            return {DRAWN_AT_LAYER, draw_flip};
        stored = rank_value(pc, ref.entry + 1 + bm + rsel * eb);
        break;
    }
    }

    const uint16_t value =
      flat ? dtm_value_from_storage(stored, wdl) : dtm50_value_from_storage(stored, wdl);
    return {value, draw_flip};
}

template struct Table_File<DTM50_Traits>;

}  // namespace Stockfish::ChessTB
