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

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <type_traits>
#include <vector>
#include <filesystem>
#include <random>
#include <sstream>

#define INCBIN_SILENCE_BITCODE_WARNING
#include "../incbin/incbin.h"

#include "../evaluate.h"
#include "../misc.h"
#include "../movegen.h"
#include "../position.h"
#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_common.h"
#include "nnue_misc.h"
#include "nnz_helper.h"

// Macro to embed the default efficiently updatable neural network (NNUE) file
// data in the engine binary (using incbin.h, by Dale Weiler).
// This macro invocation will declare the following three variables
//     const unsigned char        gEmbeddedNNUEData[];  // a pointer to the embedded data
//     const unsigned char *const gEmbeddedNNUEEnd;     // a marker to the end
//     const unsigned int         gEmbeddedNNUESize;    // the size of the embedded file
// Note that this does not work in Microsoft Visual Studio.
#if !defined(UNIVERSAL_BINARY) && !defined(_MSC_VER) && !defined(NNUE_EMBEDDING_OFF)
INCBIN(EmbeddedNNUE, EvalFileDefaultName);
#elif defined(UNIVERSAL_BINARY_MACOS_X86_SLICE)
// Determined at runtime, see universal/nnue_embed.cpp
extern const unsigned char* const gEmbeddedNNUEData;
extern const unsigned int         gEmbeddedNNUESize;
#elif defined(UNIVERSAL_BINARY)
extern const unsigned char gEmbeddedNNUEData[];
extern const unsigned int  gEmbeddedNNUESize;
#else
const unsigned char gEmbeddedNNUEData[1] = {0x0};
const unsigned int  gEmbeddedNNUESize    = 1;
#endif


namespace Stockfish::Eval::NNUE {

namespace fs = std::filesystem;

namespace Detail {

// Read evaluation function parameters
template<typename T>
bool read_parameters(std::istream& stream, T& reference) {

    u32 header;
    header = read_little_endian<u32>(stream);
    if (!stream || header != T::get_hash_value())
        return false;
    return reference.read_parameters(stream);
}

// Write evaluation function parameters
template<typename T>
bool write_parameters(std::ostream& stream, const T& reference) {

    write_little_endian<u32>(stream, T::get_hash_value());
    return reference.write_parameters(stream);
}

}  // namespace Detail

void Network::load(const fs::path& rootDirectory, fs::path evalfilePath, EvalFile& evalFile) {
#if defined(DEFAULT_NNUE_DIRECTORY)
    std::vector<fs::path> dirs = {fs::path{}, rootDirectory,
                                  fs::path(stringify(DEFAULT_NNUE_DIRECTORY))};
#else
    std::vector<fs::path> dirs = {fs::path{}, rootDirectory};
#endif

    if (evalfilePath.empty())
        evalfilePath = evalFile.defaultName;

    if (evalFile.current != evalfilePath && evalfilePath == evalFile.defaultName)
        load_internal(evalFile);

    for (const auto& directory : dirs)
    {
        if (evalFile.current != evalfilePath)
            load_external(directory, evalfilePath, evalFile);
    }
}

bool Network::save(const EvalFile& evalFile, const std::optional<fs::path>& filename) const {
    if (!evalFile.current.has_value())
    {
        sync_cout << "Failed to export a net. No network file is currently loaded. "
                     "Please load a network file first."
                  << sync_endl;
        return false;
    }

    if (!filename.has_value() && evalFile.current != evalFile.defaultName)
    {
        sync_cout << "Failed to export a net. A non-embedded net can only be "
                     "saved if the filename is specified"
                  << sync_endl;
        return false;
    }

    fs::path      actualFilename = filename.value_or(evalFile.defaultName);
    std::ofstream stream(actualFilename, std::ios_base::binary);

    bool saved = save(stream, evalFile.netDescription);

    sync_cout << (saved ? "Network saved successfully to " + actualFilename.string()
                        : "Failed to export a net")
              << sync_endl;

    return saved;
}

Value Network::evaluate(const Position&    pos,
                        AccumulatorStack&  accumulatorStack,
                        AccumulatorCaches& cache) const {

    constexpr u64 alignment = CacheLineSize;

    alignas(alignment) TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];

    ASSERT_ALIGNED(transformedFeatures, alignment);

    NNZInfo<L1> nnzInfo;

