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

#include "network.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <type_traits>
#include <vector>

#define INCBIN_SILENCE_BITCODE_WARNING
#include "../incbin/incbin.h"

#include "../evaluate.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_common.h"
#include "nnue_misc.h"

// Macro to embed the default efficiently updatable neural network (NNUE) file
// data in the engine binary (using incbin.h, by Dale Weiler).
// This macro invocation will declare the following three variables
//     const unsigned char        gEmbeddedNNUEData[];  // a pointer to the embedded data
//     const unsigned char *const gEmbeddedNNUEEnd;     // a marker to the end
//     const unsigned int         gEmbeddedNNUESize;    // the size of the embedded file
// Note that this does not work in Microsoft Visual Studio.
#if !defined(UNIVERSAL_BINARY) && !defined(_MSC_VER) && !defined(NNUE_EMBEDDING_OFF)
INCBIN(EmbeddedNNUE, EvalFileDefaultName);
#elif defined(UNIVERSAL_BINARY)
    // When building for the universal binary, use C++26 #embed with weak symbols so that a
    // separate, non-LTO nnue_embed.o (with strong symbols) can override them during the LTO link,
    // (INCBIN can't deduplicate.)
    #define WEAK_SYM __attribute__((weak))
extern const unsigned char gEmbeddedNNUEData[] WEAK_SYM = {
    #embed EvalFileDefaultName
};
extern const unsigned int gEmbeddedNNUESize WEAK_SYM = sizeof(gEmbeddedNNUEData);
#else
const unsigned char gEmbeddedNNUEData[1] = {0x0};
const unsigned int  gEmbeddedNNUESize    = 1;
#endif

namespace Stockfish::Eval::NNUE {


namespace Detail {

// Read evaluation function parameters
template<typename T>
bool read_parameters(std::istream& stream, T& reference) {

    std::uint32_t header;
    header = read_little_endian<std::uint32_t>(stream);
    if (!stream || header != T::get_hash_value())
        return false;
    return reference.read_parameters(stream);
}

// Write evaluation function parameters
template<typename T>
bool write_parameters(std::ostream& stream, const T& reference) {

    write_little_endian<std::uint32_t>(stream, T::get_hash_value());
    return reference.write_parameters(stream);
}

}  // namespace Detail

void Network::load(const std::string& rootDirectory, std::string evalfilePath) {
#if defined(DEFAULT_NNUE_DIRECTORY)
    std::vector<std::string> dirs = {"<internal>", "", rootDirectory,
                                     stringify(DEFAULT_NNUE_DIRECTORY)};
#else
    std::vector<std::string> dirs = {"<internal>", "", rootDirectory};
#endif

    if (evalfilePath.empty())
        evalfilePath = evalFile.defaultName;

    for (const auto& directory : dirs)
    {
        if (std::string(evalFile.current) != evalfilePath)
        {
            if (directory != "<internal>")
                load_user_net(directory, evalfilePath);
            else if (evalfilePath == std::string(evalFile.defaultName))
                load_internal();
        }
    }
}


bool Network::save(const std::optional<std::string>& filename) const {
    std::string actualFilename;
    std::string msg;

    if (filename.has_value())
        actualFilename = filename.value();
    else
    {
        if (std::string(evalFile.current) != std::string(evalFile.defaultName))
        {
            msg = "Failed to export a net. "
                  "A non-embedded net can only be saved if the filename is specified";

            sync_cout << msg << sync_endl;
            return false;
        }

        actualFilename = evalFile.defaultName;
    }

    std::ofstream stream(actualFilename, std::ios_base::binary);
    bool          saved = save(stream, evalFile.current, evalFile.netDescription);

    msg = saved ? "Network saved successfully to " + actualFilename : "Failed to export a net";

    sync_cout << msg << sync_endl;
    return saved;
}


NetworkOutput Network::evaluate(const Position&    pos,
                                AccumulatorStack&  accumulatorStack,
                                AccumulatorCaches& cache) const {

    constexpr uint64_t alignment = CacheLineSize;

    alignas(alignment) TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];

    ASSERT_ALIGNED(transformedFeatures, alignment);

    const int  bucket = (pos.count<ALL_PIECES>() - 1) / 4;
    const auto psqt =
      featureTransformer.transform(pos, accumulatorStack, cache, transformedFeatures, bucket);
    const auto positional = network[bucket].propagate(transformedFeatures);
    return {static_cast<Value>(psqt / OutputScale), static_cast<Value>(positional / OutputScale)};
}


