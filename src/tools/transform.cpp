#include "transform.h"

#include "sfen_stream.h"
#include "packed_sfen.h"
#include "sfen_writer.h"

#include "thread.h"
#include "position.h"
#include "evaluate.h"
#include "search.h"

#include "nnue/evaluate_nnue.h"

#include "extra/nnue_data_binpack_format.h"

#include <string>
#include <map>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>

namespace Stockfish::Tools
{
    using CommandFunc = void(*)(std::istringstream&);

    enum struct NudgedStaticMode
    {
        Absolute,
        Relative,
        Interpolate
    };

    struct NudgedStaticParams
    {
        std::string input_filename = "in.binpack";
        std::string output_filename = "out.binpack";
        NudgedStaticMode mode = NudgedStaticMode::Absolute;
        int absolute_nudge = 5;
        float relative_nudge = 0.1;
        float interpolate_nudge = 0.1;

        void enforce_constraints()
        {
            relative_nudge = std::max(relative_nudge, 0.0f);
            absolute_nudge = std::max(absolute_nudge, 0);
        }
    };

    struct RescoreParams
    {
        std::string input_filename = "in.epd";
        std::string output_filename = "out.binpack";
        int depth = 3;
        int research_count = 0;
        bool keep_moves = true;

        void enforce_constraints()
        {
            depth = std::max(1, depth);
            research_count = std::max(0, research_count);
        }
    };

    struct FilterParams
    {
        std::string input_filename = "in.binpack";
        std::string output_filename = "out.binpack";
        bool debug_print = false;
    };

    [[nodiscard]] std::int16_t nudge(NudgedStaticParams& params, std::int16_t static_eval_i16, std::int16_t deep_eval_i16)
    {
        auto saturate_i32_to_i16 = [](int v) {
            return static_cast<std::int16_t>(
                std::clamp(
                    v,
                    (int)std::numeric_limits<std::int16_t>::min(),
                    (int)std::numeric_limits<std::int16_t>::max()
                )
            );
        };

        auto saturate_f32_to_i16 = [saturate_i32_to_i16](float v) {
            return saturate_i32_to_i16((int)v);
        };

        int static_eval = static_eval_i16;
        int deep_eval = deep_eval_i16;

        switch(params.mode)
        {
            case NudgedStaticMode::Absolute:
                return saturate_i32_to_i16(
                    static_eval + std::clamp(
                        deep_eval - static_eval,
                        -params.absolute_nudge,
                        params.absolute_nudge
                    )
                );

            case NudgedStaticMode::Relative:
                return saturate_f32_to_i16(
                    (float)static_eval * std::clamp(
                        (float)deep_eval / (float)static_eval,
                        (1.0f - params.relative_nudge),
                        (1.0f + params.relative_nudge)
                    )
                );

            case NudgedStaticMode::Interpolate:
                return saturate_f32_to_i16(
                    (float)static_eval * (1.0f - params.interpolate_nudge)
                    + (float)deep_eval * params.interpolate_nudge
                );

            default:
                assert(false);
                return 0;
        }
    }

    void do_nudged_static(NudgedStaticParams& params)
    {
        Thread* th = Threads.main();
        Position& pos = th->rootPos;
        StateInfo si;

        auto in = Tools::open_sfen_input_file(params.input_filename);
        auto out = Tools::create_new_sfen_output(params.output_filename);

        if (in == nullptr)
        {
            std::cerr << "Invalid input file type.\n";
            return;
        }

        if (out == nullptr)
        {
            std::cerr << "Invalid output file type.\n";
            return;
        }

        PSVector buffer;
        uint64_t batch_size = 1'000'000;

        buffer.reserve(batch_size);

        const bool frc = Options["UCI_Chess960"];
        uint64_t num_processed = 0;
        for (;;)
        {
            auto v = in->next();
            if (!v.has_value())
                break;

            auto& ps = v.value();

            pos.set_from_packed_sfen(ps.sfen, &si, th, frc);
            auto static_eval = Eval::evaluate(pos);
            auto deep_eval = ps.score;
            ps.score = nudge(params, static_eval, deep_eval);

            buffer.emplace_back(ps);
            if (buffer.size() >= batch_size)
            {
                num_processed += buffer.size();

                out->write(buffer);
                buffer.clear();

                std::cout << "Processed " << num_processed << " positions.\n";
            }
        }

        if (!buffer.empty())
        {
            num_processed += buffer.size();

            out->write(buffer);
            buffer.clear();

            std::cout << "Processed " << num_processed << " positions.\n";
        }

        std::cout << "Finished.\n";
    }

