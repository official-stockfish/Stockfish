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

#ifndef CHESSTB_TABLE_FILES_H_INCLUDED
#define CHESSTB_TABLE_FILES_H_INCLUDED

#include "cache.h"
#include "common.h"
#include "filesystem.h"
#include "index.h"

#include "../misc.h"
#include "../types.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace Stockfish::ChessTB {

inline constexpr const char* WDL_EXT           = ".lzw";
inline constexpr const char* DTZ_EXT           = ".lzdtz";
inline constexpr const char* DTC_EXT           = ".lzdtc";
inline constexpr const char* DTM_EXT           = ".lzdtm";
inline constexpr const char* DTM50_EXT         = ".lzdtm50";
inline constexpr size_t      DTM50_PACK_LAYERS = DTM50_HMC_COUNT + 1;
inline constexpr size_t      DTC_BUDGET_LAYERS = 30;
inline constexpr size_t      DTC_PACK_LAYERS   = DTC_BUDGET_LAYERS + 1;

// Loads the little-endian value at p, of n <= sizeof(T) bytes.
template<typename T>
T read_le(const uint8_t* p, size_t n = sizeof(T)) {
    T v = 0;
    if (IsLittleEndian)
        std::memcpy(&v, p, n);
    else
        for (size_t i = 0; i < n; ++i)
            v |= T(p[i]) << (8 * i);
    return v;
}

// A colour's offset section: [u8 log2_bu][u8 sample_width][u8 offset_width]
// [u8 usz_width], the block offsets, and (DTC, DTM50) the decoded block sizes,
// unpadded. Bit reads take up to two 8-byte words past a packed region; on
// disk, whatever follows each section covers them.
constexpr uint64_t mono_low_mask(unsigned width) {
    return width >= 64 ? ~uint64_t{0} : ((uint64_t{1} << width) - 1);
}

// Read `width` (<=64) bits starting at bit `bitpos`. Requires >= 8 readable
// bytes past the last touched byte (see above).
inline uint64_t mono_read_bits(const uint8_t* base, size_t bitpos, unsigned width) {
    if (width == 0)
        return 0;
    const size_t   byte = bitpos >> 3;
    const unsigned bit  = static_cast<unsigned>(bitpos & 7);
    uint64_t       v    = read_le<uint64_t>(base + byte) >> bit;
    if (bit + width > 64)
        v |= read_le<uint64_t>(base + byte + 8) << (64 - bit);
    return v & mono_low_mask(width);
}

// On-disk/in-memory blob layout: [sample_bits][delta_bits].
// The 3 width/param bytes live in the caller's section header, not the blob.
struct Mono_Uint_Vec {
    const uint8_t* m_base         = nullptr;  // start of sample region
    const uint8_t* m_delta        = nullptr;  // start of delta region
    uint8_t        m_log2_bu      = 0;
    uint8_t        m_sample_width = 0;
    uint8_t        m_offset_width = 0;

    Mono_Uint_Vec() = default;

    // num_values is the count of stored offsets (== block_cnt + 1).
    Mono_Uint_Vec(const uint8_t* blob,
                  size_t         num_values,
                  uint8_t        log2_bu,
                  uint8_t        sample_width,
                  uint8_t        offset_width) :
        m_base(blob),
        m_log2_bu(log2_bu),
        m_sample_width(sample_width),
        m_offset_width(offset_width) {
        const size_t num_samples = ceil_div(num_values, size_t{1} << log2_bu);
        m_delta                  = blob + ceil_div(num_samples * sample_width, size_t{8});
    }

    // {O[i], O[i+1]}; valid for i in [0, num_values-2] (i.e. any block id).
    std::array<uint64_t, 2> get2(size_t i) const {
        const size_t   sb0 = i >> m_log2_bu;
        const size_t   sb1 = (i + 1) >> m_log2_bu;
        const uint64_t d0  = mono_read_bits(m_delta, i * m_offset_width, m_offset_width);
        const uint64_t d1  = mono_read_bits(m_delta, (i + 1) * m_offset_width, m_offset_width);
        const uint64_t b0  = mono_read_bits(m_base, sb0 * m_sample_width, m_sample_width);
        const uint64_t b1 =
          (sb1 == sb0) ? b0 : mono_read_bits(m_base, sb1 * m_sample_width, m_sample_width);
        return {b0 + d0, b1 + d1};
    }

    static size_t
    on_disk_bytes(size_t num_values, uint8_t log2_bu, uint8_t sample_width, uint8_t offset_width) {
        const size_t num_samples = ceil_div(num_values, size_t{1} << log2_bu);
        return ceil_div(num_samples * sample_width, size_t{8})
             + ceil_div(num_values * offset_width, size_t{8});
    }
};

struct Min0_Uint_Vec {
    const uint8_t* m_data  = nullptr;
    uint8_t        m_width = 0;

