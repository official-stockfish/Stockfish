/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2010 Marco Costalba, Joona Kiiski, Tord Romstad

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


////
//// Includes
////

#include <iostream>

#include "bitboard.h"
#include "bitcount.h"
#include "direction.h"


#if defined(IS_64BIT)

const uint64_t BMult[64] = {
  0x440049104032280ULL, 0x1021023c82008040ULL, 0x404040082000048ULL,
  0x48c4440084048090ULL, 0x2801104026490000ULL, 0x4100880442040800ULL,
  0x181011002e06040ULL, 0x9101004104200e00ULL, 0x1240848848310401ULL,
  0x2000142828050024ULL, 0x1004024d5000ULL, 0x102044400800200ULL,
  0x8108108820112000ULL, 0xa880818210c00046ULL, 0x4008008801082000ULL,
  0x60882404049400ULL, 0x104402004240810ULL, 0xa002084250200ULL,
  0x100b0880801100ULL, 0x4080201220101ULL, 0x44008080a00000ULL,
  0x202200842000ULL, 0x5006004882d00808ULL, 0x200045080802ULL,
  0x86100020200601ULL, 0xa802080a20112c02ULL, 0x80411218080900ULL,
  0x200a0880080a0ULL, 0x9a01010000104000ULL, 0x28008003100080ULL,
  0x211021004480417ULL, 0x401004188220806ULL, 0x825051400c2006ULL,
  0x140c0210943000ULL, 0x242800300080ULL, 0xc2208120080200ULL,
  0x2430008200002200ULL, 0x1010100112008040ULL, 0x8141050100020842ULL,
  0x822081014405ULL, 0x800c049e40400804ULL, 0x4a0404028a000820ULL,
  0x22060201041200ULL, 0x360904200840801ULL, 0x881a08208800400ULL,
  0x60202c00400420ULL, 0x1204440086061400ULL, 0x8184042804040ULL,
  0x64040315300400ULL, 0xc01008801090a00ULL, 0x808010401140c00ULL,
  0x4004830c2020040ULL, 0x80005002020054ULL, 0x40000c14481a0490ULL,
  0x10500101042048ULL, 0x1010100200424000ULL, 0x640901901040ULL,
  0xa0201014840ULL, 0x840082aa011002ULL, 0x10010840084240aULL,
  0x420400810420608ULL, 0x8d40230408102100ULL, 0x4a00200612222409ULL,
  0xa08520292120600ULL
};

const uint64_t RMult[64] = {
  0xa8002c000108020ULL, 0x4440200140003000ULL, 0x8080200010011880ULL,
  0x380180080141000ULL, 0x1a00060008211044ULL, 0x410001000a0c0008ULL,
  0x9500060004008100ULL, 0x100024284a20700ULL, 0x802140008000ULL,
  0x80c01002a00840ULL, 0x402004282011020ULL, 0x9862000820420050ULL,
  0x1001448011100ULL, 0x6432800200800400ULL, 0x40100010002000cULL,
  0x2800d0010c080ULL, 0x90c0008000803042ULL, 0x4010004000200041ULL,
  0x3010010200040ULL, 0xa40828028001000ULL, 0x123010008000430ULL,
  0x24008004020080ULL, 0x60040001104802ULL, 0x582200028400d1ULL,
  0x4000802080044000ULL, 0x408208200420308ULL, 0x610038080102000ULL,
  0x3601000900100020ULL, 0x80080040180ULL, 0xc2020080040080ULL,
  0x80084400100102ULL, 0x4022408200014401ULL, 0x40052040800082ULL,
  0xb08200280804000ULL, 0x8a80a008801000ULL, 0x4000480080801000ULL,
  0x911808800801401ULL, 0x822a003002001894ULL, 0x401068091400108aULL,
  0x4a10a00004cULL, 0x2000800640008024ULL, 0x1486408102020020ULL,
  0x100a000d50041ULL, 0x810050020b0020ULL, 0x204000800808004ULL,
  0x20048100a000cULL, 0x112000831020004ULL, 0x9000040810002ULL,
  0x440490200208200ULL, 0x8910401000200040ULL, 0x6404200050008480ULL,
  0x4b824a2010010100ULL, 0x4080801810c0080ULL, 0x400802a0080ULL,
  0x8224080110026400ULL, 0x40002c4104088200ULL, 0x1002100104a0282ULL,
  0x1208400811048021ULL, 0x3201014a40d02001ULL, 0x5100019200501ULL,
  0x101000208001005ULL, 0x2008450080702ULL, 0x1002080301d00cULL,
  0x410201ce5c030092ULL
};