    void nudged_static(std::istringstream& is)
    {
        NudgedStaticParams params{};

        while(true)
        {
            std::string token;
            is >> token;

            if (token == "")
                break;

            if (token == "absolute")
            {
                params.mode = NudgedStaticMode::Absolute;
                is >> params.absolute_nudge;
            }
            else if (token == "relative")
            {
                params.mode = NudgedStaticMode::Relative;
                is >> params.relative_nudge;
            }
            else if (token == "interpolate")
            {
                params.mode = NudgedStaticMode::Interpolate;
                is >> params.interpolate_nudge;
            }
            else if (token == "input_file")
                is >> params.input_filename;
            else if (token == "output_file")
                is >> params.output_filename;
            else
            {
                std::cout << "ERROR: Unknown option " << token << ". Exiting...\n";
                return;
            }
        }

        std::cout << "Performing transform nudged_static with parameters:\n";
        std::cout << "input_file          : " << params.input_filename << '\n';
        std::cout << "output_file         : " << params.output_filename << '\n';
        std::cout << "\n";
        if (params.mode == NudgedStaticMode::Absolute)
        {
            std::cout << "mode                : absolute\n";
            std::cout << "absolute_nudge      : " << params.absolute_nudge << '\n';
        }
        else if (params.mode == NudgedStaticMode::Relative)
        {
            std::cout << "mode                : relative\n";
            std::cout << "relative_nudge      : " << params.relative_nudge << '\n';
        }
        else if (params.mode == NudgedStaticMode::Interpolate)
        {
            std::cout << "mode                : interpolate\n";
            std::cout << "interpolate_nudge   : " << params.interpolate_nudge << '\n';
        }
        std::cout << '\n';

        params.enforce_constraints();
        do_nudged_static(params);
    }

    void do_rescore_epd(RescoreParams& params)
    {
        std::ifstream fens_file(params.input_filename);

        auto next_fen = [&fens_file, mutex = std::mutex{}]() mutable -> std::optional<std::string>{
            std::string fen;

            std::unique_lock lock(mutex);

            if (std::getline(fens_file, fen) && fen.size() >= 10)
            {
                return fen;
            }
            else
            {
                return std::nullopt;
            }
        };

        PSVector buffer;
        uint64_t batch_size = 10'000;

        buffer.reserve(batch_size);

        auto out = Tools::create_new_sfen_output(params.output_filename);

        std::mutex mutex;
        uint64_t num_processed = 0;

        // About Search::Limits
        // Be careful because this member variable is global and affects other threads.
        auto& limits = Search::Limits;

        // Make the search equivalent to the "go infinite" command. (Because it is troublesome if time management is done)
        limits.infinite = true;

        // Since PV is an obstacle when displayed, erase it.
        limits.silent = true;

        // If you use this, it will be compared with the accumulated nodes of each thread. Therefore, do not use it.
        limits.nodes = 0;

        // depth is also processed by the one passed as an argument of Tools::search().
        limits.depth = 0;

        Threads.execute_with_workers([&](auto& th){
            Position& pos = th.rootPos;
            StateInfo si;
            const bool frc = Options["UCI_Chess960"];

            for(;;)
            {
                auto fen = next_fen();
                if (!fen.has_value())
                    return;

                pos.set(*fen, frc, &si, &th);
                pos.state()->rule50 = 0;


                for (int cnt = 0; cnt < params.research_count; ++cnt)
                    Search::search(pos, params.depth, 1);

                auto [search_value, search_pv] = Search::search(pos, params.depth, 1);

                if (search_pv.empty())
                    continue;

                PackedSfenValue ps;
                pos.sfen_pack(ps.sfen, pos.is_chess960());
                ps.score = search_value;
                ps.move = search_pv[0];
                ps.gamePly = 1;
                ps.game_result = 0;
                ps.padding = 0;

                std::unique_lock lock(mutex);
                buffer.emplace_back(ps);
                if (buffer.size() >= batch_size)
                {
                    num_processed += buffer.size();

                    out->write(buffer);
                    buffer.clear();

                    std::cout << "Processed " << num_processed << " positions.\n";
                }
            }
        });
        Threads.wait_for_workers_finished();

        if (!buffer.empty())
        {
            num_processed += buffer.size();

            out->write(buffer);
            buffer.clear();

            std::cout << "Processed " << num_processed << " positions.\n";
        }

        std::cout << "Finished.\n";
    }

