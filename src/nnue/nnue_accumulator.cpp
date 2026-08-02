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

#include "nnue_accumulator.h"

#include <algorithm>
#include <cassert>
#include <new>

#include "../bitboard.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_common.h"
#include "nnue_feature_transformer.h"  // IWYU pragma: keep
#include "simd.h"

namespace Stockfish::Eval::NNUE {

using namespace SIMD;

namespace {

template<bool Forward>
void update_accumulator_incremental(Color                     perspective,
                                    const FeatureTransformer& featureTransformer,
                                    const Square              ksq,
                                    AccumulatorState&         target_state,
                                    const AccumulatorState&   computed);

void update_accumulator_incremental_both(const FeatureTransformer& featureTransformer,
                                         Square                    white_ksq,
                                         Square                    black_ksq,
                                         AccumulatorState&         target_state,
                                         const AccumulatorState&   computed);

void update_accumulator_refresh_cache(Color                     perspective,
                                      const FeatureTransformer& featureTransformer,
                                      const Position&           pos,
                                      AccumulatorState&         accumulatorState,
                                      AccumulatorCaches&        cache);

void update_accumulator_hybrid(Color                     perspective,
                               const Position&           pos,
                               const FeatureTransformer& featureTransformer,
                               AccumulatorState&         target,
                               const AccumulatorState&   computed,
                               AccumulatorCaches&        cache);
}

const AccumulatorState& AccumulatorStack::latest() const noexcept { return accumulators[size - 1]; }

AccumulatorState& AccumulatorStack::mut_latest() noexcept { return accumulators[size - 1]; }

void AccumulatorStack::reset() noexcept {
    accumulators[0].dirtyPiece = {};
    new (&accumulators[0].dirtyThreats) DirtyThreats;
    new (&accumulators[0].dirtyPawnPairs) DirtyPawnPairs;
    accumulators[0].computed.fill(false);
    size = 1;
}

Dirties& AccumulatorStack::push() noexcept {
    assert(size < MaxSize);
    auto& st = accumulators[size];
    st.computed.fill(false);
    new (&st.dirtyThreats) DirtyThreats;
    new (&st.dirtyPawnPairs) DirtyPawnPairs;
    size++;
    return st;
}

void AccumulatorStack::pop() noexcept {
    assert(size > 1);
    size--;
}

void AccumulatorStack::evaluate(const Position&           pos,
                                const FeatureTransformer& featureTransformer,
                                // Silence spurious warning on GCC 10
                                [[maybe_unused]] AccumulatorCaches& cache) noexcept {
    const usize last_white = find_last_usable_accumulator(WHITE);
    const usize last_black = find_last_usable_accumulator(BLACK);

    if (accumulators[last_white].computed[WHITE] && accumulators[last_black].computed[BLACK])
        forward_update_incremental_both(pos, featureTransformer, last_white, last_black);
    else
    {
        evaluate_side(WHITE, pos, featureTransformer, cache, last_white);
        evaluate_side(BLACK, pos, featureTransformer, cache, last_black);
    }
}

void AccumulatorStack::evaluate_side(Color                     perspective,
                                     const Position&           pos,
                                     const FeatureTransformer& featureTransformer,
                                     AccumulatorCaches&        cache,
                                     usize                     last_usable_accum) noexcept {

    constexpr int MIN_PC_COUNT_HYBRID = 15;

    if (accumulators[last_usable_accum].computed[perspective])
        forward_update_incremental(perspective, pos, featureTransformer, last_usable_accum);

    else
    {
        const auto& dirtyPiece = latest().dirtyPiece;

        if (dirtyPiece.pc == make_piece(perspective, KING)
            && accumulators[size - 2].computed[perspective]
            && pos.count<ALL_PIECES>() >= MIN_PC_COUNT_HYBRID
            && ((int(dirtyPiece.from) & 0b100) == (int(dirtyPiece.to) & 0b100))
            && dirtyPiece.add_sq == SQ_NONE  // excludes castling
        )
        {
            update_accumulator_hybrid(perspective, pos, featureTransformer, mut_latest(),
                                      accumulators[size - 2], cache);
            return;
        }

        update_accumulator_refresh_cache(perspective, featureTransformer, pos, mut_latest(), cache);
        backward_update_incremental(perspective, pos, featureTransformer, last_usable_accum);
    }
}

// Find the earliest usable accumulator, this can either be a computed accumulator or the accumulator
// state just before a change that requires full refresh.
usize AccumulatorStack::find_last_usable_accumulator(Color perspective) const noexcept {

    for (usize curr_idx = size - 1; curr_idx > 0; curr_idx--)
    {
        if (accumulators[curr_idx].computed[perspective])
            return curr_idx;

        // Threat feature set refreshes require a king move across the center, i.e.,
        // a subset of halfka refreshes
        if (PSQFeatureSet::requires_refresh(accumulators[curr_idx].dirtyPiece, perspective))
            return curr_idx;
    }

    return 0;
}

void AccumulatorStack::forward_update_incremental(Color                     perspective,
                                                  const Position&           pos,
                                                  const FeatureTransformer& featureTransformer,
                                                  const usize               begin) noexcept {

    assert(begin < accumulators.size());
    assert(accumulators[begin].computed[perspective]);

    const Square ksq = pos.square<KING>(perspective);

    for (usize next = begin + 1; next < size; next++)
        update_accumulator_incremental<true>(perspective, featureTransformer, ksq,
                                             accumulators[next], accumulators[next - 1]);

    assert(latest().computed[perspective]);
}

void AccumulatorStack::backward_update_incremental(Color                     perspective,
                                                   const Position&           pos,
                                                   const FeatureTransformer& featureTransformer,
                                                   const usize               end) noexcept {

    assert(end < accumulators.size());
    assert(end < size);
    assert(latest().computed[perspective]);

    const Square ksq = pos.square<KING>(perspective);

    for (i64 next = i64(size) - 2; next >= i64(end); next--)
        update_accumulator_incremental<false>(perspective, featureTransformer, ksq,
                                              accumulators[next], accumulators[next + 1]);

    assert(accumulators[end].computed[perspective]);
}

void AccumulatorStack::forward_update_incremental_both(const Position&           pos,
                                                       const FeatureTransformer& featureTransformer,
                                                       usize                     white_begin,
                                                       usize black_begin) noexcept {

    assert(white_begin < size);
    assert(black_begin < size);
    assert(accumulators[white_begin].computed[WHITE]);
    assert(accumulators[black_begin].computed[BLACK]);

    const Square white_ksq    = pos.square<KING>(WHITE);
    const Square black_ksq    = pos.square<KING>(BLACK);
    const usize  shared_begin = std::max(white_begin, black_begin);

    // Catch up the lagging perspective, then traverse the common suffix once.
    for (usize next = white_begin + 1; next <= shared_begin; ++next)
        update_accumulator_incremental<true>(WHITE, featureTransformer, white_ksq,
                                             accumulators[next], accumulators[next - 1]);
    for (usize next = black_begin + 1; next <= shared_begin; ++next)
        update_accumulator_incremental<true>(BLACK, featureTransformer, black_ksq,
                                             accumulators[next], accumulators[next - 1]);

    for (usize next = shared_begin + 1; next < size; ++next)
        update_accumulator_incremental_both(featureTransformer, white_ksq, black_ksq,
                                            accumulators[next], accumulators[next - 1]);

    assert(latest().computed[WHITE]);
    assert(latest().computed[BLACK]);
}

namespace {

constexpr IndexType Dimensions = FeatureTransformer::OutputDimensions;

#ifdef VECTOR

using Tiling = SIMDTiling<Dimensions, Dimensions, PSQTBuckets>;

template<int sign>
sf_always_inline inline void apply_psq_features(IndexType                       j,
                                                vec_t                           acc[],
                                                const PSQFeatureSet::IndexList& list,
                                                const FeatureTransformer&       ft) {
    static_assert(sign == 1 || sign == -1);

    const usize tileOff = j * Tiling::TileHeight;
    for (int i = 0; i < list.ssize(); ++i)
    {
        auto* column = reinterpret_cast<const vec_t*>(&ft.weights[list[i] * Dimensions + tileOff]);
        for (IndexType k = 0; k < Tiling::NumRegs; ++k)
            if constexpr (sign == +1)
                acc[k] = vec_add_16(acc[k], column[k]);
            else
                acc[k] = vec_sub_16(acc[k], column[k]);
    }
}

template<int sign>
sf_always_inline inline void apply_threat_features(IndexType j,
                                                   vec_t     acc[Tiling::NumRegs],
                                                   const ThreatFeatureSet::IndexList& list,
                                                   const FeatureTransformer&          ft) {
    static_assert(sign == 1 || sign == -1);

    const usize tileOff = j * Tiling::TileHeight;
    for (int i = 0; i < list.ssize(); ++i)
    {
        auto* column =
          reinterpret_cast<const vec_i8_t*>(&ft.threatAndPpWeights[list[i] * Dimensions + tileOff]);
    #ifdef USE_NEON
        for (IndexType k = 0; k < Tiling::NumRegs; k += 2)
        {
            if constexpr (sign == +1)
            {
                acc[k]     = vaddw_s8(acc[k], vget_low_s8(column[k / 2]));
                acc[k + 1] = vaddw_high_s8(acc[k + 1], column[k / 2]);
            }
            else
            {
                acc[k]     = vsubw_s8(acc[k], vget_low_s8(column[k / 2]));
                acc[k + 1] = vsubw_high_s8(acc[k + 1], column[k / 2]);
            }
        }
    #elif defined(USE_LSX) && !defined(USE_LASX)
        for (IndexType k = 0; k < Tiling::NumRegs; k += 2)
        {
            if constexpr (sign == +1)
            {
                const __m128i weight = __lsx_vld(reinterpret_cast<const void*>(&column[k]), 0);
                acc[k]               = vec_add_16(acc[k], __lsx_vsllwil_h_b(weight, 0));
                acc[k + 1]           = vec_add_16(acc[k + 1], __lsx_vexth_h_b(weight));
            }
            else
            {
                const __m128i weight = __lsx_vld(reinterpret_cast<const void*>(&column[k]), 0);
                acc[k]               = vec_sub_16(acc[k], __lsx_vsllwil_h_b(weight, 0));
                acc[k + 1]           = vec_sub_16(acc[k + 1], __lsx_vexth_h_b(weight));
            }
        }
    #else
        for (IndexType k = 0; k < Tiling::NumRegs; ++k)
            if constexpr (sign == +1)
                acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));
            else
                acc[k] = vec_sub_16(acc[k], vec_convert_8_16(column[k]));
    #endif
    }
}

