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
#include <sstream>
#include <iomanip>
#include <fstream>

#include "../evaluate.h"
#include "../position.h"
#include "../misc.h"
#include "../uci.h"
#include "../types.h"

#include "evaluate_nnue.h"

namespace Stockfish::Eval::NNUE {

  // Input feature converter
  LargePagePtr<FeatureTransformer> featureTransformer;

  // Evaluation function
  AlignedPtr<Network> network[LayerStacks];

  // Evaluation function file name
  std::string fileName;
  std::string netDescription;

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
  bool read_parameters(std::istream& stream, T& reference) {

    std::uint32_t header;
    header = read_little_endian<std::uint32_t>(stream);
    if (!stream || header != T::get_hash_value()) return false;
    return reference.read_parameters(stream);
  }

  // Write evaluation function parameters
  template <typename T>
  bool write_parameters(std::ostream& stream, const T& reference) {

    write_little_endian<std::uint32_t>(stream, T::get_hash_value());
    return reference.write_parameters(stream);
  }

  }  // namespace Detail

  // Initialize the evaluation function parameters
  void initialize() {

    Detail::initialize(featureTransformer);
    for (std::size_t i = 0; i < LayerStacks; ++i)
      Detail::initialize(network[i]);
  }

  // Read network header
  bool read_header(std::istream& stream, std::uint32_t* hashValue, std::string* desc)
  {
    std::uint32_t version, size;

    version     = read_little_endian<std::uint32_t>(stream);
    *hashValue  = read_little_endian<std::uint32_t>(stream);
    size        = read_little_endian<std::uint32_t>(stream);
    if (!stream || version != Version) return false;
    desc->resize(size);
    stream.read(&(*desc)[0], size);
    return !stream.fail();
  }

  // Write network header
  bool write_header(std::ostream& stream, std::uint32_t hashValue, const std::string& desc)
  {
    write_little_endian<std::uint32_t>(stream, Version);
    write_little_endian<std::uint32_t>(stream, hashValue);
    write_little_endian<std::uint32_t>(stream, desc.size());
    stream.write(&desc[0], desc.size());
    return !stream.fail();
  }

  // Read network parameters
  bool read_parameters(std::istream& stream) {

    std::uint32_t hashValue;
    if (!read_header(stream, &hashValue, &netDescription)) return false;
    if (hashValue != HashValue) return false;
    if (!Detail::read_parameters(stream, *featureTransformer)) return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
      if (!Detail::read_parameters(stream, *(network[i]))) return false;
    return stream && stream.peek() == std::ios::traits_type::eof();
  }

  // Write network parameters
  bool write_parameters(std::ostream& stream) {

    if (!write_header(stream, HashValue, netDescription)) return false;
    if (!Detail::write_parameters(stream, *featureTransformer)) return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
      if (!Detail::write_parameters(stream, *(network[i]))) return false;
    return (bool)stream;
  }

  // Evaluation function. Perform differential calculation.
  Value evaluate(const Position& pos, bool adjusted) {

    // We manually align the arrays on the stack because with gcc < 9.3
    // overaligning stack variables with alignas() doesn't work correctly.

    constexpr uint64_t alignment = CacheLineSize;
    int delta = 7;

#if defined(ALIGNAS_ON_STACK_VARIABLES_BROKEN)
    TransformedFeatureType transformedFeaturesUnaligned[
      FeatureTransformer::BufferSize + alignment / sizeof(TransformedFeatureType)];
    char bufferUnaligned[Network::BufferSize + alignment];

    auto* transformedFeatures = align_ptr_up<alignment>(&transformedFeaturesUnaligned[0]);
    auto* buffer = align_ptr_up<alignment>(&bufferUnaligned[0]);
#else
    alignas(alignment)
      TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];
    alignas(alignment) char buffer[Network::BufferSize];