    const int   pc_bucket    = (pos.count<ALL_PIECES>() - 1) / 4;
    const Color stm          = pos.side_to_move();
    const int   queen_bucket = (pos.pieces(stm, QUEEN) ? 2 : 0) | (pos.pieces(~stm, QUEEN) ? 1 : 0);
    const int   bucket       = pc_bucket * 4 + queen_bucket;

    featureTransformer.transform(pos, accumulatorStack, cache, transformedFeatures, nnzInfo);
    const auto positional = network[bucket].propagate(transformedFeatures, nnzInfo);
    return static_cast<Value>(positional / OutputScale);
}


void Network::verify(const std::function<void(std::string_view)>& f,
                     const EvalFile&                              evalFile,
                     fs::path                                     evalfilePath) const {
    if (evalfilePath.empty())
        evalfilePath = evalFile.defaultName;

    if (evalFile.current != evalfilePath)
    {
        if (f)
        {
            std::string msg1 =
              "Network evaluation parameters compatible with the engine must be available.";
            std::string msg2 =
              "The network file " + evalfilePath.string() + " was not loaded successfully.";
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
        usize size = sizeof(featureTransformer) + sizeof(NetworkArchitecture) * LayerStacks;
        f("NNUE evaluation using " + evalfilePath.string() + " ("
          + std::to_string(size / (1024 * 1024)) + "MiB, ("
          + std::to_string(featureTransformer.InputDimensions) + ", "
          + std::to_string(network[0].TransformedFeatureDimensions) + ", "
          + std::to_string(network[0].FC_0_OUTPUTS) + ", " + std::to_string(network[0].FC_1_OUTPUTS)
          + ", 1))");
    }
}


NnueEvalTrace Network::trace_evaluate(const Position&    pos,
                                      AccumulatorStack&  accumulatorStack,
                                      AccumulatorCaches& cache) const {

    constexpr u64 alignment = CacheLineSize;

    alignas(alignment) TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];

    ASSERT_ALIGNED(transformedFeatures, alignment);

    const int   pc_bucket    = (pos.count<ALL_PIECES>() - 1) / 4;
    const Color stm          = pos.side_to_move();
    const int   queen_bucket = (pos.pieces(stm, QUEEN) ? 2 : 0) | (pos.pieces(~stm, QUEEN) ? 1 : 0);
    const int   bucket       = pc_bucket * 4 + queen_bucket;

    NnueEvalTrace t{};
    t.correctBucket = bucket;

    NNZInfo<L1> nnzInfo;
    featureTransformer.transform(pos, accumulatorStack, cache, transformedFeatures, nnzInfo);

    for (IndexType b = 0; b < LayerStacks; ++b)
    {
        const auto positional = network[b].propagate(transformedFeatures, nnzInfo);
        t.positional[b]       = static_cast<Value>(positional / OutputScale);
    }

    return t;
}


void Network::load_external(const fs::path& dir, const fs::path& evalfilePath, EvalFile& evalFile) {
    std::ifstream stream(dir / evalfilePath, std::ios::binary);
    auto          description = load(stream);

    if (description.has_value())
    {
        evalFile.current        = evalfilePath;
        evalFile.netDescription = description.value();
    }
}


void Network::load_internal(EvalFile& evalFile) {
    // C++ way to prepare a buffer for a memory stream
    class MemoryBuffer: public std::basic_streambuf<char> {
       public:
        MemoryBuffer(char* p, usize n) {
            setg(p, p, p + n);
            setp(p, p + n);
        }
    };

#ifdef UNIVERSAL_BINARY_MACOS_X86_SLICE
    if (gEmbeddedNNUEData == nullptr)  // failed embedded load
        return;
#endif

    MemoryBuffer buffer(const_cast<char*>(reinterpret_cast<const char*>(gEmbeddedNNUEData)),
                        usize(gEmbeddedNNUESize));

    std::istream stream(&buffer);
    auto         description = load(stream);

    if (description.has_value())
    {
        evalFile.current        = evalFile.defaultName;
        evalFile.netDescription = description.value();
    }
}


void Network::initialize() { initialized = true; }


bool Network::save(std::ostream& stream, const std::string& netDescription) const {
    return write_parameters(stream, netDescription);
}


std::optional<std::string> Network::load(std::istream& stream) {
    initialize();
    std::string description;

    return read_parameters(stream, description) ? std::make_optional(description) : std::nullopt;
}


usize Network::get_content_hash() const {
    if (!initialized)
        return 0;

    usize h = 0;
    hash_combine(h, featureTransformer);
    for (auto&& layerstack : network)
        hash_combine(h, layerstack);
    return h;
}

