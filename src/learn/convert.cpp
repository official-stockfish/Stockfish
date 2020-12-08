#include "convert.h"

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

using namespace std;

namespace Learner
{
    bool fen_is_ok(Position& pos, std::string input_fen) {
        std::string pos_fen = pos.fen();
        std::istringstream ss_input(input_fen);
        std::istringstream ss_pos(pos_fen);

        // example : "2r4r/4kpp1/nb1np3/p2p3p/B2P1BP1/PP6/4NPKP/2R1R3 w - h6 0 24"
        //       --> "2r4r/4kpp1/nb1np3/p2p3p/B2P1BP1/PP6/4NPKP/2R1R3"
        std::string str_input, str_pos;
        ss_input >> str_input;
        ss_pos >> str_pos;

        // Only compare "Piece placement field" between input_fen and pos.fen().
        return str_input == str_pos;
    }

    void convert_bin(
        const vector<string>& filenames,
        const string& output_file_name,
        const int ply_minimum,
        const int ply_maximum,
        const int interpolate_eval,
        const int src_score_min_value,
        const int src_score_max_value,
        const int dest_score_min_value,
        const int dest_score_max_value,
        const bool check_invalid_fen,
        const bool check_illegal_move)
    {
        std::cout << "check_invalid_fen=" << check_invalid_fen << std::endl;
        std::cout << "check_illegal_move=" << check_illegal_move << std::endl;

        std::fstream fs;
        uint64_t data_size = 0;
        uint64_t filtered_size = 0;
        uint64_t filtered_size_fen = 0;
        uint64_t filtered_size_move = 0;
        uint64_t filtered_size_ply = 0;
        auto th = Threads.main();
        auto& tpos = th->rootPos;
        // convert plain rag to packed sfenvalue for Yaneura king
        fs.open(output_file_name, ios::app | ios::binary);
        StateListPtr states;
        for (auto filename : filenames) {
            std::cout << "convert " << filename << " ... ";
            std::string line;
            ifstream ifs;
            ifs.open(filename);
            PackedSfenValue p;
            data_size = 0;
            filtered_size = 0;
            filtered_size_fen = 0;
            filtered_size_move = 0;
            filtered_size_ply = 0;
            p.gamePly = 1; // Not included in apery format. Should be initialized
            bool ignore_flag_fen = false;
            bool ignore_flag_move = false;
            bool ignore_flag_ply = false;
            while (std::getline(ifs, line)) {
                std::stringstream ss(line);
                std::string token;
                std::string value;
                ss >> token;
                if (token == "fen") {
                    states = StateListPtr(new std::deque<StateInfo>(1)); // Drop old and create a new one
                    std::string input_fen = line.substr(4);
                    tpos.set(input_fen, false, &states->back(), Threads.main());
                    if (check_invalid_fen && !fen_is_ok(tpos, input_fen)) {
                        ignore_flag_fen = true;
                        filtered_size_fen++;
                    }
                    else {
                        tpos.sfen_pack(p.sfen);
                    }
                }
                else if (token == "move") {
                    ss >> value;
                    Move move = UCI::to_move(tpos, value);
                    if (check_illegal_move && move == MOVE_NONE) {
                        ignore_flag_move = true;
                        filtered_size_move++;
                    }
                    else {
                        p.move = move;
                    }
                }
                else if (token == "score") {
                    double score;
                    ss >> score;
                    // Training Formula ?Issue #71 ?nodchip/Stockfish https://github.com/nodchip/Stockfish/issues/71
                    // Normalize to [0.0, 1.0].
                    score = (score - src_score_min_value) / (src_score_max_value - src_score_min_value);
                    // Scale to [dest_score_min_value, dest_score_max_value].
                    score = score * (dest_score_max_value - dest_score_min_value) + dest_score_min_value;
                    p.score = Math::clamp((int32_t)std::round(score), -(int32_t)VALUE_MATE, (int32_t)VALUE_MATE);
                }
                else if (token == "ply") {
                    int temp;
                    ss >> temp;
                    if (temp < ply_minimum || temp > ply_maximum) {
                        ignore_flag_ply = true;
                        filtered_size_ply++;
                    }
                    p.gamePly = uint16_t(temp); // No cast here?
                    if (interpolate_eval != 0) {
                        p.score = min(3000, interpolate_eval * temp);
                    }
                }
                else if (token == "result") {
                    int temp;
                    ss >> temp;
                    p.game_result = int8_t(temp); // Do you need a cast here?
                    if (interpolate_eval) {
                        p.score = p.score * p.game_result;
                    }
                }
                else if (token == "e") {
                    if (!(ignore_flag_fen || ignore_flag_move || ignore_flag_ply)) {
                        fs.write((char*)&p, sizeof(PackedSfenValue));
                        data_size += 1;
                        // debug
                        // std::cout<<tpos<<std::endl;
                        // std::cout<<p.score<<","<<int(p.gamePly)<<","<<int(p.game_result)<<std::endl;
                    }
                    else {
                        filtered_size++;
                    }
                    ignore_flag_fen = false;
                    ignore_flag_move = false;
                    ignore_flag_ply = false;
                }
            }
            std::cout << "done " << data_size << " parsed " << filtered_size << " is filtered"
                << " (invalid fen:" << filtered_size_fen << ", illegal move:" << filtered_size_move << ", invalid ply:" << filtered_size_ply << ")" << std::endl;
            ifs.close();
        }
        std::cout << "all done" << std::endl;
        fs.close();
    }

