/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2023 The Stockfish developers (see AUTHORS file)

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

// Definition of layer AffineTransformSparseInput of NNUE evaluation function

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
#define NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED

#include <iostream>
#include <algorithm>
#include <array>
#include <type_traits>
#include "../nnue_common.h"
#include "affine_transform.h"
#include "simd.h"

/*
  This file contains the definition for a fully connected layer (aka affine transform) with block sparse input.
*/

namespace Stockfish::Eval::NNUE::Layers {
#if defined(__GNUC__)  // GCC, Clang, ICC

  static inline IndexType lsb_(std::uint32_t b) {
    assert(b);
    return IndexType(__builtin_ctzl(b));
  }

#elif defined(_MSC_VER)  // MSVC

  static inline IndexType lsb_(std::uint32_t b) {
    assert(b);
    unsigned long idx;
    _BitScanForward(&idx, b);
    return (IndexType) idx;
  }

#else  // Compiler is neither GCC nor MSVC compatible

#error "Compiler not supported."

#endif


#if defined(USE_SSSE3)
  alignas(CacheLineSize) static inline const std::array<std::array<std::uint16_t, 8>, 256> lookup_indices = [](){
    std::array<std::array<std::uint16_t, 8>, 256> v{};
    for (int i = 0; i < 256; ++i)
    {
      int j = i;
      int k = 0;
      while(j)
      {
        const IndexType lsbIndex = lsb_(std::uint32_t(j));
        j &= j - 1;
        v[i][k] = lsbIndex;
        ++k;
      }
    }
    return v;
  }();
  alignas(CacheLineSize) static inline const std::array<unsigned, 256> lookup_count = [](){
    std::array<unsigned, 256> v;
    for (int i = 0; i < 256; ++i)
    {
      int j = i;
      int k = 0;
      while(j)
      {
        j &= j - 1;
        ++k;
      }
      v[i] = k;
    }
    return v;
  }();

  // Find indices of nonzero numbers in an int32_t array
  template<const IndexType InputDimensions>
  void find_nnz(const std::int32_t* input, std::uint16_t* out, IndexType& count_out) {
#if defined (USE_AVX512)
    using vec_t = __m512i;
    #define vec_nnz(a) _mm512_cmpgt_epi32_mask(a, _mm512_setzero_si512())
#elif defined (USE_AVX2)
    using vec_t = __m256i;
    #define vec_nnz(a) _mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpgt_epi32(a, _mm256_setzero_si256())))
#elif defined (USE_SSSE3)
    using vec_t = __m128i;
    #define vec_nnz(a) _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpgt_epi32(a, _mm_setzero_si128())))
#endif
    constexpr IndexType InputSimdWidth = sizeof(vec_t) / sizeof(std::int32_t);
    // Inputs are processed InputSimdWidth at a time and outputs are processed 8 at a time so we process in chunks of max(InputSimdWidth, 8)
    constexpr IndexType ChunkSize = std::max<IndexType>(InputSimdWidth, 8);
    constexpr IndexType NumChunks = InputDimensions / ChunkSize;
    constexpr IndexType InputsPerChunk = ChunkSize / InputSimdWidth;
    constexpr IndexType OutputsPerChunk = ChunkSize / 8;

    const auto inputVector = reinterpret_cast<const vec_t*>(input);
    IndexType count = 0;
    __m128i base = _mm_set1_epi16(0);
    __m128i increment = _mm_set1_epi16(8);
    for (IndexType i = 0; i < NumChunks; ++i)
    {
      // bitmask of nonzero values in this chunk
      unsigned nnz = 0;
      for (IndexType j = 0; j < InputsPerChunk; ++j)
      {
        const vec_t inputChunk = inputVector[i * InputsPerChunk + j];
        nnz |= (unsigned)vec_nnz(inputChunk) << (j * InputSimdWidth);
      }
      for (IndexType j = 0; j < OutputsPerChunk; ++j)
      {
        const auto lookup = (nnz >> (j * 8)) & 0xFF;
        const auto offsets = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&lookup_indices[lookup]));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + count), _mm_add_epi16(base, offsets));
        count += lookup_count[lookup];
        base = _mm_add_epi16(base, increment);
      }
    }
    count_out = count;
  }
