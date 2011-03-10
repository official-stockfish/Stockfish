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

#include <iostream>

#include "bitboard.h"
#include "bitcount.h"

#if defined(IS_64BIT)

const uint64_t BMult[64] = {
  0x0440049104032280ULL, 0x1021023C82008040ULL, 0x0404040082000048ULL,
  0x48C4440084048090ULL, 0x2801104026490000ULL, 0x4100880442040800ULL,
  0x0181011002E06040ULL, 0x9101004104200E00ULL, 0x1240848848310401ULL,
  0x2000142828050024ULL, 0x00001004024D5000ULL, 0x0102044400800200ULL,
  0x8108108820112000ULL, 0xA880818210C00046ULL, 0x4008008801082000ULL,
  0x0060882404049400ULL, 0x0104402004240810ULL, 0x000A002084250200ULL,
  0x00100B0880801100ULL, 0x0004080201220101ULL, 0x0044008080A00000ULL,
  0x0000202200842000ULL, 0x5006004882D00808ULL, 0x0000200045080802ULL,
  0x0086100020200601ULL, 0xA802080A20112C02ULL, 0x0080411218080900ULL,
  0x000200A0880080A0ULL, 0x9A01010000104000ULL, 0x0028008003100080ULL,
  0x0211021004480417ULL, 0x0401004188220806ULL, 0x00825051400C2006ULL,
  0x00140C0210943000ULL, 0x0000242800300080ULL, 0x00C2208120080200ULL,
  0x2430008200002200ULL, 0x1010100112008040ULL, 0x8141050100020842ULL,
  0x0000822081014405ULL, 0x800C049E40400804ULL, 0x4A0404028A000820ULL,
  0x0022060201041200ULL, 0x0360904200840801ULL, 0x0881A08208800400ULL,
  0x0060202C00400420ULL, 0x1204440086061400ULL, 0x0008184042804040ULL,
  0x0064040315300400ULL, 0x0C01008801090A00ULL, 0x0808010401140C00ULL,
  0x04004830C2020040ULL, 0x0080005002020054ULL, 0x40000C14481A0490ULL,
  0x0010500101042048ULL, 0x1010100200424000ULL, 0x0000640901901040ULL,
  0x00000A0201014840ULL, 0x00840082AA011002ULL, 0x010010840084240AULL,
  0x0420400810420608ULL, 0x8D40230408102100ULL, 0x4A00200612222409ULL,
  0x0A08520292120600ULL
};

