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
#include <type_traits>

namespace Stockfish::Tools::Stats
{
    struct Indentation
    {
        char character = ' ';
        int width_per_indent = 4;
        int num_indents = 0;

        [[nodiscard]] Indentation next() const
        {
            return Indentation{ character, width_per_indent, num_indents + 1 };
        }

        [[nodiscard]] std::string to_string() const
        {
            return std::string(num_indents * width_per_indent, character);
        }
    };

    template <typename IntT>
    [[nodiscard]] int get_num_base_10_digits(IntT v)
    {
        int digits = 1;
        while (v != 0)
        {
            digits += 1;
            v /= 10;
        }
        return digits;
    }

    [[nodiscard]] std::string left_pad_to_length(const std::string& str, char ch, int length)
    {
        const int str_size = static_cast<int>(str.size());
        if (str_size < length)
        {
            return std::string(length - str_size, ch) + str;
        }
        else
        {
            return str;
        }
    }

    [[nodiscard]] std::string right_pad_to_length(const std::string& str, char ch, int length)
    {
        const int str_size = static_cast<int>(str.size());
        if (str_size < length)
        {
            return str + std::string(length - str_size, ch);
        }
        else
        {
            return str;
        }
    }

    [[nodiscard]] std::string indent_text(const std::string& text, Indentation indent)
    {
        std::string delimiter = "\n";
        std::string indent_str = indent.to_string();

        std::string indented;

        std::string::size_type pos = 0;
        std::string::size_type prev = 0;
        while ((pos = text.find(delimiter, prev)) != std::string::npos)
        {
            std::string line = text.substr(prev, pos - prev);
            indented += indent_str + line + delimiter;
            prev = pos + delimiter.size();
        }

        {
            std::string line = text.substr(prev);
            indented += indent_str + line;
        }

        return indented;
    }

    struct IndentedTextBlock
    {
        Indentation indentation;
        std::string text;

        IndentedTextBlock(Indentation indent, std::string str) :
            indentation(indent),
            text(std::move(str))
        {
        }

        [[nodiscard]] static std::string join(const std::vector<IndentedTextBlock>& blocks, const std::string& delimiter)
        {
            std::string result;

            bool is_first = true;
            for (auto&& [indentation, text] : blocks)
            {
                if (!is_first)
                {
                    result += delimiter;
                }

                result += indent_text(text, indentation);

                is_first = false;
            }

            return result;
        }
    };

    struct StatisticOutputEntryNode
    {
        [[nodiscard]] const std::vector<std::unique_ptr<StatisticOutputEntryNode>>& get_children() const
        {
            return m_children;
        }

        template <typename NodeT, typename... Ts>
        StatisticOutputEntryNode& emplace_child(Ts&&... args)
        {
            return *(m_children.emplace_back(std::make_unique<NodeT>(std::forward<Ts>(args)...)));
        }

        template <typename NodeT>
        StatisticOutputEntryNode& add_child(std::unique_ptr<NodeT>&& node)
        {
            return *(m_children.emplace_back(std::move(node)));
        }

        [[nodiscard]] virtual std::vector<IndentedTextBlock> to_indented_text_blocks(Indentation indent) const = 0;

    protected:
        std::vector<std::unique_ptr<StatisticOutputEntryNode>> m_children;

        void add_indented_children_blocks(std::vector<IndentedTextBlock>& blocks, Indentation indent) const
        {
            for (auto&& child : m_children)
            {
                auto part = child->to_indented_text_blocks(indent.next());
                blocks.insert(blocks.end(), part.begin(), part.end());
            }
        }
    };

    struct StatisticOutputEntryHeader : StatisticOutputEntryNode
    {
        StatisticOutputEntryHeader(const std::string& text) :
            m_text(text)
        {
        }