template<int sign, usize MaxLen, typename IdxType>
sf_always_inline inline void apply_psqt(IndexType                         j,
                                        psqt_vec_t                        psqt[Tiling::NumPsqtRegs],
                                        const ValueList<IdxType, MaxLen>& list,
                                        const PSQTWeightType*             weights) {
    static_assert(sign == 1 || sign == -1);

    const usize psqtTileOff = j * Tiling::PsqtTileHeight;
    for (int i = 0; i < list.ssize(); ++i)
    {
        auto* column =
          reinterpret_cast<const psqt_vec_t*>(&weights[list[i] * PSQTBuckets + psqtTileOff]);
        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            if constexpr (sign == +1)
                psqt[k] = vec_add_psqt_32(psqt[k], column[k]);
            else
                psqt[k] = vec_sub_psqt_32(psqt[k], column[k]);
    }
}

#endif

void apply_combined(Color                              perspective,
                    const FeatureTransformer&          featureTransformer,
                    const AccumulatorState&            from,
                    AccumulatorState&                  to,
                    const PSQFeatureSet::IndexList&    psqAdded,
                    const PSQFeatureSet::IndexList&    psqRemoved,
                    const ThreatFeatureSet::IndexList& thrAdded,
                    const ThreatFeatureSet::IndexList& thrRemoved) {

    const auto& fromAcc = from.accumulation[perspective];
    auto&       toAcc   = to.accumulation[perspective];

    const auto& fromPsqtAcc = from.psqtAccumulation[perspective];
    auto&       toPsqtAcc   = to.psqtAccumulation[perspective];

#ifdef VECTOR

    vec_t      acc[Tiling::NumRegs];
    psqt_vec_t psqt[Tiling::NumPsqtRegs];

    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        const usize tileOff  = j * Tiling::TileHeight;
        auto*       fromTile = reinterpret_cast<const vec_t*>(&fromAcc[tileOff]);
        auto*       toTile   = reinterpret_cast<vec_t*>(&toAcc[tileOff]);

        for (IndexType k = 0; k < Tiling::NumRegs; ++k)
            acc[k] = fromTile[k];

        apply_psq_features<-1>(j, acc, psqRemoved, featureTransformer);
        apply_psq_features<+1>(j, acc, psqAdded, featureTransformer);

        apply_threat_features<-1>(j, acc, thrRemoved, featureTransformer);
        apply_threat_features<+1>(j, acc, thrAdded, featureTransformer);

        for (IndexType k = 0; k < Tiling::NumRegs; k++)
            vec_store(&toTile[k], acc[k]);
    }

    for (IndexType j = 0; j < PSQTBuckets / Tiling::PsqtTileHeight; ++j)
    {
        const usize psqtTileOff  = j * Tiling::PsqtTileHeight;
        auto*       fromTilePsqt = reinterpret_cast<const psqt_vec_t*>(&fromPsqtAcc[psqtTileOff]);
        auto*       toTilePsqt   = reinterpret_cast<psqt_vec_t*>(&toPsqtAcc[psqtTileOff]);

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            psqt[k] = fromTilePsqt[k];

        apply_psqt<-1>(j, psqt, psqRemoved, featureTransformer.psqtWeights.data());
        apply_psqt<+1>(j, psqt, psqAdded, featureTransformer.psqtWeights.data());

        apply_psqt<-1>(j, psqt, thrRemoved, featureTransformer.threatAndPpPsqtWeights.data());
        apply_psqt<+1>(j, psqt, thrAdded, featureTransformer.threatAndPpPsqtWeights.data());

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            vec_store_psqt(&toTilePsqt[k], psqt[k]);
    }