# undef vec_nnz
#endif

  // Sparse input implementation
  template <IndexType InDims, IndexType OutDims>
  class AffineTransformSparseInput {
   public:
    // Input/output type
    // Input/output type
    using InputType = std::uint8_t;
    using OutputType = std::int32_t;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions = InDims;
    static constexpr IndexType OutputDimensions = OutDims;

    static_assert(OutputDimensions % 16 == 0, "Only implemented for OutputDimensions divisible by 16.");

    static constexpr IndexType PaddedInputDimensions =
      ceil_to_multiple<IndexType>(InputDimensions, MaxSimdWidth);
    static constexpr IndexType PaddedOutputDimensions =
      ceil_to_multiple<IndexType>(OutputDimensions, MaxSimdWidth);

#if defined (USE_SSSE3)
    static constexpr IndexType ChunkSize = 4;
#else
    static constexpr IndexType ChunkSize = 1;
#endif

    using OutputBuffer = OutputType[PaddedOutputDimensions];

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value(std::uint32_t prevHash) {
      std::uint32_t hashValue = 0xCC03DAE4u;
      hashValue += OutputDimensions;
      hashValue ^= prevHash >> 1;
      hashValue ^= prevHash << 31;
      return hashValue;
    }

    static IndexType get_weight_index_scrambled(IndexType i)
    {
      return
        (i / ChunkSize) % (PaddedInputDimensions / ChunkSize) * OutputDimensions * ChunkSize +
        i / PaddedInputDimensions * ChunkSize +
        i % ChunkSize;
    }

    static IndexType get_weight_index(IndexType i)
    {
#if defined (USE_SSSE3)
      return get_weight_index_scrambled(i);
#else
      return i;
#endif
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) {
      read_little_endian<BiasType>(stream, biases, OutputDimensions);
      for (IndexType i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
        weights[get_weight_index(i)] = read_little_endian<WeightType>(stream);

      return !stream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) const {
      write_little_endian<BiasType>(stream, biases, OutputDimensions);

      for (IndexType i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
        write_little_endian<WeightType>(stream, weights[get_weight_index(i)]);

      return !stream.fail();
    }
    // Forward propagation
    const OutputType* propagate(
        const InputType* input, OutputType* output) const {

#if defined (USE_SSSE3)
#if defined (USE_AVX512)
      using vec_t = __m512i;
      #define vec_setzero _mm512_setzero_si512
      #define vec_set_32 _mm512_set1_epi32
      #define vec_add_dpbusd_32 Simd::m512_add_dpbusd_epi32
#elif defined (USE_AVX2)
      using vec_t = __m256i;
      #define vec_setzero _mm256_setzero_si256
      #define vec_set_32 _mm256_set1_epi32
      #define vec_add_dpbusd_32 Simd::m256_add_dpbusd_epi32
#elif defined (USE_SSSE3)
      using vec_t = __m128i;
      #define vec_setzero _mm_setzero_si128
      #define vec_set_32 _mm_set1_epi32
      #define vec_add_dpbusd_32 Simd::m128_add_dpbusd_epi32
#endif
      static constexpr IndexType OutputSimdWidth = sizeof(vec_t) / sizeof(OutputType);

      constexpr IndexType NumChunks = ceil_to_multiple<IndexType>(InputDimensions, 8) / ChunkSize;
      constexpr IndexType NumRegs = OutputDimensions / OutputSimdWidth;
      std::uint16_t nnz[NumChunks];
      IndexType count;

      const auto input32 = reinterpret_cast<const std::int32_t*>(input);

      // Find indices of nonzero 32bit blocks
      find_nnz<NumChunks>(input32, nnz, count);

      const vec_t* biasvec = reinterpret_cast<const vec_t*>(biases);
      vec_t acc[NumRegs];
      for (IndexType k = 0; k < NumRegs; ++k)
        acc[k] = biasvec[k];

      for (IndexType j = 0; j < count; ++j)
      {
        const auto i = nnz[j];
        const vec_t in = vec_set_32(input32[i]);
        const auto col = reinterpret_cast<const vec_t*>(&weights[i * OutputDimensions * ChunkSize]);
        for (IndexType k = 0; k < NumRegs; ++k)
          vec_add_dpbusd_32(acc[k], in, col[k]);
      }

      vec_t* outptr = reinterpret_cast<vec_t*>(output);
      for (IndexType k = 0; k < NumRegs; ++k)
        outptr[k] = acc[k];
# undef vec_setzero
# undef vec_set_32
# undef vec_add_dpbusd_32
#else
      // Use dense implementation for the other architectures.
      affine_transform_non_ssse3<
        InputDimensions,
        PaddedInputDimensions,
        OutputDimensions>(output, weights, biases, input);
#endif

      return output;
    }

   private:
    using BiasType = OutputType;
    using WeightType = std::int8_t;

    alignas(CacheLineSize) BiasType biases[OutputDimensions];
    alignas(CacheLineSize) WeightType weights[OutputDimensions * PaddedInputDimensions];
  };

}  // namespace Stockfish::Eval::NNUE::Layers

#endif // #ifndef NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
