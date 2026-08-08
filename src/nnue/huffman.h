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

// Canonical Huffman compression for NNUE weight arrays

#ifndef NNUE_HUFFMAN_H_INCLUDED
#define NNUE_HUFFMAN_H_INCLUDED

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

#include "nnue_common.h"

namespace Stockfish::Eval::NNUE {

// A compressed array is stored as:
//   magic string, i32 min, i32 max, u32 byteCount,
//   (max - min + 1) code lengths (one byte each, 0 marks an unused symbol),
//   byteCount bytes of MSB-first bitstream.
// Only the code lengths are stored: the canonical code is rebuilt from them.
constexpr const char  HuffmanMagicString[]   = "COMPRESSED_HUFFMAN";
constexpr const usize HuffmanMagicStringSize = sizeof(HuffmanMagicString) - 1;

// A decode table entry packs the code length above the symbol, so this bounds
// the value range an array may span
constexpr u32 HuffmanSymbolBits = 24;
constexpr u32 HuffmanMaxAlphabet = 1u << HuffmanSymbolBits;

// Largest the decoder can use: a refill leaves at least 48 bits buffered,
// exactly three codes long. Also caps an array at 2^16 distinct values, which
// is what the Kraft inequality allows at this code length.
constexpr u32 HuffmanMaxLength = 16;
constexpr u32 HuffmanTableSize = 1u << HuffmanMaxLength;

// Code length produced before length limiting is applied
constexpr u32 HuffmanMaxRawLength = 63;

constexpr usize HuffmanBufferSize = 8192;

// Long arrays are coded as independent blocks of about this many symbols, each
// with its own table, since the statistics drift along the larger ones. Both
// sides derive the split from the element count alone, so it cannot desync.
constexpr usize HuffmanBlockSymbols = 1 << 17;

// Symbols in the block starting at done, of an array of count elements
inline usize huffman_block_size(usize count, usize done) {
    const usize base = count / std::max<usize>(1, count / HuffmanBlockSymbols);
    return count - done < 2 * base ? count - done : base;
}

// Reverses the byte order of a 64 bit value, compiled to a single bswap
inline u64 huffman_byte_swap(u64 v) {
    v = ((v & 0x00ff00ff00ff00ffull) << 8) | ((v >> 8) & 0x00ff00ff00ff00ffull);
    v = ((v & 0x0000ffff0000ffffull) << 16) | ((v >> 16) & 0x0000ffff0000ffffull);
    return (v << 32) | (v >> 32);
}

// Reads a MSB-first bitstream from a byte-limited region of a stream
class HuffmanBitReader {
   public:
    HuffmanBitReader(std::istream& is, u32 byteCount) :
        stream(is),
        bytesLeft(byteCount) { }

    // Enough for three codes of the maximum length
    void refill() {
        if (accBits >= 48)
            return;

        // Append as many whole bytes as fit, with a single unaligned load
        if (end - pos >= 8)
        {
            u64 chunk;
            std::memcpy(&chunk, buf.data() + pos, 8);
            if (IsLittleEndian)
                chunk = huffman_byte_swap(chunk);

            // Keeps the accumulator below 64 bits, so neither shift reaches 64
            const u32 bits = (63 - accBits) & ~7u;

            acc = (acc << bits) | (chunk >> (64 - bits));
            accBits += bits;
            pos += bits >> 3;
            return;
        }

        while (accBits <= 56)
        {
            acc = (acc << 8) | u64(next_byte());
            accBits += 8;
        }
    }

    u32 peek(u32 bits) const { return u32((acc >> (accBits - bits)) & ((u32(1) << bits) - 1)); }

    void consume(u32 length) { accBits -= length; }

   private:
    u8 next_byte() {
        if (pos == end)
        {
            stream.read(reinterpret_cast<char*>(buf.data()),
                        std::min(usize(bytesLeft), HuffmanBufferSize));
            end = u32(stream.gcount());
            pos = 0;
            bytesLeft -= end;

            if (end == 0)
                return 0;  // padding past the end of the bitstream
        }
        return buf[pos++];
    }

    std::istream&                     stream;
    u32                               bytesLeft;
    std::array<u8, HuffmanBufferSize> buf;
    u32                               pos = 0, end = 0;
    u64                               acc     = 0;
    u32                               accBits = 0;
};

// Writes a MSB-first bitstream
class HuffmanBitWriter {
   public:
    explicit HuffmanBitWriter(std::ostream& os) :
        stream(os) { }

    void write(u32 code, u32 length) {
        acc = (acc << length) | u64(code);
        accBits += length;
        while (accBits >= 8)
        {
            accBits -= 8;
            put(u8((acc >> accBits) & 0xff));
        }
    }