const uint64_t RMult[64] = {
  0x0A8002C000108020ULL, 0x4440200140003000ULL, 0x8080200010011880ULL,
  0x0380180080141000ULL, 0x1A00060008211044ULL, 0x410001000A0C0008ULL,
  0x9500060004008100ULL, 0x0100024284A20700ULL, 0x0000802140008000ULL,
  0x0080C01002A00840ULL, 0x0402004282011020ULL, 0x9862000820420050ULL,
  0x0001001448011100ULL, 0x6432800200800400ULL, 0x040100010002000CULL,
  0x0002800D0010C080ULL, 0x90C0008000803042ULL, 0x4010004000200041ULL,
  0x0003010010200040ULL, 0x0A40828028001000ULL, 0x0123010008000430ULL,
  0x0024008004020080ULL, 0x0060040001104802ULL, 0x00582200028400D1ULL,
  0x4000802080044000ULL, 0x0408208200420308ULL, 0x0610038080102000ULL,
  0x3601000900100020ULL, 0x0000080080040180ULL, 0x00C2020080040080ULL,
  0x0080084400100102ULL, 0x4022408200014401ULL, 0x0040052040800082ULL,
  0x0B08200280804000ULL, 0x008A80A008801000ULL, 0x4000480080801000ULL,
  0x0911808800801401ULL, 0x822A003002001894ULL, 0x401068091400108AULL,
  0x000004A10A00004CULL, 0x2000800640008024ULL, 0x1486408102020020ULL,
  0x000100A000D50041ULL, 0x00810050020B0020ULL, 0x0204000800808004ULL,
  0x00020048100A000CULL, 0x0112000831020004ULL, 0x0009000040810002ULL,
  0x0440490200208200ULL, 0x8910401000200040ULL, 0x6404200050008480ULL,
  0x4B824A2010010100ULL, 0x04080801810C0080ULL, 0x00000400802A0080ULL,
  0x8224080110026400ULL, 0x40002C4104088200ULL, 0x01002100104A0282ULL,
  0x1208400811048021ULL, 0x3201014A40D02001ULL, 0x0005100019200501ULL,
  0x0101000208001005ULL, 0x0002008450080702ULL, 0x001002080301D00CULL,
  0x410201CE5C030092ULL
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
  0x54142844C6A22981ULL, 0x710358A6EA25C19EULL, 0x704F746D63A4A8DCULL,
  0xBFED1A0B80F838C5ULL, 0x90561D5631E62110ULL, 0x2804260376E60944ULL,
  0x84A656409AA76871ULL, 0xF0267F64C28B6197ULL, 0x70764EBB762F0585ULL,
  0x92AA09E0CFE161DEULL, 0x41EE1F6BB266F60EULL, 0xDDCBF04F6039C444ULL,
  0x5A3FAB7BAC0D988AULL, 0xD3727877FA4EAA03ULL, 0xD988402D868DDAAEULL,
  0x812B291AFA075C7CULL, 0x94FAF987B685A932ULL, 0x3ED867D8470D08DBULL,
  0x92517660B8901DE8ULL, 0x2D97E43E058814B4ULL, 0x880A10C220B25582ULL,
  0xC7C6520D1F1A0477ULL, 0xDBFC7FBCD7656AA6ULL, 0x78B1B9BFB1A2B84FULL,
  0x2F20037F112A0BC1ULL, 0x657171EA2269A916ULL, 0xC08302B07142210EULL,
  0x0880A4403064080BULL, 0x3602420842208C00ULL, 0x852800DC7E0B6602ULL,
  0x595A3FBBAA0F03B2ULL, 0x9F01411558159D5EULL, 0x2B4A4A5F88B394F2ULL,
  0x4AFCBFFC292DD03AULL, 0x4A4094A3B3F10522ULL, 0xB06F00B491F30048ULL,
  0xD5B3820280D77004ULL, 0x8B2E01E7C8E57A75ULL, 0x2D342794E886C2E6ULL,
  0xC302C410CDE21461ULL, 0x111F426F1379C274ULL, 0xE0569220ABB31588ULL,
  0x5026D3064D453324ULL, 0xE2076040C343CD8AULL, 0x93EFD1E1738021EEULL,
  0xB680804BED143132ULL, 0x44E361B21986944CULL, 0x44C60170EF5C598CULL,
  0xF4DA475C195C9C94ULL, 0xA3AFBB5F72060B1DULL, 0xBC75F410E41C4FFCULL,
  0xB51C099390520922ULL, 0x902C011F8F8EC368ULL, 0x950B56B3D6F5490AULL,
  0x3909E0635BF202D0ULL, 0x5744F90206EC10CCULL, 0xDC59FD76317ABBC1ULL,
  0x881C7C67FCBFC4F6ULL, 0x47CA41E7E440D423ULL, 0xEB0C88112048D004ULL,
  0x51C60E04359AEF1AULL, 0x1AA1FE0E957A5554ULL, 0xDD9448DB4F5E3104ULL,
  0xDC01F6DCA4BEBBDCULL,
};

