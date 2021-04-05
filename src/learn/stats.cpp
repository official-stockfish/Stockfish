#include "stats.h"

#include "sfen_stream.h"
#include "packed_sfen.h"
#include "sfen_writer.h"

#include "thread.h"
#include "position.h"
#include "evaluate.h"
#include "search.h"

#include "nnue/evaluate_nnue.h"

#include <array>
#include <string>
#include <map>
#include <set>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>

namespace Learner::Stats
{
    struct StatisticGathererBase
    {
        virtual void on_position(const Position&) {}
        virtual void on_move(const Position&, const Move&) {}
        virtual void reset() = 0;
        [[nodiscard]] virtual const std::string& get_name() const = 0;
        [[nodiscard]] virtual std::map<std::string, std::string> get_formatted_stats() const = 0;
    };

    struct StatisticGathererFactoryBase
    {
        [[nodiscard]] virtual std::unique_ptr<StatisticGathererBase> create() const = 0;
        [[nodiscard]] virtual const std::string& get_name() const = 0;
    };

    template <typename T>
    struct StatisticGathererFactory : StatisticGathererFactoryBase
    {
        [[nodiscard]] std::unique_ptr<StatisticGathererBase> create() const override
        {
            return std::make_unique<T>();
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return T::name;
        }
    };

    struct StatisticGathererSet : StatisticGathererBase
    {
        void add(const StatisticGathererFactoryBase& factory)
        {
            const std::string name = factory.get_name();
            if (m_gatherers_names.count(name) == 0)
            {
                m_gatherers_names.insert(name);
                m_gatherers.emplace_back(factory.create());
            }
        }

        void add(std::unique_ptr<StatisticGathererBase>&& gatherer)
        {
            const std::string name = gatherer->get_name();
            if (m_gatherers_names.count(name) == 0)
            {
                m_gatherers_names.insert(name);
                m_gatherers.emplace_back(std::move(gatherer));
            }
        }

        void on_position(const Position& position) override
        {
            for (auto& g : m_gatherers)
            {
                g->on_position(position);
            }
        }

        void on_move(const Position& pos, const Move& move) override
        {
            for (auto& g : m_gatherers)
            {
                g->on_move(pos, move);
            }
        }

        void reset() override
        {
            for (auto& g : m_gatherers)
            {
                g->reset();
            }
        }

        [[nodiscard]] virtual const std::string& get_name() const override
        {
            static std::string name = "SET";
            return name;
        }

        [[nodiscard]] virtual std::map<std::string, std::string> get_formatted_stats() const override
        {
            std::map<std::string, std::string> parts;
            for (auto&& s : m_gatherers)
            {
                parts.merge(s->get_formatted_stats());
            }
            return parts;
        }

    private:
        std::vector<std::unique_ptr<StatisticGathererBase>> m_gatherers;
        std::set<std::string> m_gatherers_names;
    };

    struct StatisticGathererRegistry
    {
        void add_statistic_gatherers_by_group(
            StatisticGathererSet& gatherers,
            const std::string& group) const
        {
            auto it = m_gatherers_by_group.find(group);
            if (it != m_gatherers_by_group.end())
            {
                for (auto& factory : it->second)
                {
                    gatherers.add(*factory);
                }
            }
        }

        template <typename T>
        void add(const std::string& group)
        {
            m_gatherers_by_group[group].emplace_back(std::make_unique<StatisticGathererFactory<T>>());

            // Always add to the special group "all".
            m_gatherers_by_group["all"].emplace_back(std::make_unique<StatisticGathererFactory<T>>());
        }

    private:
        std::map<std::string, std::vector<std::unique_ptr<StatisticGathererFactoryBase>>> m_gatherers_by_group;
    };

    /*
        Statistic gatherer helpers
    */

    template <typename T>
    struct StatPerSquare
    {
        StatPerSquare()
        {
            for (int i = 0; i < SQUARE_NB; ++i)
                m_squares[i] = 0;
        }

        [[nodiscard]] T& operator[](Square sq)
        {
            return m_squares[sq];
        }

        [[nodiscard]] const T& operator[](Square sq) const
        {
            return m_squares[sq];
        }

        [[nodiscard]] std::string get_formatted_stats() const
        {
            std::stringstream ss;
            for (int i = 0; i < SQUARE_NB; ++i)
            {
                ss << std::setw(8) << m_squares[i] << ' ';
                if ((i + 1) % 8 == 0)
                    ss << '\n';
            }
            return ss.str();
        }

    private:
        std::array<T, SQUARE_NB> m_squares;
    };

    /*
        Definitions for specific statistic gatherers follow:
    */

    struct PositionCounter : StatisticGathererBase
    {
        static inline std::string name = "PositionCounter";

        PositionCounter() :
            m_num_positions(0)
        {
        }

        void on_position(const Position&) override
        {
            m_num_positions += 1;
        }

        void reset() override
        {
            m_num_positions = 0;
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return name;
        }