    Min0_Uint_Vec() = default;
    Min0_Uint_Vec(const uint8_t* data, uint8_t width) :
        m_data(data),
        m_width(width) {}

    uint64_t get(size_t i) const { return mono_read_bits(m_data, i * m_width, m_width); }

    static size_t on_disk_bytes(size_t size, uint8_t width) {
        return ceil_div(size * width, size_t{8});
    }
};

struct Serial_Memory_Reader;

// Each per-colour struct keeps its Block_Cache as a member: GCC PGO can
// misinfer the size of a derived per-colour object from a Block_Cache base
// (-Warray-bounds).
struct WDL_Per_Color {
    explicit WDL_Per_Color(std::shared_ptr<Block_Pool> p) :
        cache(std::move(p)) {}

    Block_Cache cache;

    Index_Storage_Layout layout;
    size_t               block_size = 0;
    size_t               tail_size  = 0;
    size_t               block_cnt  = 0;
    Mono_Uint_Vec        offsets;
    const uint8_t*       compressed_data = nullptr;
    Const_Span<uint8_t>  dict;  // in the mapped file
    WDL_Stored           single_val = WDL_Stored::DRAW;

    size_t data_size = 0;
};

struct LZMA_Rank_Per_Color {
    explicit LZMA_Rank_Per_Color(std::shared_ptr<Block_Pool> p) :
        cache(std::move(p)) {}

    Block_Cache cache;

    Index_Storage_Layout  layout;
    size_t                entry_bytes = 0;
    size_t                block_size  = 0;
    size_t                tail_size   = 0;
    size_t                block_cnt   = 0;
    Mono_Uint_Vec         offsets;
    const uint8_t*        compressed_data = nullptr;
    std::vector<uint16_t> rank_to_value;
    uint16_t              single_val = 0;

    size_t data_size = 0;
};

struct Layered_Rank_Per_Color {
    explicit Layered_Rank_Per_Color(std::shared_ptr<Block_Pool> p) :
        cache(std::move(p)) {}

    Block_Cache cache;

    Index_Storage_Layout  layout;
    size_t                entry_bytes     = 0;
    size_t                block_positions = 0;
    size_t                tail_positions  = 0;
    size_t                block_cnt       = 0;
    Mono_Uint_Vec         offsets;
    Min0_Uint_Vec         usizes;
    const uint8_t*        compressed_data = nullptr;
    std::vector<uint16_t> rank_to_value;

    size_t data_size = 0;
};

struct WDL_Traits {
    static constexpr EGTB_Magic MAGIC          = EGTB_Magic::WDL_MAGIC;
    static constexpr size_t     TL_BLOCK_SLOTS = 128;
    using Per_Color                            = WDL_Per_Color;

    static bool       on_singular(Serial_Memory_Reader& reader, Per_Color& pc);
    static bool       parse_header(Serial_Memory_Reader&        reader,
                                   Per_Color&                   pc,
                                   const Position_Index_Config& index_cfg);
    static void       finalize(Serial_Memory_Reader& reader,
                               Per_Color (&per_color)[COLOR_NB],
                               const bool (&is_singular)[COLOR_NB],
                               const bool (&is_dropped)[COLOR_NB]);
    static WDL_Stored read(Per_Color& pc, bool is_singular, Board_Index pos);
};

// The loaders the rank tables share: DTZ and DTM files differ only in what a
// value means, as do DTC and DTM50 files.
struct LZMA_Rank_Loader {
    using Per_Color = LZMA_Rank_Per_Color;

    static bool on_singular(Serial_Memory_Reader& reader, Per_Color& pc);
    static bool parse_header(Serial_Memory_Reader&        reader,
                             Per_Color&                   pc,
                             const Position_Index_Config& index_cfg);
    static void finalize(Serial_Memory_Reader& reader,
                         Per_Color (&per_color)[COLOR_NB],
                         const bool (&is_singular)[COLOR_NB],
                         const bool (&is_dropped)[COLOR_NB]);
};

struct Layered_Rank_Loader {
    using Per_Color = Layered_Rank_Per_Color;

    static bool on_singular(Serial_Memory_Reader& reader, Per_Color& pc);
    static bool parse_header(Serial_Memory_Reader&        reader,
                             Per_Color&                   pc,
                             const Position_Index_Config& index_cfg);
    static void finalize(Serial_Memory_Reader& reader,
                         Per_Color (&per_color)[COLOR_NB],
                         const bool (&is_singular)[COLOR_NB],
                         const bool (&is_dropped)[COLOR_NB]);
};

struct DTZ_Value {
    uint16_t dtz;
    bool     rounded;
};

