/*
  Copyright (c) 2013 Ronald de Man
  This file may be redistributed and/or modified without restrictions.

  tbprobe.cpp contains the Stockfish-specific routines of the
  tablebase probing code. It should be relatively easy to adapt
  this code to other chess engines.
*/

#include <algorithm>
#include <cstdint>
#include <cstring>   // For std::memset
#include <deque>
#include <list>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../bitboard.h"
#include "../movegen.h"
#include "../position.h"
#include "../search.h"
#include "../thread_win32.h"
#include "../types.h"

#include "tbprobe.h"

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#define TBPIECES 6

using namespace Tablebases;

int Tablebases::MaxCardinality = 0;

namespace {

typedef uint64_t base_t;

inline WDLScore operator-(WDLScore d) { return WDLScore(-int(d)); }
inline WDLScore operator+(WDLScore d1, WDLScore d2) { return WDLScore(int(d1) + int(d2)); }

inline Square operator^=(Square& s, int i) { return s = Square(int(s) ^ i); }
inline Square operator^(Square s, int i) { return Square(int(s) ^ i); }

struct PairsData {
    char *indextable;
    uint16_t *sizetable;
    uint8_t *data;
    uint16_t *offset;
    uint8_t *symlen;
    uint8_t *sympat;
    int blocksize;
    int idxbits;
    int min_len;
    base_t base[1]; // C++ complains about base[]...
};

class WDLEntry {

    static constexpr uint8_t TB_MAGIC[] = { 0x71, 0xE8, 0x23, 0x5D };

public:
    WDLEntry(const Position& pos, Key keys[]);
   ~WDLEntry();
    bool init(const std::string& fname);

    char* baseAddress;
    uint64_t key;
    uint64_t mapping;
    uint8_t ready;
    uint8_t num;
    uint8_t symmetric;
    uint8_t has_pawns;
    union {
        struct {
            uint8_t hasUniquePieces;
            PairsData* precomp;
            int factor[TBPIECES];
            uint8_t pieces[TBPIECES];
            uint8_t norm[TBPIECES];
        } piece[2];

        struct {
            uint8_t pawns[2];
            struct {
                PairsData* precomp[2];
                int factor[2][TBPIECES];
                uint8_t pieces[2][TBPIECES];
                uint8_t norm[2][TBPIECES];
            } file[4];
        } pawn;
    };
};

class DTZEntry {

    static constexpr uint8_t TB_MAGIC[] = { 0xD7, 0x66, 0x0C, 0xA5 };

public:
    DTZEntry(const WDLEntry& wdl, Key keys[]);
   ~DTZEntry();
    bool init(const std::string& fname);

    uint64_t keys[2];
    char* baseAddress;
    uint64_t key;
    uint64_t mapping;
    uint8_t ready;
    uint8_t num;
    uint8_t symmetric;
    uint8_t has_pawns;
    union {
        struct {
            uint8_t hasUniquePieces;
            PairsData* precomp;
            int factor[TBPIECES];
            uint8_t pieces[TBPIECES];
            uint8_t norm[TBPIECES];
            uint8_t flags; // accurate, mapped, side
            uint16_t map_idx[4];
            uint8_t* map;
        } piece;

        struct {
            uint8_t pawns[2];
            struct {
                PairsData* precomp;
                int factor[TBPIECES];
                uint8_t pieces[TBPIECES];
                uint8_t norm[TBPIECES];
                uint8_t flags;
                uint16_t map_idx[4];
            } file[4];
            uint8_t* map;
        } pawn;
    };
};

const signed char OffdiagA1H8[] = {
    0,-1,-1,-1,-1,-1,-1,-1,
    1, 0,-1,-1,-1,-1,-1,-1,
    1, 1, 0,-1,-1,-1,-1,-1,
    1, 1, 1, 0,-1,-1,-1,-1,
    1, 1, 1, 1, 0,-1,-1,-1,
    1, 1, 1, 1, 1, 0,-1,-1,
    1, 1, 1, 1, 1, 1, 0,-1,
    1, 1, 1, 1, 1, 1, 1, 0
};

const uint8_t Triangle[] = {
    6, 0, 1, 2, 2, 1, 0, 6,
    0, 7, 3, 4, 4, 3, 7, 0,
    1, 3, 8, 5, 5, 8, 3, 1,
    2, 4, 5, 9, 9, 5, 4, 2,
    2, 4, 5, 9, 9, 5, 4, 2,
    1, 3, 8, 5, 5, 8, 3, 1,
    0, 7, 3, 4, 4, 3, 7, 0,
    6, 0, 1, 2, 2, 1, 0, 6
};

const uint8_t Lower[] = {
    28, 0,  1,  2,  3,  4,  5,  6,
    0, 29,  7,  8,  9, 10, 11, 12,
    1,  7, 30, 13, 14, 15, 16, 17,
    2,  8, 13, 31, 18, 19, 20, 21,
    3,  9, 14, 18, 32, 22, 23, 24,
    4, 10, 15, 19, 22, 33, 25, 26,
    5, 11, 16, 20, 23, 25, 34, 27,
    6, 12, 17, 21, 24, 26, 27, 35
};

const uint8_t Diag[] = {
    0,  0,  0,  0,  0,  0,  0,  8,
    0,  1,  0,  0,  0,  0,  9,  0,
    0,  0,  2,  0,  0, 10,  0,  0,
    0,  0,  0,  3, 11,  0,  0,  0,
    0,  0,  0, 12,  4,  0,  0,  0,
    0,  0, 13,  0,  0,  5,  0,  0,
    0, 14,  0,  0,  0,  0,  6,  0,
    15, 0,  0,  0,  0,  0,  0,  7
};

const uint8_t Flap[] = {
    0,  0,  0,  0,  0,  0, 0,  0,
    0,  6, 12, 18, 18, 12, 6,  0,
    1,  7, 13, 19, 19, 13, 7,  1,
    2,  8, 14, 20, 20, 14, 8,  2,
    3,  9, 15, 21, 21, 15, 9,  3,
    4, 10, 16, 22, 22, 16, 10, 4,
    5, 11, 17, 23, 23, 17, 11, 5,
    0,  0,  0,  0,  0,  0,  0, 0
};

const uint8_t Ptwist[] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    47, 35, 23, 11, 10, 22, 34, 46,
    45, 33, 21,  9,  8, 20, 32, 44,
    43, 31, 19,  7,  6, 18, 30, 42,
    41, 29, 17,  5,  4, 16, 28, 40,
    39, 27, 15,  3,  2, 14, 26, 38,
    37, 25, 13,  1,  0, 12, 24, 36,
     0,  0,  0,  0,  0,  0,  0,  0
};

const uint8_t Invflap[] = {
     8, 16, 24, 32, 40, 48,
     9, 17, 25, 33, 41, 49,
    10, 18, 26, 34, 42, 50,
    11, 19, 27, 35, 43, 51
};