#elif defined(USE_RVV)

    usize tileOffset = 0;

    const auto* psqWeights        = &featureTransformer.weights[0];
    const auto* threatWeights     = &featureTransformer.threatAndPpWeights[0];
    const auto* psqtWeights       = &featureTransformer.psqtWeights[0];
    const auto* threatPsqtWeights = &featureTransformer.threatAndPpPsqtWeights[0];

    while (tileOffset < Dimensions)
    {
        usize vl = __riscv_vsetvl_e16m8(Dimensions - tileOffset);

        vint16m8_t accum = __riscv_vle16_v_i16m8(&fromAcc[tileOffset], vl);
        for (int i : psqRemoved)
            accum = __riscv_vsub_vv_i16m8(
              accum, __riscv_vle16_v_i16m8(&psqWeights[i * Dimensions + tileOffset], vl), vl);
        for (int i : psqAdded)
            accum = __riscv_vadd_vv_i16m8(
              accum, __riscv_vle16_v_i16m8(&psqWeights[i * Dimensions + tileOffset], vl), vl);
        for (int i : thrRemoved)
            accum = __riscv_vwsub_wv_i16m8(
              accum, __riscv_vle8_v_i8m4(&threatWeights[i * Dimensions + tileOffset], vl), vl);
        for (int i : thrAdded)
            accum = __riscv_vwadd_wv_i16m8(
              accum, __riscv_vle8_v_i8m4(&threatWeights[i * Dimensions + tileOffset], vl), vl);
        __riscv_vse16_v_i16m8(&toAcc[tileOffset], accum, vl);

        tileOffset += vl;
    }

    tileOffset = 0;

    while (tileOffset < PSQTBuckets)
    {
        usize vl = __riscv_vsetvl_e32m1(PSQTBuckets - tileOffset);

        vint32m1_t accum = __riscv_vle32_v_i32m1(&fromPsqtAcc[tileOffset], vl);
        for (int i : psqRemoved)
            accum = __riscv_vsub_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&psqtWeights[i * PSQTBuckets + tileOffset], vl), vl);
        for (int i : psqAdded)
            accum = __riscv_vadd_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&psqtWeights[i * PSQTBuckets + tileOffset], vl), vl);
        for (int i : thrRemoved)
            accum = __riscv_vsub_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&threatPsqtWeights[i * PSQTBuckets + tileOffset], vl),
              vl);
        for (int i : thrAdded)
            accum = __riscv_vadd_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&threatPsqtWeights[i * PSQTBuckets + tileOffset], vl),
              vl);

        __riscv_vse32_v_i32m1(&toPsqtAcc[tileOffset], accum, vl);

        tileOffset += vl;
    }

