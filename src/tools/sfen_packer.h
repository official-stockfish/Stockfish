#ifndef _SFEN_PACKER_H_
#define _SFEN_PACKER_H_

#include "types.h"

#include "packed_sfen.h"

#include <cstdint>

namespace Stockfish {
    class Position;
    struct StateInfo;
    class Thread;
}

namespace Stockfish::Tools {

    int set_from_packed_sfen(Position& pos, const PackedSfen& sfen, StateInfo* si, Thread* th);
    PackedSfen sfen_pack(Position& pos);
}

#endif