const int BShift[64] = {
  58, 59, 59, 59, 59, 59, 59, 58, 59, 59, 59, 59, 59, 59, 59, 59,
  59, 59, 57, 57, 57, 57, 59, 59, 59, 59, 57, 55, 55, 57, 59, 59,
  59, 59, 57, 55, 55, 57, 59, 59, 59, 59, 57, 57, 57, 57, 59, 59,
  59, 59, 59, 59, 59, 59, 59, 59, 58, 59, 59, 59, 59, 59, 59, 58
};

const int RShift[64] = {
  52, 53, 53, 53, 53, 53, 53, 52, 53, 54, 54, 54, 54, 54, 54, 53,
  53, 54, 54, 54, 54, 54, 54, 53, 53, 54, 54, 54, 54, 54, 54, 53,
  53, 54, 54, 54, 54, 54, 54, 53, 53, 54, 54, 54, 54, 54, 54, 53,
  53, 54, 54, 54, 54, 54, 54, 53, 52, 53, 53, 53, 53, 53, 53, 52
};

#else // if !defined(IS_64BIT)

const uint64_t BMult[64] = {
  0x54142844c6a22981ULL, 0x710358a6ea25c19eULL, 0x704f746d63a4a8dcULL,
  0xbfed1a0b80f838c5ULL, 0x90561d5631e62110ULL, 0x2804260376e60944ULL,
  0x84a656409aa76871ULL, 0xf0267f64c28b6197ULL, 0x70764ebb762f0585ULL,
  0x92aa09e0cfe161deULL, 0x41ee1f6bb266f60eULL, 0xddcbf04f6039c444ULL,
  0x5a3fab7bac0d988aULL, 0xd3727877fa4eaa03ULL, 0xd988402d868ddaaeULL,
  0x812b291afa075c7cULL, 0x94faf987b685a932ULL, 0x3ed867d8470d08dbULL,
  0x92517660b8901de8ULL, 0x2d97e43e058814b4ULL, 0x880a10c220b25582ULL,
  0xc7c6520d1f1a0477ULL, 0xdbfc7fbcd7656aa6ULL, 0x78b1b9bfb1a2b84fULL,
  0x2f20037f112a0bc1ULL, 0x657171ea2269a916ULL, 0xc08302b07142210eULL,
  0x880a4403064080bULL, 0x3602420842208c00ULL, 0x852800dc7e0b6602ULL,
  0x595a3fbbaa0f03b2ULL, 0x9f01411558159d5eULL, 0x2b4a4a5f88b394f2ULL,
  0x4afcbffc292dd03aULL, 0x4a4094a3b3f10522ULL, 0xb06f00b491f30048ULL,
  0xd5b3820280d77004ULL, 0x8b2e01e7c8e57a75ULL, 0x2d342794e886c2e6ULL,
  0xc302c410cde21461ULL, 0x111f426f1379c274ULL, 0xe0569220abb31588ULL,
  0x5026d3064d453324ULL, 0xe2076040c343cd8aULL, 0x93efd1e1738021eeULL,
  0xb680804bed143132ULL, 0x44e361b21986944cULL, 0x44c60170ef5c598cULL,
  0xf4da475c195c9c94ULL, 0xa3afbb5f72060b1dULL, 0xbc75f410e41c4ffcULL,
  0xb51c099390520922ULL, 0x902c011f8f8ec368ULL, 0x950b56b3d6f5490aULL,
  0x3909e0635bf202d0ULL, 0x5744f90206ec10ccULL, 0xdc59fd76317abbc1ULL,
  0x881c7c67fcbfc4f6ULL, 0x47ca41e7e440d423ULL, 0xeb0c88112048d004ULL,
  0x51c60e04359aef1aULL, 0x1aa1fe0e957a5554ULL, 0xdd9448db4f5e3104ULL,
  0xdc01f6dca4bebbdcULL,
};

