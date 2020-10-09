# Binpack

Binpack is a binary training data storage format designed to take advantage of position chains differing by a single move. Therefore it is very good at compactly storing data generated from real games (as opposed to random positions for example sourced from an opening book).

It is currently implemented through a single header library in `extra/nnue_data_binpack_format.h`.

Below follows a rough description of the format in a BNF-like notation.

```
[[nodiscard]] std::uint16_t signedToUnsigned(std::int16_t a) {
    std::uint16_t r;
    std::memcpy(&r, &a, sizeof(std::uint16_t));
    if (r & 0x8000) r ^= 0x7FFF; // flip value bits if negative
    r = (r << 1) | (r >> 15); // store sign bit at bit 0
    return r;
}

file := <block>*
block := BINP<chain>*
chain := <stem><movetext>
stem := <pos><move><score><ply_and_result><rule50> (32 bytes)
pos := https://github.com/Sopel97/nnue_data_compress/blob/master/src/chess/Position.h#L1166 (24 bytes)
move := https://github.com/Sopel97/nnue_data_compress/blob/master/src/chess/Chess.h#L1044 (2 bytes)
score := signedToUnsigned(score) (2 bytes, big endian)
ply_and_result := ply bitwise_or (signedToUnsigned(result) << 14) (2 bytes, big endian)
rule50 := rule_50_counter (2 bytes, big endian)
    // this is a small defect from old version,
    I didn't want to break backwards compatibility. Effectively means that there's
    one byte left for something else in the future because rule50 always fits in one byte.

movetext := <count><move_and_score>*
count := number of plies in the movetext (2 bytes, big endian). Can be 0.
move_and_score := <encoded_move><encoded_score> (~2 bytes)
encoded_move := oof this one is complicated to explain.
    https://github.com/Sopel97/nnue_data_compress/blob/master/src/compress_file.cpp#L827.
    https://github.com/Sopel97/chess_pos_db/blob/master/docs/bcgn/variable_length.md

encoded_score := https://en.wikipedia.org/wiki/Variable-width_encoding
    with block size of 4 bits + 1 bit for extension bit.
    Encoded value is signedToUnsigned(-prev_score - current_score)
    (scores are always seen from the perspective of side to move in <pos>, that's why the '-' before prev_score)
```