#else

    toAcc     = fromAcc;
    toPsqtAcc = fromPsqtAcc;

    for (const auto index : psqRemoved)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            toAcc[j] -= featureTransformer.weights[offset + j];
        for (usize k = 0; k < PSQTBuckets; ++k)
            toPsqtAcc[k] -= featureTransformer.psqtWeights[index * PSQTBuckets + k];
    }

    for (const auto index : psqAdded)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            toAcc[j] += featureTransformer.weights[offset + j];
        for (usize k = 0; k < PSQTBuckets; ++k)
            toPsqtAcc[k] += featureTransformer.psqtWeights[index * PSQTBuckets + k];
    }

    for (const auto index : thrRemoved)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            toAcc[j] -= featureTransformer.threatAndPpWeights[offset + j];
        for (usize k = 0; k < PSQTBuckets; ++k)
            toPsqtAcc[k] -= featureTransformer.threatAndPpPsqtWeights[index * PSQTBuckets + k];
    }

    for (const auto index : thrAdded)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            toAcc[j] += featureTransformer.threatAndPpWeights[offset + j];
        for (usize k = 0; k < PSQTBuckets; ++k)
            toPsqtAcc[k] += featureTransformer.threatAndPpPsqtWeights[index * PSQTBuckets + k];
    }

#endif
}

template<bool Forward>
void update_accumulator_incremental(Color                     perspective,
                                    const FeatureTransformer& featureTransformer,
                                    const Square              ksq,
                                    AccumulatorState&         target_state,
                                    const AccumulatorState&   computed) {

    assert(computed.computed[perspective]);
    assert(!target_state.computed[perspective]);

    // The size must be enough to contain the largest possible update.
    // That might depend on the feature set and generally relies on the
    // feature set's update cost calculation to be correct and never allow
    // updates with more added/removed features than MaxActiveDimensions.
    PSQFeatureSet::IndexList    psqRemoved, psqAdded;
    ThreatFeatureSet::IndexList thrRemoved, thrAdded;

    const auto& dirtyPiece     = Forward ? target_state.dirtyPiece : computed.dirtyPiece;
    const auto& dirtyThreats   = Forward ? target_state.dirtyThreats : computed.dirtyThreats;
    const auto& dirtyPawnPairs = Forward ? target_state.dirtyPawnPairs : computed.dirtyPawnPairs;

    // Used solely for prefetching
    const auto* threatPpBase = &featureTransformer.threatAndPpWeights[0];
    IndexType   pfStride     = FeatureTransformer::OutputDimensions;

    if constexpr (Forward)
    {
        ThreatFeatureSet::append_changed_indices(perspective, ksq, dirtyThreats, thrRemoved,
                                                 thrAdded, threatPpBase, pfStride);
        PairFeatureSet::append_changed_indices(perspective, ksq, dirtyPawnPairs, thrRemoved,
                                               thrAdded, threatPpBase, pfStride);
        PSQFeatureSet::append_changed_indices(perspective, ksq, dirtyPiece, psqRemoved, psqAdded);
    }
    else
    {
        ThreatFeatureSet::append_changed_indices(perspective, ksq, dirtyThreats, thrAdded,
                                                 thrRemoved, threatPpBase, pfStride);
        PairFeatureSet::append_changed_indices(perspective, ksq, dirtyPawnPairs, thrAdded,
                                               thrRemoved, threatPpBase, pfStride);
        PSQFeatureSet::append_changed_indices(perspective, ksq, dirtyPiece, psqAdded, psqRemoved);
    }

    apply_combined(perspective, featureTransformer, computed, target_state, psqAdded, psqRemoved,
                   thrAdded, thrRemoved);

    target_state.computed[perspective] = true;
}

void update_accumulator_incremental_both(const FeatureTransformer& featureTransformer,
                                         Square                    white_ksq,
                                         Square                    black_ksq,
                                         AccumulatorState&         target_state,
                                         const AccumulatorState&   computed) {

    assert(computed.computed[WHITE]);
    assert(computed.computed[BLACK]);
    assert(!target_state.computed[WHITE]);
    assert(!target_state.computed[BLACK]);

    PSQFeatureSet::IndexList    psq_removed[COLOR_NB], psq_added[COLOR_NB];
    ThreatFeatureSet::IndexList thr_removed[COLOR_NB], thr_added[COLOR_NB];

    const auto* threat_pp_base = &featureTransformer.threatAndPpWeights[0];
    const auto  pf_stride      = FeatureTransformer::OutputDimensions;

    ThreatFeatureSet::append_changed_indices_both(
      white_ksq, black_ksq, target_state.dirtyThreats, thr_removed[WHITE], thr_added[WHITE],
      thr_removed[BLACK], thr_added[BLACK], threat_pp_base, pf_stride);
    PairFeatureSet::append_changed_indices_both(
      white_ksq, black_ksq, target_state.dirtyPawnPairs, thr_removed[WHITE], thr_added[WHITE],
      thr_removed[BLACK], thr_added[BLACK], threat_pp_base, pf_stride);
    PSQFeatureSet::append_changed_indices(WHITE, white_ksq, target_state.dirtyPiece,
                                          psq_removed[WHITE], psq_added[WHITE]);
    PSQFeatureSet::append_changed_indices(BLACK, black_ksq, target_state.dirtyPiece,
                                          psq_removed[BLACK], psq_added[BLACK]);

    apply_combined(WHITE, featureTransformer, computed, target_state, psq_added[WHITE],
                   psq_removed[WHITE], thr_added[WHITE], thr_removed[WHITE]);
    apply_combined(BLACK, featureTransformer, computed, target_state, psq_added[BLACK],
                   psq_removed[BLACK], thr_added[BLACK], thr_removed[BLACK]);

    target_state.computed[WHITE] = true;
    target_state.computed[BLACK] = true;
}

