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
        virtual void on_move(const Move&) {}
        virtual void reset() = 0;
        [[nodiscard]] virtual std::map<std::string, std::string> get_formatted_stats() const = 0;
    };

    struct StatisticGathererFactoryBase
    {
        [[nodiscard]] virtual std::unique_ptr<StatisticGathererBase> create() const = 0;
    };

    template <typename T>
    struct StatisticGathererFactory : StatisticGathererFactoryBase
    {
        [[nodiscard]] std::unique_ptr<StatisticGathererBase> create() const override
        {
            return std::make_unique<T>();
        }
    };

    struct StatisticGathererRegistry
    {
        void add_statistic_gatherers_by_group(
            std::vector<std::unique_ptr<StatisticGathererBase>>& gatherers,
            const std::string& group) const
        {
            auto it = m_gatherers_by_group.find(group);
            if (it != m_gatherers_by_group.end())
            {
                for (auto& factory : it->second)
                {
                    gatherers.emplace_back(factory->create());
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
        }

    private:
        std::array<T, SQUARE_NB> m_squares;
    };

    /*
        Definitions for specific statistic gatherers follow:
    */

    struct PositionCounter : StatisticGathererBase
    {
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

        [[nodiscard]] std::map<std::string, std::string> get_formatted_stats() const override
        {
            return {
                { "Number of positions", std::to_string(m_num_positions) }
            };
        }

    private:
        std::uint64_t m_num_positions;
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

            return reg;
        }();

        return s_reg;
    }

    void do_gather_statistics(
        const std::string& filename,
        std::vector<std::unique_ptr<StatisticGathererBase>>& statistic_gatherers,
        std::uint64_t max_count)
    {
        Thread* th = Threads.main();
        Position& pos = th->rootPos;
        StateInfo si;

        auto in = Learner::open_sfen_input_file(filename);

        auto on_move = [&](Move move) {
            for (auto&& s : statistic_gatherers)
            {
                s->on_move(move);
            }
        };

        auto on_position = [&](const Position& position) {
            for (auto&& s : statistic_gatherers)
            {
                s->on_position(position);
            }
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
            on_move((Move)ps.move);

            num_processed += 1;
            if (num_processed % 1'000'000 == 0)
            {
                std::cout << "Processed " << num_processed << " positions.\n";
            }
        }

        std::cout << "Finished gathering statistics.\n\n";
        std::cout << "Results:\n\n";

        for (auto&& s : statistic_gatherers)
        {
            for (auto&& [name, value] : s->get_formatted_stats())
            {
                std::cout << name << ": " << value << '\n';
            }
            std::cout << '\n';
        }
    }

    void gather_statistics(std::istringstream& is)
    {
        Eval::NNUE::init();

        auto& registry = get_statistics_gatherers_registry();

        std::vector<std::unique_ptr<StatisticGathererBase>> statistic_gatherers;

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