void Network::verify(std::string                                  evalfilePath,
                     const std::function<void(std::string_view)>& f) const {
    if (evalfilePath.empty())
        evalfilePath = evalFile.defaultName;

    if (std::string(evalFile.current) != evalfilePath)
    {
        if (f)
        {
            std::string msg1 =
              "Network evaluation parameters compatible with the engine must be available.";
            std::string msg2 = "The network file " + evalfilePath + " was not loaded successfully.";
            std::string msg3 = "The UCI option EvalFile might need to specify the full path, "
                               "including the directory name, to the network file.";
            std::string msg4 = "The default net can be downloaded from: "
                               "https://tests.stockfishchess.org/api/nn/"
                             + std::string(evalFile.defaultName);
            std::string msg5 = "The engine will be terminated now.";

            std::string msg = "ERROR: " + msg1 + '\n' + "ERROR: " + msg2 + '\n' + "ERROR: " + msg3
                            + '\n' + "ERROR: " + msg4 + '\n' + "ERROR: " + msg5 + '\n';

            f(msg);
        }

        exit(EXIT_FAILURE);
    }

    if (f)
    {
        size_t size = sizeof(featureTransformer) + sizeof(NetworkArchitecture) * LayerStacks;
        f("NNUE evaluation using " + evalfilePath + " (" + std::to_string(size / (1024 * 1024))
          + "MiB, (" + std::to_string(featureTransformer.InputDimensions) + ", "
          + std::to_string(network[0].TransformedFeatureDimensions) + ", "
          + std::to_string(network[0].FC_0_OUTPUTS) + ", " + std::to_string(network[0].FC_1_OUTPUTS)
          + ", 1))");
    }
}


NnueEvalTrace Network::trace_evaluate(const Position&    pos,
                                      AccumulatorStack&  accumulatorStack,
                                      AccumulatorCaches& cache) const {

    constexpr uint64_t alignment = CacheLineSize;

    alignas(alignment) TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];

    ASSERT_ALIGNED(transformedFeatures, alignment);

    NnueEvalTrace t{};
    t.correctBucket = (pos.count<ALL_PIECES>() - 1) / 4;
    for (IndexType bucket = 0; bucket < LayerStacks; ++bucket)
    {
        const auto materialist =
          featureTransformer.transform(pos, accumulatorStack, cache, transformedFeatures, bucket);
        const auto positional = network[bucket].propagate(transformedFeatures);

        t.psqt[bucket]       = static_cast<Value>(materialist / OutputScale);
        t.positional[bucket] = static_cast<Value>(positional / OutputScale);
    }

    return t;
}


void Network::load_user_net(const std::string& dir, const std::string& evalfilePath) {
    std::ifstream stream(dir + evalfilePath, std::ios::binary);
    auto          description = load(stream);

    if (description.has_value())
    {
        evalFile.current        = evalfilePath;
        evalFile.netDescription = description.value();
    }
}


void Network::load_internal() {
    // C++ way to prepare a buffer for a memory stream
    class MemoryBuffer: public std::basic_streambuf<char> {
       public:
        MemoryBuffer(char* p, size_t n) {
            setg(p, p, p + n);
            setp(p, p + n);
        }
    };

    MemoryBuffer buffer(const_cast<char*>(reinterpret_cast<const char*>(gEmbeddedNNUEData)),
                        size_t(gEmbeddedNNUESize));

    std::istream stream(&buffer);
    auto         description = load(stream);

    if (description.has_value())
    {
        evalFile.current        = evalFile.defaultName;
        evalFile.netDescription = description.value();
    }
}


void Network::initialize() { initialized = true; }


bool Network::save(std::ostream&      stream,
                   const std::string& name,
                   const std::string& netDescription) const {
    if (name.empty() || name == "None")
        return false;

    return write_parameters(stream, netDescription);
}


std::optional<std::string> Network::load(std::istream& stream) {
    initialize();
    std::string description;

    return read_parameters(stream, description) ? std::make_optional(description) : std::nullopt;
}


std::size_t Network::get_content_hash() const {
    if (!initialized)
        return 0;

    std::size_t h = 0;
    hash_combine(h, featureTransformer);
    for (auto&& layerstack : network)
        hash_combine(h, layerstack);
    hash_combine(h, evalFile);
    return h;
}

// Read network header
bool Network::read_header(std::istream& stream, std::uint32_t* hashValue, std::string* desc) const {
    std::uint32_t version, size;

    version    = read_little_endian<std::uint32_t>(stream);
    *hashValue = read_little_endian<std::uint32_t>(stream);
    size       = read_little_endian<std::uint32_t>(stream);
    if (!stream || version != Version)
        return false;
    desc->resize(size);
    stream.read(&(*desc)[0], size);
    return !stream.fail();
}


// Write network header
bool Network::write_header(std::ostream&      stream,
                           std::uint32_t      hashValue,
                           const std::string& desc) const {
    write_little_endian<std::uint32_t>(stream, Version);
    write_little_endian<std::uint32_t>(stream, hashValue);
    write_little_endian<std::uint32_t>(stream, std::uint32_t(desc.size()));
    stream.write(&desc[0], desc.size());
    return !stream.fail();
}


bool Network::read_parameters(std::istream& stream, std::string& netDescription) {
    std::uint32_t hashValue;
    if (!read_header(stream, &hashValue, &netDescription))
        return false;
    if (hashValue != Network::hash)
        return false;
    if (!Detail::read_parameters(stream, featureTransformer))
        return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
    {
        if (!Detail::read_parameters(stream, network[i]))
            return false;
    }
    return stream && stream.peek() == std::ios::traits_type::eof();
}


bool Network::write_parameters(std::ostream& stream, const std::string& netDescription) const {
    if (!write_header(stream, Network::hash, netDescription))
        return false;
    if (!Detail::write_parameters(stream, featureTransformer))
        return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
    {
        if (!Detail::write_parameters(stream, network[i]))
            return false;
    }
    return bool(stream);
}

}  // namespace Stockfish::Eval::NNUE
