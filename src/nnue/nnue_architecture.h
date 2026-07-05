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

// Input features and network structure used in NNUE evaluation function

#ifndef NNUE_ARCHITECTURE_H_INCLUDED
#define NNUE_ARCHITECTURE_H_INCLUDED

#include <cstdint>
#include <iosfwd>

#include "features/half_ka_v2_hm.h"
#include "features/full_threats.h"
#include "features/pp_3wide.h"
#include "layers/affine_transform.h"
#include "layers/affine_transform_sparse_input.h"
#include "layers/clipped_relu.h"
#include "layers/sqr_clipped_relu.h"
#include "nnue_common.h"
#include "nnz_helper.h"

namespace Stockfish::Eval::NNUE {

// Input features used in evaluation function
using ThreatFeatureSet = Features::FullThreats;
using PairFeatureSet   = Features::PP_3Wide;
using PSQFeatureSet    = Features::HalfKAv2_hm;

// Number of input feature dimensions after conversion
constexpr IndexType L1 = 1024;
constexpr int       L2 = 32;
constexpr int       L3 = 32;

constexpr IndexType PSQTBuckets = 8;
constexpr IndexType LayerStacks = 8;

// If vector instructions are enabled, we update and refresh the
// accumulator tile by tile such that each tile fits in the CPU's
// vector registers.
static_assert(PSQTBuckets % 8 == 0,
              "Per feature PSQT values cannot be processed at granularity lower than 8 at a time.");

struct NetworkArchitecture {
    static constexpr IndexType TransformedFeatureDimensions = L1;
    static constexpr int       FC_0_OUTPUTS                 = L2;
    static constexpr int       FC_1_OUTPUTS                 = L3;
#if defined(USE_AVX2_PAIR_ACTIVATIONS)
    static constexpr bool UsePairedActivations = true;
#else
    static constexpr bool UsePairedActivations = false;
#endif

    Layers::AffineTransformSparseInput<TransformedFeatureDimensions, FC_0_OUTPUTS>        fc_0;
    Layers::SqrClippedReLU<FC_0_OUTPUTS, WeightScaleBits + 1>                             ac_sqr_0;
    Layers::ClippedReLU<FC_0_OUTPUTS, WeightScaleBits + 1>                                ac_0;
    Layers::AffineTransform<FC_0_OUTPUTS * 2, FC_1_OUTPUTS, UsePairedActivations>         fc_1;
    Layers::SqrClippedReLU<FC_1_OUTPUTS, WeightScaleBits>                                 ac_sqr_1;
    Layers::ClippedReLU<FC_1_OUTPUTS, WeightScaleBits>                                    ac_1;
    Layers::AffineTransform<FC_0_OUTPUTS * 2 + FC_1_OUTPUTS * 2, 1, UsePairedActivations> fc_2;

    // Hash value embedded in the evaluation file
    static constexpr u32 get_hash_value() {
        // input slice hash
        u32 hashValue = 0xEC42E90Du;
        hashValue ^= TransformedFeatureDimensions * 2;

        hashValue = decltype(fc_0)::get_hash_value(hashValue);
        // TODO: consider including hash value of ac_sqr_0 in the overall hash value.
        // For now omitted on purpose because hash value is not written by trainer (yet)
        hashValue = decltype(ac_0)::get_hash_value(hashValue);
        hashValue = decltype(fc_1)::get_hash_value(hashValue);
        hashValue = decltype(ac_1)::get_hash_value(hashValue);
        hashValue = decltype(fc_2)::get_hash_value(hashValue);

        return hashValue;
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) {
        return fc_0.read_parameters(stream) && ac_0.read_parameters(stream)
            && fc_1.read_parameters(stream) && ac_1.read_parameters(stream)
            && fc_2.read_parameters(stream);
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) const {
        return fc_0.write_parameters(stream) && ac_0.write_parameters(stream)
            && fc_1.write_parameters(stream) && ac_1.write_parameters(stream)
            && fc_2.write_parameters(stream);
    }

