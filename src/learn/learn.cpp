// Learning routines:
//
// 1) Automatic generation of game records in .bin format
// → "gensfen" command
//
// 2) Learning evaluation function parameters from the generated .bin files
// → "learn" command
//
// → Shuffle in the teacher phase is also an extension of this command.
// Example) "learn shuffle"
//
// 3) Automatic generation of fixed traces
// → "makebook think" command
// → implemented in extra/book/book.cpp
//
// 4) Post-station automatic review mode
// → I will not be involved in the engine because it is a problem that the GUI should assist.
// etc..

#include "learn.h"

#include "sfen_reader.h"

#include "misc.h"
#include "position.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "search.h"
#include "timeman.h"

#include "nnue/evaluate_nnue.h"
#include "nnue/evaluate_nnue_learner.h"

#include "syzygy/tbprobe.h"

#include <chrono>
#include <climits>
#include <cmath>    // std::exp(),std::pow(),std::log()
#include <cstring>  // memcpy()
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <random>
#include <regex>
#include <shared_mutex>
#include <sstream>
#include <unordered_set>
#include <iostream>

#if defined (_OPENMP)
#include <omp.h>
#endif

extern double global_learning_rate;

using namespace std;

template <typename T>
T operator +=(std::atomic<T>& x, const T rhs)
{
    T old = x.load(std::memory_order_consume);

    // It is allowed that the value is rewritten from other thread at this timing.
    // The idea that the value is not destroyed is good.
    T desired = old + rhs;
    while (!x.compare_exchange_weak(old, desired, std::memory_order_release, std::memory_order_consume))
        desired = old + rhs;
    return desired;
}
template <typename T>
T operator -= (std::atomic<T>& x, const T rhs) { return x += -rhs; }

namespace Learner
{
    static bool use_draw_games_in_training = true;
    static bool use_draw_games_in_validation = true;
    static bool skip_duplicated_positions_in_training = true;

    static double winning_probability_coefficient = 1.0 / PawnValueEg / 4.0 * std::log(10.0);

    // Score scale factors. ex) If we set src_score_min_value = 0.0,
    // src_score_max_value = 1.0, dest_score_min_value = 0.0,
    // dest_score_max_value = 10000.0, [0.0, 1.0] will be scaled to [0, 10000].
    static double src_score_min_value = 0.0;
    static double src_score_max_value = 1.0;
    static double dest_score_min_value = 0.0;
    static double dest_score_max_value = 1.0;

    // Using stockfish's WDL with win rate model instead of sigmoid
    static bool use_wdl = false;

    namespace Detail {
        template <bool AtomicV>
        struct Loss
        {
            using T =
                std::conditional_t<
                    AtomicV,
                    atomic<double>,
                    double
                >;

            T cross_entropy_eval{0.0};
            T cross_entropy_win{0.0};
            T cross_entropy{0.0};
            T entropy_eval{0.0};
            T entropy_win{0.0};
            T entropy{0.0};
            T count{0.0};

            template <bool OtherAtomicV>
            Loss& operator += (const Loss<OtherAtomicV>& rhs)
            {
                cross_entropy_eval += rhs.cross_entropy_eval;
                cross_entropy_win += rhs.cross_entropy_win;
                cross_entropy += rhs.cross_entropy;
                entropy_eval += rhs.entropy_eval;
                entropy_win += rhs.entropy_win;
                entropy += rhs.entropy;
                count += rhs.count;

                return *this;
            }

            void reset()
            {
                cross_entropy_eval = 0.0;
                cross_entropy_win = 0.0;
                cross_entropy = 0.0;
                entropy_eval = 0.0;
                entropy_win = 0.0;
                entropy = 0.0;
                count = 0.0;
            }

            void print(const std::string& prefix, ostream& s) const
            {
                s
                    << "INFO: "
                    << prefix << "_cross_entropy_eval = " << cross_entropy_eval / count
                    << " , " << prefix << "_cross_entropy_win = " << cross_entropy_win / count
                    << " , " << prefix << "_entropy_eval = " << entropy_eval / count
                    << " , " << prefix << "_entropy_win = " << entropy_win / count
                    << " , " << prefix << "_cross_entropy = " << cross_entropy / count
                    << " , " << prefix << "_entropy = " << entropy / count
                    << endl;
            }
        };
    }

    using Loss = Detail::Loss<false>;
    using AtomicLoss = Detail::Loss<true>;