    void do_rescore_data(RescoreParams& params)
    {
        // TODO: Use SfenReader once it works correctly in sequential mode. See issue #271
        auto in = Tools::open_sfen_input_file(params.input_filename);
        auto readsome = [&in, mutex = std::mutex{}](int n) mutable -> PSVector {

            PSVector psv;
            psv.reserve(n);

            std::unique_lock lock(mutex);

            for (int i = 0; i < n; ++i)
            {
                auto ps_opt = in->next();
                if (ps_opt.has_value())
                {
                    psv.emplace_back(*ps_opt);
                }
                else
                {
                    break;
                }
            }

            return psv;
        };

        auto sfen_format = ends_with(params.output_filename, ".binpack") ? SfenOutputType::Binpack : SfenOutputType::Bin;

        auto out = SfenWriter(
            params.output_filename,
            Threads.size(),
            std::numeric_limits<std::uint64_t>::max(),
            sfen_format);

        // About Search::Limits
        // Be careful because this member variable is global and affects other threads.
        auto& limits = Search::Limits;

        // Make the search equivalent to the "go infinite" command. (Because it is troublesome if time management is done)
        limits.infinite = true;

        // Since PV is an obstacle when displayed, erase it.
        limits.silent = true;

        // If you use this, it will be compared with the accumulated nodes of each thread. Therefore, do not use it.
        limits.nodes = 0;

        // depth is also processed by the one passed as an argument of Tools::search().
        limits.depth = 0;

        std::atomic<std::uint64_t> num_processed = 0;

        Threads.execute_with_workers([&](auto& th){
            Position& pos = th.rootPos;
            StateInfo si;
            const bool frc = Options["UCI_Chess960"];

            for (;;)
            {
                PSVector psv = readsome(5000);
                if (psv.empty())
                    break;

                for(auto& ps : psv)
                {
                    pos.set_from_packed_sfen(ps.sfen, &si, &th, frc);

                    for (int cnt = 0; cnt < params.research_count; ++cnt)
                        Search::search(pos, params.depth, 1);

                    auto [search_value, search_pv] = Search::search(pos, params.depth, 1);

                    if (search_pv.empty())
                        continue;

                    pos.sfen_pack(ps.sfen, false);
                    ps.score = search_value;
                    if (!params.keep_moves)
                        ps.move = search_pv[0];
                    ps.padding = 0;

                    out.write(th.id(), ps);

                    auto p = num_processed.fetch_add(1) + 1;
                    if (p % 10000 == 0)
                    {
                        std::cout << "Processed " << p << " positions.\n";
                    }
                }
            }
        });
        Threads.wait_for_workers_finished();

        std::cout << "Finished.\n";
    }

    void do_rescore(RescoreParams& params)
    {
        if (ends_with(params.input_filename, ".epd"))
        {
            do_rescore_epd(params);
        }
        else if (ends_with(params.input_filename, ".bin") || ends_with(params.input_filename, ".binpack"))
        {
            do_rescore_data(params);
        }
        else
        {
            std::cerr << "Invalid input file type.\n";
        }
    }

    void rescore(std::istringstream& is)
    {
        RescoreParams params{};

        while(true)
        {
            std::string token;
            is >> token;

            if (token == "")
                break;

            if (token == "depth")
                is >> params.depth;
            else if (token == "input_file")
                is >> params.input_filename;
            else if (token == "output_file")
                is >> params.output_filename;
            else if (token == "keep_moves")
                is >> params.keep_moves;
            else if (token == "research_count")
                is >> params.research_count;
            else
            {
                std::cout << "ERROR: Unknown option " << token << ". Exiting...\n";
                return;
            }
        }

        params.enforce_constraints();

        std::cout << "Performing transform rescore with parameters:\n";
        std::cout << "depth               : " << params.depth << '\n';
        std::cout << "input_file          : " << params.input_filename << '\n';
        std::cout << "output_file         : " << params.output_filename << '\n';
        std::cout << "keep_moves          : " << params.keep_moves << '\n';
        std::cout << "research_count      : " << params.research_count << '\n';
        std::cout << '\n';

        do_rescore(params);
    }