Bitboard get_changed_pieces(const std::array<Piece, SQUARE_NB>& oldPieces,
                            const std::array<Piece, SQUARE_NB>& newPieces) {
#if defined(USE_AVX2)
    static_assert(sizeof(Piece) == 1);
    Bitboard sameBB = 0;

    for (int i = 0; i < 64; i += 32)
    {
        const __m256i old_v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&oldPieces[i]));
        const __m256i new_v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&newPieces[i]));
        const __m256i cmpEqual  = _mm256_cmpeq_epi8(old_v, new_v);
        const u32     equalMask = _mm256_movemask_epi8(cmpEqual);
        sameBB |= static_cast<Bitboard>(equalMask) << i;
    }
    return ~sameBB;
#elif defined(USE_LASX)
    static_assert(sizeof(Piece) == 1);

    Bitboard changed = 0;

    for (int i = 0; i < 64; i += 32)
    {
        const __m256i old_v = __lasx_xvld(reinterpret_cast<const void*>(&oldPieces[i]), 0);
        const __m256i new_v = __lasx_xvld(reinterpret_cast<const void*>(&newPieces[i]), 0);
        const __m256i diff  = __lasx_xvxor_v(old_v, new_v);
        const __m256i mask  = __lasx_xvmsknz_b(diff);
        const auto    lo    = __lasx_xvpickve2gr_d(mask, 0);
        const auto    hi    = __lasx_xvpickve2gr_d(mask, 2);

        changed |= (static_cast<Bitboard>(lo) | (static_cast<Bitboard>(hi) << 16)) << i;
    }

    return changed;
#elif defined(USE_LSX)
    static_assert(sizeof(Piece) == 1);

    Bitboard changed = 0;

    for (int i = 0; i < 64; i += 16)
    {
        const __m128i old_v = __lsx_vld(reinterpret_cast<const void*>(&oldPieces[i]), 0);
        const __m128i new_v = __lsx_vld(reinterpret_cast<const void*>(&newPieces[i]), 0);
        const __m128i diff  = __lsx_vxor_v(old_v, new_v);
        const __m128i mask  = __lsx_vmsknz_b(diff);

        changed |= static_cast<Bitboard>(__lsx_vpickve2gr_d(mask, 0)) << i;
    }

    return changed;
#elif defined(USE_NEON)
    uint8x16x4_t old_v = vld4q_u8(reinterpret_cast<const u8*>(oldPieces.data()));
    uint8x16x4_t new_v = vld4q_u8(reinterpret_cast<const u8*>(newPieces.data()));
    auto         cmp   = [=](const int i) { return vceqq_u8(old_v.val[i], new_v.val[i]); };

    uint8x16_t cmp0_1 = vsriq_n_u8(cmp(1), cmp(0), 1);
    uint8x16_t cmp2_3 = vsriq_n_u8(cmp(3), cmp(2), 1);
    uint8x16_t merged = vsriq_n_u8(cmp2_3, cmp0_1, 2);
    merged            = vsriq_n_u8(merged, merged, 4);
    uint8x8_t sameBB  = vshrn_n_u16(vreinterpretq_u16_u8(merged), 4);

    return ~vget_lane_u64(vreinterpret_u64_u8(sameBB), 0);
#elif defined(USE_SSE2)
    Bitboard sameBB = 0;

    for (int i = 0; i < 64; i += 16)
    {
        const __m128i old_v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&oldPieces[i]));
        const __m128i new_v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&newPieces[i]));
        const __m128i same  = _mm_cmpeq_epi8(old_v, new_v);

        sameBB |= static_cast<Bitboard>(_mm_movemask_epi8(same)) << i;
    }

    return ~sameBB;