        [[nodiscard]] virtual std::vector<IndentedTextBlock> to_indented_text_blocks(Indentation indent) const override
        {
            std::vector<IndentedTextBlock> blocks;

            blocks.emplace_back(indent, m_text);

            this->add_indented_children_blocks(blocks, indent);

            return blocks;
        }

    private:
        std::string m_text;
    };

    template <typename T>
    struct StatisticOutputEntryValue : StatisticOutputEntryNode
    {
        StatisticOutputEntryValue(const std::string& name, const T& value, bool value_in_new_line = false) :
            m_value(name, value),
            m_value_in_new_line(value_in_new_line)
        {
        }

        [[nodiscard]] virtual std::vector<IndentedTextBlock> to_indented_text_blocks(Indentation indent) const override
        {
            std::vector<IndentedTextBlock> blocks;

            std::string value_str;
            if constexpr (std::is_same_v<T, std::string>)
            {
                value_str = m_value.second;
            }
            else
            {
                value_str = std::to_string(m_value.second);
            }

            if (m_value_in_new_line)
            {
                blocks.emplace_back(indent, m_value.first + ": ");
                blocks.emplace_back(indent.next(), value_str);
            }
            else
            {
                blocks.emplace_back(indent, m_value.first + ": " + value_str);
            }

            this->add_indented_children_blocks(blocks, indent);

            return blocks;
        }

    private:
        std::pair<std::string, T> m_value;
        bool m_value_in_new_line;
    };

    struct StatisticOutput
    {
        template <typename NodeT, typename... Ts>
        StatisticOutputEntryNode& emplace_node(Ts&&... args)
        {
            return *(m_nodes.emplace_back(std::make_unique<NodeT>(std::forward<Ts>(args)...)));
        }

        template <typename NodeT>
        StatisticOutputEntryNode& add_child(std::unique_ptr<NodeT>&& node)
        {
            return *(m_nodes.emplace_back(std::move(node)));
        }

        [[nodiscard]] const std::vector<std::unique_ptr<StatisticOutputEntryNode>>& get_nodes() const
        {
            return m_nodes;
        }

        void add(StatisticOutput&& other)
        {
            for (auto&& node : other.m_nodes)
            {
                m_nodes.emplace_back(std::move(node));
            }
        }

        [[nodiscard]] std::string to_string() const
        {
            std::vector<IndentedTextBlock> blocks;

            for (auto&& node : m_nodes)
            {
                auto part = node->to_indented_text_blocks(Indentation{});
                blocks.insert(blocks.end(), part.begin(), part.end());
            }

            return IndentedTextBlock::join(blocks, "\n");
        }

    private:
        std::vector<std::unique_ptr<StatisticOutputEntryNode>> m_nodes;
    };

    struct StatisticGathererBase
    {
        virtual void on_entry(const Position&, const Move&, const PackedSfenValue&) {}
        virtual void reset() = 0;
        [[nodiscard]] virtual const std::string& get_name() const = 0;
        [[nodiscard]] virtual StatisticOutput get_output() const = 0;
    };

    struct StatisticGathererFactoryBase
    {
        [[nodiscard]] virtual std::unique_ptr<StatisticGathererBase> create() const = 0;
        [[nodiscard]] virtual const std::string& get_name() const = 0;
    };

    template <typename T>
    struct StatisticGathererFactory : StatisticGathererFactoryBase
    {
        static inline std::string name = T::name;

