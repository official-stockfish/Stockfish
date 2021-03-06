/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2021 The Stockfish developers (see AUTHORS file)

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

// Code for calculating NNUE evaluation function

#include <iostream>
#include <set>

#include "../evaluate.h"
#include "../position.h"
#include "../misc.h"
#include "../uci.h"
#include "../types.h"

#include "evaluate_nnue.h"

namespace Eval::NNUE {

  // Input feature converter
  LargePagePtr<FeatureTransformer> feature_transformer;

  // Evaluation function
  AlignedPtr<Network> network;

  // Evaluation function file name
  std::string fileName;

  namespace Detail {

  // Initialize the evaluation function parameters
  template <typename T>
  void Initialize(AlignedPtr<T>& pointer) {

    pointer.reset(reinterpret_cast<T*>(std_aligned_alloc(alignof(T), sizeof(T))));
    std::memset(pointer.get(), 0, sizeof(T));
  }

  template <typename T>
  void Initialize(LargePagePtr<T>& pointer) {

    static_assert(alignof(T) <= 4096, "aligned_large_pages_alloc() may fail for such a big alignment requirement of T");
    pointer.reset(reinterpret_cast<T*>(aligned_large_pages_alloc(sizeof(T))));
    std::memset(pointer.get(), 0, sizeof(T));
  }

  // Read evaluation function parameters
  template <typename T>
  bool ReadParameters(std::istream& stream, T& reference) {

    std::uint32_t header;
    header = read_little_endian<std::uint32_t>(stream);
    if (!stream || header != T::GetHashValue()) return false;
    return reference.ReadParameters(stream);
  }

  }  // namespace Detail

  // Initialize the evaluation function parameters
  void Initialize() {

    Detail::Initialize(feature_transformer);
    Detail::Initialize(network);
  }

  // Read network header
  bool ReadHeader(std::istream& stream, std::uint32_t* hash_value, std::string* architecture)
  {
    std::uint32_t version, size;

    version     = read_little_endian<std::uint32_t>(stream);
    *hash_value = read_little_endian<std::uint32_t>(stream);
    size        = read_little_endian<std::uint32_t>(stream);
    if (!stream || version != kVersion) return false;
    architecture->resize(size);
    stream.read(&(*architecture)[0], size);
    return !stream.fail();
  }

  // Read network parameters
  bool ReadParameters(std::istream& stream) {

    std::uint32_t hash_value;
    std::string architecture;
    if (!ReadHeader(stream, &hash_value, &architecture)) return false;
    if (hash_value != kHashValue) return false;
    if (!Detail::ReadParameters(stream, *feature_transformer)) return false;
    if (!Detail::ReadParameters(stream, *network)) return false;
    return stream && stream.peek() == std::ios::traits_type::eof();
  }

  // Evaluation function. Perform differential calculation.
  Value evaluate(const Position& pos) {

    // We manually align the arrays on the stack because with gcc < 9.3
    // overaligning stack variables with alignas() doesn't work correctly.

    constexpr uint64_t alignment = kCacheLineSize;

#if defined(ALIGNAS_ON_STACK_VARIABLES_BROKEN)
    TransformedFeatureType transformed_features_unaligned[
      FeatureTransformer::kBufferSize + alignment / sizeof(TransformedFeatureType)];
    char buffer_unaligned[Network::kBufferSize + alignment];

    auto* transformed_features = align_ptr_up<alignment>(&transformed_features_unaligned[0]);
    auto* buffer = align_ptr_up<alignment>(&buffer_unaligned[0]);
#else
    alignas(alignment)
      TransformedFeatureType transformed_features[FeatureTransformer::kBufferSize];
    alignas(alignment) char buffer[Network::kBufferSize];
#endif

    ASSERT_ALIGNED(transformed_features, alignment);
    ASSERT_ALIGNED(buffer, alignment);

    feature_transformer->Transform(pos, transformed_features);
    const auto output = network->Propagate(transformed_features, buffer);

    return static_cast<Value>(output[0] / FV_SCALE);
  }

  // Load eval, from a file stream or a memory stream
  bool load_eval(std::string name, std::istream& stream) {

    Initialize();
    fileName = name;
    return ReadParameters(stream);
  }

} // namespace Eval::NNUE


#include "../eigen3/Eigen/Dense"

int8_t* wP = nullptr;
int32_t* bP = nullptr;

int TuneS[10] = { 0 };
int TuneB[32] = { -1636, -1296, 1434, -1881, -1544, 669, -454, 3617, -458, 11188, 2007, 3096, -3710, 6217, 1033, 4988,
                  4811, -546, 8955, -2867, -2619, 6637, 14132, 2474, -7371, 2994, -3596, 5869, 53, -4729, -8574, -8788 };

constexpr int WI = 512, HI = 32;
typedef Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> Mtx;
//typedef Eigen::Matrix<double, HI, WI> Mtx;
Eigen::BDCSVD<typename Eigen::MatrixBase<Mtx>::PlainObject> BdcSVD;
const Eigen::BDCSVD<Mtx>* SVD = nullptr;
Mtx W, U, VT, SM;
double Sigma[32];


void initSVD()
{
    W.resize(HI, WI);
    for (int i = 0; i < HI; ++i)
        for (int j = 0; j < WI; ++j)
            W(i, j) = wP[i * WI + j];

    BdcSVD = W.bdcSvd(Eigen::ComputeFullU | Eigen::ComputeFullV);
    SVD = &BdcSVD.compute(W, Eigen::ComputeFullU | Eigen::ComputeFullV);

    U = SVD->matrixU();
    VT = SVD->matrixV().transpose();

    SM.resize(HI, WI);
    SM.setZero();
    auto S = SVD->singularValues();
    for (int i = 0; i < HI; ++i)
        SM(i, i) = Sigma[i] = S(i);
}

void updateW()
{
    if (!wP)
        return;

    // Tune the first 10 biggest singular values
    for (int i = 0; i < 10; ++i)
        if (TuneS[i] > 0)
            SM(i, i) = Sigma[i] * (100.0 + TuneS[i] * (9.0 + i) / 18.0) / 100.0;
        else
            SM(i, i) = Sigma[i] * 100.0 / (100.0 - TuneS[i] * (9.0 + i) / 18.0);

    // Reconstruct matrix
    Mtx WNew = U * SM * VT;
    //Mtx Delta = W - WNew;

    for (int i = 0; i < HI; ++i)
        for (int j = 0; j < WI; ++j)
        {
            double w = WNew(i, j);
            int iw = std::min(std::max((int)round(w), -127), 127);
            wP[i * WI + j] = iw;
        }

#if defined (USE_SSSE3)
    // Permute weights
    int8_t tmp[HI * WI];
    std::memcpy(tmp, wP, HI * WI);

    for (std::size_t i = 0; i < HI * WI; ++i)
        wP[
           (i / 4) % (WI / 4) * HI * 4 +
            i / WI * 4 +
            i % 4
          ] = tmp[i];
#endif
}

void updateB()
{
    if (!bP)
        return;

    for (int i = 0; i < 32; ++i)
        bP[i] = TuneB[i];
}


#if 1
auto rangeFuncW = [](int m) { return std::pair<int, int>(m - 40, m + 40); };
TUNE(SetRange(rangeFuncW), TuneS, updateW);

auto rangeFuncB = [](int m) { return std::pair<int, int>(m - 1500, m + 1500); };
TUNE(SetRange(rangeFuncB), TuneB, updateB);

UPDATE_ON_LAST();
#endif