#elif defined(USE_RVV)

    #define IMPL(mx, bx) \
        return __riscv_vmv_x_s_u64m1_u64(__riscv_vreinterpret_v_u8m1_u64m1( \
          __riscv_vreinterpret_v_b##bx##_u8m1(__riscv_vmsne_vv_i8m##mx##_b##bx( \
            __riscv_vle8_v_i8m##mx(reinterpret_cast<const i8*>(oldPieces.data()), 64), \
            __riscv_vle8_v_i8m##mx(reinterpret_cast<const i8*>(newPieces.data()), 64), 64))))


    usize vl = __riscv_vsetvlmax_e8m1();
    if (vl >= 64)
        IMPL(1, 8);
    else if (vl == 32)
        IMPL(2, 4);
    else
        IMPL(4, 2);

    #undef IMPL

#else
    Bitboard changed = 0;

    for (Square sq = SQUARE_ZERO; sq < SQUARE_NB; ++sq)
        changed |= static_cast<Bitboard>(oldPieces[sq] != newPieces[sq]) << sq;

    return changed;
#endif
}

// Updates accumulator for a king move, and also updates the accumulator cache
// for the new king position.
void update_accumulator_hybrid(Color                     perspective,
                               const Position&           pos,
                               const FeatureTransformer& featureTransformer,
                               AccumulatorState&         target,
                               const AccumulatorState&   computed,
                               AccumulatorCaches&        cache) {
    const auto& dirtyPiece = target.dirtyPiece;

    assert(dirtyPiece.pc == make_piece(perspective, KING));
    assert(dirtyPiece.to != SQ_NONE);
    assert((int(dirtyPiece.from) & 0b100) == (int(dirtyPiece.to) & 0b100));
    assert(computed.computed[perspective]);
    assert(!target.computed[perspective]);

    const Square oldKsq = dirtyPiece.from;
    const Square newKsq = dirtyPiece.to;
    assert(oldKsq != newKsq);

    const auto& currentPieces  = pos.piece_array();
    auto        previousPieces = currentPieces;  // copies 64 bytes!

    Bitboard previousPieceBB = pos.pieces();

    assert(previousPieces[newKsq] == dirtyPiece.pc);

    if (dirtyPiece.remove_sq != SQ_NONE)
    {
        assert(dirtyPiece.remove_sq == newKsq);
        previousPieces[newKsq] = dirtyPiece.remove_pc;
    }
    else
    {
        previousPieces[newKsq] = NO_PIECE;
        previousPieceBB &= ~square_bb(newKsq);
    }

    assert(previousPieces[oldKsq] == NO_PIECE);
    previousPieces[oldKsq] = make_piece(perspective, KING);
    previousPieceBB |= square_bb(oldKsq);

    const auto& oldEntry = cache[oldKsq][perspective];
    auto&       newEntry = cache[newKsq][perspective];

    // "Remove" means we need to remove them from the cache entry,
    // "Add" means add them to the entry to get the accumulator we want
    PSQFeatureSet::IndexList oldRemove, oldAdd, newRemove, newAdd;

    Bitboard oldChangedBB = get_changed_pieces(oldEntry.pieces, previousPieces);
    Bitboard oldRemovedBB = oldChangedBB & oldEntry.pieceBB;
    Bitboard oldAddedBB   = oldChangedBB & previousPieceBB;

    Bitboard newChangedBB = get_changed_pieces(newEntry.pieces, currentPieces);
    Bitboard newRemovedBB = newChangedBB & newEntry.pieceBB;
    Bitboard newAddedBB   = newChangedBB & pos.pieces();

#if defined(USE_AVX512ICL)
    PSQFeatureSet::write_indices(oldEntry.pieces, previousPieces, oldRemovedBB, oldAddedBB,
                                 perspective, oldKsq, oldRemove, oldAdd);
    PSQFeatureSet::write_indices(newEntry.pieces, currentPieces, newRemovedBB, newAddedBB,
                                 perspective, newKsq, newRemove, newAdd);
#else
    while (oldRemovedBB)
    {
        Square sq = pop_lsb(oldRemovedBB);
        oldRemove.push_back(
          PSQFeatureSet::make_index(perspective, sq, oldEntry.pieces[sq], oldKsq));
    }
    while (oldAddedBB)
    {
        Square sq = pop_lsb(oldAddedBB);
        oldAdd.push_back(PSQFeatureSet::make_index(perspective, sq, previousPieces[sq], oldKsq));
    }
    while (newRemovedBB)
    {
        Square sq = pop_lsb(newRemovedBB);
        newRemove.push_back(
          PSQFeatureSet::make_index(perspective, sq, newEntry.pieces[sq], newKsq));
    }
    while (newAddedBB)
    {
        Square sq = pop_lsb(newAddedBB);
        newAdd.push_back(PSQFeatureSet::make_index(perspective, sq, currentPieces[sq], newKsq));
    }
#endif

    ThreatFeatureSet::IndexList thrRemoved, thrAdded;  // also contain pp indices
    const auto*                 threatPpBase = &featureTransformer.threatAndPpWeights[0];
    IndexType                   pfStride     = FeatureTransformer::OutputDimensions;
    ThreatFeatureSet::append_changed_indices(perspective, newKsq, target.dirtyThreats, thrRemoved,
                                             thrAdded, threatPpBase, pfStride);
    PairFeatureSet::append_changed_indices(perspective, newKsq, target.dirtyPawnPairs, thrRemoved,
                                           thrAdded, threatPpBase, pfStride);

    const auto& fromAcc = computed.accumulation[perspective];
    auto&       toAcc   = target.accumulation[perspective];

    const auto& fromPsqtAcc = computed.psqtAccumulation[perspective];
    auto&       toPsqtAcc   = target.psqtAccumulation[perspective];

#ifdef VECTOR
    vec_t      acc[Tiling::NumRegs];
    psqt_vec_t psqt[Tiling::NumPsqtRegs];

    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        const usize tileOff      = j * Tiling::TileHeight;
        auto*       fromTile     = reinterpret_cast<const vec_t*>(&fromAcc[tileOff]);
        auto*       oldEntryTile = reinterpret_cast<const vec_t*>(&oldEntry.accumulation[tileOff]);
        auto*       newEntryTile = reinterpret_cast<vec_t*>(&newEntry.accumulation[tileOff]);
        auto*       toTile       = reinterpret_cast<vec_t*>(&toAcc[tileOff]);

        for (IndexType k = 0; k < Tiling::NumRegs; ++k)
            acc[k] = newEntryTile[k];

        apply_psq_features<-1>(j, acc, newRemove, featureTransformer);
        apply_psq_features<+1>(j, acc, newAdd, featureTransformer);

        for (IndexType k = 0; k < Tiling::NumRegs; ++k)
        {
            vec_store(&newEntryTile[k], acc[k]);
            // adding the old accumulator adds (most of) the threats and pp weights that we need
            acc[k] = vec_add_16(acc[k], fromTile[k]);
            // But we have added a whole bunch of psq weights for the wrong king bucket which
            // we need to remove
            // first we remove the cached psq accumulation for the old king position...
            acc[k] = vec_sub_16(acc[k], oldEntryTile[k]);
        }

        // ... then we adjust
        apply_psq_features<+1>(j, acc, oldRemove, featureTransformer);
        apply_psq_features<-1>(j, acc, oldAdd, featureTransformer);

        apply_threat_features<-1>(j, acc, thrRemoved, featureTransformer);
        apply_threat_features<+1>(j, acc, thrAdded, featureTransformer);

        for (IndexType k = 0; k < Tiling::NumRegs; k++)
            vec_store(&toTile[k], acc[k]);
    }

    for (IndexType j = 0; j < PSQTBuckets / Tiling::PsqtTileHeight; ++j)
    {
        const usize psqtTileOff  = j * Tiling::PsqtTileHeight;
        auto*       fromTilePsqt = reinterpret_cast<const psqt_vec_t*>(&fromPsqtAcc[psqtTileOff]);
        auto*       oldEntryTilePsqt =
          reinterpret_cast<const psqt_vec_t*>(&oldEntry.psqtAccumulation[psqtTileOff]);
        auto* newEntryTilePsqt =
          reinterpret_cast<psqt_vec_t*>(&newEntry.psqtAccumulation[psqtTileOff]);
        auto* toTilePsqt = reinterpret_cast<psqt_vec_t*>(&toPsqtAcc[psqtTileOff]);

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            psqt[k] = newEntryTilePsqt[k];

        apply_psqt<-1>(j, psqt, newRemove, featureTransformer.psqtWeights.data());
        apply_psqt<+1>(j, psqt, newAdd, featureTransformer.psqtWeights.data());

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
        {
            vec_store_psqt(&newEntryTilePsqt[k], psqt[k]);
            psqt[k] = vec_add_psqt_32(psqt[k], fromTilePsqt[k]);
            psqt[k] = vec_sub_psqt_32(psqt[k], oldEntryTilePsqt[k]);
        }

        apply_psqt<+1>(j, psqt, oldRemove, featureTransformer.psqtWeights.data());
        apply_psqt<-1>(j, psqt, oldAdd, featureTransformer.psqtWeights.data());

        apply_psqt<-1>(j, psqt, thrRemoved, featureTransformer.threatAndPpPsqtWeights.data());
        apply_psqt<+1>(j, psqt, thrAdded, featureTransformer.threatAndPpPsqtWeights.data());

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            vec_store_psqt(&toTilePsqt[k], psqt[k]);
    }

#else
    for (const auto index : newRemove)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            newEntry.accumulation[j] -= featureTransformer.weights[offset + j];

        for (usize k = 0; k < PSQTBuckets; ++k)
            newEntry.psqtAccumulation[k] -= featureTransformer.psqtWeights[index * PSQTBuckets + k];
    }
    for (const auto index : newAdd)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            newEntry.accumulation[j] += featureTransformer.weights[offset + j];

        for (usize k = 0; k < PSQTBuckets; ++k)
            newEntry.psqtAccumulation[k] += featureTransformer.psqtWeights[index * PSQTBuckets + k];
    }

    toAcc     = newEntry.accumulation;
    toPsqtAcc = newEntry.psqtAccumulation;

    for (IndexType j = 0; j < Dimensions; ++j)
    {
        toAcc[j] += fromAcc[j];
        toAcc[j] -= oldEntry.accumulation[j];
    }
    for (usize k = 0; k < PSQTBuckets; ++k)
    {
        toPsqtAcc[k] += fromPsqtAcc[k];
        toPsqtAcc[k] -= oldEntry.psqtAccumulation[k];
    }

    for (const auto index : oldRemove)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            toAcc[j] += featureTransformer.weights[offset + j];

        for (usize k = 0; k < PSQTBuckets; ++k)
            toPsqtAcc[k] += featureTransformer.psqtWeights[index * PSQTBuckets + k];
    }
    for (const auto index : oldAdd)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            toAcc[j] -= featureTransformer.weights[offset + j];

        for (usize k = 0; k < PSQTBuckets; ++k)
            toPsqtAcc[k] -= featureTransformer.psqtWeights[index * PSQTBuckets + k];
    }

    for (const auto index : thrRemoved)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            toAcc[j] -= featureTransformer.threatAndPpWeights[offset + j];
        for (usize k = 0; k < PSQTBuckets; ++k)
            toPsqtAcc[k] -= featureTransformer.threatAndPpPsqtWeights[index * PSQTBuckets + k];
    }
    for (const auto index : thrAdded)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            toAcc[j] += featureTransformer.threatAndPpWeights[offset + j];
        for (usize k = 0; k < PSQTBuckets; ++k)
            toPsqtAcc[k] += featureTransformer.threatAndPpPsqtWeights[index * PSQTBuckets + k];
    }

