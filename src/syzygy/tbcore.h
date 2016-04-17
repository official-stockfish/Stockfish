/*
  Copyright (c) 2011-2013 Ronald de Man
*/

#ifndef TBCORE_H
#define TBCORE_H

#include <cstdint>

#ifndef _WIN32
#define SEP_CHAR ':'
#define FD int
#define FD_ERR -1
#else
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define SEP_CHAR ';'
#define FD HANDLE
#define FD_ERR INVALID_HANDLE_VALUE
#endif

#ifndef _MSC_VER
#define BSWAP32(v) __builtin_bswap32(v)
#define BSWAP64(v) __builtin_bswap64(v)
#else
#define BSWAP32(v) _byteswap_ulong(v)
#define BSWAP64(v) _byteswap_uint64(v)
#endif

#define WDLSUFFIX ".rtbw"
#define DTZSUFFIX ".rtbz"
#define WDLDIR "RTBWDIR"
#define DTZDIR "RTBZDIR"
#define TBPIECES 6

const uint8_t WDL_MAGIC[4] = { 0x71, 0xe8, 0x23, 0x5d };
const uint8_t DTZ_MAGIC[4] = { 0xd7, 0x66, 0x0c, 0xa5 };

#define TBHASHBITS 10

struct TBHashEntry;

typedef uint64_t base_t;

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

struct TBEntry {
    char *data;
    uint64_t key;
    uint64_t mapping;
    uint8_t ready;
    uint8_t num;
    uint8_t symmetric;
    uint8_t has_pawns;
}
#ifndef _WIN32
__attribute__((__may_alias__))
#endif
;

struct TBEntry_piece {
    char *data;
    uint64_t key;
    uint64_t mapping;
    uint8_t ready;
    uint8_t num;
    uint8_t symmetric;
    uint8_t has_pawns;
    uint8_t enc_type;
    struct PairsData *precomp[2];
    int factor[2][TBPIECES];
    uint8_t pieces[2][TBPIECES];
    uint8_t norm[2][TBPIECES];
};

struct TBEntry_pawn {
    char *data;
    uint64_t key;
    uint64_t mapping;
    uint8_t ready;
    uint8_t num;
    uint8_t symmetric;
    uint8_t has_pawns;
    uint8_t pawns[2];
    struct {
        struct PairsData *precomp[2];
        int factor[2][TBPIECES];
        uint8_t pieces[2][TBPIECES];
        uint8_t norm[2][TBPIECES];
    } file[4];
};

struct DTZEntry_piece {
    char *data;
    uint64_t key;
    uint64_t mapping;
    uint8_t ready;
    uint8_t num;
    uint8_t symmetric;
    uint8_t has_pawns;
    uint8_t enc_type;
    struct PairsData *precomp;
    int factor[TBPIECES];
    uint8_t pieces[TBPIECES];
    uint8_t norm[TBPIECES];
    uint8_t flags; // accurate, mapped, side
    uint16_t map_idx[4];
    uint8_t *map;
};

struct DTZEntry_pawn {
    char *data;
    uint64_t key;
    uint64_t mapping;
    uint8_t ready;
    uint8_t num;
    uint8_t symmetric;
    uint8_t has_pawns;
    uint8_t pawns[2];
    struct {
        struct PairsData *precomp;
        int factor[TBPIECES];
        uint8_t pieces[TBPIECES];
        uint8_t norm[TBPIECES];
    } file[4];
    uint8_t flags[4];
    uint16_t map_idx[4][4];
    uint8_t *map;
};

struct TBHashEntry {
    uint64_t key;
    struct TBEntry *ptr;
};

struct DTZTableEntry {
    uint64_t key1;
    uint64_t key2;
    struct TBEntry *entry;
};

#endif

