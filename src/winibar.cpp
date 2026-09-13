/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

  Winibar Extension
  Copyright (C) 2025-2026 Heriniaina Andry Raboanary

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include "winibar.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

#include "misc.h"
#include "movegen.h"
#include "position.h"
#include "search.h"
#include "tt.h"
#include "types.h"
#include "ucioption.h"

namespace Stockfish::Winibar {

namespace {

// Engineering-default HDI weights (sum to 1.0)
constexpr double W_SAFE     = 0.30;
constexpr double W_TRAP     = 0.20;
constexpr double W_VOL      = 0.15;
constexpr double W_COMEBACK = 0.15;
constexpr double W_DIST     = 0.15;
constexpr double W_E        = 0.05;

constexpr double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }

Value cp_to_value_approx(int cp) { return Value(int(PawnValue) * cp / 100); }

int value_to_cp_approx(Value v) { return int(100 * int(v) / int(PawnValue)); }

}  // namespace


// All Access::* members are friends of Search::Worker.
Value Access::shallow_probe(Search::Worker& worker,
                            Position&       pos,
                            Depth           depth,
                            Value           alpha,
                            Value           beta,
                            int             ply,
                            int             maxPly,
                            TimePoint       deadline,
                            bool&           timedOut) {

    if (now() >= deadline)
    {
        timedOut = true;
        return worker.evaluate(pos);
    }

    if (depth <= 0 || ply >= maxPly)
        return worker.evaluate(pos);

    MoveList<LEGAL> list(pos);
    if (list.size() == 0)
        return pos.checkers() ? mated_in(ply) : VALUE_DRAW;

    Move ttMove = Move::none();
    {
        auto [ttHit, ttData, ttWriter] = worker.tt.probe(pos.key());
        (void) ttWriter;
        if (ttHit)
        {
            ttMove = ttData.move;
            if (ttData.depth >= depth && is_valid(ttData.value) && !is_decisive(ttData.value))
            {
                if (ttData.bound == BOUND_EXACT)
                    return ttData.value;
                if (ttData.bound == BOUND_LOWER && ttData.value >= beta)
                    return ttData.value;
                if (ttData.bound == BOUND_UPPER && ttData.value <= alpha)
                    return ttData.value;
            }
        }
    }

    Value     best = -VALUE_INFINITE;
    StateInfo st;

    // Probe moves: TT move first, then remaining legal moves.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const auto& m : list)
        {
            if (timedOut)
                return best;
            if (pass == 0 && m != ttMove)
                continue;
            if (pass == 1 && m == ttMove)
                continue;
            if (pass == 0 && ttMove == Move::none())
                break;

            worker.do_move(pos, m, st, nullptr);
            Value v = -shallow_probe(worker, pos, Depth(depth - 1), -beta, -alpha, ply + 1, maxPly,
                                     deadline, timedOut);
            worker.undo_move(pos, m);

            if (v > best)
                best = v;
            if (v > alpha)
                alpha = v;
            if (alpha >= beta)
                return best;
        }
    }

    return best;
}


std::vector<ScoredMove> Access::order_replies(Search::Worker& worker,
                                              Position&       pos,
                                              TimePoint       deadline,
                                              bool&           timedOut) {
    std::vector<ScoredMove> moves;
    MoveList<LEGAL>         list(pos);
    moves.reserve(list.size());

    StateInfo st;
    for (const auto& m : list)
    {
        if (now() >= deadline)
        {
            timedOut = true;
            break;
        }
        worker.do_move(pos, m, st, nullptr);
        Value child = worker.evaluate(pos);
        worker.undo_move(pos, m);
        moves.push_back({m, Value(-child)});
    }

    std::stable_sort(moves.begin(), moves.end(),
                     [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });
    return moves;
}