const uint64_t RMult[64] = {
  0xd7445cdec88002c0ULL, 0xd0a505c1f2001722ULL, 0xe065d1c896002182ULL,
  0x9a8c41e75a000892ULL, 0x8900b10c89002aa8ULL, 0x9b28d1c1d60005a2ULL,
  0x15d6c88de002d9aULL, 0xb1dbfc802e8016a9ULL, 0x149a1042d9d60029ULL,
  0xb9c08050599e002fULL, 0x132208c3af300403ULL, 0xc1000ce2e9c50070ULL,
  0x9d9aa13c99020012ULL, 0xb6b078daf71e0046ULL, 0x9d880182fb6e002eULL,
  0x52889f467e850037ULL, 0xda6dc008d19a8480ULL, 0x468286034f902420ULL,
  0x7140ac09dc54c020ULL, 0xd76ffffa39548808ULL, 0xea901c4141500808ULL,
  0xc91004093f953a02ULL, 0x2882afa8f6bb402ULL, 0xaebe335692442c01ULL,
  0xe904a22079fb91eULL, 0x13a514851055f606ULL, 0x76c782018c8fe632ULL,
  0x1dc012a9d116da06ULL, 0x3c9e0037264fffa6ULL, 0x2036002853c6e4a2ULL,
  0xe3fe08500afb47d4ULL, 0xf38af25c86b025c2ULL, 0xc0800e2182cf9a40ULL,
  0x72002480d1f60673ULL, 0x2500200bae6e9b53ULL, 0xc60018c1eefca252ULL,
  0x600590473e3608aULL, 0x46002c4ab3fe51b2ULL, 0xa200011486bcc8d2ULL,
  0xb680078095784c63ULL, 0x2742002639bf11aeULL, 0xc7d60021a5bdb142ULL,
  0xc8c04016bb83d820ULL, 0xbd520028123b4842ULL, 0x9d1600344ac2a832ULL,
  0x6a808005631c8a05ULL, 0x604600a148d5389aULL, 0xe2e40103d40dea65ULL,
  0x945b5a0087c62a81ULL, 0x12dc200cd82d28eULL, 0x2431c600b5f9ef76ULL,
  0xfb142a006a9b314aULL, 0x6870e00a1c97d62ULL, 0x2a9db2004a2689a2ULL,
  0xd3594600caf5d1a2ULL, 0xee0e4900439344a7ULL, 0x89c4d266ca25007aULL,
  0x3e0013a2743f97e3ULL, 0x180e31a0431378aULL, 0x3a9e465a4d42a512ULL,
  0x98d0a11a0c0d9cc2ULL, 0x8e711c1aba19b01eULL, 0x8dcdc836dd201142ULL,
  0x5ac08a4735370479ULL,
};

const int BShift[64] = {
  26, 27, 27, 27, 27, 27, 27, 26, 27, 27, 27, 27, 27, 27, 27, 27,
  27, 27, 25, 25, 25, 25, 27, 27, 27, 27, 25, 23, 23, 25, 27, 27,
  27, 27, 25, 23, 23, 25, 27, 27, 27, 27, 25, 25, 25, 25, 27, 27,
  27, 27, 27, 27, 27, 27, 27, 27, 26, 27, 27, 27, 27, 27, 27, 26
};

