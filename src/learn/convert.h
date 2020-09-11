#ifndef _CONVERT_H_
#define _CONVERT_H_

#include <vector>
#include <string>
#include <sstream>

#if defined(EVAL_LEARN)
namespace Learner {
    void convert_bin_from_pgn_extract(
        const std::vector<std::string>& filenames,
        const std::string& output_file_name,
        const bool pgn_eval_side_to_move,
        const bool convert_no_eval_fens_as_score_zero);

    void convert_bin(
        const std::vector<std::string>& filenames,
        const std::string& output_file_name,
        const int ply_minimum,
        const int ply_maximum,
        const int interpolate_eval,
        const int src_score_min_value,
        const int src_score_max_value,
        const int dest_score_min_value,
        const int dest_score_max_value,
        const bool check_invalid_fen,
        const bool check_illegal_move);

    void convert_plain(
        const std::vector<std::string>& filenames,
        const std::string& output_file_name);

    void convert(std::istringstream& is);
}
#endif

#endif