        [[nodiscard]] std::map<std::string, std::string> get_formatted_stats() const override
        {
            return {
                { "Number of positions", std::to_string(m_num_positions) }
            };
        }

    private:
        std::uint64_t m_num_positions;
    };

    struct KingSquareCounter : StatisticGathererBase
    {
        static inline std::string name = "KingSquareCounter";

        KingSquareCounter() :
            m_white{},
            m_black{}
        {

        }

        void on_position(const Position& pos) override
        {
            m_white[pos.square<KING>(WHITE)] += 1;
            m_black[pos.square<KING>(BLACK)] += 1;
        }

        void reset() override
        {
            m_white = StatPerSquare<std::uint64_t>{};
            m_black = StatPerSquare<std::uint64_t>{};
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return name;
        }

        [[nodiscard]] std::map<std::string, std::string> get_formatted_stats() const override
        {
            return {
                { "White king squares", '\n' + m_white.get_formatted_stats() },
                { "Black king squares", '\n' + m_black.get_formatted_stats() }
            };
        }

    private:
        StatPerSquare<std::uint64_t> m_white;
        StatPerSquare<std::uint64_t> m_black;
    };

    struct MoveFromCounter : StatisticGathererBase
    {
        static inline std::string name = "MoveFromCounter";

        MoveFromCounter() :
            m_white{},
            m_black{}
        {

        }

        void on_move(const Position& pos, const Move& move) override
        {
            if (pos.side_to_move() == WHITE)
                m_white[from_sq(move)] += 1;
            else
                m_black[from_sq(move)] += 1;
        }

        void reset() override
        {
            m_white = StatPerSquare<std::uint64_t>{};
            m_black = StatPerSquare<std::uint64_t>{};
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return name;
        }

        [[nodiscard]] std::map<std::string, std::string> get_formatted_stats() const override
        {
            return {
                { "White move from squares", '\n' + m_white.get_formatted_stats() },
                { "Black move from squares", '\n' + m_black.get_formatted_stats() }
            };
        }

    private:
        StatPerSquare<std::uint64_t> m_white;
        StatPerSquare<std::uint64_t> m_black;
    };

    struct MoveToCounter : StatisticGathererBase
    {
        static inline std::string name = "MoveToCounter";

        MoveToCounter() :
            m_white{},
            m_black{}
        {

        }

        void on_move(const Position& pos, const Move& move) override
        {
            if (pos.side_to_move() == WHITE)
                m_white[to_sq(move)] += 1;
            else
                m_black[to_sq(move)] += 1;
        }

        void reset() override
        {
            m_white = StatPerSquare<std::uint64_t>{};
            m_black = StatPerSquare<std::uint64_t>{};
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return name;
        }

        [[nodiscard]] std::map<std::string, std::string> get_formatted_stats() const override
        {
            return {
                { "White move to squares", '\n' + m_white.get_formatted_stats() },
                { "Black move to squares", '\n' + m_black.get_formatted_stats() }
            };
        }

    private:
        StatPerSquare<std::uint64_t> m_white;
        StatPerSquare<std::uint64_t> m_black;
    };

    struct MoveTypeCounter : StatisticGathererBase
    {
        static inline std::string name = "MoveTypeCounter";

        MoveTypeCounter() :
            m_total(0),
            m_normal(0),
            m_capture(0),
            m_promotion(0),
            m_castling(0),
            m_enpassant(0)
        {

        }

        void on_move(const Position& pos, const Move& move) override
        {
            m_total += 1;

            if (!pos.empty(to_sq(move)))
                m_capture += 1;

            if (type_of(move) == CASTLING)
                m_castling += 1;
            else if (type_of(move) == PROMOTION)
                m_promotion += 1;
            else if (type_of(move) == ENPASSANT)
                m_enpassant += 1;
            else if (type_of(move) == NORMAL)
                m_normal += 1;
        }

        void reset() override
        {
            m_total = 0;
            m_normal = 0;
            m_capture = 0;
            m_promotion = 0;
            m_castling = 0;
            m_enpassant = 0;
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return name;
        }

        [[nodiscard]] std::map<std::string, std::string> get_formatted_stats() const override
        {
            return {
                { "Total moves", std::to_string(m_total) },
                { "Normal moves", std::to_string(m_normal) },
                { "Capture moves", std::to_string(m_capture) },
                { "Promotion moves", std::to_string(m_promotion) },
                { "Castling moves", std::to_string(m_castling) },
                { "En-passant moves", std::to_string(m_enpassant) }
            };
        }

    private:
        std::uint64_t m_total;
        std::uint64_t m_normal;
        std::uint64_t m_capture;
        std::uint64_t m_promotion;
        std::uint64_t m_castling;
        std::uint64_t m_enpassant;
    };

    struct PieceCountCounter : StatisticGathererBase
    {
        static inline std::string name = "PieceCountCounter";

        PieceCountCounter()
        {
            reset();
        }

        void on_position(const Position& pos) override
        {
            m_piece_count_hist[popcount(pos.pieces())] += 1;
        }

