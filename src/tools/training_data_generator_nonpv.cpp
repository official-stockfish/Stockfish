#include "training_data_generator_nonpv.h"

#include "sfen_writer.h"
#include "packed_sfen.h"
#include "opening_book.h"

#include "misc.h"
#include "position.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"

#include "extra/nnue_data_binpack_format.h"

#include "nnue/evaluate_nnue.h"

#include "syzygy/tbprobe.h"

#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <unordered_set>

using namespace std;

namespace Stockfish::Tools
{
    // Class to generate sfen with multiple threads
    struct TrainingDataGeneratorNonPv
    {
        struct Params
        {
            // The depth for search on the fens gathered during exploration
            int search_depth = 3;

            // the min/max number of nodes to use for exploration per ply
            int exploration_min_nodes = 5000;
            int exploration_max_nodes = 15000;

            // The pct of positions explored that are saved for rescoring
            float exploration_save_rate = 0.01;

            // Upper limit of evaluation value of generated situation
            int eval_limit = 4000;

            // the upper limit on evaluation during exploration selfplay
            int exploration_eval_limit = 4000;

            int exploration_max_ply = 200;

            int exploration_min_pieces = 8;

            std::string output_file_name = "training_data_nonpv";

            SfenOutputType sfen_format = SfenOutputType::Binpack;

            std::string seed;

            int num_threads;

            std::string book;

            bool smart_fen_skipping = false;

            void enforce_constraints()
            {
                // Limit the maximum to a one-stop score. (Otherwise you might not end the loop)
                eval_limit = std::min(eval_limit, (int)mate_in(2));
                exploration_eval_limit = std::min(eval_limit, (int)mate_in(2));
                exploration_min_nodes = std::max(100, exploration_min_nodes);
                exploration_max_nodes = std::max(exploration_min_nodes, exploration_max_nodes);

                num_threads = Options["Threads"];
            }
        };

        static constexpr uint64_t REPORT_DOT_EVERY = 5000;
        static constexpr uint64_t REPORT_STATS_EVERY = 200000;
        static_assert(REPORT_STATS_EVERY % REPORT_DOT_EVERY == 0);

        TrainingDataGeneratorNonPv(
            const Params& prm
        ) :
            params(prm),
            prng(prm.seed),
            sfen_writer(prm.output_file_name, prm.num_threads, std::numeric_limits<uint64_t>::max(), prm.sfen_format)
        {
            if (!prm.book.empty())
            {
                opening_book = open_opening_book(prm.book, prng);
                if (opening_book == nullptr)
                {
                    std::cout << "WARNING: Failed to open opening book " << prm.book << ". Falling back to startpos.\n";
                }
            }

            // Output seed to veryfy by the user if it's not identical by chance.
            std::cout << prng << std::endl;
        }

        void generate(uint64_t limit);

    private:
        Params params;

        PRNG prng;

        std::mutex stats_mutex;
        TimePoint last_stats_report_time;

        // sfen exporter
        SfenWriter sfen_writer;

        SynchronizedRegionLogger::Region out;

        std::unique_ptr<OpeningBook> opening_book;

        static void set_gensfen_search_limits();

        void generate_worker(
            Thread& th,
            std::atomic<uint64_t>& counter,
            uint64_t limit);

        bool commit_psv(
            Thread& th,
            PSVector& sfens,
            std::atomic<uint64_t>& counter,
            uint64_t limit);

        PSVector do_exploration(
            Thread& th,
            int count);

        void report(uint64_t done, uint64_t new_done);

        void maybe_report(uint64_t done);
    };

    void TrainingDataGeneratorNonPv::set_gensfen_search_limits()
    {
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
    }

    void TrainingDataGeneratorNonPv::generate(uint64_t limit)
    {
        last_stats_report_time = 0;

        set_gensfen_search_limits();

        std::atomic<uint64_t> counter{0};
        Threads.execute_with_workers([&counter, limit, this](Thread& th) {
            generate_worker(th, counter, limit);
        });
        Threads.wait_for_workers_finished();

        sfen_writer.flush();

        if (limit % REPORT_STATS_EVERY != 0)
        {
            report(limit, limit % REPORT_STATS_EVERY);
        }

        std::cout << std::endl;
    }