        [[nodiscard]] std::unique_ptr<StatisticGathererBase> create() const override
        {
            return std::make_unique<T>();
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return name;
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

        void on_entry(const Position& pos, const Move& move, const PackedSfenValue& psv) override
        {
            for (auto& g : m_gatherers)
            {
                g->on_entry(pos, move, psv);
            }
        }

        void reset() override
        {
            for (auto& g : m_gatherers)
            {
                g->reset();
            }
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            static std::string name = "SET";
            return name;
        }

        [[nodiscard]] StatisticOutput get_output() const override
        {
            StatisticOutput out;
            for (auto&& s : m_gatherers)
            {
                out.add(s->get_output());
            }
            return out;
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

        template <typename T, typename... ArgsTs>
        void add(const ArgsTs&... group)
        {
            auto dummy = {(add_single<T>(group), 0)...};
            (void)dummy;
            add_single<T>("all");
        }

    private:
        std::map<std::string, std::vector<std::unique_ptr<StatisticGathererFactoryBase>>> m_gatherers_by_group;
        std::map<std::string, std::set<std::string>> m_gatherers_names_by_group;

        template <typename T, typename ArgT>
        void add_single(const ArgT& group)
        {
            using FactoryT = StatisticGathererFactory<T>;

            if (m_gatherers_names_by_group[group].count(FactoryT::name) == 0)
            {
                m_gatherers_by_group[group].emplace_back(std::make_unique<FactoryT>());
                m_gatherers_names_by_group[group].insert(FactoryT::name);
            }
        }
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

        [[nodiscard]] std::unique_ptr<StatisticOutputEntryNode> get_output_node(const std::string& name) const
        {
            int max_digits = 1;
            for (int i = 0; i < SQUARE_NB; ++i)
            {
                const int d = get_num_base_10_digits(m_squares[i]);
                if (d > max_digits)
                {
                    max_digits = d;
                }
            }

            std::stringstream ss;
            for (int i = 0; i < SQUARE_NB; ++i)
            {
                ss << std::setw(max_digits) << m_squares[i ^ (int)SQ_A8] << ' ';
                if ((i + 1) % 8 == 0)
                    ss << '\n';
            }

            return std::make_unique<StatisticOutputEntryValue<std::string>>(name, ss.str(), true);
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

        void on_entry(const Position&, const Move&, const PackedSfenValue&) override
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

        [[nodiscard]] StatisticOutput get_output() const override
        {
            StatisticOutput out;
            out.emplace_node<StatisticOutputEntryValue<std::uint64_t>>("Number of positions", m_num_positions);
            return out;
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

        void on_entry(const Position& pos, const Move&, const PackedSfenValue&) override
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

        [[nodiscard]] StatisticOutput get_output() const override
        {
            StatisticOutput out;
            auto& header = out.emplace_node<StatisticOutputEntryHeader>("King square distribution:");
            header.add_child(m_white.get_output_node("White king squares"));
            header.add_child(m_black.get_output_node("Black king squares"));
            return out;
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

        void on_entry(const Position& pos, const Move& move, const PackedSfenValue&) override
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

        [[nodiscard]] StatisticOutput get_output() const override
        {
            StatisticOutput out;
            auto& header = out.emplace_node<StatisticOutputEntryHeader>("Move from square distribution:");
            header.add_child(m_white.get_output_node("White move from squares"));
            header.add_child(m_black.get_output_node("Black move from squares"));
            return out;
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

        void on_entry(const Position& pos, const Move& move, const PackedSfenValue&) override
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

        [[nodiscard]] StatisticOutput get_output() const override
        {
            StatisticOutput out;
            auto& header = out.emplace_node<StatisticOutputEntryHeader>("Move to square distribution:");
            header.add_child(m_white.get_output_node("White move to squares"));
            header.add_child(m_black.get_output_node("Black move to squares"));
            return out;
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

        void on_entry(const Position& pos, const Move& move, const PackedSfenValue&) override
        {
            m_total += 1;

            if (!pos.empty(to_sq(move)))
                m_capture += 1;

            if (type_of(move) == CASTLING)
                m_castling += 1;
            else if (type_of(move) == PROMOTION)
                m_promotion += 1;
            else if (type_of(move) == EN_PASSANT)
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

        [[nodiscard]] StatisticOutput get_output() const override
        {
            StatisticOutput out;
            auto& header = out.emplace_node<StatisticOutputEntryHeader>("Number of moves by type:");
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Total", m_total);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Normal", m_normal);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Capture", m_capture);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Promotion", m_promotion);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Castling", m_castling);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("En-passant", m_enpassant);
            return out;
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

        void on_entry(const Position& pos, const Move&, const PackedSfenValue&) override
        {
            m_piece_count_hist[popcount(pos.pieces())] += 1;
        }

        void reset() override
        {
            for (int i = 0; i < SQUARE_NB; ++i)
                m_piece_count_hist[i] = 0;
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return name;
        }

        [[nodiscard]] StatisticOutput get_output() const override
        {
            StatisticOutput out;
            auto& header = out.emplace_node<StatisticOutputEntryHeader>("Number of positions by piece count:");
            bool do_write = false;
            for (int i = SQUARE_NB - 1; i >= 0; --i)
            {
                if (m_piece_count_hist[i] != 0)
                    do_write = true;

                // Start writing when the first non-zero number pops up.
                if (do_write)
                {
                    header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>(std::to_string(i), m_piece_count_hist[i]);
                }
            }
            return out;
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

        void on_entry(const Position& pos, const Move& move, const PackedSfenValue&) override
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

        [[nodiscard]] StatisticOutput get_output() const override
        {
            StatisticOutput out;
            auto& header = out.emplace_node<StatisticOutputEntryHeader>("Number of moves by piece type:");
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Pawn", m_moved_piece_type_hist[PAWN]);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Knight", m_moved_piece_type_hist[KNIGHT]);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Bishop", m_moved_piece_type_hist[BISHOP]);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Rook", m_moved_piece_type_hist[ROOK]);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Queen", m_moved_piece_type_hist[QUEEN]);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("King", m_moved_piece_type_hist[KING]);
            return out;
        }

    private:
        std::uint64_t m_moved_piece_type_hist[PIECE_TYPE_NB];
    };

    struct PlyDiscontinuitiesCounter : StatisticGathererBase
    {
        static inline std::string name = "PlyDiscontinuitiesCounter";

        PlyDiscontinuitiesCounter()
        {
            reset();
        }

        void on_entry(const Position& pos, const Move&, const PackedSfenValue&) override
        {
            const int current_ply = pos.game_ply();
            if (m_prev_ply != -1)
            {
                const bool is_discontinuity = (current_ply != (m_prev_ply + 1));
                if (is_discontinuity)
                {
                    m_num_discontinuities += 1;
                }
            }
            m_prev_ply = current_ply;
        }

        void reset() override
        {
            m_num_discontinuities = 0;
            m_prev_ply = -1;
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return name;
        }

        [[nodiscard]] StatisticOutput get_output() const override
        {
            StatisticOutput out;
            out.emplace_node<StatisticOutputEntryValue<std::uint64_t>>("Number of ply discontinuities (usually games)", m_num_discontinuities);
            return out;
        }

    private:
        std::uint64_t m_num_discontinuities;
        int m_prev_ply;
    };

    struct MaterialImbalanceDistribution : StatisticGathererBase
    {
        static inline std::string name = "MaterialImbalanceDistribution";
        static constexpr int max_imbalance = 64;

        MaterialImbalanceDistribution()
        {
            reset();
        }

        void on_entry(const Position& pos, const Move&, const PackedSfenValue&) override
        {
            const int imbalance = get_simple_material(pos, WHITE) - get_simple_material(pos, BLACK);
            const int imbalance_idx = std::clamp(imbalance, -max_imbalance, max_imbalance) + max_imbalance;
            m_num_imbalances[imbalance_idx] += 1;
        }

        void reset() override
        {
            for (auto& imb : m_num_imbalances)
                imb = 0;
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return name;
        }

        [[nodiscard]] StatisticOutput get_output() const override
        {
            StatisticOutput out;
            auto& header = out.emplace_node<StatisticOutputEntryHeader>("Number of \"simple eval\" imbalances for white's perspective:");
            const int key_length = get_num_base_10_digits(max_imbalance) + 1;
            int min_non_zero = max_imbalance;
            int max_non_zero = -max_imbalance;
            for (int i = -max_imbalance; i <= max_imbalance; ++i)
            {
                if (m_num_imbalances[i + max_imbalance] != 0)
                {
                    min_non_zero = std::min(min_non_zero, i);
                    max_non_zero = std::max(max_non_zero, i);
                }
            }

            for (int i = min_non_zero; i <= max_non_zero; ++i)
            {
                header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>(
                    left_pad_to_length(std::to_string(i), ' ', key_length),
                    m_num_imbalances[i + max_imbalance]
                );
            }
            return out;
        }

    private:
        std::uint64_t m_num_imbalances[max_imbalance + 1 + max_imbalance];

        [[nodiscard]] int get_simple_material(const Position& pos, Color c)
        {
            return
                  9 * pos.count<QUEEN>(c)
                + 5 * pos.count<ROOK>(c)
                + 3 * pos.count<BISHOP>(c)
                + 3 * pos.count<KNIGHT>(c)
                +     pos.count<PAWN>(c);
        }
    };

    struct ResultDistribution : StatisticGathererBase
    {
        static inline std::string name = "ResultDistribution";

        ResultDistribution()
        {
            reset();
        }

        void on_entry(const Position& pos, const Move&, const PackedSfenValue& psv) override
        {
            const Color stm = pos.side_to_move();
            if (psv.game_result == 0)
            {
                m_draws += 1;
            }
            else if (psv.game_result == 1)
            {
                m_stm_wins += 1;
                m_wins[stm] += 1;
            }
            else
            {
                m_stm_loses += 1;
                m_wins[~stm] += 1;
            }
        }

        void reset() override
        {
            m_wins[WHITE] = 0;
            m_wins[BLACK] = 0;
            m_draws = 0;
            m_stm_wins = 0;
            m_stm_loses = 0;
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return name;
        }

        [[nodiscard]] StatisticOutput get_output() const override
        {
            StatisticOutput out;
            auto& header = out.emplace_node<StatisticOutputEntryHeader>("Distribution of results:");
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("White wins", m_wins[WHITE]);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Black wins", m_wins[BLACK]);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Draws", m_draws);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Side to move wins", m_stm_wins);
            header.emplace_child<StatisticOutputEntryValue<std::uint64_t>>("Side to move loses", m_stm_loses);
            return out;
        }

    private:
        std::uint64_t m_wins[COLOR_NB];
        std::uint64_t m_draws;
        std::uint64_t m_stm_wins;
        std::uint64_t m_stm_loses;
    };

    template <int MaxManCount>
    struct EndgameConfigurations : StatisticGathererBase
    {
        static_assert(MaxManCount < 10);
        static_assert(MaxManCount > 2);

        static inline std::string name = std::string("EndgameConfigurations") + std::to_string(MaxManCount);

        using MaterialKey = std::uint64_t;

        EndgameConfigurations()
        {
            reset();
        }

        void on_entry(const Position& pos, const Move&, const PackedSfenValue& psv) override
        {
            const int piece_count = pos.count<ALL_PIECES>();
            if (piece_count > MaxManCount)
            {
                return;
            }

            const auto index = get_material_key_for_position(pos);
            auto& entry = m_entries[index];
            entry.count += 1;
            if (psv.game_result == 0)
            {
                entry.draws += 1;
            }
            else
            {
                const Color winner_side = psv.game_result == 1 ? pos.side_to_move() : ~pos.side_to_move();
                if (winner_side == WHITE)
                {
                    entry.white_wins += 1;
                }
                else
                {
                    entry.black_wins += 1;
                }
            }
        }

        void reset() override
        {
            m_entries.clear();
        }

        [[nodiscard]] const std::string& get_name() const override
        {
            return name;
        }

        [[nodiscard]] StatisticOutput get_output() const override
        {
            StatisticOutput out;
            auto& header = out.emplace_node<StatisticOutputEntryHeader>("Distribution of endgame configurations (count W D L Perf%):");
            std::vector<std::pair<MaterialKey, Entry>> flattened(m_entries.begin(), m_entries.end());
            std::sort(flattened.begin(), flattened.end(), [](const auto& lhs, const auto& rhs) { return lhs.second.count > rhs.second.count; });
            for (auto&& [index, entry] : flattened)
            {
                header.emplace_child<StatisticOutputEntryValue<std::string>>(
                    get_padded_name_by_material_key(index),
                    entry.to_string()
                );
            }
            return out;
        }

    private:
        struct Entry
        {
            std::uint64_t count = 0;
            std::uint64_t white_wins = 0;
            std::uint64_t black_wins = 0;
            std::uint64_t draws = 0;

            [[nodiscard]] std::string to_string() const
            {
                constexpr int wide_column_width = 9;
                constexpr int narrow_column_width = 4;

                const float perf =
                      (white_wins + draws / 2.0f)
                    / (white_wins + black_wins + draws);

                return
                      left_pad_to_length(std::to_string(count), ' ', wide_column_width) + ' '
                    + left_pad_to_length(std::to_string(white_wins), ' ', wide_column_width) + ' '
                    + left_pad_to_length(std::to_string(draws), ' ', wide_column_width) + ' '
                    + left_pad_to_length(std::to_string(black_wins), ' ', wide_column_width) + ' '
                    + left_pad_to_length(std::to_string(static_cast<int>(perf * 100.0f + 0.5f)), ' ', narrow_column_width) + '%';
            }
        };
        // can support up to 17 pieces.
        // it's basically the material string encoded as a number in base 8
        // encoding is from the least significant digit to most significant
        // v=1, P=2, N=3, B=4, R=5, Q=6, K=7. 0 indicates end
        std::map<MaterialKey, Entry> m_entries;

        [[nodiscard]] MaterialKey get_material_key_for_position(const Position& pos) const
        {
            MaterialKey index = 0;
            std::uint64_t shift = 0;

            index += 7 << shift; shift += 3;

            for (int i = 0; i < pos.count<PAWN>(WHITE); ++i) { index += 2 << shift; shift += 3; }
            for (int i = 0; i < pos.count<BISHOP>(WHITE); ++i) { index += 3 << shift; shift += 3; }
            for (int i = 0; i < pos.count<KNIGHT>(WHITE); ++i) { index += 4 << shift; shift += 3; }
            for (int i = 0; i < pos.count<ROOK>(WHITE); ++i) { index += 5 << shift; shift += 3; }
            for (int i = 0; i < pos.count<QUEEN>(WHITE); ++i) { index += 6 << shift; shift += 3; }

            index += 1 << shift; shift += 3;
            index += 7 << shift; shift += 3;

            for (int i = 0; i < pos.count<PAWN>(BLACK); ++i) { index += 2 << shift; shift += 3; }
            for (int i = 0; i < pos.count<BISHOP>(BLACK); ++i) { index += 3 << shift; shift += 3; }
            for (int i = 0; i < pos.count<KNIGHT>(BLACK); ++i) { index += 4 << shift; shift += 3; }
            for (int i = 0; i < pos.count<ROOK>(BLACK); ++i) { index += 5 << shift; shift += 3; }
            for (int i = 0; i < pos.count<QUEEN>(BLACK); ++i) { index += 6 << shift; shift += 3; }

            return index;
        }

        [[nodiscard]] std::string get_padded_name_by_material_key(MaterialKey index) const
        {
            std::string sides[COLOR_NB];
            int material[COLOR_NB] = { 0, 0 };
            Color side = WHITE;

            while (index != 0)
            {
                switch (index % 8)
                {
                    case 1:
                        side = BLACK;
                        break;
                    case 2:
                        sides[side] += 'P';
                        material[side] += 1;
                        break;
                    case 3:
                        sides[side] += 'N';
                        material[side] += 3;
                        break;
                    case 4:
                        sides[side] += 'B';
                        material[side] += 3;
                        break;
                    case 5:
                        sides[side] += 'R';
                        material[side] += 5;
                        break;
                    case 6:
                        sides[side] += 'Q';
                        material[side] += 9;
                        break;
                    case 7:
                        sides[side] += 'K';
                        break;
                    default:
                        break;
                }
                index >>= 3;
            }

            const int imbalance = material[WHITE] - material[BLACK];
            const std::string imbalance_str =
                  std::string(imbalance > 0 ? "+" : "") // force + sign for positive values
                + std::string(imbalance == 0 ? " " : "") // pad 0
                + std::to_string(imbalance);

            return
                  right_pad_to_length(sides[WHITE], ' ', MaxManCount-1)
                + 'v'
                + right_pad_to_length(sides[BLACK], ' ', MaxManCount-1)
                + " ("
                + right_pad_to_length(imbalance_str, ' ', 3)
                + ')';
        }
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

            reg.add<KingSquareCounter>("king", "king_square_count");

            reg.add<MoveFromCounter>("move", "move_from_count");
            reg.add<MoveToCounter>("move", "move_to_count");
            reg.add<MoveTypeCounter>("move", "move_type");
            reg.add<MovedPieceTypeCounter>("move", "moved_piece_type");

            reg.add<PlyDiscontinuitiesCounter>("ply_discontinuities");

            reg.add<MaterialImbalanceDistribution>("material_imbalance");

            reg.add<ResultDistribution>("results");

            reg.add<PieceCountCounter>("piece_count");

            reg.add<EndgameConfigurations<6>>("endgames_6man");

            return reg;
        }();

        return s_reg;
    }

    void do_gather_statistics(
        const std::string& filename,
        StatisticGathererSet& statistic_gatherers,
        std::uint64_t max_count,
        const std::optional<std::string>& output_filename)
    {
        Thread* th = Threads.main();
        Position& pos = th->rootPos;
        StateInfo si;

        auto in = Tools::open_sfen_input_file(filename);

        auto on_entry = [&](const Position& position, const Move& move, const PackedSfenValue& psv) {
            statistic_gatherers.on_entry(position, move, psv);
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

            auto& psv = v.value();

            pos.set_from_packed_sfen(psv.sfen, &si, th);

            on_entry(pos, (Move)psv.move, psv);

            num_processed += 1;
            if (num_processed % 1'000'000 == 0)
            {
                std::cout << "Processed " << num_processed << " positions.\n";
            }
        }

        std::cout << "Finished gathering statistics.\n\n";
        std::cout << "Results:\n\n";

        const auto output_str = statistic_gatherers.get_output().to_string();
        std::cout << output_str;
        if (output_filename.has_value())
        {
            std::ofstream out_file(*output_filename);
            out_file << output_str;
        }
    }

    void gather_statistics(std::istringstream& is)
    {
        Eval::NNUE::init();

        auto& registry = get_statistics_gatherers_registry();

        StatisticGathererSet statistic_gatherers;

        std::string input_file;
        std::optional<std::string> output_file;
        std::uint64_t max_count = std::numeric_limits<std::uint64_t>::max();

        while(true)
        {
            std::string token;
            is >> token;

            if (token == "")
                break;

            if (token == "input_file")
                is >> input_file;
            else if (token == "output_file")
            {
                std::string s;
                is >> s;
                output_file = s;
            }
            else if (token == "max_count")
                is >> max_count;
            else
                registry.add_statistic_gatherers_by_group(statistic_gatherers, token);
        }

        do_gather_statistics(input_file, statistic_gatherers, max_count, output_file);
    }

}
