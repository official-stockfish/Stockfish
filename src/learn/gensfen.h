#ifndef _GENSFEN_H_
#define _GENSFEN_H_

#include <sstream>

#include "../position.h"

#if defined(EVAL_LEARN)
namespace Learner {

    // Automatic generation of teacher position
    void gen_sfen(Position& pos, std::istringstream& is);
}
#endif

#endif