    void flush() {
        if (accBits > 0)
        {
            put(u8((acc << (8 - accBits)) & 0xff));
            accBits = 0;
        }
        flush_buffer();
    }

   private:
    void put(u8 byte) {
        buf[pos++] = byte;
        if (pos == HuffmanBufferSize)
            flush_buffer();
    }

    void flush_buffer() {
        if (pos > 0)
        {
            stream.write(reinterpret_cast<const char*>(buf.data()), pos);
            pos = 0;
        }
    }

    std::ostream&                     stream;
    std::array<u8, HuffmanBufferSize> buf;
    usize                             pos     = 0;
    u64                               acc     = 0;
    u32                               accBits = 0;
};

// Builds canonical code lengths for the given symbol frequencies, limited to
// HuffmanMaxLength bits. Symbols with zero frequency get length 0.
inline void huffman_build_lengths(const std::vector<u64>& freq, std::vector<u8>& lengths) {

    const usize alphabet = freq.size();
    lengths.assign(alphabet, 0);

    // Most frequent first, so the limited lengths can be handed out in
    // frequency order further down
    std::vector<u32> used;
    for (u32 s = 0; s < alphabet; ++s)
        if (freq[s] != 0)
            used.push_back(s);

    assert(!used.empty());

    if (used.size() == 1)
    {
        lengths[used[0]] = 1;
        return;
    }

    std::stable_sort(used.begin(), used.end(), [&](u32 a, u32 b) { return freq[a] > freq[b]; });

    // Unconstrained code lengths come from the depths of a Huffman tree
    struct Node {
        u64 freq;
        i32 left, right;
    };

    std::vector<Node> nodes;
    nodes.reserve(2 * used.size());

    using Entry = std::pair<u64, i32>;  // (frequency, node index)
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;

    for (u32 s : used)
    {
        nodes.push_back({freq[s], -1, -1});
        queue.emplace(freq[s], i32(nodes.size()) - 1);
    }

    while (queue.size() > 1)
    {
        const auto a = queue.top();
        queue.pop();
        const auto b = queue.top();
        queue.pop();

        nodes.push_back({a.first + b.first, a.second, b.second});
        queue.emplace(a.first + b.first, i32(nodes.size()) - 1);
    }

    // Count how many codes end up at each depth
    std::vector<u32> counts(HuffmanMaxRawLength + 2, 0);

    std::vector<std::pair<i32, u32>> stack;  // (node index, depth)
    stack.emplace_back(queue.top().second, 0);

    while (!stack.empty())
    {
        const auto [index, depth] = stack.back();
        stack.pop_back();

        if (nodes[index].left < 0)
        {
            assert(depth >= 1 && depth <= HuffmanMaxRawLength);
            ++counts[depth];
        }
        else
        {
            stack.emplace_back(nodes[index].left, depth + 1);
            stack.emplace_back(nodes[index].right, depth + 1);
        }
    }

    // JPEG Annex K: moving a pair of codes up one level and pushing a shallower
    // code down keeps the Kraft sum at 1
    assert(used.size() <= HuffmanTableSize);

    for (u32 i = HuffmanMaxRawLength; i > HuffmanMaxLength; --i)
    {
        while (counts[i] > 0)
        {
            u32 j = i - 2;
            while (j > 0 && counts[j] == 0)
                --j;

            assert(counts[j] > 0);

            counts[i] -= 2;
            counts[i - 1] += 1;
            counts[j + 1] += 2;
            counts[j] -= 1;
        }
    }

    // Hand out the shortest codes to the most frequent symbols
    usize next = 0;
    for (u32 length = 1; length <= HuffmanMaxLength; ++length)
        for (u32 n = 0; n < counts[length]; ++n)
            lengths[used[next++]] = u8(length);

    assert(next == used.size());
}

// Assigns canonical codes to symbols from their code lengths
inline void huffman_build_codes(const std::vector<u8>& lengths, std::vector<u32>& codes) {

    std::array<u32, HuffmanMaxLength + 2> countOfLength{};
    for (u8 length : lengths)
        if (length != 0)
            ++countOfLength[length];

    std::array<u32, HuffmanMaxLength + 2> nextCode{};
    u32                                   code = 0;
    for (u32 length = 1; length <= HuffmanMaxLength; ++length)
    {
        code             = (code + countOfLength[length - 1]) << 1;
        nextCode[length] = code;
    }

    codes.assign(lengths.size(), 0);
    for (usize s = 0; s < lengths.size(); ++s)
        if (lengths[s] != 0)
            codes[s] = nextCode[lengths[s]]++;
}

template<typename IntType>
inline void write_huffman_block(std::ostream& stream, const IntType* values, usize count) {

    static_assert(std::is_signed_v<IntType>, "Not implemented for unsigned types");
    static_assert(sizeof(IntType) <= 4, "Not implemented for types larger than 32 bit");

    assert(count > 0);

    i64 lo = values[0], hi = values[0];
    for (usize i = 1; i < count; ++i)
    {
        lo = std::min(lo, i64(values[i]));
        hi = std::max(hi, i64(values[i]));
    }

    const usize alphabet = usize(hi - lo + 1);

    std::vector<u64> freq(alphabet, 0);
    for (usize i = 0; i < count; ++i)
        ++freq[usize(i64(values[i]) - lo)];

    std::vector<u8>  lengths;
    std::vector<u32> codes;
    huffman_build_lengths(freq, lengths);
    huffman_build_codes(lengths, codes);

    u64 bits = 0;
    for (usize s = 0; s < alphabet; ++s)
        bits += u64(lengths[s]) * freq[s];

    const u32 byteCount = u32((bits + 7) / 8);

    stream.write(HuffmanMagicString, HuffmanMagicStringSize);
    write_little_endian<i32>(stream, i32(lo));
    write_little_endian<i32>(stream, i32(hi));
    write_little_endian<u32>(stream, byteCount);
    stream.write(reinterpret_cast<const char*>(lengths.data()), alphabet);

    HuffmanBitWriter writer(stream);
    for (usize i = 0; i < count; ++i)
    {
        const usize s = usize(i64(values[i]) - lo);
        writer.write(codes[s], lengths[s]);
    }
    writer.flush();
}

// Write signed integers to a stream with canonical Huffman compression.
template<typename IntType>
inline void write_huffman(std::ostream& stream, const IntType* values, usize count) {
    for (usize done = 0; done < count;)
    {
        const usize n = huffman_block_size(count, done);
        write_huffman_block(stream, values + done, n);
        done += n;
    }
}

// The table is the one buffer worth reusing across the blocks of an array
template<typename IntType>
inline void
read_huffman_block(std::istream& stream, IntType* out, usize count, std::vector<u32>& table) {

    static_assert(std::is_signed_v<IntType>, "Not implemented for unsigned types");
    static_assert(sizeof(IntType) <= 4, "Not implemented for types larger than 32 bit");

    char magic[HuffmanMagicStringSize];
    stream.read(magic, HuffmanMagicStringSize);
    assert(std::strncmp(HuffmanMagicString, magic, HuffmanMagicStringSize) == 0);

    const i32 lo        = read_little_endian<i32>(stream);
    const i32 hi        = read_little_endian<i32>(stream);
    const u32 byteCount = read_little_endian<u32>(stream);

    const usize alphabet = usize(hi - lo + 1);
    assert(alphabet <= HuffmanMaxAlphabet);

    std::vector<u8> lengths(alphabet);
    stream.read(reinterpret_cast<char*>(lengths.data()), alphabet);

    // The table is indexed by the longest code, and the code is complete, so
    // every entry in use is overwritten and it needs no clearing
    u32 tableBits = 1;
    for (u8 length : lengths)
        tableBits = std::max(tableBits, u32(length));

    std::vector<u32> codes;
    huffman_build_codes(lengths, codes);

    for (usize s = 0; s < alphabet; ++s)
    {
        const u32 length = lengths[s];
        if (length == 0)
            continue;

        const u32 shift = tableBits - length;
        std::fill_n(table.begin() + (codes[s] << shift), usize(1) << shift,
                    (length << HuffmanSymbolBits) | u32(s));
    }

    HuffmanBitReader reader(stream, byteCount);

    const auto decode = [&](usize at) {
        const u32 entry = table[reader.peek(tableBits)];
        assert((entry >> HuffmanSymbolBits) != 0);

        out[at] = IntType(i64(entry & (HuffmanMaxAlphabet - 1)) + lo);
        reader.consume(entry >> HuffmanSymbolBits);
    };

    // One refill covers three codes
    usize i = 0;
    for (; i + 3 <= count; i += 3)
    {
        reader.refill();
        decode(i);
        decode(i + 1);
        decode(i + 2);
    }

    for (; i < count; ++i)
    {
        reader.refill();
        decode(i);
    }
}

// Read signed integers from a stream compressed with canonical Huffman coding.
template<typename IntType>
inline void read_huffman(std::istream& stream, IntType* out, usize count) {
    std::vector<u32> table(HuffmanTableSize);

    for (usize done = 0; done < count;)
    {
        const usize n = huffman_block_size(count, done);
        read_huffman_block(stream, out + done, n, table);
        done += n;
    }
}

}  // namespace Stockfish::Eval::NNUE

#endif  // #ifndef NNUE_HUFFMAN_H_INCLUDED