Metrics Access::compute(Search::Worker& worker) {

    const OptionsMap& options = worker.options;

    const Depth     probeDepth = Depth(std::max(0, int(options["WinibarDepth"])));
    const usize     topK       = usize(std::max(1, int(options["WinibarTopK"])));
    const int       horizon    = std::max(1, int(options["WinibarHorizon"]));
    const int       trapDropCp = std::max(1, int(options["WinibarTrapDrop"]));
    const TimePoint budgetMs   = TimePoint(std::max(1, int(options["WinibarPerfBudgetMs"])));

    const TimePoint deadline = now() + budgetMs;
    bool            timedOut = false;

    Metrics m;

    if (worker.rootMoves.empty() || worker.rootMoves[0].pv.empty())
        return m;

    Position&   root      = worker.rootPos;
    const Value rootScore = worker.rootMoves[0].score;
    const Move  bestMove  = worker.rootMoves[0].pv[0];

    StateInfo stAfterBest;
    worker.do_move(root, bestMove, stAfterBest, nullptr);

    auto        ordered = order_replies(worker, root, deadline, timedOut);
    const usize legalN  = ordered.size();
    const usize nTake   = std::min(topK, legalN);

    std::vector<Value> replyScores;
    replyScores.reserve(nTake);

    const Value trapDrop = cp_to_value_approx(trapDropCp);

    for (usize i = 0; i < nTake && !timedOut; ++i)
    {
        if (now() >= deadline)
        {
            timedOut = true;
            break;
        }

        Move      mv = ordered[i].move;
        StateInfo st;
        worker.do_move(root, mv, st, nullptr);

        Value vChild;
        if (probeDepth <= 0)
            vChild = worker.evaluate(root);
        else
            vChild = shallow_probe(worker, root, probeDepth, -VALUE_INFINITE, VALUE_INFINITE, 0,
                                   horizon, deadline, timedOut);

        worker.undo_move(root, mv);
        replyScores.push_back(vChild);
    }

    worker.undo_move(root, bestMove);

    const double N = double(replyScores.size());
    m.n            = N;
    // partial only when the time budget stopped us before finishing Top-K
    m.partial      = timedOut && replyScores.size() < nTake;
    // E: fraction of legal replies covered (Top-K / budget limited)
    m.e            = legalN == 0 ? 0.0 : clamp01(double(replyScores.size()) / double(legalN));

    if (replyScores.empty())
    {
        m.partial = true;
        return m;
    }

    Value bestReply      = *std::max_element(replyScores.begin(), replyScores.end());
    Value worstReply     = *std::min_element(replyScores.begin(), replyScores.end());
    Value bestForReplier = worstReply;

    int safe = 0;
    int save = 0;
    for (Value v : replyScores)
    {
        if (v <= bestForReplier + trapDrop)
            ++safe;

        Value weakerPov = rootScore < 0 ? Value(-v) : v;
        if (weakerPov >= -2 * trapDrop)
            ++save;
    }

    m.s = double(safe);
    m.t = clamp01(1.0 - (m.s / N));

    double mean = 0;
    for (Value v : replyScores)
        mean += double(v);
    mean /= N;

    double var = 0;
    for (Value v : replyScores)
    {
        double d = double(v) - mean;
        var += d * d;
    }
    var /= N;

    const double stdevCp = std::sqrt(var) * 100.0 / double(PawnValue);
    m.v                  = clamp01(stdevCp / std::max(1.0, double(trapDropCp)));

    {
        double bestW, meanW;
        if (rootScore < 0)
        {
            bestW = double(bestReply);
            meanW = mean;
        }
        else
        {
            bestW = double(-bestForReplier);
            meanW = -mean;
        }
        m.c = clamp01((bestW - meanW) * 100.0 / double(PawnValue)
                      / std::max(1.0, double(trapDropCp)));
    }

    {
        const double spanCp =
          std::abs(double(value_to_cp_approx(bestReply) - value_to_cp_approx(worstReply)));
        const double steps = spanCp / std::max(1.0, double(trapDropCp));
        m.d                = clamp01(steps / double(std::max(1, horizon)));
    }

    // Psave v0: WINIMAX equiprobability P_human(m) = 1/N
    m.psave = double(save) / N;

    const double safeRatio = m.s / N;
    m.hdi                  = clamp01(W_SAFE * safeRatio + W_TRAP * (1.0 - m.t) + W_VOL * (1.0 - m.v)
                                     + W_COMEBACK * m.c + W_DIST * m.d + W_E * m.e);

    return m;
}


std::string format_info(const Metrics& m) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3);
    ss << "Winibar HDI=" << m.hdi << " Psave=" << m.psave << " S=" << m.s << " N=" << m.n
       << " T=" << m.t << " V=" << m.v << " C=" << m.c << " D=" << m.d << " E=" << m.e;
    if (m.partial)
        ss << " partial=true";
    return ss.str();
}

}  // namespace Stockfish::Winibar


namespace Stockfish {

void Search::Worker::winibar_analyze_and_emit() {
    if (!bool(options["Winibar"]))
        return;

    Winibar::Metrics m = Winibar::Access::compute(*this);
    sync_cout << "info string " << Winibar::format_info(m) << sync_endl;
}

}  // namespace Stockfish
