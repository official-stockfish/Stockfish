#ifndef _CONVERT_H_
#define _CONVERT_H_

#include <vector>
#include <string>
#include <sstream>

namespace Stockfish::Tools {
    void convert(std::istringstream& is);

    void convert_bin_from_pgn_extract(std::istringstream& is);

    void convert_bin(std::istringstream& is);

    void convert_plain(std::istringstream& is);
}

#endif