const short KK_idx[10][64] = {
    {
        -1, -1, -1,  0,  1,  2,  3,  4,
        -1, -1, -1,  5,  6,  7,  8,  9,
        10, 11, 12, 13, 14, 15, 16, 17,
        18, 19, 20, 21, 22, 23, 24, 25,
        26, 27, 28, 29, 30, 31, 32, 33,
        34, 35, 36, 37, 38, 39, 40, 41,
        42, 43, 44, 45, 46, 47, 48, 49,
        50, 51, 52, 53, 54, 55, 56, 57
    },
    {
        58,  -1, -1, -1, 59, 60, 61, 62,
        63,  -1, -1, -1, 64, 65, 66, 67,
        68,  69, 70, 71, 72, 73, 74, 75,
        76,  77, 78, 79, 80, 81, 82, 83,
        84,  85, 86, 87, 88, 89, 90, 91,
        92,  93, 94, 95, 96, 97, 98, 99,
        100,101,102,103,104,105,106,107,
        108,109,110,111,112,113,114,115
    },
    {
        116,117, -1, -1, -1,118,119,120,
        121,122, -1, -1, -1,123,124,125,
        126,127,128,129,130,131,132,133,
        134,135,136,137,138,139,140,141,
        142,143,144,145,146,147,148,149,
        150,151,152,153,154,155,156,157,
        158,159,160,161,162,163,164,165,
        166,167,168,169,170,171,172,173
    },
    {
        174, -1, -1, -1,175,176,177,178,
        179, -1, -1, -1,180,181,182,183,
        184, -1, -1, -1,185,186,187,188,
        189,190,191,192,193,194,195,196,
        197,198,199,200,201,202,203,204,
        205,206,207,208,209,210,211,212,
        213,214,215,216,217,218,219,220,
        221,222,223,224,225,226,227,228
    },
    {
        229,230, -1, -1, -1,231,232,233,
        234,235, -1, -1, -1,236,237,238,
        239,240, -1, -1, -1,241,242,243,
        244,245,246,247,248,249,250,251,
        252,253,254,255,256,257,258,259,
        260,261,262,263,264,265,266,267,
        268,269,270,271,272,273,274,275,
        276,277,278,279,280,281,282,283
    },
    {
        284,285,286,287,288,289,290,291,
        292,293, -1, -1, -1,294,295,296,
        297,298, -1, -1, -1,299,300,301,
        302,303, -1, -1, -1,304,305,306,
        307,308,309,310,311,312,313,314,
        315,316,317,318,319,320,321,322,
        323,324,325,326,327,328,329,330,
        331,332,333,334,335,336,337,338
    },
    {
        -1, -1,339,340,341,342,343,344,
        -1, -1,345,346,347,348,349,350,
        -1, -1,441,351,352,353,354,355,
        -1, -1, -1,442,356,357,358,359,
        -1, -1, -1, -1,443,360,361,362,
        -1, -1, -1, -1, -1,444,363,364,
        -1, -1, -1, -1, -1, -1,445,365,
        -1, -1, -1, -1, -1, -1, -1,446
    },
    {
        -1, -1, -1,366,367,368,369,370,
        -1, -1, -1,371,372,373,374,375,
        -1, -1, -1,376,377,378,379,380,
        -1, -1, -1,447,381,382,383,384,
        -1, -1, -1, -1,448,385,386,387,
        -1, -1, -1, -1, -1,449,388,389,
        -1, -1, -1, -1, -1, -1,450,390,
        -1, -1, -1, -1, -1, -1, -1,451
    },
    {
        452,391,392,393,394,395,396,397,
         -1, -1, -1, -1,398,399,400,401,
         -1, -1, -1, -1,402,403,404,405,
         -1, -1, -1, -1,406,407,408,409,
         -1, -1, -1, -1,453,410,411,412,
         -1, -1, -1, -1, -1,454,413,414,
         -1, -1, -1, -1, -1, -1,455,415,
         -1, -1, -1, -1, -1, -1, -1,456
    },
    {
        457,416,417,418,419,420,421,422,
         -1,458,423,424,425,426,427,428,
         -1, -1, -1, -1, -1,429,430,431,
         -1, -1, -1, -1, -1,432,433,434,
         -1, -1, -1, -1, -1,435,436,437,
         -1, -1, -1, -1, -1,459,438,439,
         -1, -1, -1, -1, -1, -1,460,440,
         -1, -1, -1, -1, -1, -1, -1,461
    }
};

const int wdl_to_dtz[] = { -1, -101, 0, 101, 1 };
const int wdl_to_map[] = { 1, 3, 0, 2, 0 };
const uint8_t pa_flags[] = { 8, 0, 0, 0, 4 };

const Value WDL_to_value[] = {
    -VALUE_MATE + MAX_PLY + 1,
    VALUE_DRAW - 2,
    VALUE_DRAW,
    VALUE_DRAW + 2,
    VALUE_MATE - MAX_PLY - 1
};

const std::string PieceToChar = " PNBRQK";

Mutex TB_mutex;
std::string TBPaths;
std::deque<WDLEntry> WDLTable;
std::list<DTZEntry> DTZTable;

int Binomial[6][64];
int Pawnidx[5][24];
int Pfactor[5][4];

class HashTable {

    struct Entry {
        Key key;
        WDLEntry* ptr;
    };

    static const int TBHASHBITS = 10;
    static const int HSHMAX     = 5;

    Entry table[1 << TBHASHBITS][HSHMAX];

    void insert(Key key, WDLEntry* ptr) {
        Entry* entry = table[key >> (64 - TBHASHBITS)];

        for (int i = 0; i < HSHMAX; ++i, ++entry)
            if (!entry->ptr || entry->key == key) {
                entry->key = key;
                entry->ptr = ptr;
                return;
            }

        std::cerr << "HSHMAX too low!" << std::endl;
        exit(1);
    }

public:
  WDLEntry* operator[](Key key) {
      Entry* entry = table[key >> (64 - TBHASHBITS)];

      for (int i = 0; i < HSHMAX; ++i, ++entry)
          if (entry->key == key)
              return entry->ptr;

      return nullptr;
  }

  void clear() { std::memset(table, 0, sizeof(table)); }
  void insert(const std::vector<PieceType>& pieces);
};

HashTable WDLHash;


class TBFile : public std::ifstream {

    std::string fname;

public:
    // Open the file with the given name found among the TBPaths. TBPaths stores
    // the paths to directories where the .rtbw and .rtbz files can be found.
    // Multiple directories are separated by ";" on Windows and by ":" on
    // Unix-based operating systems.
    //
    // Example:
    // C:\tb\wdl345;C:\tb\wdl6;D:\tb\dtz345;D:\tb\dtz6
    TBFile(const std::string& f) {

#ifndef _WIN32
        const char SepChar = ':';
#else
        const char SepChar = ';';
#endif
        std::stringstream ss(TBPaths);
        std::string path;

        while (std::getline(ss, path, SepChar)) {
            fname = path + "/" + f;
            std::ifstream::open(fname);

            if (is_open())
                return;
        }
    }