        void reset() override
        {
            for (int i = 0; i < SQUARE_NB; ++i)
                m_num_pieces[i] = 0;
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return name;
        }

        [[nodiscard]] std::map<std::string, std::string> get_formatted_stats() const override
        {
            std::map<std::string, std::string> result;
            bool do_write = false;
            for (int i = SQUARE_NB; i >= 0; --i)
            {
                if (m_piece_count_hist[i] != 0)
                    do_write = true;

                // Start writing when the first non-zero number pops up.
                if (do_write)
                {
                    result.try_emplace(
                        std::string("Number of positions with ") + std::to_string(i) + " pieces",
                        std::to_string(m_piece_count_hist[i])
                    );
                }
            }
            return result;
        }

    private:
        std::uint64_t m_piece_count_hist[SQUARE_NB];
    };

    struct MovedPieceTypeCounter : StatisticGathererBase
    {
        static inline std::string name = "MovedPieceTypeCounter";

        MovedPieceTypeCounter()
        {
            reset();
        }

        void on_move(const Position& pos, const Move& move) override
        {
            m_moved_piece_type_hist[type_of(pos.piece_on(from_sq(move)))] += 1;
        }

        void reset() override
        {
            for (int i = 0; i < PIECE_TYPE_NB; ++i)
                m_moved_piece_type_hist[i] = 0;
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return name;
        }

        [[nodiscard]] std::map<std::string, std::string> get_formatted_stats() const override
        {
            return {
                { "Pawn moves", std::to_string(m_moved_piece_type_hist[PAWN]) },
                { "Knight moves", std::to_string(m_moved_piece_type_hist[KNIGHT]) },
                { "Bishop moves", std::to_string(m_moved_piece_type_hist[BISHOP]) },
                { "Rook moves", std::to_string(m_moved_piece_type_hist[ROOK]) },
                { "Queen moves", std::to_string(m_moved_piece_type_hist[QUEEN]) },
                { "King moves", std::to_string(m_moved_piece_type_hist[KING]) }
            };
        }

    private:
        std::uint64_t m_moved_piece_type_hist[PIECE_TYPE_NB];
    };

    /*
        This function provides factories for all possible statistic gatherers.
        Each new statistic gatherer needs to be added there.
    */
    const auto& get_statistics_gatherers_registry()
    {
        static StatisticGathererRegistry s_reg = [](){
            StatisticGathererRegistry reg;

            reg.add<PositionCounter>("position_count");

            reg.add<KingSquareCounter>("king");
            reg.add<KingSquareCounter>("king_square_count");

            reg.add<MoveFromCounter>("move");
            reg.add<MoveFromCounter>("move_from_count");
            reg.add<MoveToCounter>("move_to_count");
            reg.add<MoveTypeCounter>("move_type");
            reg.add<MovedPieceTypeCounter>("moved_piece_type");

            reg.add<PieceCountCounter>("piece_count")

            return reg;
        }();

        return s_reg;
    }

    void do_gather_statistics(
        const std::string& filename,
        StatisticGathererSet& statistic_gatherers,
        std::uint64_t max_count)
    {
        Thread* th = Threads.main();
        Position& pos = th->rootPos;
        StateInfo si;

        auto in = Learner::open_sfen_input_file(filename);

        auto on_move = [&](const Position& position, const Move& move) {
            statistic_gatherers.on_move(position, move);
        };

        auto on_position = [&](const Position& position) {
            statistic_gatherers.on_position(position);
        };

        if (in == nullptr)
        {
            std::cerr << "Invalid input file type.\n";
            return;
        }

        uint64_t num_processed = 0;
        while (num_processed < max_count)
        {
            auto v = in->next();
            if (!v.has_value())
                break;

            auto& ps = v.value();

            pos.set_from_packed_sfen(ps.sfen, &si, th);

            on_position(pos);
            on_move(pos, (Move)ps.move);

            num_processed += 1;
            if (num_processed % 1'000'000 == 0)
            {
                std::cout << "Processed " << num_processed << " positions.\n";
            }
        }

        std::cout << "Finished gathering statistics.\n\n";
        std::cout << "Results:\n\n";

        for (auto&& [name, value] : statistic_gatherers.get_formatted_stats())
        {
            std::cout << name << ": " << value << '\n';
        }
    }

    void gather_statistics(std::istringstream& is)
    {
        Eval::NNUE::init();

        auto& registry = get_statistics_gatherers_registry();

        StatisticGathererSet statistic_gatherers;

        std::string input_file;
        std::uint64_t max_count = std::numeric_limits<std::uint64_t>::max();

        while(true)
        {
            std::string token;
            is >> token;

            if (token == "")
                break;

            if (token == "input_file")
                is >> input_file;
            else if (token == "max_count")
                is >> max_count;
            else
                registry.add_statistic_gatherers_by_group(statistic_gatherers, token);
        }

        do_gather_statistics(input_file, statistic_gatherers, max_count);
    }

}