#endif

    ASSERT_ALIGNED(transformedFeatures, alignment);
    ASSERT_ALIGNED(buffer, alignment);

    const std::size_t bucket = (pos.count<ALL_PIECES>() - 1) / 4;
    const auto psqt = featureTransformer->transform(pos, transformedFeatures, bucket);
    const auto positional = network[bucket]->propagate(transformedFeatures, buffer)[0];

    // Give more value to positional evaluation when material is balanced
    if (   adjusted
        && abs(pos.non_pawn_material(WHITE) - pos.non_pawn_material(BLACK)) <= RookValueMg - BishopValueMg)
      return  static_cast<Value>(((128 - delta) * psqt + (128 + delta) * positional) / 128 / OutputScale);
    else
      return static_cast<Value>((psqt + positional) / OutputScale);
  }

  struct NnueEvalTrace {
    static_assert(LayerStacks == PSQTBuckets);

    Value psqt[LayerStacks];
    Value positional[LayerStacks];
    std::size_t correctBucket;
  };

  static NnueEvalTrace trace_evaluate(const Position& pos) {

    // We manually align the arrays on the stack because with gcc < 9.3
    // overaligning stack variables with alignas() doesn't work correctly.

    constexpr uint64_t alignment = CacheLineSize;

#if defined(ALIGNAS_ON_STACK_VARIABLES_BROKEN)
    TransformedFeatureType transformedFeaturesUnaligned[
      FeatureTransformer::BufferSize + alignment / sizeof(TransformedFeatureType)];
    char bufferUnaligned[Network::BufferSize + alignment];

    auto* transformedFeatures = align_ptr_up<alignment>(&transformedFeaturesUnaligned[0]);
    auto* buffer = align_ptr_up<alignment>(&bufferUnaligned[0]);
#else
    alignas(alignment)
      TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];
    alignas(alignment) char buffer[Network::BufferSize];