#endif

    newEntry.pieces  = currentPieces;
    newEntry.pieceBB = pos.pieces();

    target.computed[perspective] = true;
}

// HalfKA data comes from the Finny table entry, while the threats are built
// from the active threat features
void update_accumulator_refresh_cache(Color                     perspective,
                                      const FeatureTransformer& featureTransformer,
                                      const Position&           pos,
                                      AccumulatorState&         accumulator,
                                      AccumulatorCaches&        cache) {

    const Square             ksq   = pos.square<KING>(perspective);
    auto&                    entry = cache[ksq][perspective];
    PSQFeatureSet::IndexList removed, added;

    const Bitboard changedBB = get_changed_pieces(entry.pieces, pos.piece_array());
    Bitboard       removedBB = changedBB & entry.pieceBB;
    Bitboard       addedBB   = changedBB & pos.pieces();

#if defined(USE_AVX512ICL)
    PSQFeatureSet::write_indices(entry.pieces, pos.piece_array(), removedBB, addedBB, perspective,
                                 ksq, removed, added);
#else
    while (removedBB)
    {
        Square sq = pop_lsb(removedBB);
        removed.push_back(PSQFeatureSet::make_index(perspective, sq, entry.pieces[sq], ksq));
    }
    while (addedBB)
    {
        Square sq = pop_lsb(addedBB);
        added.push_back(PSQFeatureSet::make_index(perspective, sq, pos.piece_on(sq), ksq));
    }
#endif

    entry.pieceBB = pos.pieces();
    entry.pieces  = pos.piece_array();

    ThreatFeatureSet::IndexList active;
    ThreatFeatureSet::append_active_indices(perspective, pos, active);
    PairFeatureSet::append_active_indices(perspective, pos, active);

    accumulator.computed[perspective] = true;

#ifdef VECTOR
    vec_t      acc[Tiling::NumRegs];
    psqt_vec_t psqt[Tiling::NumPsqtRegs];

    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        const usize tileOff = j * Tiling::TileHeight;
        auto* accTile   = reinterpret_cast<vec_t*>(&accumulator.accumulation[perspective][tileOff]);
        auto* entryTile = reinterpret_cast<vec_t*>(&entry.accumulation[tileOff]);

        for (IndexType k = 0; k < Tiling::NumRegs; ++k)
            acc[k] = entryTile[k];

        apply_psq_features<-1>(j, acc, removed, featureTransformer);
        apply_psq_features<+1>(j, acc, added, featureTransformer);

        for (IndexType k = 0; k < Tiling::NumRegs; k++)
            vec_store(&entryTile[k], acc[k]);

        apply_threat_features<+1>(j, acc, active, featureTransformer);

        for (IndexType k = 0; k < Tiling::NumRegs; k++)
            vec_store(&accTile[k], acc[k]);
    }

    for (IndexType j = 0; j < PSQTBuckets / Tiling::PsqtTileHeight; ++j)
    {
        const usize psqtTileOff = j * Tiling::PsqtTileHeight;
        auto*       accTilePsqt =
          reinterpret_cast<psqt_vec_t*>(&accumulator.psqtAccumulation[perspective][psqtTileOff]);
        auto* entryTilePsqt = reinterpret_cast<psqt_vec_t*>(&entry.psqtAccumulation[psqtTileOff]);

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            psqt[k] = entryTilePsqt[k];

        apply_psqt<-1>(j, psqt, removed, featureTransformer.psqtWeights.data());
        apply_psqt<+1>(j, psqt, added, featureTransformer.psqtWeights.data());

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            vec_store_psqt(&entryTilePsqt[k], psqt[k]);

        apply_psqt<+1>(j, psqt, active, featureTransformer.threatAndPpPsqtWeights.data());

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            vec_store_psqt(&accTilePsqt[k], psqt[k]);
    }