    PSVector TrainingDataGeneratorNonPv::do_exploration(
        Thread& th,
        int count)
    {
        constexpr int max_depth = 30;

        PSVector psv;

        std::vector<StateInfo, AlignedAllocator<StateInfo>> states(
            max_depth + MAX_PLY /* == search_depth_min + α */);

        th.set_eval_callback([this, &psv](Position& pos) {
            if ((double)prng.rand<uint64_t>() / std::numeric_limits<uint64_t>::max() < params.exploration_save_rate)
            {
                psv.emplace_back();
                pos.sfen_pack(psv.back().sfen, pos.is_chess960());
            }
        });

        auto& pos = th.rootPos;
        StateInfo si;

        const bool frc = Options["UCI_Chess960"];
        for (int i = 0; i < count; ++i)
        {
            if (opening_book != nullptr)
            {
                auto& fen = opening_book->next_fen();
                pos.set(fen, frc, &si, &th);
            }
            else
            {
                pos.set(StartFEN, frc, &si, &th);
            }

            for(int ply = 0; ply < params.exploration_max_ply; ++ply)
            {
                auto nodes = prng.rand(params.exploration_max_nodes - params.exploration_min_nodes + 1) + params.exploration_min_nodes;

                auto [search_value, search_pv] = Search::search(pos, max_depth, 1, nodes);

                if (search_pv.empty())
                {
                    break;
                }

                if (std::abs(search_value) > params.exploration_eval_limit)
                {
                    break;
                }

                pos.do_move(search_pv[0], states[ply]);

                if (popcount(pos.pieces()) < params.exploration_min_pieces)
                {
                    break;
                }
            }
        }

        th.clear_eval_callback();

        return psv;
    }

    void TrainingDataGeneratorNonPv::generate_worker(
        Thread& th,
        std::atomic<uint64_t>& counter,
        uint64_t limit)
    {
        constexpr int exploration_batch_size = 1;

        StateInfo si;

        PSVector psv;

        // end flag
        bool quit = false;

        const bool frc = Options["UCI_Chess960"];
        // repeat until the specified number of times
        while (!quit)
        {
            // It is necessary to set a dependent thread for Position.
            // When parallelizing, Threads (since this is a vector<Thread*>,
            // Do the same for up to Threads[0]...Threads[thread_num-1].
            auto& pos = th.rootPos;

            auto packed_sfens = do_exploration(th, exploration_batch_size);
            psv.clear();

            for (auto& ps : packed_sfens)
            {
                pos.set_from_packed_sfen(ps.sfen, &si, &th, frc);
                pos.state()->rule50 = 0;

                if (params.smart_fen_skipping && pos.checkers())
                {
                    continue;
                }

                auto [search_value, search_pv] = Search::search(pos, params.search_depth, 1);

                if (search_pv.empty())
                {
                    continue;
                }

                if (std::abs(search_value) > params.eval_limit)
                {
                    continue;
                }

                if (params.smart_fen_skipping && pos.capture_or_promotion(search_pv[0]))
                {
                    continue;
                }

                auto& new_ps = psv.emplace_back();
                pos.sfen_pack(new_ps.sfen, pos.is_chess960());
                new_ps.score = search_value;
                new_ps.move = search_pv[0];
                new_ps.gamePly = 1;
                new_ps.game_result = 0;
                new_ps.padding = 0;
            }

            quit = commit_psv(th, psv, counter, limit);
        }
    }

    // Write out the phases loaded in sfens to a file.
    // result: win/loss in the next phase after the final phase in sfens
    // 1 when winning. -1 when losing. Pass 0 for a draw.
    // Return value: true if the specified number of
    // sfens has already been reached and the process ends.
    bool TrainingDataGeneratorNonPv::commit_psv(
        Thread& th,
        PSVector& sfens,
        std::atomic<uint64_t>& counter,
        uint64_t limit)
    {
        const bool frc = th.rootPos.is_chess960();
        // Write sfens in move order to make potential compression easier
        for (auto& sfen : sfens)
        {
            // Skip positions with castling bestmove in FRC so that we don't
            // need to support it in the trainer.
            if (frc && type_of((Move)sfen.move) == CASTLING)
            {
                continue;
            }

            // Return true if there is already enough data generated.
            const auto iter = counter.fetch_add(1);
            if (iter >= limit)
                return true;

            // because `iter` was done, now we do one more
            maybe_report(iter + 1);

            // Write out one sfen.
            sfen_writer.write(th.id(), sfen);
        }

        return false;
    }

