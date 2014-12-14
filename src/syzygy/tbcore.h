/*
  Copyright (c) 2011-2013 Ronald de Man
*/

#ifndef TBCORE_H
#define TBCORE_H

#ifndef _WIN32
#include <pthread.h>
#define SEP_CHAR ':'
#define FD int
#define FD_ERR -1
#else
#include <windows.h>
#define SEP_CHAR ';'
#define FD HANDLE
#define FD_ERR INVALID_HANDLE_VALUE
#endif

#ifndef _WIN32
#define LOCK_T pthread_mutex_t
#define LOCK_INIT(x) pthread_mutex_init(&(x), NULL)
#define LOCK(x) pthread_mutex_lock(&(x))
#define UNLOCK(x) pthread_mutex_unlock(&(x))
#else
#define LOCK_T HANDLE
#define LOCK_INIT(x) do { x = CreateMutex(NULL, FALSE, NULL); } while (0)
#define LOCK(x) WaitForSingleObject(x, INFINITE)
#define UNLOCK(x) ReleaseMutex(x)
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

typedef unsigned long long uint64;
typedef unsigned int uint32;
typedef unsigned char ubyte;
typedef unsigned short ushort;

const ubyte WDL_MAGIC[4] = { 0x71, 0xe8, 0x23, 0x5d };
const ubyte DTZ_MAGIC[4] = { 0xd7, 0x66, 0x0c, 0xa5 };

#define TBHASHBITS 10

struct TBHashEntry;

typedef uint64 base_t;

struct PairsData {
  char *indextable;
  ushort *sizetable;
  ubyte *data;
  ushort *offset;
  ubyte *symlen;
  ubyte *sympat;
  int blocksize;
  int idxbits;
  int min_len;
  base_t base[1]; // C++ complains about base[]...
};

struct TBEntry {
  char *data;
  uint64 key;
  uint64 mapping;
  ubyte ready;
  ubyte num;
  ubyte symmetric;
  ubyte has_pawns;
}
#ifndef _WIN32
__attribute__((__may_alias__))
#endif
;

struct TBEntry_piece {
  char *data;
  uint64 key;
  uint64 mapping;
  ubyte ready;
  ubyte num;
  ubyte symmetric;
  ubyte has_pawns;
  ubyte enc_type;
  struct PairsData *precomp[2];
  int factor[2][TBPIECES];
  ubyte pieces[2][TBPIECES];
  ubyte norm[2][TBPIECES];
};

struct TBEntry_pawn {
  char *data;
  uint64 key;
  uint64 mapping;
  ubyte ready;
  ubyte num;
  ubyte symmetric;
  ubyte has_pawns;
  ubyte pawns[2];
  struct {
    struct PairsData *precomp[2];
    int factor[2][TBPIECES];
    ubyte pieces[2][TBPIECES];
    ubyte norm[2][TBPIECES];
  } file[4];
};

struct DTZEntry_piece {
  char *data;
  uint64 key;
  uint64 mapping;
  ubyte ready;
  ubyte num;
  ubyte symmetric;
  ubyte has_pawns;
  ubyte enc_type;
  struct PairsData *precomp;
  int factor[TBPIECES];
  ubyte pieces[TBPIECES];
  ubyte norm[TBPIECES];
  ubyte flags; // accurate, mapped, side
  ushort map_idx[4];
  ubyte *map;
};

struct DTZEntry_pawn {
  char *data;
  uint64 key;
  uint64 mapping;
  ubyte ready;
  ubyte num;
  ubyte symmetric;
  ubyte has_pawns;
  ubyte pawns[2];
  struct {
    struct PairsData *precomp;
    int factor[TBPIECES];
    ubyte pieces[TBPIECES];
    ubyte norm[TBPIECES];
  } file[4];
  ubyte flags[4];
  ushort map_idx[4][4];
  ubyte *map;
};

struct TBHashEntry {
  uint64 key;
  struct TBEntry *ptr;
};

struct DTZTableEntry {
  uint64 key1;
  uint64 key2;
  struct TBEntry *entry;
};

#endif