// Read network header
bool Network::read_header(std::istream& stream, u32* hashValue, std::string* desc) const {
    u32 version, size;

    version    = read_little_endian<u32>(stream);
    *hashValue = read_little_endian<u32>(stream);
    size       = read_little_endian<u32>(stream);
    if (!stream || version != Version)
        return false;

    constexpr u32 Chunk = 4096;
    char          buf[Chunk];

    desc->clear();
    for (u32 remaining = size; remaining > 0;)
    {
        const u32 want = std::min(remaining, Chunk);
        stream.read(buf, want);
        const u32 got = u32(stream.gcount());
        desc->append(buf, got);
        if (got != want)
            return false;
        remaining -= want;
    }
    return !stream.fail();
}


// Write network header
bool Network::write_header(std::ostream& stream, u32 hashValue, const std::string& desc) const {
    write_little_endian<u32>(stream, Version);
    write_little_endian<u32>(stream, hashValue);
    write_little_endian<u32>(stream, u32(desc.size()));
    stream.write(&desc[0], desc.size());
    return !stream.fail();
}


bool Network::read_parameters(std::istream& stream, std::string& netDescription) {
    u32 hashValue;
    if (!read_header(stream, &hashValue, &netDescription))
        return false;
    if (hashValue != Network::hash)
        return false;
    if (!Detail::read_parameters(stream, featureTransformer))
        return false;
    for (usize i = 0; i < LayerStacks; ++i)
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
    for (usize i = 0; i < LayerStacks; ++i)
    {
        if (!Detail::write_parameters(stream, network[i]))
            return false;
    }
    return bool(stream);
}

std::string generate_random_network_stream(std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::ostringstream ss(std::ios::binary);

    // 1. Header
    write_little_endian<u32>(ss, Version);
    write_little_endian<u32>(ss, Network::hash);
    std::string desc = "Random Network";
    write_little_endian<u32>(ss, u32(desc.size()));
    ss.write(desc.data(), desc.size());

    // 2. Feature Transformer
    write_little_endian<u32>(ss, FeatureTransformer::get_hash_value());

    // biases (LEB128 of 1024 i16)
    std::vector<BiasType> biases(FeatureTransformer::OutputDimensions);
    std::uniform_int_distribution<int> dist_bias(-200, 200);
    for (auto& b : biases)
        b = static_cast<BiasType>(dist_bias(rng));
    write_leb_128(ss, biases.data(), biases.size());

    // threat and pair weights (raw i8)
    std::vector<ThreatWeightType> threatWeights(FeatureTransformer::ThreatWeightSize);
    std::uniform_int_distribution<int> dist_i8(-120, 120);
    for (auto& w : threatWeights)
        w = static_cast<ThreatWeightType>(dist_i8(rng));
    write_little_endian(ss, threatWeights.data(), threatWeights.size());

    std::vector<ThreatWeightType> pairWeights(FeatureTransformer::PairWeightSize);
    for (auto& w : pairWeights)
        w = static_cast<ThreatWeightType>(dist_i8(rng));
    write_little_endian(ss, pairWeights.data(), pairWeights.size());

    // weights (raw i8)
    std::vector<WeightType> weights(FeatureTransformer::OutputDimensions * FeatureTransformer::PsqDimensions);
    for (auto& w : weights)
        w = static_cast<WeightType>(dist_i8(rng));
    write_little_endian(ss, weights.data(), weights.size());

    // 3. 32 LayerStacks
    for (usize b = 0; b < LayerStacks; ++b)
    {
        write_little_endian<u32>(ss, NetworkArchitecture::get_hash_value());

        // fc_0: 32 i32 biases, 32 * 1024 i8 weights
        std::uniform_int_distribution<int> dist_fc0_bias(-1000, 1000);
        for (int i = 0; i < 32; ++i)
            write_little_endian<i32>(ss, dist_fc0_bias(rng));
        for (int i = 0; i < 32 * 1024; ++i)
            write_little_endian<i8>(ss, static_cast<i8>(dist_i8(rng)));

        // fc_1: 32 i32 biases, 32 * 64 i8 weights
        for (int i = 0; i < 32; ++i)
            write_little_endian<i32>(ss, dist_fc0_bias(rng));
        for (int i = 0; i < 32 * 64; ++i)
            write_little_endian<i8>(ss, static_cast<i8>(dist_i8(rng)));

        // fc_2: 1 i32 bias, 1 * 128 (padded) i8 weights
        write_little_endian<i32>(ss, dist_fc0_bias(rng));
        for (int i = 0; i < 1 * 128; ++i)
            write_little_endian<i8>(ss, static_cast<i8>(dist_i8(rng)));
    }

    return ss.str();
}