const int RShift[64] = {
  20, 21, 21, 21, 21, 21, 21, 20, 21, 22, 22, 22, 22, 22, 22, 21,
  21, 22, 22, 22, 22, 22, 22, 21, 21, 22, 22, 22, 22, 22, 22, 21,
  21, 22, 22, 22, 22, 22, 22, 21, 21, 22, 22, 22, 22, 22, 22, 21,
  21, 22, 22, 22, 22, 22, 22, 21, 20, 21, 21, 21, 21, 21, 21, 20
};

#endif // defined(IS_64BIT)

const Bitboard SquaresByColorBB[2] = { BlackSquaresBB, WhiteSquaresBB };

const Bitboard FileBB[8] = {
  FileABB, FileBBB, FileCBB, FileDBB, FileEBB, FileFBB, FileGBB, FileHBB
};

const Bitboard NeighboringFilesBB[8] = {
  FileBBB, FileABB|FileCBB, FileBBB|FileDBB, FileCBB|FileEBB,
  FileDBB|FileFBB, FileEBB|FileGBB, FileFBB|FileHBB, FileGBB
};

const Bitboard ThisAndNeighboringFilesBB[8] = {
  FileABB|FileBBB, FileABB|FileBBB|FileCBB,
  FileBBB|FileCBB|FileDBB, FileCBB|FileDBB|FileEBB,
  FileDBB|FileEBB|FileFBB, FileEBB|FileFBB|FileGBB,
  FileFBB|FileGBB|FileHBB, FileGBB|FileHBB
};

const Bitboard RankBB[8] = {
  Rank1BB, Rank2BB, Rank3BB, Rank4BB, Rank5BB, Rank6BB, Rank7BB, Rank8BB
};

const Bitboard RelativeRankBB[2][8] = {
  { Rank1BB, Rank2BB, Rank3BB, Rank4BB, Rank5BB, Rank6BB, Rank7BB, Rank8BB },
  { Rank8BB, Rank7BB, Rank6BB, Rank5BB, Rank4BB, Rank3BB, Rank2BB, Rank1BB }
};

const Bitboard InFrontBB[2][8] = {
  { Rank2BB | Rank3BB | Rank4BB | Rank5BB | Rank6BB | Rank7BB | Rank8BB,
    Rank3BB | Rank4BB | Rank5BB | Rank6BB | Rank7BB | Rank8BB,
    Rank4BB | Rank5BB | Rank6BB | Rank7BB | Rank8BB,
    Rank5BB | Rank6BB | Rank7BB | Rank8BB,
    Rank6BB | Rank7BB | Rank8BB,
    Rank7BB | Rank8BB,
    Rank8BB,
    EmptyBoardBB
  },
  { EmptyBoardBB,
    Rank1BB,
    Rank2BB | Rank1BB,
    Rank3BB | Rank2BB | Rank1BB,
    Rank4BB | Rank3BB | Rank2BB | Rank1BB,
    Rank5BB | Rank4BB | Rank3BB | Rank2BB | Rank1BB,
    Rank6BB | Rank5BB | Rank4BB | Rank3BB | Rank2BB | Rank1BB,
    Rank7BB | Rank6BB | Rank5BB | Rank4BB | Rank3BB | Rank2BB | Rank1BB
  }
};

Bitboard RMask[64];
int RAttackIndex[64];
Bitboard RAttacks[0x19000];

Bitboard BMask[64];
int BAttackIndex[64];
Bitboard BAttacks[0x1480];

Bitboard SetMaskBB[65];
Bitboard ClearMaskBB[65];

Bitboard StepAttackBB[16][64];
Bitboard RayBB[64][8];
Bitboard BetweenBB[64][64];

Bitboard PassedPawnMask[2][64];
Bitboard OutpostMask[2][64];