    void TrainingDataGeneratorNonPv::report(uint64_t done, uint64_t new_done)
    {
        const auto now_time = now();
        const TimePoint elapsed = now_time - last_stats_report_time + 1;

        out
            << endl
            << done << " sfens, "
            << new_done * 1000 / elapsed << " sfens/second, "
            << "at " << now_string() << sync_endl;

        last_stats_report_time = now_time;

        out = sync_region_cout.new_region();
    }

    void TrainingDataGeneratorNonPv::maybe_report(uint64_t done)
    {
        if (done % REPORT_DOT_EVERY == 0)
        {
            std::lock_guard lock(stats_mutex);

            if (last_stats_report_time == 0)
            {
                last_stats_report_time = now();
                out = sync_region_cout.new_region();
            }

            if (done != 0)
            {
                out << '.';

                if (done % REPORT_STATS_EVERY == 0)
                {
                    report(done, REPORT_STATS_EVERY);
                }
            }
        }
    }

    // Command to generate a game record
    void generate_training_data_nonpv(istringstream& is)
    {
        // Number of generated game records default = 8 billion phases (Ponanza specification)
        TrainingDataGeneratorNonPv::Params params;

        uint64_t count = 1'000'000;

        // Add a random number to the end of the file name.
        std::string sfen_format = "binpack";

        string token;
        while (true)
        {
            token = "";
            is >> token;
            if (token == "")
                break;

            if (token == "depth")
                is >> params.search_depth;
            else if (token == "count")
                is >> count;
            else if (token == "output_file")
                is >> params.output_file_name;
            else if (token == "exploration_eval_limit")
                is >> params.exploration_eval_limit;
            else if (token == "eval_limit")
                is >> params.eval_limit;
            else if (token == "exploration_min_nodes")
                is >> params.exploration_min_nodes;
            else if (token == "exploration_max_nodes")
                is >> params.exploration_max_nodes;
            else if (token == "exploration_min_pieces")
                is >> params.exploration_min_pieces;
            else if (token == "exploration_save_rate")
                is >> params.exploration_save_rate;
            else if (token == "book")
                is >> params.book;
            else if (token == "data_format")
                is >> sfen_format;
            else if (token == "seed")
                is >> params.seed;
            else if (token == "smart_fen_skipping")
                params.smart_fen_skipping = true;
            else if (token == "set_recommended_uci_options")
            {
                UCI::setoption("Skill Level", "20");
                UCI::setoption("UCI_LimitStrength", "false");
                UCI::setoption("PruneAtShallowDepth", "false");
                UCI::setoption("EnableTranspositionTable", "true");
            }
            else
            {
                cout << "ERROR: Unknown option " << token << ". Exiting...\n";
                return;
            }
        }

        if (!sfen_format.empty())
        {
            if (sfen_format == "bin")
                params.sfen_format = SfenOutputType::Bin;
            else if (sfen_format == "binpack")
                params.sfen_format = SfenOutputType::Binpack;
            else
                cout << "WARNING: Unknown sfen format `" << sfen_format << "`. Using bin\n";
        }

        params.enforce_constraints();

        std::cout << "INFO: Executing generate_training_data_nonpv command\n";

        std::cout << "INFO: Parameters:\n";
        std::cout
            << "  - search_depth           = " << params.search_depth << endl
            << "  - output_file            = " << params.output_file_name << endl
            << "  - exploration_eval_limit = " << params.exploration_eval_limit << endl
            << "  - eval_limit             = " << params.eval_limit << endl
            << "  - exploration_min_nodes  = " << params.exploration_min_nodes << endl
            << "  - exploration_max_nodes  = " << params.exploration_max_nodes << endl
            << "  - exploration_min_pieces = " << params.exploration_min_pieces << endl
            << "  - exploration_save_rate  = " << params.exploration_save_rate << endl
            << "  - book                   = " << params.book << endl
            << "  - data_format            = " << sfen_format << endl
            << "  - seed                   = " << params.seed << endl
            << "  - count                  = " << count << endl;

        // Show if the training data generator uses NNUE.
        Eval::NNUE::verify();

        Threads.main()->ponder = false;

        TrainingDataGeneratorNonPv gensfen(params);
        gensfen.generate(count);

        std::cout << "INFO: generate_training_data_nonpv finished." << endl;
    }
}
