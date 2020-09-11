#ifndef _GENSFEN_H_
#define _GENSFEN_H_

#include "position.h"

#include <sstream>

#if defined(EVAL_LEARN)
namespace Learner {

    // Automatic generation of teacher position
    void gen_sfen(Position& pos, std::istringstream& is);
}
#endif

#endif