    // Memory map the file and check it. File should be already open and
    // will be closed after mapping.
    uint8_t* map(char** baseAddress, uint64_t* mapping, const uint8_t TB_MAGIC[]) {

        if (!is_open()) {
            std::cerr << "Could not find " << fname << std::endl;
            *baseAddress = nullptr;
            return nullptr;
        }
        close();

#ifndef _WIN32
        struct stat statbuf;
        int fd = ::open(fname.c_str(), O_RDONLY);
        fstat(fd, &statbuf);
        *mapping = statbuf.st_size;
        *baseAddress = (char*)mmap(nullptr, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
        ::close(fd);

        if (*baseAddress == (char*)(-1)) {
            std::cerr << "Could not mmap() " << fname << std::endl;
            exit(1);
        }
#else
        HANDLE fd = CreateFile(fname.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD size_high;
        DWORD size_low = GetFileSize(fd, &size_high);
        HANDLE mmap = CreateFileMapping(fd, nullptr, PAGE_READONLY, size_high, size_low, nullptr);
        CloseHandle(fd);

        if (!mmap) {
            std::cerr << "CreateFileMapping() failed" << std::endl;
            exit(1);
        }

        *mapping = (uint64_t)mmap;
        *baseAddress = (char*)MapViewOfFile(mmap, FILE_MAP_READ, 0, 0, 0);

        if (!*baseAddress) {
            std::cerr << "MapViewOfFile() failed, name = " << fname
                      << ", error = " << GetLastError() << std::endl;
            exit(1);
        }
#endif
        uint8_t* data = (uint8_t*)*baseAddress;

        if (   *data++ != TB_MAGIC[0]
            || *data++ != TB_MAGIC[1]
            || *data++ != TB_MAGIC[2]
            || *data++ != TB_MAGIC[3]) {
            std::cerr << "Corrupted table in file " << fname << std::endl;
            unmap(*baseAddress, *mapping);
            *baseAddress = nullptr;
            return nullptr;
        }

        return data;
    }

    static void unmap(char* baseAddress, uint64_t mapping) {

#ifndef _WIN32
        munmap(baseAddress, mapping);
#else
        UnmapViewOfFile(baseAddress);
        CloseHandle((HANDLE)mapping);
#endif
    }
};

WDLEntry::WDLEntry(const Position& pos, Key keys[])
{
    memset(this, 0, sizeof(WDLEntry));

    key = keys[WHITE];
    num = pos.count<ALL_PIECES>(WHITE) + pos.count<ALL_PIECES>(BLACK);
    symmetric = (keys[WHITE] == keys[BLACK]);
    has_pawns = pos.count<PAWN>(WHITE) + pos.count<PAWN>(BLACK);

    if (has_pawns) {
        // FIXME: What it means this one?
        bool c = (   !pos.count<PAWN>(BLACK)
                  || (   pos.count<PAWN>(WHITE)
                      && pos.count<PAWN>(BLACK) >= pos.count<PAWN>(WHITE)));

        pawn.pawns[0] = pos.count<PAWN>(c ? WHITE : BLACK);
        pawn.pawns[1] = pos.count<PAWN>(c ? BLACK : WHITE);
    } else
        for (Color c = WHITE; c <= BLACK; ++c)
            for (PieceType pt = PAWN; pt < KING; ++pt)
                if (popcount(pos.pieces(c, pt)) == 1)
                    piece[0].hasUniquePieces = piece[1].hasUniquePieces = true;
}

WDLEntry::~WDLEntry()
{
    if (baseAddress)
        TBFile::unmap(baseAddress, mapping);

    if (has_pawns)
        for (File f = FILE_A; f <= FILE_D; ++f) {
            free(pawn.file[f].precomp[0]);
            free(pawn.file[f].precomp[1]);
        }
    else {
        free(piece[0].precomp);
        free(piece[1].precomp);
    }
}

DTZEntry::DTZEntry(const WDLEntry& wdl, Key k[])
{
    memset(this, 0, sizeof(DTZEntry));

    keys[0] = k[0];
    keys[1] = k[1];
    key = wdl.key;
    num = wdl.num;
    symmetric = wdl.symmetric;
    has_pawns = wdl.has_pawns;

    if (has_pawns) {
        pawn.pawns[0] = wdl.pawn.pawns[0];
        pawn.pawns[1] = wdl.pawn.pawns[1];
    } else
        piece.hasUniquePieces = wdl.piece[0].hasUniquePieces;
}

DTZEntry::~DTZEntry()
{
    if (baseAddress)
        TBFile::unmap(baseAddress, mapping);

    if (has_pawns)
        for (File f = FILE_A; f <= FILE_D; ++f)
            free(pawn.file[f].precomp);
    else
        free(piece.precomp);
}

// Given a position with 6 or fewer pieces, produce a text string
// of the form KQPvKRP, where "KQP" represents the white pieces if
// mirror == false and the black pieces if mirror == true.
std::string file_name(const Position& pos, bool mirror)
{
    std::string w, b;

    for (PieceType pt = KING; pt >= PAWN; --pt) {
        w += std::string(popcount(pos.pieces(WHITE, pt)), PieceToChar[pt]);
        b += std::string(popcount(pos.pieces(BLACK, pt)), PieceToChar[pt]);
    }

    return mirror ? b + 'v' + w : w + 'v' + b;
}

void HashTable::insert(const std::vector<PieceType>& pieces)
{
    StateInfo st;
    Position pos;
    std::string code;

    for (PieceType pt : pieces)
        code += PieceToChar[pt];

    int bk = code.find('K', 1); // Black king
    TBFile f(code.substr(0, bk) + 'v' + code.substr(bk) + ".rtbw");

    if (!f.is_open())
        return;

    f.close();

    if (int(pieces.size()) > Tablebases::MaxCardinality)
        Tablebases::MaxCardinality = pieces.size();

    Key keys[] = { pos.set(code, WHITE, &st).material_key(),
                   pos.set(code, BLACK, &st).material_key() };

    WDLTable.push_back(WDLEntry(pos.set(code, WHITE, &st), keys));

    insert(keys[WHITE], &WDLTable.back());
    insert(keys[BLACK], &WDLTable.back());
}

uint64_t encode_piece(uint8_t hasUniquePieces, uint8_t* norm, Square* pos, int* factor, int n)
{
    uint64_t idx;
    int i;

    if (file_of(pos[0]) > FILE_D)
        for (i = 0; i < n; ++i)
            pos[i] ^= 7; // Mirror SQ_H1 -> SQ_A1

    if (rank_of(pos[0]) > RANK_4)
        for (i = 0; i < n; ++i)
            pos[i] ^= 070; // Vertical flip SQ_A8 -> SQ_A1

    for (i = 0; i < n; ++i)
        if (OffdiagA1H8[pos[i]])
            break; // First piece not on A1-H8 diagonal

    if (i < (hasUniquePieces ? 3 : 2) && OffdiagA1H8[pos[i]] > 0)
        for (i = 0; i < n; ++i)
            pos[i] = Square(((pos[i] >> 3) | (pos[i] << 3)) & 63); // Flip about the A1-H8 diagonal

    if (hasUniquePieces) {
        // There are unique pieces other than W_KING and B_KING
        i = pos[1] > pos[0];
        int j = (pos[2] > pos[0]) + (pos[2] > pos[1]);

        if (OffdiagA1H8[pos[0]])
            idx = Triangle[pos[0]] * 63*62 + (pos[1] - i) * 62 + (pos[2] - j);
        else if (OffdiagA1H8[pos[1]])
            idx = 6*63*62 + Diag[pos[0]] * 28*62 + Lower[pos[1]] * 62 + pos[2] - j;
        else if (OffdiagA1H8[pos[2]])
            idx = 6*63*62 + 4*28*62 + (Diag[pos[0]]) * 7*28 + (Diag[pos[1]] - i) * 28 + Lower[pos[2]];
        else
            idx = 6*63*62 + 4*28*62 + 4*7*28 + (Diag[pos[0]] * 7*6) + (Diag[pos[1]] - i) * 6 + (Diag[pos[2]] - j);

        i = 3;
    } else {
        idx = KK_idx[Triangle[pos[0]]][pos[1]];
        i = 2;
    }

    idx *= factor[0];

    while (i < n) {
        int t = norm[i];

        std::sort(&pos[i], &pos[i + t]);

        uint64_t s = 0;

        for (int l = i; l < i + t; ++l) {
            int j = 0;

            for (int k = 0; k < i; ++k)
                j += pos[l] > pos[k];

            s += Binomial[l - i + 1][pos[l] - j];
        }

        idx += s * factor[i];
        i += t;
    }

    return idx;
}

// determine file of leftmost pawn and sort pawns
File pawn_file(uint8_t pawns[], Square *pos)
{
    static const File file_to_file[] = {
        FILE_A, FILE_B, FILE_C, FILE_D, FILE_D, FILE_C, FILE_B, FILE_A
    };

    for (int i = 1; i < pawns[0]; ++i)
        if (Flap[pos[0]] > Flap[pos[i]])
            std::swap(pos[0], pos[i]);

    return file_to_file[pos[0] & 7];
}

uint64_t encode_pawn(uint8_t pawns[], uint8_t *norm, Square *pos, int *factor, int n)
{
    int i;

    if (pos[0] & 4)
        for (i = 0; i < n; ++i)
            pos[i] ^= 7;

    for (i = 1; i < pawns[0]; ++i)
        for (int j = i + 1; j < pawns[0]; ++j)
            if (Ptwist[pos[i]] < Ptwist[pos[j]])
                std::swap(pos[i], pos[j]);

    int t = pawns[0] - 1;
    uint64_t idx = Pawnidx[t][Flap[pos[0]]];

    for (i = t; i > 0; --i)
        idx += Binomial[t - i + 1][Ptwist[pos[i]]];

    idx *= factor[0];

    // remaining pawns
    i = pawns[0];
    t = i + pawns[1];

    if (t > i) {
        std::sort(&pos[i], &pos[t]);

        uint64_t s = 0;

        for (int m = i; m < t; ++m) {
            int j = 0;

            for (int k = 0; k < i; ++k)
                j += pos[m] > pos[k];

            s += Binomial[m - i + 1][pos[m] - j - 8];
        }

        idx += s * factor[i];
        i = t;
    }

    while (i < n) {
        t = norm[i];

        std::sort(&pos[i], &pos[i + t]);

        uint64_t s = 0;

        for (int l = i; l < i + t; ++l) {
            int j = 0;

            for (int k = 0; k < i; ++k)
                j += pos[l] > pos[k];

            s += Binomial[l - i + 1][pos[l] - j];
        }

        idx += s * factor[i];
        i += t;
    }

    return idx;
}

template<typename T>
uint64_t set_factors(T& p, int num, int order)
{
    int n = 64 - p.norm[0];
    uint64_t result = 1;

    for (int i = p.norm[0], k = 0; i < num || k == order; ++k) {
        if (k == order) {
            p.factor[0] = (int)result;
            result *= p.hasUniquePieces ? 31332 : 462;
        } else {
            p.factor[i] = (int)result;
            result *= Binomial[p.norm[i]][n];
            n -= p.norm[i];
            i += p.norm[i];
        }
    }

    return result;
}

uint64_t calc_factors_pawn(int *factor, int num, int order, int order2, uint8_t *norm, File f)
{
    assert(FILE_A <= f && f <= FILE_D);

    int i = norm[0];

    if (order2 < 0x0f)
        i += norm[i];

    int n = 64 - i;
    uint64_t result = 1;

    for (int k = 0; i < num || k == order || k == order2; ++k) {
        if (k == order) {
            factor[0] = (int)result;
            result *= Pfactor[norm[0] - 1][f];
        } else if (k == order2) {
            factor[norm[0]] = (int)result;
            result *= Binomial[norm[norm[0]]][48 - norm[0]];
        } else {
            factor[i] = (int)result;
            result *= Binomial[norm[i]][n];
            n -= norm[i];
            i += norm[i];
        }
    }

    return result;
}

template<typename T>
void set_norms(T& p, int num)
{
    for (int i = 0; i < num; ++i)
        p.norm[i] = 0;

    p.norm[0] = p.hasUniquePieces ? 3 : 2;

    for (int i = p.norm[0]; i < num; i += p.norm[i])
        for (int j = i; j < num && p.pieces[j] == p.pieces[i]; ++j)
            ++p.norm[i];
}

void set_norm_pawn(uint8_t pawns[], uint8_t *norm, uint8_t *pieces, int num)
{
    int i, j;

    for (i = 0; i < num; ++i)
        norm[i] = 0;

    norm[0] = pawns[0];

    if (pawns[1]) norm[pawns[0]] = pawns[1];

    for (i = pawns[0] + pawns[1]; i < num; i += norm[i])
        for (j = i; j < num && pieces[j] == pieces[i]; ++j)
            ++norm[i];
}

void calc_symlen(PairsData *d, int s, char *tmp)
{
    int s1, s2;

    uint8_t* w = d->sympat + 3 * s;
    s2 = (w[2] << 4) | (w[1] >> 4);

    if (s2 == 0x0fff)
        d->symlen[s] = 0;
    else {
        s1 = ((w[1] & 0xf) << 8) | w[0];

        if (!tmp[s1])
            calc_symlen(d, s1, tmp);

        if (!tmp[s2])
            calc_symlen(d, s2, tmp);

        d->symlen[s] = uint8_t(d->symlen[s1] + d->symlen[s2] + 1);
    }

    tmp[s] = 1;
}

uint16_t ReadUshort(uint8_t* d)
{
    return uint16_t(d[0] | (d[1] << 8));
}

uint32_t ReadUint32(uint8_t* d)
{
    return d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
}

PairsData *setup_pairs(uint8_t *data, uint64_t tb_size, uint64_t *size, unsigned char **next, uint8_t *flags, int wdl)
{
    PairsData *d;
    int i;

    *flags = data[0];

    if (data[0] & 0x80) {
        d = (PairsData *)malloc(sizeof(PairsData));
        d->idxbits = 0;

        if (wdl)
            d->min_len = data[1];
        else
            d->min_len = 0;

        *next = data + 2;
        size[0] = size[1] = size[2] = 0;
        return d;
    }

    int blocksize = data[1];
    int idxbits = data[2];
    int real_num_blocks = ReadUint32(&data[4]);
    int num_blocks = real_num_blocks + *(uint8_t *)(&data[3]);
    int max_len = data[8];
    int min_len = data[9];
    int h = max_len - min_len + 1;
    int num_syms = ReadUshort(&data[10 + 2 * h]);
    d = (PairsData *)malloc(sizeof(PairsData) + (h - 1) * sizeof(base_t) + num_syms);
    d->blocksize = blocksize;
    d->idxbits = idxbits;
    d->offset = (uint16_t*)(&data[10]);
    d->symlen = ((uint8_t *)d) + sizeof(PairsData) + (h - 1) * sizeof(base_t);
    d->sympat = &data[12 + 2 * h];
    d->min_len = min_len;
    *next = &data[12 + 2 * h + 3 * num_syms + (num_syms & 1)];

    uint64_t num_indices = (tb_size + (1ULL << idxbits) - 1) >> idxbits;
    size[0] = 6ULL * num_indices;
    size[1] = 2ULL * num_blocks;
    size[2] = (1ULL << blocksize) * real_num_blocks;

    // char tmp[num_syms];
    char tmp[4096];

    for (i = 0; i < num_syms; ++i)
        tmp[i] = 0;

    for (i = 0; i < num_syms; ++i)
        if (!tmp[i])
            calc_symlen(d, i, tmp);

    d->base[h - 1] = 0;

    for (i = h - 2; i >= 0; --i)
        d->base[i] = (d->base[i + 1] + ReadUshort((uint8_t*)(d->offset + i)) - ReadUshort((uint8_t*)(d->offset + i + 1))) / 2;

    for (i = 0; i < h; ++i)
        d->base[i] <<= 64 - (min_len + i);

    d->offset -= d->min_len;

    return d;
}

bool WDLEntry::init(const std::string& fname)
{
    uint8_t* next;
    int s;
    uint64_t tb_size[8];
    uint64_t size[8 * 3];
    uint8_t flags;

    uint8_t* data = TBFile(fname).map(&baseAddress, &mapping, TB_MAGIC);
    if (!data)
        return false;

    int split = *data & 1;
    File maxFile = *data & 2 ? FILE_D : FILE_A;

    data++;

    if (!has_pawns) {

        int order[] = { *data & 0xF, *data >> 4 };

        data++;

        for (int i = 0; i < num; ++i, ++data) {
            piece[0].pieces[i] = *data & 0xF;
            piece[1].pieces[i] = *data >> 4;
        }

        for (int i = 0; i < 2; ++i) {
            set_norms(piece[i], num);
            tb_size[i] = set_factors(piece[i], num, order[i]);
        }

        data += (uintptr_t)data & 1;

        piece[0].precomp = setup_pairs(data, tb_size[0], &size[0], &next, &flags, 1);
        data = next;

        if (split) {
            piece[1].precomp = setup_pairs(data, tb_size[1], &size[3], &next, &flags, 1);
            data = next;
        } else
            piece[1].precomp = nullptr;

        piece[0].precomp->indextable = (char*)data;
        data += size[0];

        if (split) {
            piece[1].precomp->indextable = (char*)data;
            data += size[3];
        }

        piece[0].precomp->sizetable = (uint16_t*)data;
        data += size[1];

        if (split) {
            piece[1].precomp->sizetable = (uint16_t*)data;
            data += size[4];
        }

        data = (uint8_t*)(((uintptr_t)data + 0x3f) & ~0x3F);
        piece[0].precomp->data = data;
        data += size[2];

        if (split) {
            data = (uint8_t*)(((uintptr_t)data + 0x3F) & ~0x3F);
            piece[1].precomp->data = data;
        }
    } else {
        s = 1 + (pawn.pawns[1] > 0);

        for (File f = FILE_A; f <= FILE_D; ++f) {
            int j = 1 + (pawn.pawns[1] > 0);
            int order = data[0] & 0x0f;
            int order2 = pawn.pawns[1] ? (data[1] & 0x0f) : 0x0f;

            for (int i = 0; i < num; ++i)
                pawn.file[f].pieces[0][i] = uint8_t(data[i + j] & 0x0f);

            set_norm_pawn(pawn.pawns, pawn.file[f].norm[0], pawn.file[f].pieces[0], num);
            tb_size[2 * f] = calc_factors_pawn(pawn.file[f].factor[0], num, order, order2, pawn.file[f].norm[0], f);

            order = data[0] >> 4;
            order2 = pawn.pawns[1] ? (data[1] >> 4) : 0x0f;

            for (int i = 0; i < num; ++i)
                pawn.file[f].pieces[1][i] = uint8_t(data[i + j] >> 4);

            set_norm_pawn(pawn.pawns, pawn.file[f].norm[1], pawn.file[f].pieces[1], num);
            tb_size[2 * f + 1] = calc_factors_pawn(pawn.file[f].factor[1], num, order, order2, pawn.file[f].norm[1], f);

            data += num + s;
        }

        data += (uintptr_t)data & 1;

        for (File f = FILE_A; f <= maxFile; ++f) {
            pawn.file[f].precomp[0] = setup_pairs(data, tb_size[2 * f], &size[6 * f], &next, &flags, 1);
            data = next;

            if (split) {
                pawn.file[f].precomp[1] = setup_pairs(data, tb_size[2 * f + 1], &size[6 * f + 3], &next, &flags, 1);
                data = next;
            } else
                pawn.file[f].precomp[1] = nullptr;
        }

        for (File f = FILE_A; f <= maxFile; ++f) {
            pawn.file[f].precomp[0]->indextable = (char *)data;
            data += size[6 * f];

            if (split) {
                pawn.file[f].precomp[1]->indextable = (char *)data;
                data += size[6 * f + 3];
            }
        }

        for (File f = FILE_A; f <= maxFile; ++f) {
            pawn.file[f].precomp[0]->sizetable = (uint16_t *)data;
            data += size[6 * f + 1];

            if (split) {
                pawn.file[f].precomp[1]->sizetable = (uint16_t *)data;
                data += size[6 * f + 4];
            }
        }

        for (File f = FILE_A; f <= maxFile; ++f) {
            data = (uint8_t *)(((uintptr_t)data + 0x3f) & ~0x3f);
            pawn.file[f].precomp[0]->data = data;
            data += size[6 * f + 2];

            if (split) {
                data = (uint8_t *)(((uintptr_t)data + 0x3f) & ~0x3f);
                pawn.file[f].precomp[1]->data = data;
                data += size[6 * f + 5];
            }
        }
    }

    return true;
}

bool DTZEntry::init(const std::string& fname)
{
    uint8_t *next;
    int s;
    uint64_t tb_size[4];
    uint64_t size[4 * 3];

    uint8_t* data = TBFile(fname).map(&baseAddress, &mapping, TB_MAGIC);
    if (!data)
        return false;

    File maxFile = *data & 2 ? FILE_D : FILE_A;

    data++;

    if (!has_pawns) {

        int order = *data & 0xF;

        data++;

        for (int i = 0; i < num; ++i, ++data)
            piece.pieces[i] = *data & 0x0F;

        set_norms(piece, num);
        tb_size[0] = set_factors(piece, num, order);

        data += (uintptr_t)data & 1;

        piece.precomp = setup_pairs(data, tb_size[0], &size[0], &next, &(piece.flags), 0);
        data = next;

        piece.map = data;

        if (piece.flags & 2) {
            int i;

            for (i = 0; i < 4; ++i) {
                piece.map_idx[i] = (uint16_t)(data + 1 - piece.map);
                data += 1 + data[0];
            }

            data += (uintptr_t)data & 1;
        }

        piece.precomp->indextable = (char *)data;
        data += size[0];

        piece.precomp->sizetable = (uint16_t *)data;
        data += size[1];

        data = (uint8_t*)(((uintptr_t)data + 0x3F) & ~0x3F);
        piece.precomp->data = data;
        data += size[2];
    } else {
        s = 1 + (pawn.pawns[1] > 0);

        for (File f = FILE_A; f <= FILE_D; ++f) {
            int j = 1 + (pawn.pawns[1] > 0);
            int order = data[0] & 0x0f;
            int order2 = pawn.pawns[1] ? (data[1] & 0x0f) : 0x0f;

            for (int i = 0; i < num; ++i)
                pawn.file[f].pieces[i] = uint8_t(data[i + j] & 0x0f);

            set_norm_pawn(pawn.pawns, pawn.file[f].norm, pawn.file[f].pieces, num);
            tb_size[f] = calc_factors_pawn(pawn.file[f].factor, num, order, order2, pawn.file[f].norm, f);

            data += num + s;
        }

        data += (uintptr_t)data & 1;

        for (File f = FILE_A; f <= maxFile; ++f) {
            pawn.file[f].precomp = setup_pairs(data, tb_size[f], &size[3 * f], &next, &(pawn.file[f].flags), 0);
            data = next;
        }

        pawn.map = data;

        for (File f = FILE_A; f <= maxFile; ++f) {
            if (pawn.file[f].flags & 2)
                for (int i = 0; i < 4; ++i) {
                    pawn.file[f].map_idx[i] = (uint16_t)(data + 1 - pawn.map);
                    data += 1 + data[0];
                }
        }

        data += (uintptr_t)data & 1;

        for (File f = FILE_A; f <= maxFile; ++f) {
            pawn.file[f].precomp->indextable = (char *)data;
            data += size[3 * f];
        }

        for (File f = FILE_A; f <= maxFile; ++f) {
            pawn.file[f].precomp->sizetable = (uint16_t *)data;
            data += size[3 * f + 1];
        }

        for (File f = FILE_A; f <= maxFile; ++f) {
            data = (uint8_t *)(((uintptr_t)data + 0x3f) & ~0x3f);
            pawn.file[f].precomp->data = data;
            data += size[3 * f + 2];
        }
    }

    return true;
}

template<typename T, int Half = sizeof(T)/2, int End = sizeof(T)-1>
inline void byteSwap(T& x)
{
    char tmp, *c = (char*)(&x);
    for (int i = 0; i < Half; ++i)
        tmp = c[i], c[i] = c[End-i], c[End-i] = tmp;
}

int decompress_pairs(PairsData* d, uint64_t idx)
{
    const union { uint32_t i; char c[4]; } LE = { 0x01020304 };
    const bool LittleEndian = (LE.c[0] == 4);

    if (!d->idxbits)
        return d->min_len;

    // idx = blockidx | litidx where litidx is a signed number of lenght d->idxbits
    uint32_t blockidx = (uint32_t)(idx >> d->idxbits);
    int litidx = (idx & ((1ULL << d->idxbits) - 1)) - (1ULL << (d->idxbits - 1));

    // indextable points to an array of blocks of 6 bytes representing numbers in
    // little endian. The low 4 bytes are the block, the high 2 bytes the idxOffset.
    uint32_t block = *(uint32_t *)(d->indextable + 6 * blockidx);
    uint16_t idxOffset = *(uint16_t *)(d->indextable + 6 * blockidx + 4);

    if (!LittleEndian) {
        byteSwap(block);
        byteSwap(idxOffset);
    }

    litidx += idxOffset;

    while (litidx < 0)
        litidx += d->sizetable[--block] + 1;

    while (litidx > d->sizetable[block])
        litidx -= d->sizetable[block++] + 1;

    uint32_t* ptr = (uint32_t*)(d->data + (block << d->blocksize));
    uint64_t code = *((uint64_t*)ptr);

    if (LittleEndian)
        byteSwap(code);

    int m = d->min_len;
    uint16_t *offset = d->offset;
    base_t* base = d->base - m;
    uint8_t* symlen = d->symlen;
    int sym, bitcnt;

    ptr += 2;
    bitcnt = 0; // number of "empty bits" in code

    for (;;) {
        int l = m;

        while (code < base[l])
            ++l;

        sym = offset[l];

        if (!LittleEndian)
            sym = ((sym & 0xff) << 8) | (sym >> 8);

        sym += (int)((code - base[l]) >> (64 - l));

        if (litidx < (int)symlen[sym] + 1)
            break;

        litidx -= (int)symlen[sym] + 1;
        code <<= l;
        bitcnt += l;

        if (bitcnt >= 32) {
            bitcnt -= 32;
            uint32_t tmp = *ptr++;

            if (LittleEndian)
                byteSwap(tmp);

            code |= (uint64_t)tmp << bitcnt;
        }
    }

    uint8_t *sympat = d->sympat;

    while (symlen[sym] != 0) {
        uint8_t* w = sympat + (3 * sym);
        int s1 = ((w[1] & 0xf) << 8) | w[0];

        if (litidx < (int)symlen[s1] + 1)
            sym = s1;
        else {
            litidx -= (int)symlen[s1] + 1;
            sym = (w[2] << 4) | (w[1] >> 4);
        }
    }

    return sympat[3 * sym];
}

WDLScore probe_wdl_table(Position& pos, int* success)
{
    Key key = pos.material_key();

    if (pos.count<ALL_PIECES>(WHITE) + pos.count<ALL_PIECES>(BLACK) == 2)
        return WDLDraw; // KvK

    WDLEntry* entry = WDLHash[key];
    if (!entry) {
        *success = 0;
        return WDLDraw;
    }

    // Init table at first access attempt
    if (!entry->ready) {
        std::unique_lock<Mutex> lk(TB_mutex);
        if (!entry->ready) {
            std::string fname = file_name(pos, entry->key != key) + ".rtbw";
            if (!entry->init(fname)) {
                // Was ptr2->key = 0ULL;  Just leave !ptr->ready condition
                *success = 0;
                return WDLDraw;
            }
            entry->ready = 1;
        }
    }

    Square squares[TBPIECES];
    int bside, smirror, cmirror;

    assert(key == entry->key || !entry->symmetric);

    // Entries are stored from point of view of white, so in case of a symmetric
    // material distribution, we just need to lookup the relative TB entry in
    // case we are black. Instead in case of asymmetric distribution, because
    // stored entry is the same for both keys, we have first to verify if the
    // entry is stored according to our key, otherwise we have to lookup
    // the relative entry.
    if (entry->symmetric) {
        cmirror = pos.side_to_move() * 8;
        smirror = pos.side_to_move() * 070;
        bside = WHITE;
    } else {
        cmirror = (key != entry->key) * 8;   // Switch color
        smirror = (key != entry->key) * 070; // Vertical flip SQ_A1 -> SQ_A8
        bside   = (key != entry->key) ^ pos.side_to_move();
    }

    // squares[i] is to contain the square 0-63 (A1-H8) for a piece of type
    // pc[i] ^ cmirror, where 1 = white pawn, ..., 14 = black king.
    // Pieces of the same type are guaranteed to be consecutive.
    if (!entry->has_pawns) {
        for (int i = 0; i < entry->num; ) {
            Piece pc = Piece(entry->piece[bside].pieces[i] ^ cmirror);
            Bitboard b = pos.pieces(color_of(pc), type_of(pc));
            do
                squares[i++] = pop_lsb(&b);
            while (b);
        }

        uint64_t idx = encode_piece(entry->piece[bside].hasUniquePieces, entry->piece[bside].norm, squares, entry->piece[bside].factor, entry->num);
        return WDLScore(decompress_pairs(entry->piece[bside].precomp, idx) - 2);
    } else {
        Piece pc = Piece(entry->pawn.file[0].pieces[0][0] ^ cmirror);
        Bitboard b = pos.pieces(color_of(pc), type_of(pc));
        int i = 0;
        do
            squares[i++] = pop_lsb(&b) ^ smirror;
        while (b);

        File f = pawn_file(entry->pawn.pawns, squares);

        for ( ; i < entry->num; ) {
            pc = Piece(entry->pawn.file[f].pieces[bside][i] ^ cmirror);
            b = pos.pieces(color_of(pc), type_of(pc));
            do
                squares[i++] = pop_lsb(&b) ^ smirror;
            while (b);
        }

        uint64_t idx = encode_pawn(entry->pawn.pawns, entry->pawn.file[f].norm[bside], squares, entry->pawn.file[f].factor[bside], entry->num);
        return WDLScore(decompress_pairs(entry->pawn.file[f].precomp[bside], idx) - 2);
    }
}

int probe_dtz_table(const Position& pos, int wdl, int *success)
{
    Key key = pos.material_key();

    if (   DTZTable.front().keys[0] != key
        && DTZTable.front().keys[1] != key) {

        // Enforce "Most Recently Used" (MRU) order for DTZ_list
        for (auto it = DTZTable.begin(); it != DTZTable.end(); ++it)
            if (it->keys[0] == key) {
                // Move to front without deleting the element
                DTZTable.splice(DTZTable.begin(),DTZTable, it);
                break;
            }

        // If still not found, add a new one
        if (DTZTable.front().keys[0] != key) {

            WDLEntry* ptr = WDLHash[key];
            if (!ptr) {
                *success = 0;
                return 0;
            }

            StateInfo st;
            Position p;
            std::string code = file_name(pos, ptr->key != key);
            std::string fname = code + ".rtbz";
            code.erase(code.find('v'), 1);

            Key keys[] = { p.set(code, WHITE, &st).material_key(),
                           p.set(code, BLACK, &st).material_key() };

            DTZTable.push_front(DTZEntry(*ptr, keys));

            if (!DTZTable.front().init(fname)) {
                // In case file is not found init() fails, but we leave
                // the entry so to avoid rechecking at every probe (same
                // functionality as WDL case).
                // FIXME: This is different form original functionality!
                /* DTZTable.pop_front(); */
                *success = 0;
                return 0;
            }

            // Keep list size within 64 entries
            // FIXME remove it when we will know what we are doing
            if (DTZTable.size() > 64)
               DTZTable.pop_back();
        }
    }

    DTZEntry* ptr = &DTZTable.front();

    if (!ptr->baseAddress) {
        *success = 0;
        return 0;
    }

    uint64_t idx;
    int i, res;
    Square squares[TBPIECES];
    int bside, mirror, cmirror;

    if (!ptr->symmetric) {
        if (key != ptr->key) {
            cmirror = 8;
            mirror = 070;
            bside = (pos.side_to_move() == WHITE);
        } else {
            cmirror = mirror = 0;
            bside = !(pos.side_to_move() == WHITE);
        }
    } else {
        cmirror = pos.side_to_move() == WHITE ? 0 : 8;
        mirror = pos.side_to_move() == WHITE ? 0 : 070;
        bside = 0;
    }

    if (!ptr->has_pawns) {
        if ((ptr->piece.flags & 1) != bside && !ptr->symmetric) {
            *success = -1;
            return 0;
        }

        uint8_t *pc = ptr->piece.pieces;

        for (i = 0; i < ptr->num;) {
            Bitboard bb = pos.pieces((Color)((pc[i] ^ cmirror) >> 3),
                                     (PieceType)(pc[i] & 7));

            do {
                squares[i++] = pop_lsb(&bb);
            } while (bb);
        }

        idx = encode_piece(ptr->piece.hasUniquePieces, ptr->piece.norm, squares, ptr->piece.factor, ptr->num);
        res = decompress_pairs(ptr->piece.precomp, idx);

        if (ptr->piece.flags & 2)
            res = ptr->piece.map[ptr->piece.map_idx[wdl_to_map[wdl + 2]] + res];

        if (!(ptr->piece.flags & pa_flags[wdl + 2]) || (wdl & 1))
            res *= 2;
    } else {
        int k = ptr->pawn.file[0].pieces[0] ^ cmirror;
        Bitboard bb = pos.pieces((Color)(k >> 3), (PieceType)(k & 7));
        i = 0;

        do {
            squares[i++] = pop_lsb(&bb) ^ mirror;
        } while (bb);

        File f = pawn_file(ptr->pawn.pawns, squares);

        if ((ptr->pawn.file[f].flags & 1) != bside) {
            *success = -1;
            return 0;
        }

        uint8_t *pc = ptr->pawn.file[f].pieces;

        for (; i < ptr->num;) {
            bb = pos.pieces((Color)((pc[i] ^ cmirror) >> 3),
                            (PieceType)(pc[i] & 7));

            do {
                squares[i++] = pop_lsb(&bb) ^ mirror;
            } while (bb);
        }

        idx = encode_pawn(ptr->pawn.pawns, ptr->pawn.file[f].norm, squares, ptr->pawn.file[f].factor, ptr->num);
        res = decompress_pairs(ptr->pawn.file[f].precomp, idx);

        if (ptr->pawn.file[f].flags & 2)
            res = ptr->pawn.map[ptr->pawn.file[f].map_idx[wdl_to_map[wdl + 2]] + res];

        if (!(ptr->pawn.file[f].flags & pa_flags[wdl + 2]) || (wdl & 1))
            res *= 2;
    }

    return res;
}

// Add underpromotion captures to list of captures.
ExtMove *add_underprom_caps(Position& pos, ExtMove *stack, ExtMove *end)
{
    ExtMove *moves, *extra = end;

    for (moves = stack; moves < end; ++moves) {
        Move move = moves->move;

        if (type_of(move) == PROMOTION && !pos.empty(to_sq(move))) {
            (*extra++).move = (Move)(move - (1 << 12));
            (*extra++).move = (Move)(move - (2 << 12));
            (*extra++).move = (Move)(move - (3 << 12));
        }
    }

    return extra;
}

WDLScore probe_ab(Position& pos, WDLScore alpha, WDLScore beta, int *success)
{
    WDLScore value;
    ExtMove stack[64];
    ExtMove *moves, *end;
    StateInfo st;

    // Generate (at least) all legal non-ep captures including (under)promotions.
    // It is OK to generate more, as long as they are filtered out below.
    if (!pos.checkers()) {
        end = generate<CAPTURES>(pos, stack);
        // Since underpromotion captures are not included, we need to add them.
        end = add_underprom_caps(pos, stack, end);
    } else
        end = generate<EVASIONS>(pos, stack);

    CheckInfo ci(pos);

    for (moves = stack; moves < end; ++moves) {
        Move capture = moves->move;

        if (   !pos.capture(capture)
            ||  type_of(capture) == ENPASSANT
            || !pos.legal(capture, ci.pinned))
            continue;

        pos.do_move(capture, st, pos.gives_check(capture, ci));
        value = -probe_ab(pos, -beta, -alpha, success);
        pos.undo_move(capture);

        if (*success == 0)
            return WDLDraw;

        if (value > alpha) {
            if (value >= beta) {
                *success = 2;
                return value;
            }

            alpha = value;
        }
    }

    value = probe_wdl_table(pos, success); // FIXME why this is not at the beginning?

    if (*success == 0)
        return WDLDraw;

    if (alpha >= value) {
        *success = 1 + (alpha > 0);
        return alpha;
    } else {
        *success = 1;
        return value;
    }
}

int probe_dtz(Position& pos, int *success);

// This routine treats a position with en passant captures as one without.
int probe_dtz_no_ep(Position& pos, int *success)
{
    int dtz;

    WDLScore wdl = probe_ab(pos, WDLHardLoss, WDLHardWin, success);

    if (*success == 0) return 0;

    if (wdl == WDLDraw) return 0;

    if (*success == 2)
        return wdl == WDLHardWin ? 1 : 101;

    ExtMove stack[MAX_MOVES];
    ExtMove *moves, *end = nullptr;
    StateInfo st;
    CheckInfo ci(pos);

    if (wdl > 0) {
        // Generate at least all legal non-capturing pawn moves
        // including non-capturing promotions.
        if (!pos.checkers())
            end = generate<NON_EVASIONS>(pos, stack);
        else
            end = generate<EVASIONS>(pos, stack);

        for (moves = stack; moves < end; ++moves) {
            Move move = moves->move;

            if (   type_of(pos.moved_piece(move)) != PAWN
                || pos.capture(move)
                || !pos.legal(move, ci.pinned))
                continue;

            pos.do_move(move, st, pos.gives_check(move, ci));
            WDLScore v = -probe_ab(pos, WDLHardLoss, -wdl + WDLSoftWin, success);
            pos.undo_move(move);

            if (*success == 0) return 0;

            if (v == wdl)
                return v == WDLHardWin ? 1 : 101;
        }
    }

    dtz = 1 + probe_dtz_table(pos, wdl, success);

    if (*success >= 0) {
        if (wdl & 1) dtz += 100;

        return wdl >= 0 ? dtz : -dtz;
    }

    if (wdl > 0) {
        int best = 0xffff;

        for (moves = stack; moves < end; ++moves) {
            Move move = moves->move;

            if (pos.capture(move) || type_of(pos.moved_piece(move)) == PAWN
                    || !pos.legal(move, ci.pinned))
                continue;

            pos.do_move(move, st, pos.gives_check(move, ci));
            int v = -probe_dtz(pos, success);
            pos.undo_move(move);

            if (*success == 0)
                return 0;

            if (v > 0 && v + 1 < best)
                best = v + 1;
        }

        return best;
    } else {
        int best = -1;

        if (!pos.checkers())
            end = generate<NON_EVASIONS>(pos, stack);
        else
            end = generate<EVASIONS>(pos, stack);

        for (moves = stack; moves < end; ++moves) {
            int v;
            Move move = moves->move;

            if (!pos.legal(move, ci.pinned))
                continue;

            pos.do_move(move, st, pos.gives_check(move, ci));

            if (st.rule50 == 0) {
                if (wdl == -2) v = -1;
                else {
                    v = probe_ab(pos, WDLSoftWin, WDLHardWin, success);
                    v = (v == 2) ? 0 : -101;
                }
            } else {
                v = -probe_dtz(pos, success) - 1;
            }

            pos.undo_move(move);

            if (*success == 0)
                return 0;

            if (v < best)
                best = v;
        }

        return best;
    }
}

// Probe the DTZ table for a particular position.
// If *success != 0, the probe was successful.
// The return value is from the point of view of the side to move:
//         n < -100 : loss, but draw under 50-move rule
// -100 <= n < -1   : loss in n ply (assuming 50-move counter == 0)
//         0        : draw
//     1 < n <= 100 : win in n ply (assuming 50-move counter == 0)
//   100 < n        : win, but draw under 50-move rule
//
// The return value n can be off by 1: a return value -n can mean a loss
// in n+1 ply and a return value +n can mean a win in n+1 ply. This
// cannot happen for tables with positions exactly on the "edge" of
// the 50-move rule.
//
// This implies that if dtz > 0 is returned, the position is certainly
// a win if dtz + 50-move-counter <= 99. Care must be taken that the engine
// picks moves that preserve dtz + 50-move-counter <= 99.
//
// If n = 100 immediately after a capture or pawn move, then the position
// is also certainly a win, and during the whole phase until the next
// capture or pawn move, the inequality to be preserved is
// dtz + 50-movecounter <= 100.
//
// In short, if a move is available resulting in dtz + 50-move-counter <= 99,
// then do not accept moves leading to dtz + 50-move-counter == 100.
//
int probe_dtz(Position& pos, int *success)
{
    *success = 1;
    int v = probe_dtz_no_ep(pos, success);

    if (pos.ep_square() == SQ_NONE)
        return v;

    if (*success == 0)
        return 0;

    // Now handle en passant.
    int v1 = -3;

    ExtMove stack[MAX_MOVES];
    ExtMove *moves, *end;
    StateInfo st;

    if (!pos.checkers())
        end = generate<CAPTURES>(pos, stack);
    else
        end = generate<EVASIONS>(pos, stack);

    CheckInfo ci(pos);

    for (moves = stack; moves < end; ++moves) {
        Move capture = moves->move;

        if (type_of(capture) != ENPASSANT
                || !pos.legal(capture, ci.pinned))
            continue;

        pos.do_move(capture, st, pos.gives_check(capture, ci));
        WDLScore v0 = -probe_ab(pos, WDLHardLoss, WDLHardWin, success);
        pos.undo_move(capture);

        if (*success == 0)
            return 0;

        if (v0 > v1) v1 = v0;
    }

    if (v1 > -3) {
        v1 = wdl_to_dtz[v1 + 2];

        if (v < -100) {
            if (v1 >= 0)
                v = v1;
        } else if (v < 0) {
            if (v1 >= 0 || v1 < -100)
                v = v1;
        } else if (v > 100) {
            if (v1 > 0)
                v = v1;
        } else if (v > 0) {
            if (v1 == 1)
                v = v1;
        } else if (v1 >= 0) {
            v = v1;
        } else {
            for (moves = stack; moves < end; ++moves) {
                Move move = moves->move;

                if (type_of(move) == ENPASSANT) continue;

                if (pos.legal(move, ci.pinned))
                    break;
            }

            if (moves == end && !pos.checkers()) {
                end = generate<QUIETS>(pos, end);

                for (; moves < end; ++moves) {
                    Move move = moves->move;

                    if (pos.legal(move, ci.pinned))
                        break;
                }
            }

            if (moves == end)
                v = v1;
        }
    }

    return v;
}

} // namespace

void Tablebases::init(const std::string& paths)
{
    DTZTable.clear();
    WDLTable.clear();
    WDLHash.clear();

    MaxCardinality = 0;
    TBPaths = paths;

    if (TBPaths.empty() || TBPaths == "<empty>")
        return;

    // Fill binomial[] with the Binomial Coefficents using Pascal triangle
    Binomial[0][0] = 1;

    for (int n = 1; n < 64; n++)
        for (int k = 0; k < 6 && k <= n; ++k)
            Binomial[k][n] = (k > 0 ? Binomial[k-1][n-1] : 0)
                           + (k < n ? Binomial[k][n-1] : 0);

    for (int i = 0; i < 5; ++i) {
        int k = 0;

        for (int j = 1; j <= 4; ++j) {
            int s = 0;

            for ( ; k < 6 * j; ++k) {
                Pawnidx[i][k] = s;
                s += Binomial[i][Ptwist[Invflap[k]]];
            }

            Pfactor[i][j - 1] = s;
        }
    }

    for (PieceType p1 = PAWN; p1 < KING; ++p1) {
        WDLHash.insert({KING, p1, KING});

        for (PieceType p2 = PAWN; p2 <= p1; ++p2) {
            WDLHash.insert({KING, p1, p2, KING});
            WDLHash.insert({KING, p1, KING, p2});

            for (PieceType p3 = PAWN; p3 < KING; ++p3)
                WDLHash.insert({KING, p1, p2, KING, p3});

            for (PieceType p3 = PAWN; p3 <= p2; ++p3) {
                WDLHash.insert({KING, p1, p2, p3, KING});

                for (PieceType p4 = PAWN; p4 <= p3; ++p4)
                    WDLHash.insert({KING, p1, p2, p3, p4, KING});

                for (PieceType p4 = PAWN; p4 < KING; ++p4)
                    WDLHash.insert({KING, p1, p2, p3, KING, p4});
            }

            for (PieceType p3 = PAWN; p3 <= p1; ++p3)
                for (PieceType p4 = PAWN; p4 <= (p1 == p3 ? p2 : p3); ++p4)
                    WDLHash.insert({KING, p1, p2, KING, p3, p4});
        }
    }

    std::cerr << "info string Found " << WDLTable.size() << " tablebases" << std::endl;
}

// Probe the WDL table for a particular position.
// If *success != 0, the probe was successful.
// The return value is from the point of view of the side to move:
// -2 : loss
// -1 : loss, but draw under 50-move rule
//  0 : draw
//  1 : win, but draw under 50-move rule
//  2 : win
WDLScore Tablebases::probe_wdl(Position& pos, int *success)
{
    *success = 1;
    WDLScore v = probe_ab(pos, WDLHardLoss, WDLHardWin, success);

    // If en passant is not possible, we are done.
    if (pos.ep_square() == SQ_NONE)
        return v;

    if (*success == 0)
        return WDLDraw;

    // Now handle en passant.
    WDLScore v1 = WDLScore(-3); // FIXME use a proper enum value here
    // Generate (at least) all legal en passant captures.
    ExtMove stack[MAX_MOVES];
    ExtMove *moves, *end;
    StateInfo st;

    if (!pos.checkers())
        end = generate<CAPTURES>(pos, stack);
    else
        end = generate<EVASIONS>(pos, stack);

    CheckInfo ci(pos);

    for (moves = stack; moves < end; ++moves) {
        Move capture = moves->move;

        if (type_of(capture) != ENPASSANT
                || !pos.legal(capture, ci.pinned))
            continue;

        pos.do_move(capture, st, pos.gives_check(capture, ci));
        WDLScore v0 = -probe_ab(pos, WDLHardLoss, WDLHardWin, success);
        pos.undo_move(capture);

        if (*success == 0)
            return WDLDraw;

        if (v0 > v1) v1 = v0;
    }

    if (v1 > -3) {
        if (v1 >= v) v = v1;
        else if (v == 0) {
            // Check whether there is at least one legal non-ep move.
            for (moves = stack; moves < end; ++moves) {
                Move capture = moves->move;

                if (type_of(capture) == ENPASSANT) continue;

                if (pos.legal(capture, ci.pinned))
                    break;
            }

            if (moves == end && !pos.checkers()) {
                end = generate<QUIETS>(pos, end);

                for (; moves < end; ++moves) {
                    Move move = moves->move;

                    if (pos.legal(move, ci.pinned))
                        break;
                }
            }

            // If not, then we are forced to play the losing ep capture.
            if (moves == end)
                v = v1;
        }
    }

    return v;
}

// Check whether there has been at least one repetition of positions
// since the last capture or pawn move.
static int has_repeated(StateInfo *st)
{
    while (1) {
        int i = 4, e = std::min(st->rule50, st->pliesFromNull);

        if (e < i)
            return 0;

        StateInfo *stp = st->previous->previous;

        do {
            stp = stp->previous->previous;

            if (stp->key == st->key)
                return 1;

            i += 2;
        } while (i <= e);

        st = st->previous;
    }
}

// Use the DTZ tables to filter out moves that don't preserve the win or draw.
// If the position is lost, but DTZ is fairly high, only keep moves that
// maximise DTZ.
//
// A return value false indicates that not all probes were successful and that
// no moves were filtered out.
bool Tablebases::root_probe(Position& pos, Search::RootMoves& rootMoves, Value& score)
{
    int success;
    int dtz = probe_dtz(pos, &success);

    if (!success)
        return false;

    StateInfo st;
    CheckInfo ci(pos);

    // Probe each move
    for (size_t i = 0; i < rootMoves.size(); ++i) {
        Move move = rootMoves[i].pv[0];
        pos.do_move(move, st, pos.gives_check(move, ci));
        int v = 0;

        if (pos.checkers() && dtz > 0) {
            ExtMove s[MAX_MOVES];

            if (generate<LEGAL>(pos, s) == s)
                v = 1;
        }

        if (!v) {
            if (st.rule50 != 0) {
                v = -probe_dtz(pos, &success);

                if (v > 0)
                    ++v;
                else if (v < 0)
                    --v;
            } else {
                v = -probe_wdl(pos, &success);
                v = wdl_to_dtz[v + 2];
            }
        }

        pos.undo_move(move);

        if (!success)
            return false;

        rootMoves[i].score = (Value)v;
    }

    // Obtain 50-move counter for the root position.
    // In Stockfish there seems to be no clean way, so we do it like this:
    int cnt50 = st.previous->rule50;

    // Use 50-move counter to determine whether the root position is
    // won, lost or drawn.
    int wdl = 0;

    if (dtz > 0)
        wdl = (dtz + cnt50 <= 100) ? 2 : 1;
    else if (dtz < 0)
        wdl = (-dtz + cnt50 <= 100) ? -2 : -1;

    // Determine the score to report to the user.
    score = WDL_to_value[wdl + 2];

    // If the position is winning or losing, but too few moves left, adjust the
    // score to show how close it is to winning or losing.
    // NOTE: int(PawnValueEg) is used as scaling factor in score_to_uci().
    if (wdl == 1 && dtz <= 100)
        score = (Value)(((200 - dtz - cnt50) * int(PawnValueEg)) / 200);
    else if (wdl == -1 && dtz >= -100)
        score = -(Value)(((200 + dtz - cnt50) * int(PawnValueEg)) / 200);

    // Now be a bit smart about filtering out moves.
    size_t j = 0;

    if (dtz > 0) { // winning (or 50-move rule draw)
        int best = 0xffff;

        for (size_t i = 0; i < rootMoves.size(); ++i) {
            int v = rootMoves[i].score;

            if (v > 0 && v < best)
                best = v;
        }

        int max = best;

        // If the current phase has not seen repetitions, then try all moves
        // that stay safely within the 50-move budget, if there are any.
        if (!has_repeated(st.previous) && best + cnt50 <= 99)
            max = 99 - cnt50;

        for (size_t i = 0; i < rootMoves.size(); ++i) {
            int v = rootMoves[i].score;

            if (v > 0 && v <= max)
                rootMoves[j++] = rootMoves[i];
        }
    } else if (dtz < 0) { // losing (or 50-move rule draw)
        int best = 0;

        for (size_t i = 0; i < rootMoves.size(); ++i) {
            int v = rootMoves[i].score;

            if (v < best)
                best = v;
        }

        // Try all moves, unless we approach or have a 50-move rule draw.
        if (-best * 2 + cnt50 < 100)
            return true;

        for (size_t i = 0; i < rootMoves.size(); ++i) {
            if (rootMoves[i].score == best)
                rootMoves[j++] = rootMoves[i];
        }
    } else { // drawing
        // Try all moves that preserve the draw.
        for (size_t i = 0; i < rootMoves.size(); ++i) {
            if (rootMoves[i].score == 0)
                rootMoves[j++] = rootMoves[i];
        }
    }

    rootMoves.resize(j, Search::RootMove(MOVE_NONE));

    return true;
}

// Use the WDL tables to filter out moves that don't preserve the win or draw.
// This is a fallback for the case that some or all DTZ tables are missing.
//
// A return value false indicates that not all probes were successful and that
// no moves were filtered out.
bool Tablebases::root_probe_wdl(Position& pos, Search::RootMoves& rootMoves, Value& score)
{
    int success;

    WDLScore wdl = Tablebases::probe_wdl(pos, &success);

    if (!success)
        return false;

    score = WDL_to_value[wdl + 2];

    StateInfo st;
    CheckInfo ci(pos);

    int best = WDLHardLoss;

    // Probe each move
    for (size_t i = 0; i < rootMoves.size(); ++i) {
        Move move = rootMoves[i].pv[0];
        pos.do_move(move, st, pos.gives_check(move, ci));
        WDLScore v = -Tablebases::probe_wdl(pos, &success);
        pos.undo_move(move);

        if (!success)
            return false;

        rootMoves[i].score = (Value)v;

        if (v > best)
            best = v;
    }

    size_t j = 0;

    for (size_t i = 0; i < rootMoves.size(); ++i) {
        if (rootMoves[i].score == best)
            rootMoves[j++] = rootMoves[i];
    }

    rootMoves.resize(j, Search::RootMove(MOVE_NONE));

    return true;
}
