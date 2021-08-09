#include "validate_training_data.h"

#include "uci.h"
#include "misc.h"
#include "thread.h"
#include "position.h"
#include "tt.h"

#include "extra/nnue_data_binpack_format.h"

#include "nnue/evaluate_nnue.h"

#include "syzygy/tbprobe.h"

#include <sstream>
#include <fstream>
#include <unordered_set>
#include <iomanip>
#include <list>
#include <cmath>    // std::exp(),std::pow(),std::log()
#include <cstring>  // memcpy()
#include <memory>
#include <limits>
#include <optional>
#include <chrono>
#include <random>
#include <regex>
#include <filesystem>

using namespace std;
namespace sys = std::filesystem;

namespace Stockfish::Tools
{
    static inline const std::string plain_extension = ".plain";
    static inline const std::string bin_extension = ".bin";
    static inline const std::string binpack_extension = ".binpack";

    static bool file_exists(const std::string& name)
    {
        std::ifstream f(name);
        return f.good();
    }

    static bool ends_with(const std::string& lhs, const std::string& end)
    {
        if (end.size() > lhs.size()) return false;

        return std::equal(end.rbegin(), end.rend(), lhs.rbegin());
    }

    static bool is_validation_of_type(
        const std::string& input_path,
        const std::string& expected_input_extension)
    {
        return ends_with(input_path, expected_input_extension);
    }

    using ValidateFunctionType = void(std::string inputPath);

    static ValidateFunctionType* get_validate_function(const std::string& input_path)
    {
        if (is_validation_of_type(input_path, plain_extension))
            return binpack::validatePlain;

        if (is_validation_of_type(input_path, bin_extension))
            return binpack::validateBin;

        if (is_validation_of_type(input_path, binpack_extension))
            return binpack::validateBinpack;

        return nullptr;
    }

    static void validate_training_data(const std::string& input_path)
    {
        if(!file_exists(input_path))
        {
            std::cerr << "Input file does not exist.\n";
            return;
        }

        auto func = get_validate_function(input_path);
        if (func != nullptr)
        {
            func(input_path);
        }
        else
        {
            std::cerr << "Validation of files of this type is not supported.\n";
        }
    }

    static void validate_training_data(const std::vector<std::string>& args)
    {
        if (args.size() != 1)
        {
            std::cerr << "Invalid arguments.\n";
            std::cerr << "Usage: validate in_path\n";
            return;
        }

        validate_training_data(args[0]);
    }

    void validate_training_data(istringstream& is)
    {
        std::vector<std::string> args;

        while (true)
        {
            std::string token = "";
            is >> token;
            if (token == "")
                break;

            args.push_back(token);
        }

        validate_training_data(args);
    }
}