#endif

    ASSERT_ALIGNED(transformedFeatures, alignment);
    ASSERT_ALIGNED(buffer, alignment);

    NnueEvalTrace t{};
    t.correctBucket = (pos.count<ALL_PIECES>() - 1) / 4;
    for (std::size_t bucket = 0; bucket < LayerStacks; ++bucket) {
      const auto psqt = featureTransformer->transform(pos, transformedFeatures, bucket);
      const auto output = network[bucket]->propagate(transformedFeatures, buffer);

      int materialist = psqt;
      int positional  = output[0];

      t.psqt[bucket] = static_cast<Value>( materialist / OutputScale );
      t.positional[bucket] = static_cast<Value>( positional / OutputScale );
    }

    return t;
  }

  static const std::string PieceToChar(" PNBRQK  pnbrqk");


  // format_cp_compact() converts a Value into (centi)pawns and writes it in a buffer.
  // The buffer must have capacity for at least 5 chars.
  static void format_cp_compact(Value v, char* buffer) {

    buffer[0] = (v < 0 ? '-' : v > 0 ? '+' : ' ');

    int cp = std::abs(100 * v / PawnValueEg);
    if (cp >= 10000)
    {
        buffer[1] = '0' + cp / 10000; cp %= 10000;
        buffer[2] = '0' + cp / 1000; cp %= 1000;
        buffer[3] = '0' + cp / 100; cp %= 100;
        buffer[4] = ' ';
    }
    else if (cp >= 1000)
    {
        buffer[1] = '0' + cp / 1000; cp %= 1000;
        buffer[2] = '0' + cp / 100; cp %= 100;
        buffer[3] = '.';
        buffer[4] = '0' + cp / 10;
    }
    else
    {
        buffer[1] = '0' + cp / 100; cp %= 100;
        buffer[2] = '.';
        buffer[3] = '0' + cp / 10; cp %= 10;
        buffer[4] = '0' + cp / 1;
    }
  }


  // format_cp_aligned_dot() converts a Value into (centi)pawns and writes it in a buffer,
  // always keeping two decimals. The buffer must have capacity for at least 7 chars.
  static void format_cp_aligned_dot(Value v, char* buffer) {

    buffer[0] = (v < 0 ? '-' : v > 0 ? '+' : ' ');

    double cp = 1.0 * std::abs(int(v)) / PawnValueEg;
    sprintf(&buffer[1], "%6.2f", cp);
  }


  // trace() returns a string with the value of each piece on a board,
  // and a table for (PSQT, Layers) values bucket by bucket.

  std::string trace(Position& pos) {

    std::stringstream ss;

    char board[3*8+1][8*8+2];
    std::memset(board, ' ', sizeof(board));
    for (int row = 0; row < 3*8+1; ++row)
      board[row][8*8+1] = '\0';

    // A lambda to output one box of the board
    auto writeSquare = [&board](File file, Rank rank, Piece pc, Value value) {

      const int x = ((int)file) * 8;
      const int y = (7 - (int)rank) * 3;
      for (int i = 1; i < 8; ++i)
         board[y][x+i] = board[y+3][x+i] = '-';
      for (int i = 1; i < 3; ++i)
         board[y+i][x] = board[y+i][x+8] = '|';
      board[y][x] = board[y][x+8] = board[y+3][x+8] = board[y+3][x] = '+';
      if (pc != NO_PIECE)
        board[y+1][x+4] = PieceToChar[pc];
      if (value != VALUE_NONE)
        format_cp_compact(value, &board[y+2][x+2]);
    };

    // We estimate the value of each piece by doing a differential evaluation from
    // the current base eval, simulating the removal of the piece from its square.
    Value base = evaluate(pos);
    base = pos.side_to_move() == WHITE ? base : -base;

    for (File f = FILE_A; f <= FILE_H; ++f)
      for (Rank r = RANK_1; r <= RANK_8; ++r)
      {
        Square sq = make_square(f, r);
        Piece pc = pos.piece_on(sq);
        Value v = VALUE_NONE;

        if (pc != NO_PIECE && type_of(pc) != KING)
        {
          auto st = pos.state();

          pos.remove_piece(sq);
          st->accumulator.computed[WHITE] = false;
          st->accumulator.computed[BLACK] = false;

          Value eval = evaluate(pos);
          eval = pos.side_to_move() == WHITE ? eval : -eval;
          v = base - eval;

          pos.put_piece(pc, sq);
          st->accumulator.computed[WHITE] = false;
          st->accumulator.computed[BLACK] = false;
        }

        writeSquare(f, r, pc, v);
      }

    ss << " NNUE derived piece values:\n";
    for (int row = 0; row < 3*8+1; ++row)
        ss << board[row] << '\n';
    ss << '\n';

    auto t = trace_evaluate(pos);

    ss << " NNUE network contributions "
       << (pos.side_to_move() == WHITE ? "(White to move)" : "(Black to move)") << std::endl
       << "+------------+------------+------------+------------+\n"
       << "|   Bucket   |  Material  | Positional |   Total    |\n"
       << "|            |   (PSQT)   |  (Layers)  |            |\n"
       << "+------------+------------+------------+------------+\n";

    for (std::size_t bucket = 0; bucket < LayerStacks; ++bucket)
    {
      char buffer[3][8];
      std::memset(buffer, '\0', sizeof(buffer));

      format_cp_aligned_dot(t.psqt[bucket], buffer[0]);
      format_cp_aligned_dot(t.positional[bucket], buffer[1]);
      format_cp_aligned_dot(t.psqt[bucket] + t.positional[bucket], buffer[2]);

      ss <<  "|  " << bucket    << "        "
         << " |  " << buffer[0] << "  "
         << " |  " << buffer[1] << "  "
         << " |  " << buffer[2] << "  "
         << " |";
      if (bucket == t.correctBucket)
          ss << " <-- this bucket is used";
      ss << '\n';
    }

    ss << "+------------+------------+------------+------------+\n";

    return ss.str();
  }


  // Load eval, from a file stream or a memory stream
  bool load_eval(std::string name, std::istream& stream) {

    initialize();
    fileName = name;
    return read_parameters(stream);
  }

  // Save eval, to a file stream or a memory stream
  bool save_eval(std::ostream& stream) {

    if (fileName.empty())
      return false;

    return write_parameters(stream);
  }

  /// Save eval, to a file given by its name
  bool save_eval(const std::optional<std::string>& filename) {

    std::string actualFilename;
    std::string msg;

    if (filename.has_value())
        actualFilename = filename.value();
    else
    {
        if (currentEvalFileName != EvalFileDefaultName)
        {
             msg = "Failed to export a net. A non-embedded net can only be saved if the filename is specified";

             sync_cout << msg << sync_endl;
             return false;
        }
        actualFilename = EvalFileDefaultName;
    }

    std::ofstream stream(actualFilename, std::ios_base::binary);
    bool saved = save_eval(stream);

    msg = saved ? "Network saved successfully to " + actualFilename
                : "Failed to export a net";

    sync_cout << msg << sync_endl;
    return saved;
  }


} // namespace Stockfish::Eval::NNUE