    static inline void ltrim(std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
            return !std::isspace(ch);
            }));
    }

    static inline void rtrim(std::string& s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
            return !std::isspace(ch);
            }).base(), s.end());
    }

    static inline void trim(std::string& s) {
        ltrim(s);
        rtrim(s);
    }

    int parse_game_result_from_pgn_extract(std::string result) {
        // White Win
        if (result == "\"1-0\"") {
            return 1;
        }
        // Black Win
        else if (result == "\"0-1\"") {
            return -1;
        }
        // Draw
        else {
            return 0;
        }
    }

    // 0.25 -->  0.25 * PawnValueEg
    // #-4  --> -mate_in(4)
    // #3   -->  mate_in(3)
    // -M4  --> -mate_in(4)
    // +M3  -->  mate_in(3)
    Value parse_score_from_pgn_extract(std::string eval, bool& success) {
        success = true;

        if (eval.substr(0, 1) == "#") {
            if (eval.substr(1, 1) == "-") {
                return -mate_in(stoi(eval.substr(2, eval.length() - 2)));
            }
            else {
                return mate_in(stoi(eval.substr(1, eval.length() - 1)));
            }
        }
        else if (eval.substr(0, 2) == "-M") {
            //std::cout << "eval=" << eval << std::endl;
            return -mate_in(stoi(eval.substr(2, eval.length() - 2)));
        }
        else if (eval.substr(0, 2) == "+M") {
            //std::cout << "eval=" << eval << std::endl;
            return mate_in(stoi(eval.substr(2, eval.length() - 2)));
        }
        else {
            char* endptr;
            double value = strtod(eval.c_str(), &endptr);

            if (*endptr != '\0') {
                success = false;
                return VALUE_ZERO;
            }
            else {
                return Value(value * static_cast<double>(PawnValueEg));
            }
        }
    }

    // for Debug
    //#define DEBUG_CONVERT_BIN_FROM_PGN_EXTRACT

    bool is_like_fen(std::string fen) {
        int count_space = std::count(fen.cbegin(), fen.cend(), ' ');
        int count_slash = std::count(fen.cbegin(), fen.cend(), '/');

#if defined(DEBUG_CONVERT_BIN_FROM_PGN_EXTRACT)
        //std::cout << "count_space=" << count_space << std::endl;
        //std::cout << "count_slash=" << count_slash << std::endl;
#endif

        return count_space == 5 && count_slash == 7;
    }

    void convert_bin_from_pgn_extract(
        const vector<string>& filenames,
        const string& output_file_name,
        const bool pgn_eval_side_to_move,
        const bool convert_no_eval_fens_as_score_zero)
    {
        std::cout << "pgn_eval_side_to_move=" << pgn_eval_side_to_move << std::endl;
        std::cout << "convert_no_eval_fens_as_score_zero=" << convert_no_eval_fens_as_score_zero << std::endl;

        auto th = Threads.main();
        auto& pos = th->rootPos;

        std::fstream ofs;
        ofs.open(output_file_name, ios::out | ios::binary);

        int game_count = 0;
        int fen_count = 0;

        for (auto filename : filenames) {
            std::cout << now_string() << " convert " << filename << std::endl;
            ifstream ifs;
            ifs.open(filename);

            int game_result = 0;

            std::string line;
            while (std::getline(ifs, line)) {

                if (line.empty()) {
                    continue;
                }

                else if (line.substr(0, 1) == "[") {
                    std::regex pattern_result(R"(\[Result (.+?)\])");
                    std::smatch match;

                    // example: [Result "1-0"]
                    if (std::regex_search(line, match, pattern_result)) {
                        game_result = parse_game_result_from_pgn_extract(match.str(1));
#if defined(DEBUG_CONVERT_BIN_FROM_PGN_EXTRACT)
                        std::cout << "game_result=" << game_result << std::endl;
#endif
                        game_count++;
                        if (game_count % 10000 == 0) {
                            std::cout << now_string() << " game_count=" << game_count << ", fen_count=" << fen_count << std::endl;
                        }
                    }

                    continue;
                }

                else {
                    int gamePly = 1;
                    auto itr = line.cbegin();

                    while (true) {
                        gamePly++;

                        PackedSfenValue psv;
                        memset((char*)&psv, 0, sizeof(PackedSfenValue));

                        // fen
                        {
                            bool fen_found = false;

                            while (!fen_found) {
                                std::regex pattern_bracket(R"(\{(.+?)\})");
                                std::smatch match;
                                if (!std::regex_search(itr, line.cend(), match, pattern_bracket)) {
                                    break;
                                }

                                itr += match.position(0) + match.length(0) - 1;
                                std::string str_fen = match.str(1);
                                trim(str_fen);

                                if (is_like_fen(str_fen)) {
                                    fen_found = true;

                                    StateInfo si;
                                    pos.set(str_fen, false, &si, th);
                                    pos.sfen_pack(psv.sfen);
                                }

#if defined(DEBUG_CONVERT_BIN_FROM_PGN_EXTRACT)
                                std::cout << "str_fen=" << str_fen << std::endl;
                                std::cout << "fen_found=" << fen_found << std::endl;
#endif
                            }

                            if (!fen_found) {
                                break;
                            }
                        }

                        // move
                        {
                            std::regex pattern_move(R"(\}(.+?)\{)");
                            std::smatch match;
                            if (!std::regex_search(itr, line.cend(), match, pattern_move)) {
                                break;
                            }

                            itr += match.position(0) + match.length(0) - 1;
                            std::string str_move = match.str(1);
                            trim(str_move);
#if defined(DEBUG_CONVERT_BIN_FROM_PGN_EXTRACT)
                            std::cout << "str_move=" << str_move << std::endl;
#endif
                            psv.move = UCI::to_move(pos, str_move);
                        }

                        // eval
                        bool eval_found = false;
                        {
                            std::regex pattern_bracket(R"(\{(.+?)\})");
                            std::smatch match;
                            if (!std::regex_search(itr, line.cend(), match, pattern_bracket)) {
                                break;
                            }

                            std::string str_eval_clk = match.str(1);
                            trim(str_eval_clk);
#if defined(DEBUG_CONVERT_BIN_FROM_PGN_EXTRACT)
                            std::cout << "str_eval_clk=" << str_eval_clk << std::endl;
#endif

                            // example: { [%eval 0.25] [%clk 0:10:00] }
                            // example: { [%eval #-4] [%clk 0:10:00] }
                            // example: { [%eval #3] [%clk 0:10:00] }
                            // example: { +0.71/22 1.2s }
                            // example: { -M4/7 0.003s }
                            // example: { M3/245 0.017s }
                            // example: { +M1/245 0.010s, White mates }
                            // example: { 0.60 }
                            // example: { book }
                            // example: { rnbqkb1r/pp3ppp/2p1pn2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R w KQkq - 0 5 }

                            // Considering the absence of eval
                            if (!is_like_fen(str_eval_clk)) {
                                itr += match.position(0) + match.length(0) - 1;

                                if (str_eval_clk != "book") {
                                    std::regex pattern_eval1(R"(\[\%eval (.+?)\])");
                                    std::regex pattern_eval2(R"((.+?)\/)");

                                    std::string str_eval;
                                    if (std::regex_search(str_eval_clk, match, pattern_eval1) ||
                                        std::regex_search(str_eval_clk, match, pattern_eval2)) {
                                        str_eval = match.str(1);
                                        trim(str_eval);
                                    }
                                    else {
                                        str_eval = str_eval_clk;
                                    }

                                    bool success = false;
                                    Value value = parse_score_from_pgn_extract(str_eval, success);
                                    if (success) {
                                        eval_found = true;
                                        psv.score = Math::clamp(value, -VALUE_MATE, VALUE_MATE);
                                    }

#if defined(DEBUG_CONVERT_BIN_FROM_PGN_EXTRACT)
                                    std::cout << "str_eval=" << str_eval << std::endl;
                                    std::cout << "success=" << success << ", psv.score=" << psv.score << std::endl;
#endif
                                }
                            }
                        }

                        // write
                        if (eval_found || convert_no_eval_fens_as_score_zero) {
                            if (!eval_found && convert_no_eval_fens_as_score_zero) {
                                psv.score = 0;
                            }

                            psv.gamePly = gamePly;
                            psv.game_result = game_result;

                            if (pos.side_to_move() == BLACK) {
                                if (!pgn_eval_side_to_move) {
                                    psv.score *= -1;
                                }
                                psv.game_result *= -1;
                            }

                            ofs.write((char*)&psv, sizeof(PackedSfenValue));

                            fen_count++;
                        }
                    }

                    game_result = 0;
                }
            }
        }

        std::cout << now_string() << " game_count=" << game_count << ", fen_count=" << fen_count << std::endl;
        std::cout << now_string() << " all done" << std::endl;
        ofs.close();
    }

    void convert_plain(
        const vector<string>& filenames,
        const string& output_file_name)
    {
        Position tpos;
        std::ofstream ofs;
        ofs.open(output_file_name, ios::app);
        auto th = Threads.main();
        for (auto filename : filenames) {
            std::cout << "convert " << filename << " ... ";

            // Just convert packedsfenvalue to text
            std::fstream fs;
            fs.open(filename, ios::in | ios::binary);
            PackedSfenValue p;
            while (true)
            {
                if (fs.read((char*)&p, sizeof(PackedSfenValue))) {
                    StateInfo si;
                    tpos.set_from_packed_sfen(p.sfen, &si, th);

                    // write as plain text
                    ofs << "fen " << tpos.fen() << std::endl;
                    ofs << "move " << UCI::move(Move(p.move), false) << std::endl;
                    ofs << "score " << p.score << std::endl;
                    ofs << "ply " << int(p.gamePly) << std::endl;
                    ofs << "result " << int(p.game_result) << std::endl;
                    ofs << "e" << std::endl;
                }
                else {
                    break;
                }
            }
            fs.close();
            std::cout << "done" << std::endl;
        }
        ofs.close();
        std::cout << "all done" << std::endl;
    }

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

    static bool is_convert_of_type(
        const std::string& input_path,
        const std::string& output_path,
        const std::string& expected_input_extension,
        const std::string& expected_output_extension)
    {
        return ends_with(input_path, expected_input_extension)
            && ends_with(output_path, expected_output_extension);
    }

    using ConvertFunctionType = void(std::string inputPath, std::string outputPath, std::ios_base::openmode om, bool validate);

    static ConvertFunctionType* get_convert_function(const std::string& input_path, const std::string& output_path)
    {
        if (is_convert_of_type(input_path, output_path, plain_extension, bin_extension))
            return binpack::convertPlainToBin;
        if (is_convert_of_type(input_path, output_path, plain_extension, binpack_extension))
            return binpack::convertPlainToBinpack;

        if (is_convert_of_type(input_path, output_path, bin_extension, plain_extension))
            return binpack::convertBinToPlain;
        if (is_convert_of_type(input_path, output_path, bin_extension, binpack_extension))
            return binpack::convertBinToBinpack;

        if (is_convert_of_type(input_path, output_path, binpack_extension, plain_extension))
            return binpack::convertBinpackToPlain;
        if (is_convert_of_type(input_path, output_path, binpack_extension, bin_extension))
            return binpack::convertBinpackToBin;

        return nullptr;
    }

    static void convert(const std::string& input_path, const std::string& output_path, std::ios_base::openmode om, bool validate)
    {
        if(!file_exists(input_path))
        {
            std::cerr << "Input file does not exist.\n";
            return;
        }

        auto func = get_convert_function(input_path, output_path);
        if (func != nullptr)
        {
            func(input_path, output_path, om, validate);
        }
        else
        {
            std::cerr << "Conversion between files of these types is not supported.\n";
        }
    }

    static void convert(const std::vector<std::string>& args)
    {
        if (args.size() < 2 || args.size() > 4)
        {
            std::cerr << "Invalid arguments.\n";
            std::cerr << "Usage: convert from_path to_path [append] [validate]\n";
            return;
        }

        const bool append = std::find(args.begin() + 2, args.end(), "append") != args.end();
        const bool validate = std::find(args.begin() + 2, args.end(), "validate") != args.end();

        const std::ios_base::openmode openmode =
            append
            ? std::ios_base::app
            : std::ios_base::trunc;

        convert(args[0], args[1], openmode, validate);
    }

    void convert(istringstream& is)
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

        convert(args);
    }

    static void append_files_from_dir(
        std::vector<std::string>& filenames,
        const std::string& base_dir,
        const std::string& target_dir)
    {
        string kif_base_dir = Path::combine(base_dir, target_dir);

        sys::path p(kif_base_dir); // Origin of enumeration
        std::for_each(sys::directory_iterator(p), sys::directory_iterator(),
            [&](const sys::path& path) {
                if (sys::is_regular_file(path))
                    filenames.push_back(Path::combine(target_dir, path.filename().generic_string()));
            });
    }

    static void rebase_files(
        std::vector<std::string>& filenames,
        const std::string& base_dir)
    {
        for (auto& file : filenames)
        {
            file = Path::combine(base_dir, file);
        }
    }

    void convert_bin_from_pgn_extract(std::istringstream& is)
    {
        std::vector<std::string> filenames;

        string base_dir;
        string target_dir;

        bool pgn_eval_side_to_move = false;
        bool convert_no_eval_fens_as_score_zero = false;

        string output_file_name = "shuffled_sfen.bin";

        while (true)
        {
            string option;
            is >> option;

            if (option == "")
                break;

            if (option == "targetdir") is >> target_dir;
            else if (option == "targetfile")
            {
                std::string filename;
                is >> filename;
                filenames.push_back(filename);
            }

            else if (option == "basedir")   is >> base_dir;

            else if (option == "pgn_eval_side_to_move") is >> pgn_eval_side_to_move;
            else if (option == "convert_no_eval_fens_as_score_zero") is >> convert_no_eval_fens_as_score_zero;
            else if (option == "output_file_name") is >> output_file_name;
            else
            {
                cout << "Unknown option: " << option << ". Ignoring.\n";
            }
        }

        if (!target_dir.empty())
        {
            append_files_from_dir(filenames, base_dir, target_dir);
        }
        rebase_files(filenames, base_dir);

        Eval::NNUE::init();

        cout << "convert_bin_from_pgn-extract.." << endl;
        convert_bin_from_pgn_extract(
            filenames,
            output_file_name,
            pgn_eval_side_to_move,
            convert_no_eval_fens_as_score_zero);
    }

    void convert_bin(std::istringstream& is)
    {
        std::vector<std::string> filenames;

        string base_dir;
        string target_dir;

        int ply_minimum = 0;
        int ply_maximum = 114514;
        bool interpolate_eval = 0;
        bool check_invalid_fen = false;
        bool check_illegal_move = false;

        bool pgn_eval_side_to_move = false;
        bool convert_no_eval_fens_as_score_zero = false;

        double src_score_min_value = 0.0;
        double src_score_max_value = 1.0;
        double dest_score_min_value = 0.0;
        double dest_score_max_value = 1.0;

        string output_file_name = "shuffled_sfen.bin";

        while (true)
        {
            string option;
            is >> option;

            if (option == "")
                break;

            if (option == "targetdir") is >> target_dir;
            else if (option == "targetfile")
            {
                std::string filename;
                is >> filename;
                filenames.push_back(filename);
            }

            else if (option == "basedir")   is >> base_dir;

            else if (option == "ply_minimum") is >> ply_minimum;
            else if (option == "ply_maximum") is >> ply_maximum;
            else if (option == "interpolate_eval") is >> interpolate_eval;
            else if (option == "check_invalid_fen") is >> check_invalid_fen;
            else if (option == "check_illegal_move") is >> check_illegal_move;
            else if (option == "pgn_eval_side_to_move") is >> pgn_eval_side_to_move;
            else if (option == "convert_no_eval_fens_as_score_zero") is >> convert_no_eval_fens_as_score_zero;
            else if (option == "src_score_min_value") is >> src_score_min_value;
            else if (option == "src_score_max_value") is >> src_score_max_value;
            else if (option == "dest_score_min_value") is >> dest_score_min_value;
            else if (option == "dest_score_max_value") is >> dest_score_max_value;
            else if (option == "output_file_name") is >> output_file_name;
            else
            {
                cout << "Unknown option: " << option << ". Ignoring.\n";
            }
        }

        if (!target_dir.empty())
        {
            append_files_from_dir(filenames, base_dir, target_dir);
        }
        rebase_files(filenames, base_dir);

        Eval::NNUE::init();

        cout << "convert_bin.." << endl;
            convert_bin(
                filenames,
                output_file_name,
                ply_minimum,
                ply_maximum,
                interpolate_eval,
                src_score_min_value,
                src_score_max_value,
                dest_score_min_value,
                dest_score_max_value,
                check_invalid_fen,
                check_illegal_move
            );
    }

    void convert_plain(std::istringstream& is)
    {
        std::vector<std::string> filenames;

        string base_dir;
        string target_dir;

        string output_file_name = "shuffled_sfen.bin";

        while (true)
        {
            string option;
            is >> option;

            if (option == "")
                break;

            if (option == "targetdir") is >> target_dir;
            else if (option == "targetfile")
            {
                std::string filename;
                is >> filename;
                filenames.push_back(filename);
            }

            else if (option == "basedir")   is >> base_dir;

            else if (option == "output_file_name") is >> output_file_name;
            else
            {
                cout << "Unknown option: " << option << ". Ignoring.\n";
            }
        }

        if (!target_dir.empty())
        {
            append_files_from_dir(filenames, base_dir, target_dir);
        }
        rebase_files(filenames, base_dir);

        Eval::NNUE::init();

        cout << "convert_plain.." << endl;
        convert_plain(filenames, output_file_name);
    }
}
