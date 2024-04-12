/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "uci.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include "benchmark.h"
#include "engine.h"
#include "evaluate.h"
#include "movegen.h"
#include "position.h"
#include "score.h"
#include "search.h"
#include "syzygy/tbprobe.h"
#include "types.h"
#include "ucioption.h"

namespace Stockfish {

constexpr auto StartFEN  = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
constexpr int  MaxHashMB = Is64Bit ? 33554432 : 2048;

template<typename... Ts>
struct overload: Ts... {
    using Ts::operator()...;
};

template<typename... Ts>
overload(Ts...) -> overload<Ts...>;

UCIEngine::UCIEngine(int argc, char** argv) :
    engine(argv[0]),
    cli(argc, argv) {

    auto& options = engine.get_options();

    options["Debug Log File"] << Option("", [](const Option& o) { start_logger(o); });

    options["Threads"] << Option(1, 1, 1024, [this](const Option&) { engine.resize_threads(); });

    options["Hash"] << Option(16, 1, MaxHashMB, [this](const Option& o) { engine.set_tt_size(o); });

    options["Clear Hash"] << Option([this](const Option&) { engine.search_clear(); });
    options["Ponder"] << Option(false);
    options["MultiPV"] << Option(1, 1, MAX_MOVES);
    options["Skill Level"] << Option(20, 0, 20);
    options["Move Overhead"] << Option(10, 0, 5000);
    options["nodestime"] << Option(0, 0, 10000);
    options["UCI_Chess960"] << Option(false);
    options["UCI_LimitStrength"] << Option(false);
    options["UCI_Elo"] << Option(1320, 1320, 3190);
    options["UCI_ShowWDL"] << Option(false);
    options["SyzygyPath"] << Option("<empty>", [](const Option& o) { Tablebases::init(o); });
    options["SyzygyProbeDepth"] << Option(1, 1, 100);
    options["Syzygy50MoveRule"] << Option(true);
    options["SyzygyProbeLimit"] << Option(7, 0, 7);
    options["EvalFile"] << Option(EvalFileDefaultNameBig,
                                  [this](const Option& o) { engine.load_big_network(o); });
    options["EvalFileSmall"] << Option(EvalFileDefaultNameSmall,
                                       [this](const Option& o) { engine.load_small_network(o); });


    engine.set_on_iter([](const auto& i) { on_iter(i); });
    engine.set_on_update_no_moves([](const auto& i) { on_update_no_moves(i); });
    engine.set_on_update_full([&](const auto& i) { on_update_full(i, options["UCI_ShowWDL"]); });
    engine.set_on_bestmove([](const auto& bm, const auto& p) { on_bestmove(bm, p); });

    engine.load_networks();
    engine.resize_threads();
    engine.search_clear();  // After threads are up
}

void UCIEngine::loop() {

    Position     pos;
    std::string  token, cmd;
    StateListPtr states(new std::deque<StateInfo>(1));

    pos.set(StartFEN, false, &states->back());

    for (int i = 1; i < cli.argc; ++i)
        cmd += std::string(cli.argv[i]) + " ";

    do
    {
        if (cli.argc == 1
            && !getline(std::cin, cmd))  // Wait for an input or an end-of-file (EOF) indication
            cmd = "quit";

        std::istringstream is(cmd);

        token.clear();  // Avoid a stale if getline() returns nothing or a blank line
        is >> std::skipws >> token;

        if (token == "quit" || token == "stop")
            engine.stop();

        // The GUI sends 'ponderhit' to tell that the user has played the expected move.
        // So, 'ponderhit' is sent if pondering was done on the same move that the user
        // has played. The search should continue, but should also switch from pondering
        // to the normal search.
        else if (token == "ponderhit")
            engine.set_ponderhit(false);

        else if (token == "uci")
            sync_cout << "id name " << engine_info(true) << "\n"
                      << engine.get_options() << "\nuciok" << sync_endl;

        else if (token == "setoption")
            setoption(is);
        else if (token == "go")
            go(pos, is);
        else if (token == "position")
            position(is);
        else if (token == "ucinewgame")
            engine.search_clear();
        else if (token == "isready")
            sync_cout << "readyok" << sync_endl;

        // Add custom non-UCI commands, mainly for debugging purposes.
        // These commands must not be used during a search!
        else if (token == "flip")
            pos.flip();
        else if (token == "bench")
            bench(pos, is);
        else if (token == "d")
            sync_cout << pos << sync_endl;
        else if (token == "eval")
            engine.trace_eval();
        else if (token == "compiler")
            sync_cout << compiler_info() << sync_endl;
        else if (token == "export_net")
        {
            std::pair<std::optional<std::string>, std::string> files[2];

            if (is >> std::skipws >> files[0].second)
                files[0].first = files[0].second;

            if (is >> std::skipws >> files[1].second)
                files[1].first = files[1].second;

            engine.save_network(files);
        }
        else if (token == "--help" || token == "help" || token == "--license" || token == "license")
            sync_cout
              << "\nStockfish is a powerful chess engine for playing and analyzing."
                 "\nIt is released as free software licensed under the GNU GPLv3 License."
                 "\nStockfish is normally used with a graphical user interface (GUI) and implements"
                 "\nthe Universal Chess Interface (UCI) protocol to communicate with a GUI, an API, etc."
                 "\nFor any further information, visit https://github.com/official-stockfish/Stockfish#readme"
                 "\nor read the corresponding README.md and Copying.txt files distributed along with this program.\n"
              << sync_endl;
        else if (!token.empty() && token[0] != '#')
            sync_cout << "Unknown command: '" << cmd << "'. Type help for more information."
                      << sync_endl;

    } while (token != "quit" && cli.argc == 1);  // The command-line arguments are one-shot
}

Search::LimitsType UCIEngine::parse_limits(const Position& pos, std::istream& is) {
    Search::LimitsType limits;
    std::string        token;

    limits.startTime = now();  // The search starts as early as possible

    while (is >> token)
        if (token == "searchmoves")  // Needs to be the last command on the line
            while (is >> token)
                limits.searchmoves.push_back(to_move(pos, token));

        else if (token == "wtime")
            is >> limits.time[WHITE];
        else if (token == "btime")
            is >> limits.time[BLACK];
        else if (token == "winc")
            is >> limits.inc[WHITE];
        else if (token == "binc")
            is >> limits.inc[BLACK];
        else if (token == "movestogo")
            is >> limits.movestogo;
        else if (token == "depth")
            is >> limits.depth;
        else if (token == "nodes")
            is >> limits.nodes;
        else if (token == "movetime")
            is >> limits.movetime;
        else if (token == "mate")
            is >> limits.mate;
        else if (token == "perft")
            is >> limits.perft;
        else if (token == "infinite")
            limits.infinite = 1;
        else if (token == "ponder")
            limits.ponderMode = true;

    return limits;
}

void UCIEngine::go(Position& pos, std::istringstream& is) {

    Search::LimitsType limits = parse_limits(pos, is);
    engine.go(limits);
}

void UCIEngine::bench(Position& pos, std::istream& args) {
    std::string token;
    uint64_t    num, nodes = 0, cnt = 1;
    uint64_t    nodesSearched = 0;
    const auto& options       = engine.get_options();

    engine.set_on_update_full([&](const auto& i) {
        nodesSearched = i.nodes;
        on_update_full(i, options["UCI_ShowWDL"]);
    });

    std::vector<std::string> list = setup_bench(pos, args);

    num = count_if(list.begin(), list.end(),
                   [](const std::string& s) { return s.find("go ") == 0 || s.find("eval") == 0; });

    TimePoint elapsed = now();

    for (const auto& cmd : list)
    {
        std::istringstream is(cmd);
        is >> std::skipws >> token;

        if (token == "go" || token == "eval")
        {
            std::cerr << "\nPosition: " << cnt++ << '/' << num << " (" << pos.fen() << ")"
                      << std::endl;
            if (token == "go")
            {
                go(pos, is);
                engine.wait_for_search_finished();
                nodes += nodesSearched;
                nodesSearched = 0;
            }
            else
                engine.trace_eval();
        }
        else if (token == "setoption")
            setoption(is);
        else if (token == "position")
            position(is);
        else if (token == "ucinewgame")
        {
            engine.search_clear();  // search_clear may take a while
            elapsed = now();
        }
    }

    elapsed = now() - elapsed + 1;  // Ensure positivity to avoid a 'divide by zero'

    dbg_print();

    std::cerr << "\n==========================="
              << "\nTotal time (ms) : " << elapsed << "\nNodes searched  : " << nodes
              << "\nNodes/second    : " << 1000 * nodes / elapsed << std::endl;

    // reset callback, to not capture a dangling reference to nodesSearched
    engine.set_on_update_full([&](const auto& i) { on_update_full(i, options["UCI_ShowWDL"]); });
}


void UCIEngine::setoption(std::istringstream& is) {
    engine.wait_for_search_finished();
    engine.get_options().setoption(is);
}

void UCIEngine::position(std::istringstream& is) {
    std::string token, fen;

    is >> token;

    if (token == "startpos")
    {
        fen = StartFEN;
        is >> token;  // Consume the "moves" token, if any
    }
    else if (token == "fen")
        while (is >> token && token != "moves")
            fen += token + " ";
    else
        return;

    std::vector<std::string> moves;

    while (is >> token)
    {
        moves.push_back(token);
    }

    engine.set_position(fen, moves);
}

namespace {

struct WinRateParams {
    double a;
    double b;
};

WinRateParams win_rate_params(const Position& pos) {

    int material = pos.count<PAWN>() + 3 * pos.count<KNIGHT>() + 3 * pos.count<BISHOP>()
                 + 5 * pos.count<ROOK>() + 9 * pos.count<QUEEN>();

    // The fitted model only uses data for material counts in [10, 78], and is anchored at count 58.
    double m = std::clamp(material, 10, 78) / 58.0;

    // Return a = p_a(material) and b = p_b(material), see github.com/official-stockfish/WDL_model
    constexpr double as[] = {-185.71965483, 504.85014385, -438.58295743, 474.04604627};
    constexpr double bs[] = {89.23542728, -137.02141296, 73.28669021, 47.53376190};

    double a = (((as[0] * m + as[1]) * m + as[2]) * m) + as[3];
    double b = (((bs[0] * m + bs[1]) * m + bs[2]) * m) + bs[3];

    return {a, b};
}

// The win rate model is 1 / (1 + exp((a - eval) / b)), where a = p_a(material) and b = p_b(material).
// It fits the LTC fishtest statistics rather accurately.
int win_rate_model(Value v, const Position& pos) {

    auto [a, b] = win_rate_params(pos);

    // Return the win rate in per mille units, rounded to the nearest integer.
    return int(0.5 + 1000 / (1 + std::exp((a - double(v)) / b)));
}
}

std::string UCIEngine::format_score(const Score& s) {
    constexpr int TB_CP = 20000;
    const auto    format =
      overload{[](Score::Mate mate) -> std::string {
                   auto m = (mate.plies > 0 ? (mate.plies + 1) : mate.plies) / 2;
                   return std::string("mate ") + std::to_string(m);
               },
               [](Score::TBWin tb) -> std::string {
                   return std::string("cp ")
                        + std::to_string((tb.plies > 0 ? TB_CP - tb.plies : -TB_CP - tb.plies));
               },
               [](Score::InternalUnits units) -> std::string {
                   return std::string("cp ") + std::to_string(units.value);
               }};

    return s.visit(format);
}

// Turns a Value to an integer centipawn number,
// without treatment of mate and similar special scores.
int UCIEngine::to_cp(Value v, const Position& pos) {

    // In general, the score can be defined via the the WDL as
    // (log(1/L - 1) - log(1/W - 1)) / ((log(1/L - 1) + log(1/W - 1))
    // Based on our win_rate_model, this simply yields v / a.

    auto [a, b] = win_rate_params(pos);

    return std::round(100 * int(v) / a);
}

std::string UCIEngine::wdl(Value v, const Position& pos) {
    std::stringstream ss;

    int wdl_w = win_rate_model(v, pos);
    int wdl_l = win_rate_model(-v, pos);
    int wdl_d = 1000 - wdl_w - wdl_l;
    ss << " wdl " << wdl_w << " " << wdl_d << " " << wdl_l;

    return ss.str();
}

std::string UCIEngine::square(Square s) {
    return std::string{char('a' + file_of(s)), char('1' + rank_of(s))};
}

std::string UCIEngine::move(Move m, bool chess960) {
    if (m == Move::none())
        return "(none)";

    if (m == Move::null())
        return "0000";

    Square from = m.from_sq();
    Square to   = m.to_sq();

    if (m.type_of() == CASTLING && !chess960)
        to = make_square(to > from ? FILE_G : FILE_C, rank_of(from));

    std::string move = square(from) + square(to);

    if (m.type_of() == PROMOTION)
        move += " pnbrqk"[m.promotion_type()];

    return move;
}


Move UCIEngine::to_move(const Position& pos, std::string str) {
    if (str.length() == 5)
        str[4] = char(tolower(str[4]));  // The promotion piece character must be lowercased

    for (const auto& m : MoveList<LEGAL>(pos))
        if (str == move(m, pos.is_chess960()))
            return m;

    return Move::none();
}

void UCIEngine::on_update_no_moves(const Engine::InfoShort& info) {
    sync_cout << "info depth" << info.depth << " score " << format_score(info.score) << sync_endl;
}

void UCIEngine::on_update_full(const Engine::InfoFull& info, bool showWDL) {
    std::stringstream ss;

    ss << "info";
    ss << " depth " << info.depth                 //
       << " seldepth " << info.selDepth           //
       << " multipv " << info.multiPV             //
       << " score " << format_score(info.score);  //

    if (showWDL)
        ss << " wdl " << info.wdl;

    if (!info.bound.empty())
        ss << " " << info.bound;

    ss << " nodes " << info.nodes        //
       << " nps " << info.nps            //
       << " hashfull " << info.hashfull  //
       << " tbhits " << info.tbHits      //
       << " time " << info.timeMs        //
       << " pv " << info.pv;             //

    sync_cout << ss.str() << sync_endl;
}

void UCIEngine::on_iter(const Engine::InfoIter& info) {
    std::stringstream ss;

    ss << "info";
    ss << " depth " << info.depth                     //
       << " currmove " << info.currmove               //
       << " currmovenumber " << info.currmovenumber;  //

    sync_cout << ss.str() << sync_endl;
}

void UCIEngine::on_bestmove(std::string_view bestmove, std::string_view ponder) {
    sync_cout << "bestmove " << bestmove;
    if (!ponder.empty())
        std::cout << " ponder " << ponder;
    std::cout << sync_endl;
}

}  // namespace Stockfish