bool verify_nnue_roundtrip(std::ostream& os) {
    os << "Testing NNUE serialization round-trip..." << std::endl;

    // 1. Generate random net stream
    std::string bytes1 = generate_random_network_stream(12345);

    // 2. Load into net1
    auto net1 = std::make_unique<Network>();
    std::istringstream iss1(bytes1, std::ios::binary);
    auto desc1 = net1->load(iss1);
    if (!desc1.has_value())
    {
        os << "FAILED: net1 failed to load random stream!" << std::endl;
        return false;
    }

    // 3. Save net1 to ss2
    std::ostringstream ss2(std::ios::binary);
    if (!net1->save(ss2, *desc1))
    {
        os << "FAILED: net1 failed to save!" << std::endl;
        return false;
    }
    std::string bytes2 = ss2.str();
    if (bytes1 != bytes2)
    {
        os << "FAILED: bytes1 != bytes2 (length " << bytes1.size() << " vs " << bytes2.size() << ")" << std::endl;
        return false;
    }

    // 4. Load into net2 from ss2
    auto net2 = std::make_unique<Network>();
    std::istringstream iss2(bytes2, std::ios::binary);
    auto desc2 = net2->load(iss2);
    if (!desc2.has_value())
    {
        os << "FAILED: net2 failed to load!" << std::endl;
        return false;
    }

    // 5. Save net2 to ss3
    std::ostringstream ss3(std::ios::binary);
    if (!net2->save(ss3, *desc2))
    {
        os << "FAILED: net2 failed to save!" << std::endl;
        return false;
    }
    std::string bytes3 = ss3.str();
    if (bytes2 != bytes3)
    {
        os << "FAILED: bytes2 != bytes3 (length " << bytes2.size() << " vs " << bytes3.size() << ")" << std::endl;
        return false;
    }

    os << "Round-trip binary serialization check PASSED (" << bytes1.size() << " bytes)!" << std::endl;

    // 6. Test inference consistency: incremental vs full refresh on moves
    os << "Testing incremental vs refresh inference consistency..." << std::endl;

    const char* test_fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbqkb1r/pp1p1ppp/2p5/4P3/2B5/8/PPP1NnPP/RNBQK2R w KQkq - 0 6",
        "r1bqk2r/pppp1ppp/2n5/1B2p3/4n3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 5"
    };

    auto caches       = std::make_unique<AccumulatorCaches>(*net1);
    auto stack        = std::make_unique<AccumulatorStack>();
    auto fresh_caches = std::make_unique<AccumulatorCaches>(*net1);
    auto fresh_stack  = std::make_unique<AccumulatorStack>();

    for (const char* fen : test_fens)
    {
        Position pos;
        StateListPtr states(new std::deque<StateInfo>(1));
        pos.set(fen, false, &states->back());

        caches->clear(*net1);
        stack->reset();

        Value val_initial = net1->evaluate(pos, *stack, *caches);

        // Test moves
        for (const auto& move : MoveList<LEGAL>(pos))
        {
            states->emplace_back();
            Dirties& dirties = stack->push();
            pos.do_move(move, states->back(), pos.gives_check(move), dirties, nullptr, nullptr);

            // Incremental eval
            Value val_inc = net1->evaluate(pos, *stack, *caches);

            // Fresh refresh eval
            fresh_caches->clear(*net1);
            fresh_stack->reset();
            Value val_fresh = net1->evaluate(pos, *fresh_stack, *fresh_caches);

            if (val_inc != val_fresh)
            {
                os << "FAILED: Mismatch after move " << move.raw()
                   << " on FEN: " << fen
                   << ", incremental = " << val_inc << ", refresh = " << val_fresh << std::endl;
                return false;
            }

            pos.undo_move(move);
            stack->pop();
            states->pop_back();

            // After undo_move, check that eval is still consistent with initial
            Value val_after_undo = net1->evaluate(pos, *stack, *caches);
            if (val_after_undo != val_initial)
            {
                os << "FAILED: Mismatch after undo_move on FEN: " << fen
                   << ", after_undo = " << val_after_undo << ", initial = " << val_initial << std::endl;
                return false;
            }
        }
    }

    os << "Inference incremental vs refresh check PASSED!" << std::endl;
    return true;
}

}  // namespace Stockfish::Eval::NNUE