    static void append_files_from_dir(
        std::vector<std::string>& filenames,
        const std::string& base_dir,
        const std::string& target_dir)
    {
        string kif_base_dir = Path::combine(base_dir, target_dir);

        namespace sys = std::filesystem;
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

    // A function that converts the evaluation value to the winning rate [0,1]
    double winning_percentage(double value)
    {
        // 1/(1+10^(-Eval/4))
        // = 1/(1+e^(-Eval/4*ln(10))
        // = sigmoid(Eval/4*ln(10))
        return Math::sigmoid(value * winning_probability_coefficient);
    }

    // A function that converts the evaluation value to the winning rate [0,1]
    double winning_percentage_wdl(double value, int ply)
    {
        constexpr double wdl_total = 1000.0;
        constexpr double draw_score = 0.5;

        const double wdl_w = UCI::win_rate_model_double(value, ply);
        const double wdl_l = UCI::win_rate_model_double(-value, ply);
        const double wdl_d = wdl_total - wdl_w - wdl_l;

        return (wdl_w + wdl_d * draw_score) / wdl_total;
    }

    // A function that converts the evaluation value to the winning rate [0,1]
    double winning_percentage(double value, int ply)
    {
        if (use_wdl)
        {
            return winning_percentage_wdl(value, ply);
        }
        else
        {
            return winning_percentage(value);
        }
    }

    double calc_cross_entropy_of_winning_percentage(
        double deep_win_rate,
        double shallow_eval,
        int ply)
    {
        const double p = deep_win_rate;
        const double q = winning_percentage(shallow_eval, ply);
        return -p * std::log(q) - (1.0 - p) * std::log(1.0 - q);
    }

    double calc_d_cross_entropy_of_winning_percentage(
        double deep_win_rate,
        double shallow_eval,
        int ply)
    {
        constexpr double epsilon = 0.000001;

        const double y1 = calc_cross_entropy_of_winning_percentage(
            deep_win_rate, shallow_eval, ply);

        const double y2 = calc_cross_entropy_of_winning_percentage(
            deep_win_rate, shallow_eval + epsilon, ply);

        // Divide by the winning_probability_coefficient to
        // match scale with the sigmoidal win rate
        return ((y2 - y1) / epsilon) / winning_probability_coefficient;
    }

    // A constant used in elmo (WCSC27). Adjustment required.
    // Since elmo does not internally divide the expression, the value is different.
    // You can set this value with the learn command.
    // 0.33 is equivalent to the constant (0.5) used in elmo (WCSC27)
    double ELMO_LAMBDA = 0.33;
    double ELMO_LAMBDA2 = 0.33;
    double ELMO_LAMBDA_LIMIT = 32000;

    // Training Formula · Issue #71 · nodchip/Stockfish https://github.com/nodchip/Stockfish/issues/71
    double get_scaled_signal(double signal)
    {
        double scaled_signal = signal;

        // Normalize to [0.0, 1.0].
        scaled_signal =
            (scaled_signal - src_score_min_value)
            / (src_score_max_value - src_score_min_value);

        // Scale to [dest_score_min_value, dest_score_max_value].
        scaled_signal =
            scaled_signal * (dest_score_max_value - dest_score_min_value)
            + dest_score_min_value;

        return scaled_signal;
    }

    // Teacher winning probability.
    double calculate_p(double teacher_signal, int ply)
    {
        const double scaled_teacher_signal = get_scaled_signal(teacher_signal);
        return winning_percentage(scaled_teacher_signal, ply);
    }

    double calculate_lambda(double teacher_signal)
    {
        // If the evaluation value in deep search exceeds ELMO_LAMBDA_LIMIT
        // then apply ELMO_LAMBDA2 instead of ELMO_LAMBDA.
        const double lambda =
            (std::abs(teacher_signal) >= ELMO_LAMBDA_LIMIT)
            ? ELMO_LAMBDA2
            : ELMO_LAMBDA;

        return lambda;
    }

    double calculate_t(int game_result)
    {
        // Use 1 as the correction term if the expected win rate is 1,
        // 0 if you lose, and 0.5 if you draw.
        // game_result = 1,0,-1 so add 1 and divide by 2.
        const double t = double(game_result + 1) * 0.5;

        return t;
    }

    double calc_grad(Value teacher_signal, Value shallow, const PackedSfenValue& psv)
    {
        // elmo (WCSC27) method
        // Correct with the actual game wins and losses.
        const double q = winning_percentage(shallow, psv.gamePly);
        const double p = calculate_p(teacher_signal, psv.gamePly);
        const double t = calculate_t(psv.game_result);
        const double lambda = calculate_lambda(teacher_signal);

        double grad;
        if (use_wdl)
        {
            const double dce_p = calc_d_cross_entropy_of_winning_percentage(p, shallow, psv.gamePly);
            const double dce_t = calc_d_cross_entropy_of_winning_percentage(t, shallow, psv.gamePly);
            grad = lambda * dce_p + (1.0 - lambda) * dce_t;
        }
        else
        {
            // Use the actual win rate as a correction term.
            // This is the idea of ​​elmo (WCSC27), modern O-parts.
            grad = lambda * (q - p) + (1.0 - lambda) * (q - t);
        }

        return grad;
    }

    // Calculate cross entropy during learning
    // The individual cross entropy of the win/loss term and win
    // rate term of the elmo expression is returned
    // to the arguments cross_entropy_eval and cross_entropy_win.
    Loss calc_cross_entropy(
        Value teacher_signal,
        Value shallow,
        const PackedSfenValue& psv)
    {
        // Teacher winning probability.
        const double q = winning_percentage(shallow, psv.gamePly);
        const double p = calculate_p(teacher_signal, psv.gamePly);
        const double t = calculate_t(psv.game_result);
        const double lambda = calculate_lambda(teacher_signal);

        constexpr double epsilon = 0.000001;

        const double m = (1.0 - lambda) * t + lambda * p;

        Loss loss{};

        loss.cross_entropy_eval =
            (-p * std::log(q + epsilon) - (1.0 - p) * std::log(1.0 - q + epsilon));
        loss.cross_entropy_win =
            (-t * std::log(q + epsilon) - (1.0 - t) * std::log(1.0 - q + epsilon));
        loss.entropy_eval =
            (-p * std::log(p + epsilon) - (1.0 - p) * std::log(1.0 - p + epsilon));
        loss.entropy_win =
            (-t * std::log(t + epsilon) - (1.0 - t) * std::log(1.0 - t + epsilon));

        loss.cross_entropy =
            (-m * std::log(q + epsilon) - (1.0 - m) * std::log(1.0 - q + epsilon));
        loss.entropy =
            (-m * std::log(m + epsilon) - (1.0 - m) * std::log(1.0 - m + epsilon));

        loss.count = 1;

        return loss;
    }

    // Other objective functions may be considered in the future...
    double calc_grad(Value shallow, const PackedSfenValue& psv)
    {
        return calc_grad((Value)psv.score, shallow, psv);
    }

    // Class to generate sfen with multiple threads
    struct LearnerThink
    {
        // Number of phases used for calculation such as mse
        // mini-batch size = 1M is standard, so 0.2% of that should be negligible in terms of time.
        // Since search() is performed with depth = 1 in calculation of
        // move match rate, simple comparison is not possible...
        static constexpr uint64_t sfen_for_mse_size = 2000;

        LearnerThink(
            const std::vector<std::string>& filenames,
            bool shuffle,
            uint64_t thread_num,
            const std::string& seed
        ) :
            prng(seed),
            sr(filenames, shuffle, SfenReaderMode::Cyclic, thread_num, std::to_string(prng.next_random_seed())),
            learn_loss_sum{}
        {
            save_only_once = false;
            save_count = 0;
            loss_output_count = 0;
            newbob_decay = 1.0;
            newbob_num_trials = 2;
            auto_lr_drop = 0;
            last_lr_drop = 0;
            best_loss = std::numeric_limits<double>::infinity();
            latest_loss_sum = 0.0;
            latest_loss_count = 0;
            total_done = 0;
        }

        void learn(uint64_t epochs);


        std::string validation_set_file_name;

        // Mini batch size size. Be sure to set it on the side that uses this class.
        uint64_t mini_batch_size = LEARN_MINI_BATCH_SIZE;

        // Option to exclude early stage from learning
        int reduction_gameply;

        // If the absolute value of the evaluation value of the deep search
        // of the teacher phase exceeds this value, discard the teacher phase.
        int eval_limit;

        // Flag whether to dig a folder each time the evaluation function is saved.
        // If true, do not dig the folder.
        bool save_only_once;

        double newbob_decay;
        int newbob_num_trials;
        uint64_t auto_lr_drop;

        std::string best_nn_directory;

        uint64_t eval_save_interval;
        uint64_t loss_output_interval;

    private:
        void learn_worker(Thread& th, std::atomic<uint64_t>& counter, uint64_t limit);

        void update_weights(const PSVector& psv, uint64_t epoch);

        void calc_loss(const PSVector& psv, uint64_t epoch);

        void calc_loss_worker(
            Thread& th,
            std::atomic<uint64_t>& counter,
            const PSVector& psv,
            AtomicLoss& test_loss_sum,
            atomic<double>& sum_norm,
            atomic<int>& move_accord_count
        );

        Value get_shallow_value(Position& pos);

        // save merit function parameters to a file
        bool save(bool is_final = false);

        PRNG prng;

        // sfen reader
        SfenReader sr;

        uint64_t save_count;
        uint64_t loss_output_count;

        std::atomic<bool> stop_flag;

        uint64_t total_done;

        uint64_t last_lr_drop;
        double best_loss;
        double latest_loss_sum;
        uint64_t latest_loss_count;

        // For calculation of learning data loss
        AtomicLoss learn_loss_sum;
    };

    void LearnerThink::learn(uint64_t epochs)
    {

#if defined(_OPENMP)
        omp_set_num_threads((int)Options["Threads"]);
#endif

        Eval::NNUE::verify_any_net_loaded();

        const PSVector sfen_for_mse =
            validation_set_file_name.empty()
            ? sr.read_for_mse(sfen_for_mse_size)
            : sr.read_validation_set(validation_set_file_name, eval_limit, use_draw_games_in_validation);

        if (validation_set_file_name.empty()
            && sfen_for_mse.size() != sfen_for_mse_size)
        {
            cout
                << "Error reading sfen_for_mse. Read " << sfen_for_mse.size()
                << " out of " << sfen_for_mse_size << '\n';

            return;
        }

        if (newbob_decay != 1.0) {

            calc_loss(sfen_for_mse, 0);

            best_loss = latest_loss_sum / latest_loss_count;
            latest_loss_sum = 0.0;
            latest_loss_count = 0;

            cout << "initial loss: " << best_loss << endl;
        }

        stop_flag = false;

        for(uint64_t epoch = 1; epoch <= epochs; ++epoch)
        {
            std::atomic<uint64_t> counter{0};

            Threads.execute_with_workers([this, &counter](auto& th){
                learn_worker(th, counter, mini_batch_size);
            });

            total_done += mini_batch_size;

            Threads.wait_for_workers_finished();

            if (stop_flag)
                break;

            update_weights(sfen_for_mse, epoch);

            if (stop_flag)
                break;
        }

        Eval::NNUE::finalize_net();

        save(true);
    }

    void LearnerThink::learn_worker(Thread& th, std::atomic<uint64_t>& counter, uint64_t limit)
    {
        const auto thread_id = th.thread_idx();
        auto& pos = th.rootPos;

        Loss local_loss_sum{};
        std::vector<StateInfo, AlignedAllocator<StateInfo>> state(MAX_PLY);

        while(!stop_flag)
        {
            const auto iter = counter.fetch_add(1);
            if (iter >= limit)
                break;

            PackedSfenValue ps;

        RETRY_READ:;

            if (!sr.read_to_thread_buffer(thread_id, ps))
            {
                // If we ran out of data we stop completely
                // because there's nothing left to do.
                stop_flag = true;
                break;
            }

            if (eval_limit < abs(ps.score))
                goto RETRY_READ;

            if (!use_draw_games_in_training && ps.game_result == 0)
                goto RETRY_READ;

            // Skip over the opening phase
            if (ps.gamePly < prng.rand(reduction_gameply))
                goto RETRY_READ;

            StateInfo si;
            if (pos.set_from_packed_sfen(ps.sfen, &si, &th) != 0)
            {
                // Malformed sfen
                cout << "Error! : illigal packed sfen = " << pos.fen() << endl;
                goto RETRY_READ;
            }

            const auto rootColor = pos.side_to_move();

            // A function that adds the current `pos` and `ps`
            // to the training set.
            auto pos_add_grad = [&]() {

                // Evaluation value of deep search
                const auto deep_value = (Value)ps.score;

                const Value shallow_value =
                    (rootColor == pos.side_to_move())
                    ? Eval::evaluate(pos)
                    : -Eval::evaluate(pos);

                const auto loss = calc_cross_entropy(
                    deep_value,
                    shallow_value,
                    ps);

                local_loss_sum += loss;

                Eval::NNUE::add_example(pos, rootColor, ps, 1.0);
            };

            if (!pos.pseudo_legal((Move)ps.move) || !pos.legal((Move)ps.move))
            {
                goto RETRY_READ;
            }

            int ply = 0;
            pos.do_move((Move)ps.move, state[ply++]);

            // We want to position being trained on not to be terminal
            if (MoveList<LEGAL>(pos).size() == 0)
                goto RETRY_READ;

            // Evaluation value of shallow search (qsearch)
            const auto [_, pv] = Search::qsearch(pos);

            for (auto m : pv)
            {
                pos.do_move(m, state[ply++]);
            }

            // Since we have reached the end phase of PV, add the slope here.
            pos_add_grad();
        }

        learn_loss_sum += local_loss_sum;
    }

    void LearnerThink::update_weights(const PSVector& psv, uint64_t epoch)
    {
        // I'm not sure this fencing is correct. But either way there
        // should be no real issues happening since
        // the read/write phases are isolated.
        atomic_thread_fence(memory_order_seq_cst);
        Eval::NNUE::update_parameters();
        atomic_thread_fence(memory_order_seq_cst);

        if (++save_count * mini_batch_size >= eval_save_interval)
        {
            save_count = 0;

            const bool converged = save();
            if (converged)
            {
                stop_flag = true;
                return;
            }
        }

        if (++loss_output_count * mini_batch_size >= loss_output_interval)
        {
            loss_output_count = 0;

            // loss calculation
            calc_loss(psv, epoch);

            Eval::NNUE::check_health();
        }
    }

    void LearnerThink::calc_loss(const PSVector& psv, uint64_t epoch)
    {
        TT.new_search();
        TimePoint elapsed = now() - Search::Limits.startTime + 1;

        cout << "PROGRESS: " << now_string() << ", ";
        cout << total_done << " sfens, ";
        cout << total_done * 1000 / elapsed  << " sfens/second";
        cout << ", iteration " << epoch;
        cout << ", learning rate = " << global_learning_rate << ", ";

        // For calculation of verification data loss
        AtomicLoss test_loss_sum{};

        // norm for learning
        atomic<double> sum_norm{0.0};

        // The number of times the pv first move of deep
        // search matches the pv first move of search(1).
        atomic<int> move_accord_count{0};

        auto mainThread = Threads.main();
        mainThread->execute_with_worker([](auto& th){
            auto& pos = th.rootPos;
            StateInfo si;
            pos.set(StartFEN, false, &si, &th);
            cout << "startpos eval = " << Eval::evaluate(pos) << endl;
        });
        mainThread->wait_for_worker_finished();

        // The number of tasks to do.
        atomic<uint64_t> counter{0};
        Threads.execute_with_workers([&](auto& th){
            calc_loss_worker(
                th,
                counter,
                psv,
                test_loss_sum,
                sum_norm,
                move_accord_count
            );
        });
        Threads.wait_for_workers_finished();

        latest_loss_sum += test_loss_sum.cross_entropy - test_loss_sum.entropy;
        latest_loss_count += psv.size();

        if (psv.size() && test_loss_sum.count > 0.0)
        {
            cout << "INFO: norm = " << sum_norm
                << " , move accuracy = " << (move_accord_count * 100.0 / psv.size()) << "%"
                << endl;

            test_loss_sum.print("test", cout);

            if (learn_loss_sum.count > 0.0)
            {
                learn_loss_sum.print("learn", cout);
            }
        }
        else
        {
            cout << "Error! : psv.size() = " << psv.size() << " ,  done = " << test_loss_sum.count << endl;
        }

        learn_loss_sum.reset();
    }

    void LearnerThink::calc_loss_worker(
        Thread& th,
        std::atomic<uint64_t>& counter,
        const PSVector& psv,
        AtomicLoss& test_loss_sum,
        atomic<double>& sum_norm,
        atomic<int>& move_accord_count
    )
    {
        Loss local_loss_sum{};
        auto& pos = th.rootPos;

        for(;;)
        {
            const auto task_id = counter.fetch_add(1);
            if (task_id >= psv.size())
            {
                break;
            }

            const auto& ps = psv[task_id];

            StateInfo si;
            if (pos.set_from_packed_sfen(ps.sfen, &si, &th) != 0)
            {
                cout << "Error! : illegal packed sfen " << pos.fen() << endl;
                continue;
            }

            const Value shallow_value = get_shallow_value(pos);

            // Evaluation value of deep search
            const auto deep_value = (Value)ps.score;

            const auto loss = calc_cross_entropy(
                deep_value,
                shallow_value,
                ps);

            local_loss_sum += loss;
            sum_norm += (double)abs(shallow_value);

            // Determine if the teacher's move and the score of the shallow search match
            const auto [value, pv] = Search::search(pos, 1);
            if (pv.size() > 0 && (uint16_t)pv[0] == ps.move)
                move_accord_count.fetch_add(1, std::memory_order_relaxed);
        }

        test_loss_sum += local_loss_sum;
    }

    Value LearnerThink::get_shallow_value(Position& pos)
    {
        // Evaluation value for shallow search
        // The value of evaluate() may be used, but when calculating loss, learn_cross_entropy and
        // Use qsearch() because it is difficult to compare the values.
        // EvalHash has been disabled in advance. (If not, the same value will be returned every time)
        const auto [_, pv] = Search::qsearch(pos);

        const auto rootColor = pos.side_to_move();

        std::vector<StateInfo, AlignedAllocator<StateInfo>> states(pv.size());
        for (size_t i = 0; i < pv.size(); ++i)
        {
            pos.do_move(pv[i], states[i]);
        }

        const Value shallow_value =
            (rootColor == pos.side_to_move())
            ? Eval::evaluate(pos)
            : -Eval::evaluate(pos);

        for (auto it = pv.rbegin(); it != pv.rend(); ++it)
            pos.undo_move(*it);

        return shallow_value;
    }

    // Write evaluation function file.
    bool LearnerThink::save(bool is_final)
    {
        // Each time you save, change the extension part of the file name like "0","1","2",..
        // (Because I want to compare the winning rate for each evaluation function parameter later)

        if (save_only_once)
        {
            // When EVAL_SAVE_ONLY_ONCE is defined,
            // Do not dig a subfolder because I want to save it only once.
            Eval::NNUE::save_eval("");
        }
        else if (is_final)
        {
            Eval::NNUE::save_eval("final");
            return true;
        }
        else
        {
            static int dir_number = 0;
            const std::string dir_name = std::to_string(dir_number++);
            Eval::NNUE::save_eval(dir_name);

            if (newbob_decay != 1.0 && latest_loss_count > 0) {
                static int trials = newbob_num_trials;
                const double latest_loss = latest_loss_sum / latest_loss_count;
                latest_loss_sum = 0.0;
                latest_loss_count = 0;
                cout << "loss: " << latest_loss;
                auto tot = total_done;
                if (auto_lr_drop)
                {
                    cout << " < best (" << best_loss << "), accepted" << endl;
                    best_loss = latest_loss;
                    best_nn_directory = Path::combine((std::string)Options["EvalSaveDir"], dir_name);
                    trials = newbob_num_trials;

                    if (tot >= last_lr_drop + auto_lr_drop)
                    {
                        last_lr_drop = tot;
                        global_learning_rate *= newbob_decay;
                    }
                }
                else if (latest_loss < best_loss)
                {
                    cout << " < best (" << best_loss << "), accepted" << endl;
                    best_loss = latest_loss;
                    best_nn_directory = Path::combine((std::string)Options["EvalSaveDir"], dir_name);
                    trials = newbob_num_trials;
                }
                else
                {
                    cout << " >= best (" << best_loss << "), rejected" << endl;
                    best_nn_directory = Path::combine((std::string)Options["EvalSaveDir"], dir_name);

                    if (--trials > 0 && !is_final)
                    {
                        cout
                            << "reducing learning rate from " << global_learning_rate
                            << " to " << (global_learning_rate * newbob_decay)
                            << " (" << trials << " more trials)" << endl;

                        global_learning_rate *= newbob_decay;
                    }
                }

                if (trials == 0)
                {
                    cout << "converged" << endl;
                    return true;
                }
            }
        }
        return false;
    }

    static void set_learning_search_limits()
    {
        // About Search::Limits
        // Be careful because this member variable is global and affects other threads.
        auto& limits = Search::Limits;

        limits.startTime = now();

        // Make the search equivalent to the "go infinite" command. (Because it is troublesome if time management is done)
        limits.infinite = true;

        // Since PV is an obstacle when displayed, erase it.
        limits.silent = true;

        // If you use this, it will be compared with the accumulated nodes of each thread. Therefore, do not use it.
        limits.nodes = 0;

        // depth is also processed by the one passed as an argument of Learner::search().
        limits.depth = 0;
    }

    // Learning from the generated game record
    void learn(Position&, istringstream& is)
    {
        const auto thread_num = (int)Options["Threads"];

        vector<string> filenames;

        // mini_batch_size 1M aspect by default. This can be increased.
        auto mini_batch_size = LEARN_MINI_BATCH_SIZE;

        // Number of epochs
        uint64_t epochs = std::numeric_limits<uint64_t>::max();

        // Game file storage folder (get game file with relative path from here)
        string base_dir;
        string target_dir;

        // If the absolute value of the evaluation value
        // in the deep search of the teacher phase exceeds this value,
        // that phase is discarded.
        int eval_limit = 32000;

        // Flag to save the evaluation function file only once near the end.
        bool save_only_once = false;

        // Shuffle about what you are pre-reading on the teacher aspect.
        // (Shuffle of about 10 million phases)
        // Turn on if you want to pass a pre-shuffled file.
        bool no_shuffle = false;

        global_learning_rate = 1.0;

        // elmo lambda
        ELMO_LAMBDA = 1.0;
        ELMO_LAMBDA2 = 1.0;
        ELMO_LAMBDA_LIMIT = 32000;

        // if (gamePly <rand(reduction_gameply)) continue;
        // An option to exclude the early stage from the learning target moderately like
        // If set to 1, rand(1)==0, so nothing is excluded.
        int reduction_gameply = 1;

        uint64_t nn_batch_size = 1000;
        double newbob_decay = 0.5;
        int newbob_num_trials = 4;
        uint64_t auto_lr_drop = 0;
        string nn_options;

        uint64_t eval_save_interval = LEARN_EVAL_SAVE_INTERVAL;
        uint64_t loss_output_interval = 1'000'000;

        string validation_set_file_name;
        string seed;

        // Assume the filenames are staggered.
        while (true)
        {
            string option;
            is >> option;

            if (option == "")
                break;

            // specify the number of phases of mini-batch
            if (option == "bat")
            {
                is >> mini_batch_size;
                mini_batch_size *= 10000; // Unit is ten thousand
            }

            // Specify the folder in which the game record is stored and make it the rooting target.
            else if (option == "targetdir") is >> target_dir;
            else if (option == "targetfile")
            {
                std::string filename;
                is >> filename;
                filenames.push_back(filename);
            }

            // Specify the number of loops
            else if (option == "epochs")      is >> epochs;

            // Game file storage folder (get game file with relative path from here)
            else if (option == "basedir")   is >> base_dir;

            // Mini batch size
            else if (option == "batchsize") is >> mini_batch_size;

            // learning rate
            else if (option == "lr")        is >> global_learning_rate;

            // Accept also the old option name.
            else if (option == "use_draw_in_training"
                  || option == "use_draw_games_in_training")
                is >> use_draw_games_in_training;

            // Accept also the old option name.
            else if (option == "use_draw_in_validation"
                  || option == "use_draw_games_in_validation")
                is >> use_draw_games_in_validation;

            // Accept also the old option name.
            else if (option == "use_hash_in_training"
                  || option == "skip_duplicated_positions_in_training")
                is >> skip_duplicated_positions_in_training;

            else if (option == "winning_probability_coefficient") is >> winning_probability_coefficient;

            // Using WDL with win rate model instead of sigmoid
            else if (option == "use_wdl") is >> use_wdl;


            // LAMBDA
            else if (option == "lambda")       is >> ELMO_LAMBDA;
            else if (option == "lambda2")      is >> ELMO_LAMBDA2;
            else if (option == "lambda_limit") is >> ELMO_LAMBDA_LIMIT;

            else if (option == "reduction_gameply") is >> reduction_gameply;

            else if (option == "eval_limit") is >> eval_limit;
            else if (option == "save_only_once") save_only_once = true;
            else if (option == "no_shuffle") no_shuffle = true;

            else if (option == "nn_batch_size") is >> nn_batch_size;
            else if (option == "newbob_decay") is >> newbob_decay;
            else if (option == "newbob_num_trials") is >> newbob_num_trials;
            else if (option == "nn_options") is >> nn_options;
            else if (option == "auto_lr_drop") is >> auto_lr_drop;

            else if (option == "eval_save_interval") is >> eval_save_interval;
            else if (option == "loss_output_interval") is >> loss_output_interval;
            else if (option == "validation_set_file_name") is >> validation_set_file_name;

            else if (option == "src_score_min_value") is >> src_score_min_value;
            else if (option == "src_score_max_value") is >> src_score_max_value;
            else if (option == "dest_score_min_value") is >> dest_score_min_value;
            else if (option == "dest_score_max_value") is >> dest_score_max_value;

            else if (option == "seed") is >> seed;
            else if (option == "set_recommended_uci_options")
            {
                UCI::setoption("Use NNUE", "pure");
                UCI::setoption("MultiPV", "1");
                UCI::setoption("Contempt", "0");
                UCI::setoption("Skill Level", "20");
                UCI::setoption("UCI_Chess960", "false");
                UCI::setoption("UCI_AnalyseMode", "false");
                UCI::setoption("UCI_LimitStrength", "false");
                UCI::setoption("PruneAtShallowDepth", "false");
                UCI::setoption("EnableTranspositionTable", "false");
            }
            else
            {
                cout << "Unknown option: " << option << ". Ignoring.\n";
            }
        }

        if (loss_output_interval == 0)
        {
            loss_output_interval = LEARN_RMSE_OUTPUT_INTERVAL * mini_batch_size;
        }

        cout << "learn command , ";

        // Issue a warning if OpenMP is disabled.
#if !defined(_OPENMP)
        cout << "Warning! OpenMP disabled." << endl;
#endif

        // Right now we only have the individual files.
        // We need to apply base_dir here
        if (!target_dir.empty())
        {
            append_files_from_dir(filenames, base_dir, target_dir);
        }
        rebase_files(filenames, base_dir);

        cout << "learn from ";
        for (auto s : filenames)
            cout << s << " , ";

        cout << endl;
        if (!validation_set_file_name.empty())
        {
            cout << "validation set  : " << validation_set_file_name << endl;
        }

        cout << "base dir        : " << base_dir << endl;
        cout << "target dir      : " << target_dir << endl;

        cout << "epochs            : " << epochs << endl;
        cout << "eval_limit        : " << eval_limit << endl;
        cout << "save_only_once    : " << (save_only_once ? "true" : "false") << endl;
        cout << "no_shuffle        : " << (no_shuffle ? "true" : "false") << endl;

        cout << "Loss Function     : " << LOSS_FUNCTION << endl;
        cout << "mini-batch size   : " << mini_batch_size << endl;

        cout << "nn_batch_size     : " << nn_batch_size << endl;
        cout << "nn_options        : " << nn_options << endl;

        cout << "learning rate     : " << global_learning_rate << endl;
        cout << "use_draw_games_in_training : " << use_draw_games_in_training << endl;
        cout << "use_draw_games_in_validation : " << use_draw_games_in_validation << endl;
        cout << "skip_duplicated_positions_in_training : " << skip_duplicated_positions_in_training << endl;

        if (newbob_decay != 1.0) {
            cout << "scheduling        : newbob with decay = " << newbob_decay
                << ", " << newbob_num_trials << " trials" << endl;
        }
        else {
            cout << "scheduling        : default" << endl;
        }

        // If reduction_gameply is set to 0, rand(0) will be divided by 0, so correct it to 1.
        reduction_gameply = max(reduction_gameply, 1);
        cout << "reduction_gameply : " << reduction_gameply << endl;

        cout << "LAMBDA            : " << ELMO_LAMBDA << endl;
        cout << "LAMBDA2           : " << ELMO_LAMBDA2 << endl;
        cout << "LAMBDA_LIMIT      : " << ELMO_LAMBDA_LIMIT << endl;
        cout << "eval_save_interval  : " << eval_save_interval << " sfens" << endl;
        cout << "loss_output_interval: " << loss_output_interval << " sfens" << endl;

        // -----------------------------------
        // various initialization
        // -----------------------------------

        cout << "init.." << endl;

        Threads.main()->ponder = false;

        set_learning_search_limits();

        cout << "init_training.." << endl;
        Eval::NNUE::initialize_training(seed);
        Eval::NNUE::set_batch_size(nn_batch_size);
        Eval::NNUE::set_options(nn_options);

        LearnerThink learn_think(filenames, !no_shuffle, thread_num, seed);

        if (newbob_decay != 1.0 && !Options["SkipLoadingEval"]) {
            // Save the current net to [EvalSaveDir]\original.
            Eval::NNUE::save_eval("original");

            // Set the folder above to best_nn_directory so that the trainer can
            // resotre the network parameters from the original net file.
            learn_think.best_nn_directory =
                Path::combine(Options["EvalSaveDir"], "original");
        }

        // Reflect other option settings.
        learn_think.eval_limit = eval_limit;
        learn_think.save_only_once = save_only_once;
        learn_think.reduction_gameply = reduction_gameply;

        learn_think.newbob_decay = newbob_decay;
        learn_think.newbob_num_trials = newbob_num_trials;
        learn_think.auto_lr_drop = auto_lr_drop;

        learn_think.eval_save_interval = eval_save_interval;
        learn_think.loss_output_interval = loss_output_interval;

        learn_think.mini_batch_size = mini_batch_size;
        learn_think.validation_set_file_name = validation_set_file_name;

        cout << "init done." << endl;

        // Start learning.
        learn_think.learn(epochs);
    }

} // namespace Learner
