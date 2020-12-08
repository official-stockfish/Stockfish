#ifndef LEARN_OPENING_BOOK_H
#define LEARN_OPENING_BOOK_H

#include "misc.h"
#include "position.h"
#include "thread.h"

#include <vector>
#include <random>
#include <optional>
#include <string>
#include <cstdint>
#include <memory>

namespace Learner {

    struct OpeningBook {

        const std::string& next_fen()
        {
            assert(fens.size() > 0);

            auto& fen = fens[current_index++];
            if (current_index >= fens.size())
                current_index = 0;

            return fen;
        }

        std::size_t size() const { return fens.size(); }

        const std::string& get_filename() const { return filename; }

    protected:
        OpeningBook(const std::string& file) :
            filename(file),
            current_index(0)
        {
        }


        std::string filename;
        std::vector<std::string> fens;
        std::size_t current_index;
    };

    struct EpdOpeningBook : OpeningBook {

        EpdOpeningBook(const std::string& file, PRNG& prng);
    };

    std::unique_ptr<OpeningBook> open_opening_book(const std::string& filename, PRNG& prng);

}

#endif
