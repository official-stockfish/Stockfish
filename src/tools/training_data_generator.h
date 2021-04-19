#ifndef _GENSFEN_H_
#define _GENSFEN_H_

#include "position.h"

#include <sstream>

namespace Stockfish::Tools {

    // Automatic generation of teacher position
    void generate_training_data(std::istringstream& is);
}

#endif