Bitboard BishopPseudoAttacks[64];
Bitboard RookPseudoAttacks[64];
Bitboard QueenPseudoAttacks[64];

uint8_t BitCount8Bit[256];


////
//// Local definitions
////

namespace {

  void init_masks();
  void init_ray_bitboards();
  void init_attacks();
  void init_between_bitboards();
  Bitboard sliding_attacks(int sq, Bitboard block, int dirs, int deltas[][2],
                           int fmin, int fmax, int rmin, int rmax);
  Bitboard index_to_bitboard(int index, Bitboard mask);
  void init_sliding_attacks(Bitboard attacks[],
                            int attackIndex[], Bitboard mask[],
                            const int shift[2], const Bitboard mult[],
                            int deltas[][2]);
  void init_pseudo_attacks();
}


////
//// Functions
////

/// print_bitboard() prints a bitboard in an easily readable format to the
/// standard output.  This is sometimes useful for debugging.

void print_bitboard(Bitboard b) {
  for(Rank r = RANK_8; r >= RANK_1; r--) {
    std::cout << "+---+---+---+---+---+---+---+---+" << std::endl;
    for(File f = FILE_A; f <= FILE_H; f++)
      std::cout << "| " << (bit_is_set(b, make_square(f, r))? 'X' : ' ') << ' ';
    std::cout << "|" << std::endl;
  }
  std::cout << "+---+---+---+---+---+---+---+---+" << std::endl;
}


/// init_bitboards() initializes various bitboard arrays.  It is called during
/// program initialization.

void init_bitboards() {
  int rookDeltas[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};
  int bishopDeltas[4][2] = {{1,1},{-1,1},{1,-1},{-1,-1}};
  init_masks();
  init_ray_bitboards();
  init_attacks();
  init_between_bitboards();
  init_sliding_attacks(RAttacks, RAttackIndex, RMask, RShift, RMult, rookDeltas);
  init_sliding_attacks(BAttacks, BAttackIndex, BMask, BShift, BMult, bishopDeltas);
  init_pseudo_attacks();
}


/// first_1() finds the least significant nonzero bit in a nonzero bitboard.
/// pop_1st_bit() finds and clears the least significant nonzero bit in a
/// nonzero bitboard.

#if defined(IS_64BIT) && !defined(USE_BSFQ)

static CACHE_LINE_ALIGNMENT
const int BitTable[64] = {
  0, 1, 2, 7, 3, 13, 8, 19, 4, 25, 14, 28, 9, 34, 20, 40, 5, 17, 26, 38, 15,
  46, 29, 48, 10, 31, 35, 54, 21, 50, 41, 57, 63, 6, 12, 18, 24, 27, 33, 39,
  16, 37, 45, 47, 30, 53, 49, 56, 62, 11, 23, 32, 36, 44, 52, 55, 61, 22, 43,
  51, 60, 42, 59, 58
};

Square first_1(Bitboard b) {
  return Square(BitTable[((b & -b) * 0x218a392cd3d5dbfULL) >> 58]);
}

Square pop_1st_bit(Bitboard* b) {
  Bitboard bb = *b;
  *b &= (*b - 1);
  return Square(BitTable[((bb & -bb) * 0x218a392cd3d5dbfULL) >> 58]);
}

#elif !defined(USE_BSFQ)

static CACHE_LINE_ALIGNMENT
const int BitTable[64] = {
  63, 30, 3, 32, 25, 41, 22, 33, 15, 50, 42, 13, 11, 53, 19, 34, 61, 29, 2,
  51, 21, 43, 45, 10, 18, 47, 1, 54, 9, 57, 0, 35, 62, 31, 40, 4, 49, 5, 52,
  26, 60, 6, 23, 44, 46, 27, 56, 16, 7, 39, 48, 24, 59, 14, 12, 55, 38, 28,
  58, 20, 37, 17, 36, 8
};