struct DTZ_Traits: LZMA_Rank_Loader {
    static constexpr EGTB_Magic MAGIC          = EGTB_Magic::DTZ_MAGIC;
    static constexpr size_t     TL_BLOCK_SLOTS = 16;
    static DTZ_Value read(Per_Color& pc, bool is_singular, Board_Index pos, WDL_Entry wdl);
};

inline unsigned dtc_budget_plies(unsigned rule50) {
    if (rule50 == IGNORE_50MR)
        return DTZ_MAX_NON_CURSED;
    return (rule50 >= DTZ_MAX_NON_CURSED) ? 0u : DTZ_MAX_NON_CURSED - rule50;
}

struct DTC_Cell {
    uint16_t order       = 0;
    uint16_t value       = DRAWN_AT_LAYER;
    uint16_t dtz         = DRAWN_AT_LAYER;
    bool     dtz_rounded = false;

    bool priced() const { return value != DRAWN_AT_LAYER; }
};

struct DTC_Curve {
    uint16_t value[DTC_PACK_LAYERS];
};

struct DTC_Traits: Layered_Rank_Loader {
    static constexpr EGTB_Magic MAGIC          = EGTB_Magic::DTC_MAGIC;
    static constexpr size_t     TL_BLOCK_SLOTS = 16;
    static DTC_Cell
    read(Per_Color& pc, bool is_singular, Board_Index pos, WDL_Entry wdl, unsigned rule50);
    static void
    read_curve(Per_Color& pc, bool is_singular, Board_Index pos, WDL_Entry wdl, DTC_Curve& curve);
};

struct DTM_Traits: LZMA_Rank_Loader {
    static constexpr EGTB_Magic MAGIC          = EGTB_Magic::DTM_MAGIC;
    static constexpr size_t     TL_BLOCK_SLOTS = 16;
    static uint16_t read(Per_Color& pc, bool is_singular, Board_Index pos, WDL_Entry wdl);
};

// One DTM50 cell. `draw_flip` is the layer where the position turns DRAW, 0 if
// it never does; for a plain WIN/LOSE cell it pins DTZ. `value` is DRAWN_AT_LAYER
// when the probed layer is DRAW.
struct DTM50_Cell {
    uint16_t value     = DRAWN_AT_LAYER;
    uint16_t draw_flip = 0;
};

struct DTM50_Traits: Layered_Rank_Loader {
    static constexpr EGTB_Magic MAGIC          = EGTB_Magic::DTM50_MAGIC;
    static constexpr size_t     TL_BLOCK_SLOTS = 16;
    static DTM50_Cell
    read(Per_Color& pc, bool is_singular, Board_Index pos, WDL_Entry wdl, unsigned hmc);
};

template<typename Traits>
struct Table_File {
    using Per_Color = typename Traits::Per_Color;

    bool                                   is_singular[COLOR_NB]  = {false, false};
    bool                                   is_dropped[COLOR_NB]   = {false, false};
    bool                                   is_loss_only[COLOR_NB] = {false, false};
    bool                                   is_relaxed[COLOR_NB]   = {false, false};
    Per_Color                              per_color[COLOR_NB];
    Memory_Mapped_File                     mapped;
    std::unique_ptr<Position_Index_Config> index_cfg;

    explicit Table_File(std::shared_ptr<Block_Pool> pool) :
        per_color{Per_Color(pool), Per_Color(pool)} {
        static_assert(COLOR_NB == 2);
    }

    bool load(const Piece_Config& ps, const std::filesystem::path& path);

    Board_Index storage_index(Color c, const View& pos) const {
        assert(!is_dropped[c]);
        if (is_singular[c])
            return BOARD_INDEX_ZERO;
        return board_index_of_position(*index_cfg, per_color[c].layout, pos);
    }

    template<typename... Args>
    auto read(Color c, const View& pos, Args... args) {
        const Board_Index storage_pos = storage_index(c, pos);
        return Traits::read(per_color[c], is_singular[c], storage_pos, args...);
    }

    template<typename... Args>
    auto read_curve(Color c, const View& pos, Args&&... args) {
        const Board_Index storage_pos = storage_index(c, pos);
        return Traits::read_curve(per_color[c], is_singular[c], storage_pos,
                                  std::forward<Args>(args)...);
    }
};

using WDL_File   = Table_File<WDL_Traits>;
using DTZ_File   = Table_File<DTZ_Traits>;
using DTC_File   = Table_File<DTC_Traits>;
using DTM_File   = Table_File<DTM_Traits>;
using DTM50_File = Table_File<DTM50_Traits>;

// Each type is instantiated once, in table_files.cpp.
extern template struct Table_File<WDL_Traits>;
extern template struct Table_File<DTZ_Traits>;
extern template struct Table_File<DTC_Traits>;
extern template struct Table_File<DTM_Traits>;
extern template struct Table_File<DTM50_Traits>;

}  // namespace Stockfish::ChessTB

#endif  // CHESSTB_TABLE_FILES_H_INCLUDED