const uint64_t RMult[64] = {
  0xD7445CDEC88002C0ULL, 0xD0A505C1F2001722ULL, 0xE065D1C896002182ULL,
  0x9A8C41E75A000892ULL, 0x8900B10C89002AA8ULL, 0x9B28D1C1D60005A2ULL,
  0x015D6C88DE002D9AULL, 0xB1DBFC802E8016A9ULL, 0x149A1042D9D60029ULL,
  0xB9C08050599E002FULL, 0x132208C3AF300403ULL, 0xC1000CE2E9C50070ULL,
  0x9D9AA13C99020012ULL, 0xB6B078DAF71E0046ULL, 0x9D880182FB6E002EULL,
  0x52889F467E850037ULL, 0xDA6DC008D19A8480ULL, 0x468286034F902420ULL,
  0x7140AC09DC54C020ULL, 0xD76FFFFA39548808ULL, 0xEA901C4141500808ULL,
  0xC91004093F953A02ULL, 0x02882AFA8F6BB402ULL, 0xAEBE335692442C01ULL,
  0x0E904A22079FB91EULL, 0x13A514851055F606ULL, 0x76C782018C8FE632ULL,
  0x1DC012A9D116DA06ULL, 0x3C9E0037264FFFA6ULL, 0x2036002853C6E4A2ULL,
  0xE3FE08500AFB47D4ULL, 0xF38AF25C86B025C2ULL, 0xC0800E2182CF9A40ULL,
  0x72002480D1F60673ULL, 0x2500200BAE6E9B53ULL, 0xC60018C1EEFCA252ULL,
  0x0600590473E3608AULL, 0x46002C4AB3FE51B2ULL, 0xA200011486BCC8D2ULL,
  0xB680078095784C63ULL, 0x2742002639BF11AEULL, 0xC7D60021A5BDB142ULL,
  0xC8C04016BB83D820ULL, 0xBD520028123B4842ULL, 0x9D1600344AC2A832ULL,
  0x6A808005631C8A05ULL, 0x604600A148D5389AULL, 0xE2E40103D40DEA65ULL,
  0x945B5A0087C62A81ULL, 0x012DC200CD82D28EULL, 0x2431C600B5F9EF76ULL,
  0xFB142A006A9B314AULL, 0x06870E00A1C97D62ULL, 0x2A9DB2004A2689A2ULL,
  0xD3594600CAF5D1A2ULL, 0xEE0E4900439344A7ULL, 0x89C4D266CA25007AULL,
  0x3E0013A2743F97E3ULL, 0x0180E31A0431378AULL, 0x3A9E465A4D42A512ULL,
  0x98D0A11A0C0D9CC2ULL, 0x8E711C1ABA19B01EULL, 0x8DCDC836DD201142ULL,
  0x5AC08A4735370479ULL,
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

// Global bitboards definitions with static storage duration are
// automatically set to zero before enter main().
Bitboard RMask[64];
int RAttackIndex[64];
Bitboard RAttacks[0x19000];

Bitboard BMask[64];
int BAttackIndex[64];
Bitboard BAttacks[0x1480];

Bitboard SetMaskBB[65];
Bitboard ClearMaskBB[65];

Bitboard SquaresByColorBB[2];
Bitboard FileBB[8];
Bitboard RankBB[8];
Bitboard NeighboringFilesBB[8];
Bitboard ThisAndNeighboringFilesBB[8];
Bitboard InFrontBB[2][8];
Bitboard StepAttacksBB[16][64];
Bitboard BetweenBB[64][64];
Bitboard SquaresInFrontMask[2][64];
Bitboard PassedPawnMask[2][64];
Bitboard AttackSpanMask[2][64];

Bitboard BishopPseudoAttacks[64];
Bitboard RookPseudoAttacks[64];
Bitboard QueenPseudoAttacks[64];

uint8_t BitCount8Bit[256];


namespace {

  void init_masks();
  void init_step_attacks();
  void init_pseudo_attacks();
  void init_between_bitboards();
  Bitboard index_to_bitboard(int index, Bitboard mask);
  Bitboard sliding_attacks(int sq, Bitboard occupied, int deltas[][2],
                           int fmin, int fmax, int rmin, int rmax);
  void init_sliding_attacks(Bitboard attacks[], int attackIndex[], Bitboard mask[],
                            const int shift[], const Bitboard mult[], int deltas[][2]);
}


/// print_bitboard() prints a bitboard in an easily readable format to the
/// standard output. This is sometimes useful for debugging.

void print_bitboard(Bitboard b) {

  for (Rank r = RANK_8; r >= RANK_1; r--)
  {
      std::cout << "+---+---+---+---+---+---+---+---+" << '\n';
      for (File f = FILE_A; f <= FILE_H; f++)
          std::cout << "| " << (bit_is_set(b, make_square(f, r)) ? 'X' : ' ') << ' ';

      std::cout << "|\n";
  }
  std::cout << "+---+---+---+---+---+---+---+---+" << std::endl;
}


/// first_1() finds the least significant nonzero bit in a nonzero bitboard.
/// pop_1st_bit() finds and clears the least significant nonzero bit in a
/// nonzero bitboard.

#if defined(IS_64BIT) && !defined(USE_BSFQ)

static CACHE_LINE_ALIGNMENT
const int BitTable[64] = {
   0,  1,  2,  7,  3, 13,  8, 19,  4, 25, 14, 28,  9, 34, 20, 40,  5, 17, 26,
  38, 15, 46, 29, 48, 10, 31, 35, 54, 21, 50, 41, 57, 63,  6, 12, 18, 24, 27,
  33, 39, 16, 37, 45, 47, 30, 53, 49, 56, 62, 11, 23, 32, 36, 44, 52, 55, 61,
  22, 43, 51, 60, 42, 59, 58
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
  63, 30,  3, 32, 25, 41, 22, 33, 15, 50, 42, 13, 11, 53, 19, 34, 61, 29,  2,
  51, 21, 43, 45, 10, 18, 47,  1, 54,  9, 57,  0, 35, 62, 31, 40,  4, 49,  5,
  52, 26, 60,  6, 23, 44, 46, 27, 56, 16,  7, 39, 48, 24, 59, 14, 12, 55, 38,
  28, 58, 20, 37, 17, 36, 8
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

#endif // !defined(USE_BSFQ)


/// init_bitboards() initializes various bitboard arrays. It is called during
/// program initialization.

void init_bitboards() {

  int rookDeltas[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};
  int bishopDeltas[4][2] = {{1,1},{-1,1},{1,-1},{-1,-1}};

  init_masks();
  init_step_attacks();
  init_sliding_attacks(RAttacks, RAttackIndex, RMask, RShift, RMult, rookDeltas);
  init_sliding_attacks(BAttacks, BAttackIndex, BMask, BShift, BMult, bishopDeltas);
  init_pseudo_attacks();
  init_between_bitboards();
}

namespace {

  // All functions below are used to precompute various bitboards during
  // program initialization.  Some of the functions may be difficult to
  // understand, but they all seem to work correctly, and it should never
  // be necessary to touch any of them.

  void init_masks() {

    SquaresByColorBB[DARK]  =  0xAA55AA55AA55AA55ULL;
    SquaresByColorBB[LIGHT] = ~SquaresByColorBB[DARK];

    FileBB[FILE_A] = FileABB;
    RankBB[RANK_1] = Rank1BB;

    for (int f = FILE_B; f <= FILE_H; f++)
    {
        FileBB[f] = FileBB[f - 1] << 1;
        RankBB[f] = RankBB[f - 1] << 8;
    }

    for (int f = FILE_A; f <= FILE_H; f++)
    {
        NeighboringFilesBB[f] = (f > FILE_A ? FileBB[f - 1] : 0) | (f < FILE_H ? FileBB[f + 1] : 0);
        ThisAndNeighboringFilesBB[f] = FileBB[f] | NeighboringFilesBB[f];
    }

    for (int rw = RANK_7, rb = RANK_2; rw >= RANK_1; rw--, rb++)
    {
        InFrontBB[WHITE][rw] = InFrontBB[WHITE][rw + 1] | RankBB[rw + 1];
        InFrontBB[BLACK][rb] = InFrontBB[BLACK][rb - 1] | RankBB[rb - 1];
    }

    SetMaskBB[SQ_NONE] = EmptyBoardBB;
    ClearMaskBB[SQ_NONE] = ~SetMaskBB[SQ_NONE];

    for (Square s = SQ_A1; s <= SQ_H8; s++)
    {
        SetMaskBB[s] = (1ULL << s);
        ClearMaskBB[s] = ~SetMaskBB[s];
    }

    for (Color c = WHITE; c <= BLACK; c++)
        for (Square s = SQ_A1; s <= SQ_H8; s++)
        {
            SquaresInFrontMask[c][s] = in_front_bb(c, s) & file_bb(s);
            PassedPawnMask[c][s] = in_front_bb(c, s) & this_and_neighboring_files_bb(s);
            AttackSpanMask[c][s] = in_front_bb(c, s) & neighboring_files_bb(s);
        }

    for (Bitboard b = 0; b < 256; b++)
        BitCount8Bit[b] = (uint8_t)count_1s<CNT32>(b);
  }

  void init_step_attacks() {

    const int step[][9] =  {
      {0},
      {7,9,0}, {17,15,10,6,-6,-10,-15,-17,0}, {0}, {0}, {0},
      {9,7,-7,-9,8,1,-1,-8,0}, {0}, {0},
      {-7,-9,0}, {17,15,10,6,-6,-10,-15,-17,0}, {0}, {0}, {0},
      {9,7,-7,-9,8,1,-1,-8,0}
    };

    for (Square s = SQ_A1; s <= SQ_H8; s++)
        for (Piece pc = WP; pc <= BK; pc++)
            for (int k = 0; step[pc][k] != 0; k++)
            {
                Square to = s + Square(step[pc][k]);

                if (square_is_ok(to) && square_distance(s, to) < 3)
                    set_bit(&StepAttacksBB[pc][s], to);
           }
  }

  Bitboard sliding_attacks(int sq, Bitboard occupied, int deltas[][2],
                           int fmin, int fmax, int rmin, int rmax) {
    int dx, dy, f, r;
    int rk = sq / 8;
    int fl = sq % 8;
    Bitboard attacks = EmptyBoardBB;

    for (int i = 0; i < 4; i++)
    {
        dx = deltas[i][0];
        dy = deltas[i][1];
        f = fl + dx;
        r = rk + dy;

        while (   (dx == 0 || (f >= fmin && f <= fmax))
               && (dy == 0 || (r >= rmin && r <= rmax)))
        {
            attacks |= (1ULL << (f + r * 8));

            if (occupied & (1ULL << (f + r * 8)))
                break;

            f += dx;
            r += dy;
        }
    }
    return attacks;
  }

  Bitboard index_to_bitboard(int index, Bitboard mask) {

    Bitboard result = EmptyBoardBB;
    int sq, cnt = 0;

    while (mask)
    {
        sq = pop_1st_bit(&mask);

        if (index & (1 << cnt++))
            result |= (1ULL << sq);
    }
    return result;
  }

  void init_sliding_attacks(Bitboard attacks[], int attackIndex[], Bitboard mask[],
                            const int shift[], const Bitboard mult[], int deltas[][2]) {
    Bitboard b, v;
    int i, j, index;

    for (i = index = 0; i < 64; i++)
    {
        attackIndex[i] = index;
        mask[i] = sliding_attacks(i, 0, deltas, 1, 6, 1, 6);
        j = 1 << ((CpuIs64Bit ? 64 : 32) - shift[i]);

        for (int k = 0; k < j; k++)
        {
            b = index_to_bitboard(k, mask[i]);
            v = CpuIs64Bit ? b * mult[i] : unsigned(b * mult[i] ^ (b >> 32) * (mult[i] >> 32));
            attacks[index + (v >> shift[i])] = sliding_attacks(i, b, deltas, 0, 7, 0, 7);
        }
        index += j;
    }
  }

  void init_pseudo_attacks() {

    for (Square s = SQ_A1; s <= SQ_H8; s++)
    {
        BishopPseudoAttacks[s] = bishop_attacks_bb(s, EmptyBoardBB);
        RookPseudoAttacks[s]   = rook_attacks_bb(s, EmptyBoardBB);
        QueenPseudoAttacks[s]  = queen_attacks_bb(s, EmptyBoardBB);
    }
  }

  void init_between_bitboards() {

    Square s1, s2, s3, d;
    int f, r;

    for (s1 = SQ_A1; s1 <= SQ_H8; s1++)
        for (s2 = SQ_A1; s2 <= SQ_H8; s2++)
            if (bit_is_set(QueenPseudoAttacks[s1], s2))
            {
                f = file_distance(s1, s2);
                r = rank_distance(s1, s2);

                d = (s2 - s1) / Max(f, r);

                for (s3 = s1 + d; s3 != s2; s3 += d)
                    set_bit(&(BetweenBB[s1][s2]), s3);
            }
  }

}
