#ifndef _SFEN_PACKER_H_
#define _SFEN_PACKER_H_

#if defined(EVAL_LEARN)

#include <cstdint>

#include "../types.h"

#include "../learn/packed_sfen.h"
class Position;
struct StateInfo;
class Thread;

namespace Learner {

    int set_from_packed_sfen(Position& pos, const PackedSfen& sfen, StateInfo* si, Thread* th, bool mirror);
    PackedSfen sfen_pack(Position& pos);
}

#endif

#endif