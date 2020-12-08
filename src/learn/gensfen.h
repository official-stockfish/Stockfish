#ifndef _GENSFEN_H_
#define _GENSFEN_H_

#include "position.h"

#include <sstream>

namespace Learner {

    // Automatic generation of teacher position
    void gensfen(std::istringstream& is);
}

#endif