#elif defined(USE_RVV)

    const auto* weights           = &featureTransformer.weights[0];
    const auto* threatWeights     = &featureTransformer.threatAndPpWeights[0];
    const auto* psqtWeights       = &featureTransformer.psqtWeights[0];
    const auto* threatPsqtWeights = &featureTransformer.threatAndPpPsqtWeights[0];

    usize tileOffset = 0;

    while (tileOffset < Dimensions)
    {
        usize vl = __riscv_vsetvl_e16m8(Dimensions - tileOffset);

        vint16m8_t accum = __riscv_vle16_v_i16m8(&entry.accumulation[tileOffset], vl);
        for (int i : removed)
            accum = __riscv_vsub_vv_i16m8(
              accum, __riscv_vle16_v_i16m8(&weights[i * Dimensions + tileOffset], vl), vl);
        for (int i : added)
            accum = __riscv_vadd_vv_i16m8(
              accum, __riscv_vle16_v_i16m8(&weights[i * Dimensions + tileOffset], vl), vl);

        __riscv_vse16_v_i16m8(&entry.accumulation[tileOffset], accum, vl);

        for (int i : active)
            accum = __riscv_vwadd_wv_i16m8(
              accum, __riscv_vle8_v_i8m4(&threatWeights[i * Dimensions + tileOffset], vl), vl);

        __riscv_vse16_v_i16m8(&accumulator.accumulation[perspective][tileOffset], accum, vl);

        tileOffset += vl;
    }

    tileOffset = 0;

    while (tileOffset < PSQTBuckets)
    {
        usize vl = __riscv_vsetvl_e32m1(PSQTBuckets - tileOffset);

        vint32m1_t accum = __riscv_vle32_v_i32m1(&entry.psqtAccumulation[tileOffset], vl);
        for (int i : removed)
            accum = __riscv_vsub_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&psqtWeights[i * PSQTBuckets + tileOffset], vl), vl);
        for (int i : added)
            accum = __riscv_vadd_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&psqtWeights[i * PSQTBuckets + tileOffset], vl), vl);

        __riscv_vse32_v_i32m1(&entry.psqtAccumulation[tileOffset], accum, vl);

        for (int i : active)
            accum = __riscv_vadd_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&threatPsqtWeights[i * PSQTBuckets + tileOffset], vl),
              vl);

        __riscv_vse32_v_i32m1(&accumulator.psqtAccumulation[perspective][tileOffset], accum, vl);

        tileOffset += vl;
    }

#else

    for (const auto index : removed)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            entry.accumulation[j] -= featureTransformer.weights[offset + j];

        for (usize k = 0; k < PSQTBuckets; ++k)
            entry.psqtAccumulation[k] -= featureTransformer.psqtWeights[index * PSQTBuckets + k];
    }
    for (const auto index : added)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            entry.accumulation[j] += featureTransformer.weights[offset + j];

        for (usize k = 0; k < PSQTBuckets; ++k)
            entry.psqtAccumulation[k] += featureTransformer.psqtWeights[index * PSQTBuckets + k];
    }

    // The accumulator of the refresh entry has been updated.
    // Now copy its content to the actual accumulator we were refreshing.
    accumulator.accumulation[perspective]     = entry.accumulation;
    accumulator.psqtAccumulation[perspective] = entry.psqtAccumulation;

    for (const auto index : active)
    {
        const IndexType offset = Dimensions * index;

        for (IndexType j = 0; j < Dimensions; ++j)
            accumulator.accumulation[perspective][j] +=
              featureTransformer.threatAndPpWeights[offset + j];

        for (usize k = 0; k < PSQTBuckets; ++k)
            accumulator.psqtAccumulation[perspective][k] +=
              featureTransformer.threatAndPpPsqtWeights[index * PSQTBuckets + k];
    }

#endif
}

}

}
