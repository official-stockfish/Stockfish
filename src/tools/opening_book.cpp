#include "opening_book.h"

#include <fstream>

namespace Stockfish::Tools {

    EpdOpeningBook::EpdOpeningBook(const std::string& file, PRNG& prng) :
        OpeningBook(file)
    {
        std::ifstream in(file);
        if (!in)
        {
            return;
        }

        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty())
                continue;

            fens.emplace_back(line);
        }

        Algo::shuffle(fens, prng);
    }

    static bool ends_with(const std::string& lhs, const std::string& end)
    {
        if (end.size() > lhs.size()) return false;

        return std::equal(end.rbegin(), end.rend(), lhs.rbegin());
    }

    std::unique_ptr<OpeningBook> open_opening_book(const std::string& filename, PRNG& prng)
    {
        if (ends_with(filename, ".epd"))
            return std::make_unique<EpdOpeningBook>(filename, prng);

        return nullptr;
    }

}