    void do_filter_data_335a9b2d8a80(FilterParams& params)
    {
        // TODO: Use SfenReader once it works correctly in sequential mode. See issue #271
        auto in = Tools::open_sfen_input_file(params.input_filename);
        auto readsome = [&in, mutex = std::mutex{}](int n) mutable -> PSVector {

            PSVector psv;
            psv.reserve(n);

            std::unique_lock lock(mutex);

            for (int i = 0; i < n; ++i)
            {
                auto ps_opt = in->next();
                if (ps_opt.has_value())
                {
                    psv.emplace_back(*ps_opt);
                }
                else
                {
                    break;
                }
            }

            return psv;
        };

        auto sfen_format = SfenOutputType::Binpack;

        auto out = SfenWriter(
            params.output_filename,
            Threads.size(),
            std::numeric_limits<std::uint64_t>::max(),
            sfen_format);

        // About Search::Limits
        // Be careful because this member variable is global and affects other threads.
        auto& limits = Search::Limits;

        // Make the search equivalent to the "go infinite" command. (Because it is troublesome if time management is done)
        limits.infinite = true;

        // Since PV is an obstacle when displayed, erase it.
        limits.silent = true;

        // If you use this, it will be compared with the accumulated nodes of each thread. Therefore, do not use it.
        limits.nodes = 0;

        // depth is also processed by the one passed as an argument of Tools::search().
        limits.depth = 0;

        std::atomic<std::uint64_t> num_processed = 0;
        std::atomic<std::uint64_t> num_standard_startpos = 0;
        std::atomic<std::uint64_t> num_position_in_check = 0;
        std::atomic<std::uint64_t> num_move_already_is_capture = 0;
        std::atomic<std::uint64_t> num_capture_or_promo_skipped_multipv_cap0 = 0;
        std::atomic<std::uint64_t> num_capture_or_promo_skipped_multipv_cap1 = 0;

        Threads.execute_with_workers([&](auto& th){
            Position& pos = th.rootPos;
            StateInfo si;
            const bool frc = Options["UCI_Chess960"];

            const bool debug_print = params.debug_print;
            for (;;)
            {
                PSVector psv = readsome(5000);
                if (psv.empty())
                    break;

                for(auto& ps : psv)
                {
                    pos.set_from_packed_sfen(ps.sfen, &si, &th, frc);
                    bool should_skip_position = false;
                    if (pos.checkers()) {
                        // Skip if in check
                        if (debug_print) {
                            sync_cout << "[debug] " << pos.fen() << sync_endl
                                      << "[debug] Position is in check" << sync_endl
                                      << "[debug]" << sync_endl;
                        }
                        num_position_in_check.fetch_add(1);
                        should_skip_position = true;
                    } else if (pos.capture_or_promotion((Stockfish::Move)ps.move)) {
                        // Skip if the provided move is already a capture or promotion
                        if (debug_print) {
                            sync_cout << "[debug] " << pos.fen() << sync_endl
                                      << "[debug] Provided move is capture or promo: "
                                      << UCI::move((Stockfish::Move)ps.move, false)
                                      << sync_endl
                                      << "[debug]" << sync_endl;
                        }
                        num_move_already_is_capture.fetch_add(1);
                        should_skip_position = true;
		    } else if (pos.fen() == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {
                        num_standard_startpos.fetch_add(1);
                        should_skip_position = true;
                    } else {
                        auto [search_val, pvs] = Search::search(pos, 6, 2);
                        if (!pvs.empty() && th.rootMoves.size() > 0) {
                            auto best_move = th.rootMoves[0].pv[0];
                            bool more_than_one_valid_move = th.rootMoves.size() > 1;
                            if (debug_print) {
                                sync_cout << "[debug] " << pos.fen() << sync_endl;
                                sync_cout << "[debug] Main PV move:    "
                                          << UCI::move(best_move, false) << " "
                                          << th.rootMoves[0].score << " " << sync_endl;
                                if (more_than_one_valid_move) {
                                    sync_cout << "[debug] 2nd PV move:     "
                                              << UCI::move(th.rootMoves[1].pv[0], false) << " "
                                              << th.rootMoves[1].score << " " << sync_endl;
                                } else {
                                    sync_cout << "[debug] The only valid move" << sync_endl;
                                }
                            }
                            if (pos.capture_or_promotion(best_move)) {
                                // skip if multipv 1st line bestmove is a capture or promo
                                if (debug_print) {
                                    sync_cout << "[debug] Move is capture or promo: " << UCI::move(best_move, false)
                                              << sync_endl
                                              << "[debug] 1st best move at depth 6 multipv 2" << sync_endl
                                              << "[debug]" << sync_endl;
                                }
                                num_capture_or_promo_skipped_multipv_cap0.fetch_add(1);
                                should_skip_position = true;
                            } else if (more_than_one_valid_move && pos.capture_or_promotion(th.rootMoves[1].pv[0])) {
                                // skip if multipv 2nd line bestmove is a capture or promo
                                if (debug_print) {
                                    sync_cout << "[debug] Move is capture or promo: " << UCI::move(best_move, false)
                                              << sync_endl
                                              << "[debug] 2nd best move at depth 6 multipv 2" << sync_endl
                                              << "[debug]" << sync_endl;
                                }
                                num_capture_or_promo_skipped_multipv_cap1.fetch_add(1);
                                should_skip_position = true;
                            }
			}
                    }
                    pos.sfen_pack(ps.sfen, false);
                    // nnue-pytorch training data loader skips positions with score VALUE_NONE
                    if (should_skip_position)
                        ps.score = 32002; // VALUE_NONE
                    ps.padding = 0;

                    out.write(th.id(), ps);

                    auto p = num_processed.fetch_add(1) + 1;
                    if (p % 10000 == 0) {
                        auto c = num_position_in_check.load();
                        auto a = num_move_already_is_capture.load();
                        auto s = num_standard_startpos.load();
                        auto multipv_cap0 = num_capture_or_promo_skipped_multipv_cap0.load();
                        auto multipv_cap1 = num_capture_or_promo_skipped_multipv_cap1.load();
                        sync_cout << "Processed " << p << " positions. Skipped " << (c + a + s + multipv_cap0 + multipv_cap1) << " positions."
                                  << sync_endl
                                  << "  Static filter: " << (a + c + s)
                                  << " (capture or promo: " << a << ", in check: " << c << ", startpos: " << s << ")"
                                  << sync_endl
                                  << "  MultiPV filter: " << (multipv_cap0 + multipv_cap1)
                                  << " (cap0: " << multipv_cap0 << ", cap1: " << multipv_cap1 << ")"
                                  << " depth 6 multipv 2" << sync_endl;
                    }
                }
            }
        });
        Threads.wait_for_workers_finished();

        std::cout << "Finished.\n";
    }

    void do_filter_335a9b2d8a80(FilterParams& params)
    {
        if (ends_with(params.input_filename, ".binpack"))
        {
            do_filter_data_335a9b2d8a80(params);
        }
        else
        {
            std::cerr << "Invalid input file type.\n";
        }
    }

    void filter_335a9b2d8a80(std::istringstream& is)
    {
        FilterParams params{};

        while(true)
        {
            std::string token;
            is >> token;

            if (token == "")
                break;

            else if (token == "input_file")
                is >> params.input_filename;
            else if (token == "output_file")
                is >> params.output_filename;
            else if (token == "debug_print")
                is >> params.debug_print;
            else
            {
                std::cout << "ERROR: Unknown option " << token << ". Exiting...\n";
                return;
            }
        }

        std::cout << "Performing transform filter_335a9b2d8a80 with parameters:\n";
        std::cout << "input_file          : " << params.input_filename << '\n';
        std::cout << "output_file         : " << params.output_filename << '\n';
        std::cout << "debug_print         : " << params.debug_print << '\n';
        std::cout << '\n';

        do_filter_335a9b2d8a80(params);
    }

    struct MinimizeBinpackParams
    {
        std::string input_filename = "in.binpack";
        std::string output_filename = "out.binpack";
        bool debug_print = false;
        uint64_t chain_search_nodes = 1024 * 64;

        void enforce_constraints()
        {
        }
    };

    [[nodiscard]] binpack::nodchip::PackedSfenValue packed_sfen_tools_to_lib(Stockfish::Tools::PackedSfenValue ps)
    {
        binpack::nodchip::PackedSfenValue ret;
        static_assert(sizeof(ret) == sizeof(ps));
        std::memcpy(&ret, &ps, sizeof(ret));
        return ret;
    }

    [[nodiscard]] Stockfish::Tools::PackedSfenValue packed_sfen_lib_to_tools(binpack::nodchip::PackedSfenValue ps)
    {
        Stockfish::Tools::PackedSfenValue ret;
        static_assert(sizeof(ret) == sizeof(ps));
        std::memcpy(&ret, &ps, sizeof(ret));
        return ret;
    }

    [[nodiscard]] bool find_move_chain_between_positions_impl(
        const binpack::TrainingDataEntry& curr_entry,
        const binpack::TrainingDataEntry& last_entry,
        uint64_t max_nodes,
        uint64_t& curr_nodes,
        std::vector<chess::Move>& reverse_chain_moves
    )
    {
        const chess::EnumArray<chess::Color, int> piece_count_diff = {
            last_entry.pos.piecesBB(chess::Color::White).count() - curr_entry.pos.piecesBB(chess::Color::White).count(),
            last_entry.pos.piecesBB(chess::Color::Black).count() - curr_entry.pos.piecesBB(chess::Color::Black).count()
        };

        const int ply_diff = last_entry.ply - curr_entry.ply;

        // Last position is older than current.
        if (ply_diff <= 0)
            return false;

        // Not enough plies for that many captures.
        if (piece_count_diff[chess::Color::White] + piece_count_diff[chess::Color::Black] > ply_diff)
            return false;

        // Not enough plies for that many captures. For each side separately.
        if (   piece_count_diff[chess::Color::White] > (ply_diff + 1) / 2
            || piece_count_diff[chess::Color::Black] > (ply_diff + 1) / 2)
            return false;

        std::vector<std::pair<chess::Move, int>> legal_moves;
        chess::movegen::forEachLegalMove(curr_entry.pos, [&](const chess::Move move) {
            int score = 0;

            // Moving a piece that's already on a correct square.
            if (curr_entry.pos.pieceAt(move.from) == last_entry.pos.pieceAt(move.from))
                score -= 10'000;

            // Moving a piece to a correct square.
            if (curr_entry.pos.pieceAt(move.from) == last_entry.pos.pieceAt(move.to))
                score += 10'000;

            // Not a capture move but needs to be a capture to fullfill piece difference.
            if (   (   piece_count_diff[chess::Color::White] + piece_count_diff[chess::Color::Black] == ply_diff
                    || (piece_count_diff[curr_entry.pos.sideToMove()] == (ply_diff + 1) / 2))
                && curr_entry.pos.pieceAt(move.to) == chess::Piece::none())
                score -= 10'000'000;

            legal_moves.emplace_back(move, score);
        });

        // A heuristic for searching the legal moves such that we hope to find the solution earlier.
        std::sort(legal_moves.begin(), legal_moves.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.second > rhs.second;
        });

        for (const auto [move, score] : legal_moves)
        {
            auto next_entry = curr_entry;
            next_entry.result = -next_entry.result;
            next_entry.ply += 1;
            next_entry.pos.doMove(move);

            // We reached the destination position.
            if (   next_entry.ply == last_entry.ply
                && next_entry.result == last_entry.result
                && next_entry.pos == last_entry.pos)
            {
                reverse_chain_moves.emplace_back(move);
                return true;
            }

            // Reached the search limit, aborting.
            if (++curr_nodes > max_nodes)
                return false;

            // We reached the destination position somewhere later in the search.
            if (   next_entry.ply < last_entry.ply
                && find_move_chain_between_positions_impl(next_entry, last_entry, max_nodes, curr_nodes, reverse_chain_moves))
            {
                reverse_chain_moves.emplace_back(move);
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] std::vector<chess::Move> find_move_chain_between_positions(
        const binpack::TrainingDataEntry& curr_entry,
        const binpack::TrainingDataEntry& next_entry,
        uint64_t max_nodes
    )
    {
        constexpr int MAX_PLY_DISTANCE = 6;
        if (   binpack::isContinuation(curr_entry, next_entry)
            || curr_entry.ply >= next_entry.ply
            || curr_entry.ply + MAX_PLY_DISTANCE < next_entry.ply)
            return {};

        std::vector<chess::Move> reverse_chain_moves;
        uint64_t curr_nodes = 0;
        if (find_move_chain_between_positions_impl(curr_entry, next_entry, max_nodes, curr_nodes, reverse_chain_moves))
        {
            std::reverse(reverse_chain_moves.begin(), reverse_chain_moves.end());
            return reverse_chain_moves;
        }
        else
        {
            return {};
        }
    }

    [[nodiscard]] bool discarded_during_training_based_on_move(const binpack::TrainingDataEntry& e)
    {
        return e.isCapturingMove() || e.isInCheck();
    }

    void do_minimize_binpack(MinimizeBinpackParams& params)
    {
        static constexpr int VALUE_NONE = 32002;

        if (!ends_with(params.input_filename, ".binpack"))
        {
            std::cerr << "Invalid input file type. Must be .binpack.\n";
            return;
        }

        std::atomic<std::uint64_t> num_positions_read = 0;
        std::atomic<std::uint64_t> num_positions_intermediate = 0;
        std::atomic<std::uint64_t> num_positions_filtered = 0;

        auto in = Tools::open_sfen_input_file(params.input_filename);
        auto readsome = [&in, mutex = std::mutex{}](int n) mutable -> std::vector<binpack::TrainingDataEntry> {

            std::vector<binpack::TrainingDataEntry> psv;
            psv.reserve(n);

            std::unique_lock lock(mutex);

            for (int i = 0; i < n; ++i)
            {
                auto ps_opt = in->next();
                if (ps_opt.has_value())
                {
                    psv.emplace_back(binpack::packedSfenValueToTrainingDataEntry(packed_sfen_tools_to_lib(*ps_opt)));
                }
                else
                {
                    break;
                }
            }

            return psv;
        };

        auto out = SfenWriter(
            params.output_filename,
            Threads.size(),
            std::numeric_limits<std::uint64_t>::max(),
            SfenOutputType::Binpack);

        Threads.execute_with_workers([&](auto& th){
            std::vector<binpack::TrainingDataEntry> intermediate_entries;

            auto write_one_intermediate = [&](const binpack::TrainingDataEntry& e) {
                intermediate_entries.emplace_back(e);

                // If a position already present in the data would have been discarded anyway
                // due to the move then we can set the score to something that takes less space.
                if (discarded_during_training_based_on_move(e))
                    intermediate_entries.back().score = 0;

                const auto pi = num_positions_intermediate.fetch_add(1) + 1;
                if (pi % 10000 == 0)
                {
                    const auto pr = num_positions_read.load();
                    const auto pf = num_positions_filtered.load();
                    std::cout << "Read: ~" << pr << ". Intermediate: " << pi << ". Write: ~" << pf << "\n";
                }
            };

            auto flush_intermediate_entries = [&]() {

                std::vector<binpack::TrainingDataEntry> filtered_entries;
                filtered_entries.reserve(intermediate_entries.size());

                // Remove positions that are always skipped and at the beginning of the chain.
                // Remove positions that are always skipped and at the end of the chain.
                // Remove chains of always skipped positions that are larger than starting a chain again.

                auto is_skipped = [&](const binpack::TrainingDataEntry& e) {
                    return e.score == VALUE_NONE || discarded_during_training_based_on_move(e);
                };

                for (size_t i = 0; i < intermediate_entries.size();)
                {
                    const auto& curr_entry = intermediate_entries[i];

                    if (is_skipped(curr_entry))
                    {
                        bool is_continuation = false;
                        if (!filtered_entries.empty())
                        {
                            const auto& prev_entry = filtered_entries.back();
                            is_continuation = binpack::isContinuation(prev_entry, curr_entry);
                        }

                        if (!is_continuation)
                        {
                            ++i;
                            continue;
                        }

                        bool is_tail = true;
                        size_t skip_run_end;
                        // Check if it's start of a tail.
                        for (skip_run_end = i + 1; skip_run_end < intermediate_entries.size(); ++skip_run_end)
                        {
                            // Go until we end the chain.
                            if (!binpack::isContinuation(intermediate_entries[skip_run_end - 1], intermediate_entries[skip_run_end]))
                                break;

                            // If we found a non-skippable position then this is not the tail and we cannot skip it entirely.
                            if (!is_skipped(intermediate_entries[skip_run_end]))
                            {
                                is_tail = false;
                                break;
                            }
                        }

                        // If tail then we don't save it at all.
                        if (is_tail)
                        {
                            // Remove (don't save) tail. Move to the next position to consider.
                            i = skip_run_end;
                            continue;
                        }

                        // Otherwise check if the skip run can be removed with a space saving.
                        if (skip_run_end - i < 6) // don't unnecessarily check total cost if upper bound is ~below 32 bytes
                        {
                            binpack::PackedMoveScoreList encoding;
                            for (size_t j = i; j < skip_run_end; ++j)
                            {
                                const auto& e = intermediate_entries[j];
                                encoding.addMoveScore(e.pos, e.move, e.score);
                            }

                            // Full new entry would have 32 bytes
                            if (encoding.numBytes() >= 32)
                            {
                                i = skip_run_end;
                                continue;
                            }
                        }
                    }

                    filtered_entries.emplace_back(curr_entry);
                    ++i;
                }

                num_positions_filtered.fetch_add(filtered_entries.size());
                for (auto& e : filtered_entries)
                {
                    const auto ps = packed_sfen_lib_to_tools(binpack::trainingDataEntryToPackedSfenValue(e));
                    out.write(th.id(), ps);
                }

                intermediate_entries.clear();
            };

            for (;;)
            {
                auto psv = readsome(10000);

                if (psv.empty())
                    break;

                for(size_t i = 0; i + 1 < psv.size(); ++i)
                {
                    num_positions_read.fetch_add(1);

                    binpack::TrainingDataEntry curr_entry = psv[i];
                    const binpack::TrainingDataEntry& next_entry = psv[i + 1];

                    auto move_chain = find_move_chain_between_positions(curr_entry, next_entry, params.chain_search_nodes);
                    if (move_chain.empty())
                    {
                        write_one_intermediate(curr_entry);
                    }
                    else
                    {
                        binpack::TrainingDataEntry e = curr_entry;

                        int j = 0;
                        for (const auto& move : move_chain)
                        {
                            e.move = move;
                            // If the positions would have been discarded with the old move but we changed the move
                            // then we need to mark the positions with VALUE_NONE score so that it's skipped.
                            if (j == 0 && discarded_during_training_based_on_move(curr_entry))
                                e.score = VALUE_NONE;
                            write_one_intermediate(e);

                            e.ply += 1;
                            e.result = -e.result;
                            e.score = VALUE_NONE; // change subsequent scores to VALUE_NONE so these new positions are ignored
                            e.pos.doMove(move);

                            j += 1;
                        }
                    }
                }

                num_positions_read.fetch_add(1);
                write_one_intermediate(psv.back());

                flush_intermediate_entries();
            }
        });
        Threads.wait_for_workers_finished();

        const auto pi = num_positions_intermediate.load();
        const auto pr = num_positions_read.load();
        const auto pf = num_positions_filtered.load();
        std::cout << "Read: " << pr << ". Intermediate: " << pi << ". Write: " << pf << "\n";
        std::cout << "Finished.\n";
    }

    void minimize_binpack(std::istringstream& is)
    {
        MinimizeBinpackParams params{};

        while(true)
        {
            std::string token;
            is >> token;

            if (token == "")
                break;

            else if (token == "input_file")
                is >> params.input_filename;
            else if (token == "output_file")
                is >> params.output_filename;
            else if (token == "debug_print")
                is >> params.debug_print;
            else if (token == "chain_search_nodes")
                is >> params.chain_search_nodes;
            else
            {
                std::cout << "ERROR: Unknown option " << token << ". Exiting...\n";
                return;
            }
        }

        std::cout << "Performing transform minimize_binpack with parameters:\n";
        std::cout << "input_file          : " << params.input_filename << '\n';
        std::cout << "output_file         : " << params.output_filename << '\n';
        std::cout << "debug_print         : " << params.debug_print << '\n';
        std::cout << "chain_search_nodes  : " << params.chain_search_nodes << '\n';
        std::cout << '\n';

        do_minimize_binpack(params);
    }

    void transform(std::istringstream& is)
    {
        const std::map<std::string, CommandFunc> subcommands = {
            { "nudged_static", &nudged_static },
            { "rescore", &rescore },
            { "filter_335a9b2d8a80", &filter_335a9b2d8a80 },
            { "minimize_binpack", &minimize_binpack }
        };

        Eval::NNUE::init();

        std::string subcommand;
        is >> subcommand;

        auto func = subcommands.find(subcommand);
        if (func == subcommands.end())
        {
            std::cout << "Invalid subcommand " << subcommand << ". Exiting...\n";
            return;
        }

        func->second(is);
    }

}