Square first_1(Bitboard b) {
  b ^= (b - 1);
  uint32_t fold = int(b) ^ int(b >> 32);
  return Square(BitTable[(fold * 0x783a9b23) >> 26]);
}

// Use type-punning
union b_union {

    Bitboard b;
    struct {
#if defined (BIGENDIAN)
        uint32_t h;
        uint32_t l;
#else
        uint32_t l;
        uint32_t h;
#endif
    } dw;
};

Square pop_1st_bit(Bitboard* bb) {

   b_union u;
   Square ret;

   u.b = *bb;

   if (u.dw.l)
   {
       ret = Square(BitTable[((u.dw.l ^ (u.dw.l - 1)) * 0x783a9b23) >> 26]);
       u.dw.l &= (u.dw.l - 1);
       *bb = u.b;
       return ret;
   }
   ret = Square(BitTable[((~(u.dw.h ^ (u.dw.h - 1))) * 0x783a9b23) >> 26]);
   u.dw.h &= (u.dw.h - 1);
   *bb = u.b;
   return ret;
}

#endif

// Optimized bitScanReverse32() implementation by Pascal Georges. Note
// that first bit is 1, this allow to differentiate between 0 and 1.
static CACHE_LINE_ALIGNMENT
const char MsbTable[256] = {
  0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
};

int bitScanReverse32(uint32_t b)
{
   int result = 0;

   if (b > 0xFFFF)
   {
      b >>= 16;
      result += 16;
   }
   if (b > 0xFF)
   {
      b >>= 8;
      result += 8;
   }
   return result + MsbTable[b];
}

namespace {

  // All functions below are used to precompute various bitboards during
  // program initialization.  Some of the functions may be difficult to
  // understand, but they all seem to work correctly, and it should never
  // be necessary to touch any of them.

  void init_masks() {
    SetMaskBB[SQ_NONE] = 0ULL;
    ClearMaskBB[SQ_NONE] = ~SetMaskBB[SQ_NONE];
    for(Square s = SQ_A1; s <= SQ_H8; s++) {
      SetMaskBB[s] = (1ULL << s);
      ClearMaskBB[s] = ~SetMaskBB[s];
    }
    for(Color c = WHITE; c <= BLACK; c++)
      for(Square s = SQ_A1; s <= SQ_H8; s++) {
        PassedPawnMask[c][s] =
          in_front_bb(c, s) & this_and_neighboring_files_bb(s);
        OutpostMask[c][s] = in_front_bb(c, s) & neighboring_files_bb(s);
      }

    for (Bitboard b = 0ULL; b < 256ULL; b++)
        BitCount8Bit[b] = (uint8_t)count_1s(b);
  }


  void init_ray_bitboards() {
    int d[8] = {1, -1, 16, -16, 17, -17, 15, -15};
    for(int i = 0; i < 128; i = (i + 9) & ~8) {
      for(int j = 0; j < 8; j++) {
        RayBB[(i&7)|((i>>4)<<3)][j] = EmptyBoardBB;
        for(int k = i + d[j]; (k & 0x88) == 0; k += d[j])
          set_bit(&(RayBB[(i&7)|((i>>4)<<3)][j]), Square((k&7)|((k>>4)<<3)));
      }
    }
  }


  void init_attacks() {
    int i, j, k, l;
    int step[16][8] =  {
      {0},
      {7,9,0}, {17,15,10,6,-6,-10,-15,-17}, {9,7,-7,-9,0}, {8,1,-1,-8,0},
      {9,7,-7,-9,8,1,-1,-8}, {9,7,-7,-9,8,1,-1,-8}, {0}, {0},
      {-7,-9,0}, {17,15,10,6,-6,-10,-15,-17}, {9,7,-7,-9,0}, {8,1,-1,-8,0},
      {9,7,-7,-9,8,1,-1,-8}, {9,7,-7,-9,8,1,-1,-8}
    };

    for(i = 0; i < 64; i++) {
      for(j = 0; j <= int(BK); j++) {
        StepAttackBB[j][i] = EmptyBoardBB;
        for(k = 0; k < 8 && step[j][k] != 0; k++) {
          l = i + step[j][k];
          if(l >= 0 && l < 64 && abs((i&7) - (l&7)) < 3)
            StepAttackBB[j][i] |= (1ULL << l);
        }
      }
    }
  }