    i32 propagate(const TransformedFeatureType* transformedFeatures,
                  const NNZInfo<L1>&            nnzInfo) const {
        struct alignas(CacheLineSize) Buffer {
            alignas(CacheLineSize) typename decltype(fc_0)::OutputBuffer fc_0_out;
            alignas(CacheLineSize) typename decltype(ac_sqr_0)::OutputType
              concat_buffer[ceil_to_multiple<IndexType>(FC_0_OUTPUTS * 2 + FC_1_OUTPUTS * 2, 32)];
            alignas(CacheLineSize) typename decltype(fc_1)::OutputBuffer fc_1_out;
            alignas(CacheLineSize) typename decltype(fc_2)::OutputBuffer fc_2_out;
        };

        Buffer buffer;

        fc_0.propagate(transformedFeatures, buffer.fc_0_out, nnzInfo);
#if defined(USE_AVX2_PAIR_ACTIVATIONS)
        ac_sqr_0.propagate_pair(buffer.fc_0_out, buffer.concat_buffer,
                                buffer.concat_buffer + FC_0_OUTPUTS);
#else
        ac_sqr_0.propagate(buffer.fc_0_out, buffer.concat_buffer);
        ac_0.propagate(buffer.fc_0_out, buffer.concat_buffer + FC_0_OUTPUTS);
#endif

        fc_1.propagate(buffer.concat_buffer, buffer.fc_1_out);
#if defined(USE_AVX2_PAIR_ACTIVATIONS)
        ac_sqr_1.propagate_pair(buffer.fc_1_out, buffer.concat_buffer + FC_0_OUTPUTS * 2,
                                buffer.concat_buffer + FC_0_OUTPUTS * 2 + FC_1_OUTPUTS);
#else
        ac_sqr_1.propagate(buffer.fc_1_out, buffer.concat_buffer + FC_0_OUTPUTS * 2);
        ac_1.propagate(buffer.fc_1_out, buffer.concat_buffer + FC_0_OUTPUTS * 2 + FC_1_OUTPUTS);
#endif

        fc_2.propagate(buffer.concat_buffer, buffer.fc_2_out);

        static_assert(FC_0_OUTPUTS >= 2);
        i32 fwdOut = buffer.fc_2_out[0];
        i32 skip_0 = buffer.fc_0_out[FC_0_OUTPUTS - 2] - buffer.fc_0_out[FC_0_OUTPUTS - 1];
        fwdOut += skip_0;

        // fwdOut is such that 1.0 is equal to HiddenOneVal*(1<<WeightScaleBits)*2 in
        // quantized form, but we want 1.0 to be equal to 600*OutputScale
        // to make overflow impossible we cast to int64_t
        constexpr i64 multiplier = 600 * OutputScale;
        constexpr i64 denominator =
          static_cast<i64>(HiddenOneVal) * static_cast<i64>(1U << WeightScaleBits) * 2;

        i32 outputValue = static_cast<i32>((static_cast<i64>(fwdOut) * multiplier) / denominator);
        return outputValue;
    }

    usize get_content_hash() const {
        usize h = 0;
        hash_combine(h, fc_0.get_content_hash());
        hash_combine(h, ac_sqr_0.get_content_hash());
        hash_combine(h, ac_0.get_content_hash());
        hash_combine(h, fc_1.get_content_hash());
        // hash_combine(h, ac_sqr_1.get_content_hash()); TODO
        hash_combine(h, ac_1.get_content_hash());
        hash_combine(h, fc_2.get_content_hash());
        hash_combine(h, get_hash_value());
        return h;
    }
};

}  // namespace Stockfish::Eval::NNUE

template<>
struct std::hash<Stockfish::Eval::NNUE::NetworkArchitecture> {
    Stockfish::usize
    operator()(const Stockfish::Eval::NNUE::NetworkArchitecture& arch) const noexcept {
        return arch.get_content_hash();
    }
};

#endif  // #ifndef NNUE_ARCHITECTURE_H_INCLUDED
