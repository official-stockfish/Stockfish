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

#include "evaluate_nnue.h"

#include "position.h"
#include "misc.h"
#include "uci.h"
#include "types.h"

#include <iostream>
#include <string>
#include <fstream>
#include <set>

#include "../evaluate.h"
#include "../position.h"
#include "../misc.h"
#include "../uci.h"
#include "../types.h"

#include "evaluate_nnue.h"

namespace Stockfish::Eval::NNUE {

  // Input feature converter
  LargePagePtr<FeatureTransformer> feature_transformer;

  // Evaluation function
  AlignedPtr<Network> network;

  // Evaluation function file name
  std::string fileName;

  // Saved evaluation function file name
  std::string savedfileName = "nn.bin";

  // Get a string that represents the structure of the evaluation function
  std::string get_architecture_string() {
    return "Features=" + FeatureTransformer::get_structure_string() +
        ",Network=" + Network::get_structure_string();
  }

  std::string get_layers_info() {
    return
        FeatureTransformer::get_layers_info()
        + '\n' + Network::get_layers_info();
  }

  UseNNUEMode useNNUE;
  std::string eval_file_loaded = "None";

  namespace Detail {

  // Initialize the evaluation function parameters
  template <typename T>
  void initialize(AlignedPtr<T>& pointer) {

    pointer.reset(reinterpret_cast<T*>(std_aligned_alloc(alignof(T), sizeof(T))));
    std::memset(pointer.get(), 0, sizeof(T));
  }

  template <typename T>
  void initialize(LargePagePtr<T>& pointer) {

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

  // write evaluation function parameters
  template <typename T>
  bool WriteParameters(std::ostream& stream, const AlignedPtr<T>& pointer) {
    constexpr std::uint32_t header = T::GetHashValue();

    stream.write(reinterpret_cast<const char*>(&header), sizeof(header));

    return pointer->WriteParameters(stream);
  }

  template <typename T>
  bool WriteParameters(std::ostream& stream, const LargePagePtr<T>& pointer) {
    constexpr std::uint32_t header = T::GetHashValue();

    stream.write(reinterpret_cast<const char*>(&header), sizeof(header));

    return pointer->WriteParameters(stream);
  }
  }  // namespace Detail

  // Initialize the evaluation function parameters
  void initialize() {

    Detail::initialize(feature_transformer);
    Detail::initialize(network);
  }

  // Read network header
  bool read_header(std::istream& stream, std::uint32_t* hash_value, std::string* architecture)
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

  // write the header
  bool write_header(std::ostream& stream,
    std::uint32_t hash_value, const std::string& architecture) {

    stream.write(reinterpret_cast<const char*>(&kVersion), sizeof(kVersion));
    stream.write(reinterpret_cast<const char*>(&hash_value), sizeof(hash_value));

    const std::uint32_t size = static_cast<std::uint32_t>(architecture.size());

    stream.write(reinterpret_cast<const char*>(&size), sizeof(size));
    stream.write(architecture.data(), size);

    return !stream.fail();
  }

  // Read network parameters
  bool ReadParameters(std::istream& stream) {

    std::uint32_t hash_value;
    std::string architecture;
    if (!read_header(stream, &hash_value, &architecture)) return false;
    if (hash_value != kHashValue) return false;
    if (!Detail::ReadParameters(stream, *feature_transformer)) return false;
    if (!Detail::ReadParameters(stream, *network)) return false;
    return stream && stream.peek() == std::ios::traits_type::eof();
  }

  // write evaluation function parameters
  bool WriteParameters(std::ostream& stream) {

    if (!write_header(stream, kHashValue, get_architecture_string()))
        return false;

    if (!Detail::WriteParameters(stream, feature_transformer))
        return false;

    if (!Detail::WriteParameters(stream, network))
        return false;

    return !stream.fail();
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

    initialize();
    fileName = name;
    return ReadParameters(stream);
}

static UseNNUEMode nnue_mode_from_option(const UCI::Option& mode)
{
  if (mode == "false")
    return UseNNUEMode::False;
  else if (mode == "true")
     return UseNNUEMode::True;
  else if (mode == "pure")
    return UseNNUEMode::Pure;

  return UseNNUEMode::False;
}

void init() {

  useNNUE = nnue_mode_from_option(Options["Use NNUE"]);

  if (Options["SkipLoadingEval"])
  {
    eval_file_loaded.clear();
    return;
  }

  if (useNNUE == UseNNUEMode::False)
  {
    // Keep the eval file loaded. Useful for mixed bench.
    return;
  }

  std::string eval_file = std::string(Options["EvalFile"]);

#if defined(DEFAULT_NNUE_DIRECTORY)
#define stringify2(x) #x
#define stringify(x) stringify2(x)
  std::vector<std::string> dirs = { "" , CommandLine::binaryDirectory , stringify(DEFAULT_NNUE_DIRECTORY) };
#else
  std::vector<std::string> dirs = { "" , CommandLine::binaryDirectory };
#endif

  for (std::string directory : dirs)
  {
    if (eval_file_loaded != eval_file)
    {
      std::ifstream stream(directory + eval_file, std::ios::binary);
      if (load_eval(eval_file, stream))
      {
        sync_cout << "info string Loaded eval file " << directory + eval_file << sync_endl;
        eval_file_loaded = eval_file;
      }
      else
      {
        sync_cout << "info string ERROR: failed to load eval file " << directory + eval_file << sync_endl;
        eval_file_loaded.clear();
      }
    }
  }

} // namespace Stockfish::Eval::NNUE