  Bitboard sliding_attacks(int sq, Bitboard block, int dirs, int deltas[][2],
                           int fmin=0, int fmax=7, int rmin=0, int rmax=7) {
    Bitboard result = 0ULL;
    int rk = sq / 8, fl = sq % 8, r, f, i;
    for(i = 0; i < dirs; i++) {
      int dx = deltas[i][0], dy = deltas[i][1];
      for(f = fl+dx, r = rk+dy;
          (dx==0 || (f>=fmin && f<=fmax)) && (dy==0 || (r>=rmin && r<=rmax));
          f += dx, r += dy) {
        result |= (1ULL << (f + r*8));
        if(block & (1ULL << (f + r*8))) break;
      }
    }
    return result;
  }


  void init_between_bitboards() {
    SquareDelta step[8] = {
      DELTA_E, DELTA_W, DELTA_N, DELTA_S, DELTA_NE, DELTA_SW, DELTA_NW, DELTA_SE
    };
    SignedDirection d;
    for(Square s1 = SQ_A1; s1 <= SQ_H8; s1++)
      for(Square s2 = SQ_A1; s2 <= SQ_H8; s2++) {
        BetweenBB[s1][s2] = EmptyBoardBB;
        d = signed_direction_between_squares(s1, s2);
        if(d != SIGNED_DIR_NONE)
          for(Square s3 = s1 + step[d]; s3 != s2; s3 += step[d])
            set_bit(&(BetweenBB[s1][s2]), s3);
      }
  }


  Bitboard index_to_bitboard(int index, Bitboard mask) {
    int i, j, bits = count_1s(mask);
    Bitboard result = 0ULL;
    for(i = 0; i < bits; i++) {
      j = pop_1st_bit(&mask);
      if(index & (1 << i)) result |= (1ULL << j);
    }
    return result;
  }


  void init_sliding_attacks(Bitboard attacks[],
                            int attackIndex[], Bitboard mask[],
                            const int shift[2], const Bitboard mult[],
                            int deltas[][2]) {
    int i, j, k, index = 0;
    Bitboard b;
    for(i = 0; i < 64; i++) {
      attackIndex[i] = index;
      mask[i] = sliding_attacks(i, 0ULL, 4, deltas, 1, 6, 1, 6);

#if defined(IS_64BIT)
      j = (1 << (64 - shift[i]));
#else
      j = (1 << (32 - shift[i]));
#endif

      for(k = 0; k < j; k++) {

#if defined(IS_64BIT)
        b = index_to_bitboard(k, mask[i]);
        attacks[index + ((b * mult[i]) >> shift[i])] =
          sliding_attacks(i, b, 4, deltas);
#else
        b = index_to_bitboard(k, mask[i]);
        attacks[index +
                 (unsigned(int(b) * int(mult[i]) ^
                           int(b >> 32) * int(mult[i] >> 32))
                  >> shift[i])] =
          sliding_attacks(i, b, 4, deltas);
#endif
      }
      index += j;
    }
  }


  void init_pseudo_attacks() {
    Square s;
    for(s = SQ_A1; s <= SQ_H8; s++) {
      BishopPseudoAttacks[s] = bishop_attacks_bb(s, EmptyBoardBB);
      RookPseudoAttacks[s] = rook_attacks_bb(s, EmptyBoardBB);
      QueenPseudoAttacks[s] = queen_attacks_bb(s, EmptyBoardBB);
    }
  }

}
