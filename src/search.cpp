/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2010 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "book.h"
#include "evaluate.h"
#include "history.h"
#include "misc.h"
#include "move.h"
#include "movegen.h"
#include "movepick.h"
#include "search.h"
#include "timeman.h"
#include "thread.h"
#include "tt.h"
#include "ucioption.h"

using std::cout;
using std::endl;
using std::string;

namespace {

  // Set to true to force running with one thread. Used for debugging
  const bool FakeSplit = false;

  // Different node types, used as template parameter
  enum NodeType { Root, PV, NonPV, SplitPointPV, SplitPointNonPV };

  // RootMove struct is used for moves at the root of the tree. For each root
  // move, we store two scores, a node count, and a PV (really a refutation
  // in the case of moves which fail low). Value pv_score is normally set at
  // -VALUE_INFINITE for all non-pv moves, while non_pv_score is computed
  // according to the order in which moves are returned by MovePicker.
  struct RootMove {

    RootMove();
    RootMove(const RootMove& rm) { *this = rm; }
    RootMove& operator=(const RootMove& rm);

    // RootMove::operator<() is the comparison function used when
    // sorting the moves. A move m1 is considered to be better
    // than a move m2 if it has an higher pv_score, or if it has
    // equal pv_score but m1 has the higher non_pv_score. In this way
    // we are guaranteed that PV moves are always sorted as first.
    bool operator<(const RootMove& m) const {
      return pv_score != m.pv_score ? pv_score < m.pv_score
                                    : non_pv_score < m.non_pv_score;
    }

    void extract_pv_from_tt(Position& pos);
    void insert_pv_in_tt(Position& pos);

    int64_t nodes;
    Value pv_score;
    Value non_pv_score;
    Move pv[PLY_MAX_PLUS_2];
  };

  // RootMoveList struct is mainly a std::vector of RootMove objects
  struct RootMoveList : public std::vector<RootMove> {
    void init(Position& pos, Move searchMoves[]);
    int bestMoveChanges;
  };


  /// Constants

  // Lookup table to check if a Piece is a slider and its access function
  const bool Slidings[18] = { 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1 };
  inline bool piece_is_slider(Piece p) { return Slidings[p]; }

  // Step 6. Razoring

  // Maximum depth for razoring
  const Depth RazorDepth = 4 * ONE_PLY;

  // Dynamic razoring margin based on depth
  inline Value razor_margin(Depth d) { return Value(0x200 + 0x10 * int(d)); }

  // Maximum depth for use of dynamic threat detection when null move fails low
  const Depth ThreatDepth = 5 * ONE_PLY;

  // Step 9. Internal iterative deepening

  // Minimum depth for use of internal iterative deepening
  const Depth IIDDepth[] = { 8 * ONE_PLY, 5 * ONE_PLY };

  // At Non-PV nodes we do an internal iterative deepening search
  // when the static evaluation is bigger then beta - IIDMargin.
  const Value IIDMargin = Value(0x100);

  // Step 11. Decide the new search depth

  // Extensions. Array index 0 is used for non-PV nodes, index 1 for PV nodes
  const Depth CheckExtension[]         = { ONE_PLY / 2, ONE_PLY / 1 };
  const Depth PawnEndgameExtension[]   = { ONE_PLY / 1, ONE_PLY / 1 };
  const Depth PawnPushTo7thExtension[] = { ONE_PLY / 2, ONE_PLY / 2 };
  const Depth PassedPawnExtension[]    = {  DEPTH_ZERO, ONE_PLY / 2 };

  // Minimum depth for use of singular extension
  const Depth SingularExtensionDepth[] = { 8 * ONE_PLY, 6 * ONE_PLY };

  // Step 12. Futility pruning

  // Futility margin for quiescence search
  const Value FutilityMarginQS = Value(0x80);

  // Futility lookup tables (initialized at startup) and their access functions
  Value FutilityMargins[16][64]; // [depth][moveNumber]
  int FutilityMoveCounts[32];    // [depth]

  inline Value futility_margin(Depth d, int mn) {

    return d < 7 * ONE_PLY ? FutilityMargins[Max(d, 1)][Min(mn, 63)]
                           : 2 * VALUE_INFINITE;
  }

  inline int futility_move_count(Depth d) {

    return d < 16 * ONE_PLY ? FutilityMoveCounts[d] : MAX_MOVES;
  }

  // Step 14. Reduced search

  // Reduction lookup tables (initialized at startup) and their access function
  int8_t Reductions[2][64][64]; // [pv][depth][moveNumber]

  template <bool PvNode> inline Depth reduction(Depth d, int mn) {

    return (Depth) Reductions[PvNode][Min(d / ONE_PLY, 63)][Min(mn, 63)];
  }

  // Easy move margin. An easy move candidate must be at least this much
  // better than the second best move.
  const Value EasyMoveMargin = Value(0x200);


  /// Namespace variables

  // Root move list
  RootMoveList Rml;

  // MultiPV mode
  int MultiPV, UCIMultiPV;

  // Time management variables
  bool StopOnPonderhit, FirstRootMove, StopRequest, QuitRequest, AspirationFailLow;
  TimeManager TimeMgr;
  SearchLimits Limits;

  // Log file
  std::ofstream LogFile;

  // Skill level adjustment
  int SkillLevel;
  bool SkillLevelEnabled;

  // Node counters, used only by thread[0] but try to keep in different cache
  // lines (64 bytes each) from the heavy multi-thread read accessed variables.
  bool SendSearchedNodes;
  int NodesSincePoll;
  int NodesBetweenPolls = 30000;

  // History table
  History H;


  /// Local functions

  Move id_loop(Position& pos, Move searchMoves[], Move* ponderMove);

  template <NodeType NT>
  Value search(Position& pos, SearchStack* ss, Value alpha, Value beta, Depth depth);

  template <NodeType NT>
  Value qsearch(Position& pos, SearchStack* ss, Value alpha, Value beta, Depth depth);

  bool check_is_dangerous(Position &pos, Move move, Value futilityBase, Value beta, Value *bValue);
  bool connected_moves(const Position& pos, Move m1, Move m2);
  Value value_to_tt(Value v, int ply);
  Value value_from_tt(Value v, int ply);
  bool ok_to_use_TT(const TTEntry* tte, Depth depth, Value beta, int ply);
  bool connected_threat(const Position& pos, Move m, Move threat);
  Value refine_eval(const TTEntry* tte, Value defaultEval, int ply);
  void update_history(const Position& pos, Move move, Depth depth, Move movesSearched[], int moveCount);
  void update_gains(const Position& pos, Move move, Value before, Value after);
  void do_skill_level(Move* best, Move* ponder);

  int current_search_time(int set = 0);
  string score_to_uci(Value v, Value alpha, Value beta);
  string speed_to_uci(int64_t nodes);
  string pv_to_uci(Move pv[], int pvNum, bool chess960);
  string pretty_pv(Position& pos, int depth, Value score, int time, Move pv[]);
  string depth_to_uci(Depth depth);
  void poll(const Position& pos);
  void wait_for_stop_or_ponderhit();

  // MovePickerExt template class extends MovePicker and allows to choose at compile
  // time the proper moves source according to the type of node. In the default case
  // we simply create and use a standard MovePicker object.
  template<NodeType> struct MovePickerExt : public MovePicker {

    MovePickerExt(const Position& p, Move ttm, Depth d, const History& h, SearchStack* ss, Value b)
                  : MovePicker(p, ttm, d, h, ss, b) {}

    RootMove& current() { assert(false); return Rml[0]; } // Dummy, needed to compile
  };

  // In case of a SpNode we use split point's shared MovePicker object as moves source
  template<> struct MovePickerExt<SplitPointNonPV> : public MovePickerExt<NonPV> {

    MovePickerExt(const Position& p, Move ttm, Depth d, const History& h, SearchStack* ss, Value b)
                  : MovePickerExt<NonPV>(p, ttm, d, h, ss, b), mp(ss->sp->mp) {}

    Move get_next_move() { return mp->get_next_move(); }
    MovePicker* mp;
  };

  template<> struct MovePickerExt<SplitPointPV> : public MovePickerExt<SplitPointNonPV> {

    MovePickerExt(const Position& p, Move ttm, Depth d, const History& h, SearchStack* ss, Value b)
                  : MovePickerExt<SplitPointNonPV>(p, ttm, d, h, ss, b) {}
  };

  // In case of a Root node we use RootMoveList as moves source
  template<> struct MovePickerExt<Root> : public MovePicker {

    MovePickerExt(const Position&, Move, Depth, const History&, SearchStack*, Value);
    RootMove& current() { return Rml[cur]; }
    Move get_next_move() { return ++cur < (int)Rml.size() ? Rml[cur].pv[0] : MOVE_NONE; }

    int cur;
  };

  // Overload operator<<() to make it easier to print moves in a coordinate
  // notation compatible with UCI protocol.
  std::ostream& operator<<(std::ostream& os, Move m) {

    bool chess960 = (os.iword(0) != 0); // See set960()
    return os << move_to_uci(m, chess960);
  }

  // When formatting a move for std::cout we must know if we are in Chess960
  // or not. To keep using the handy operator<<() on the move the trick is to
  // embed this flag in the stream itself. Function-like named enum set960 is
  // used as a custom manipulator and the stream internal general-purpose array,
  // accessed through ios_base::iword(), is used to pass the flag to the move's
  // operator<<() that will read it to properly format castling moves.
  enum set960 {};

  std::ostream& operator<< (std::ostream& os, const set960& f) {

    os.iword(0) = int(f);
    return os;
  }

  // extension() decides whether a move should be searched with normal depth,
  // or with extended depth. Certain classes of moves (checking moves, in
  // particular) are searched with bigger depth than ordinary moves and in
  // any case are marked as 'dangerous'. Note that also if a move is not
  // extended, as example because the corresponding UCI option is set to zero,
  // the move is marked as 'dangerous' so, at least, we avoid to prune it.
  template <bool PvNode>
  FORCE_INLINE Depth extension(const Position& pos, Move m, bool captureOrPromotion,
                               bool moveIsCheck, bool* dangerous) {
    assert(m != MOVE_NONE);

    Depth result = DEPTH_ZERO;
    *dangerous = moveIsCheck;

    if (moveIsCheck && pos.see_sign(m) >= 0)
        result += CheckExtension[PvNode];

    if (piece_type(pos.piece_on(move_from(m))) == PAWN)
    {
        Color c = pos.side_to_move();
        if (relative_rank(c, move_to(m)) == RANK_7)
        {
            result += PawnPushTo7thExtension[PvNode];
            *dangerous = true;
        }
        if (pos.pawn_is_passed(c, move_to(m)))
        {
            result += PassedPawnExtension[PvNode];
            *dangerous = true;
        }
    }

    if (   captureOrPromotion
        && piece_type(pos.piece_on(move_to(m))) != PAWN
        && (  pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK)
            - piece_value_midgame(pos.piece_on(move_to(m))) == VALUE_ZERO)
        && !move_is_special(m))
    {
        result += PawnEndgameExtension[PvNode];
        *dangerous = true;
    }

    return Min(result, ONE_PLY);
  }

} // namespace


/// init_search() is called during startup to initialize various lookup tables

void init_search() {

  int d;  // depth (ONE_PLY == 2)
  int hd; // half depth (ONE_PLY == 1)
  int mc; // moveCount

  // Init reductions array
  for (hd = 1; hd < 64; hd++) for (mc = 1; mc < 64; mc++)
  {
      double    pvRed = log(double(hd)) * log(double(mc)) / 3.0;
      double nonPVRed = 0.33 + log(double(hd)) * log(double(mc)) / 2.25;
      Reductions[1][hd][mc] = (int8_t) (   pvRed >= 1.0 ? floor(   pvRed * int(ONE_PLY)) : 0);
      Reductions[0][hd][mc] = (int8_t) (nonPVRed >= 1.0 ? floor(nonPVRed * int(ONE_PLY)) : 0);
  }

  // Init futility margins array
  for (d = 1; d < 16; d++) for (mc = 0; mc < 64; mc++)
      FutilityMargins[d][mc] = Value(112 * int(log(double(d * d) / 2) / log(2.0) + 1.001) - 8 * mc + 45);

  // Init futility move count array
  for (d = 0; d < 32; d++)
      FutilityMoveCounts[d] = int(3.001 + 0.25 * pow(d, 2.0));
}


/// perft() is our utility to verify move generation. All the leaf nodes up to
/// the given depth are generated and counted and the sum returned.

int64_t perft(Position& pos, Depth depth) {

  StateInfo st;
  int64_t sum = 0;

  // Generate all legal moves
  MoveList<MV_LEGAL> ml(pos);

  // If we are at the last ply we don't need to do and undo
  // the moves, just to count them.
  if (depth <= ONE_PLY)
      return ml.size();

  // Loop through all legal moves
  CheckInfo ci(pos);
  for ( ; !ml.end(); ++ml)
  {
      pos.do_move(ml.move(), st, ci, pos.move_gives_check(ml.move(), ci));
      sum += perft(pos, depth - ONE_PLY);
      pos.undo_move(ml.move());
  }
  return sum;
}


/// think() is the external interface to Stockfish's search, and is called when
/// the program receives the UCI 'go' command. It initializes various global
/// variables, and calls id_loop(). It returns false when a "quit" command is
/// received during the search.

bool think(Position& pos, const SearchLimits& limits, Move searchMoves[]) {

  static Book book;

  // Initialize global search-related variables
  StopOnPonderhit = StopRequest = QuitRequest = AspirationFailLow = SendSearchedNodes = false;
  NodesSincePoll = 0;
  current_search_time(get_system_time());
  Limits = limits;
  TimeMgr.init(Limits, pos.startpos_ply_counter());

  // Set output steram in normal or chess960 mode
  cout << set960(pos.is_chess960());

  // Set best NodesBetweenPolls interval to avoid lagging under time pressure
  if (Limits.maxNodes)
      NodesBetweenPolls = Min(Limits.maxNodes, 30000);
  else if (Limits.time && Limits.time < 1000)
      NodesBetweenPolls = 1000;
  else if (Limits.time && Limits.time < 5000)
      NodesBetweenPolls = 5000;
  else
      NodesBetweenPolls = 30000;

  // Look for a book move
  if (Options["OwnBook"].value<bool>())
  {
      if (Options["Book File"].value<string>() != book.name())
          book.open(Options["Book File"].value<string>());

      Move bookMove = book.get_move(pos, Options["Best Book Move"].value<bool>());
      if (bookMove != MOVE_NONE)
      {
          if (Limits.ponder)
              wait_for_stop_or_ponderhit();

          cout << "bestmove " << bookMove << endl;
          return !QuitRequest;
      }
  }

  // Read UCI options
  UCIMultiPV = Options["MultiPV"].value<int>();
  SkillLevel = Options["Skill Level"].value<int>();

  read_evaluation_uci_options(pos.side_to_move());
  Threads.read_uci_options();

  // If needed allocate pawn and material hash tables and adjust TT size
  Threads.init_hash_tables();
  TT.set_size(Options["Hash"].value<int>());

  if (Options["Clear Hash"].value<bool>())
  {
      Options["Clear Hash"].set_value("false");
      TT.clear();
  }

  // Do we have to play with skill handicap? In this case enable MultiPV that
  // we will use behind the scenes to retrieve a set of possible moves.
  SkillLevelEnabled = (SkillLevel < 20);
  MultiPV = (SkillLevelEnabled ? Max(UCIMultiPV, 4) : UCIMultiPV);

  // Wake up needed threads and reset maxPly counter
  for (int i = 0; i < Threads.size(); i++)
  {
      Threads[i].wake_up();
      Threads[i].maxPly = 0;
  }

  // Write to log file and keep it open to be accessed during the search
  if (Options["Use Search Log"].value<bool>())
  {
      string name = Options["Search Log Filename"].value<string>();
      LogFile.open(name.c_str(), std::ios::out | std::ios::app);

      if (LogFile.is_open())
          LogFile << "\nSearching: "  << pos.to_fen()
                  << "\ninfinite: "   << Limits.infinite
                  << " ponder: "      << Limits.ponder
                  << " time: "        << Limits.time
                  << " increment: "   << Limits.increment
                  << " moves to go: " << Limits.movesToGo
                  << endl;
  }

  // We're ready to start thinking. Call the iterative deepening loop function
  Move ponderMove = MOVE_NONE;
  Move bestMove = id_loop(pos, searchMoves, &ponderMove);

  // Write final search statistics and close log file
  if (LogFile.is_open())
  {
      int t = current_search_time();

      LogFile << "Nodes: "          << pos.nodes_searched()
              << "\nNodes/second: " << (t > 0 ? pos.nodes_searched() * 1000 / t : 0)
              << "\nBest move: "    << move_to_san(pos, bestMove);

      StateInfo st;
      pos.do_move(bestMove, st);
      LogFile << "\nPonder move: " << move_to_san(pos, ponderMove) << endl;
      pos.undo_move(bestMove); // Return from think() with unchanged position
      LogFile.close();
  }

  // This makes all the threads to go to sleep
  Threads.set_size(1);

  // If we are pondering or in infinite search, we shouldn't print the
  // best move before we are told to do so.
  if (!StopRequest && (Limits.ponder || Limits.infinite))
      wait_for_stop_or_ponderhit();

  // Could be MOVE_NONE when searching on a stalemate position
  cout << "bestmove " << bestMove;

  // UCI protol is not clear on allowing sending an empty ponder move, instead
  // it is clear that ponder move is optional. So skip it if empty.
  if (ponderMove != MOVE_NONE)
      cout << " ponder " << ponderMove;

  cout << endl;

  return !QuitRequest;
}


namespace {

  // id_loop() is the main iterative deepening loop. It calls search() repeatedly
  // with increasing depth until the allocated thinking time has been consumed,
  // user stops the search, or the maximum search depth is reached.

  Move id_loop(Position& pos, Move searchMoves[], Move* ponderMove) {

    SearchStack ss[PLY_MAX_PLUS_2];
    Value bestValues[PLY_MAX_PLUS_2];
    int bestMoveChanges[PLY_MAX_PLUS_2];
    int depth, aspirationDelta;
    Value value, alpha, beta;
    Move bestMove, easyMove, skillBest, skillPonder;

    // Initialize stuff before a new search
    memset(ss, 0, 4 * sizeof(SearchStack));
    TT.new_search();
    H.clear();
    *ponderMove = bestMove = easyMove = skillBest = skillPonder = MOVE_NONE;
    depth = aspirationDelta = 0;
    alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
    ss->currentMove = MOVE_NULL; // Hack to skip update_gains()

    // Moves to search are verified and copied
    Rml.init(pos, searchMoves);

    // Handle special case of searching on a mate/stalemate position
    if (!Rml.size())
    {
        cout << "info" << depth_to_uci(DEPTH_ZERO)
             << score_to_uci(pos.in_check() ? -VALUE_MATE : VALUE_DRAW, alpha, beta) << endl;

        return MOVE_NONE;
    }

    // Iterative deepening loop until requested to stop or target depth reached
    while (!StopRequest && ++depth <= PLY_MAX && (!Limits.maxDepth || depth <= Limits.maxDepth))
    {
        Rml.bestMoveChanges = 0;

        // Calculate dynamic aspiration window based on previous iterations
        if (MultiPV == 1 && depth >= 5 && abs(bestValues[depth - 1]) < VALUE_KNOWN_WIN)
        {
            int prevDelta1 = bestValues[depth - 1] - bestValues[depth - 2];
            int prevDelta2 = bestValues[depth - 2] - bestValues[depth - 3];

            aspirationDelta = Min(Max(abs(prevDelta1) + abs(prevDelta2) / 2, 16), 24);
            aspirationDelta = (aspirationDelta + 7) / 8 * 8; // Round to match grainSize

            alpha = Max(bestValues[depth - 1] - aspirationDelta, -VALUE_INFINITE);
            beta  = Min(bestValues[depth - 1] + aspirationDelta,  VALUE_INFINITE);
        }

        // Start with a small aspiration window and, in case of fail high/low,
        // research with bigger window until not failing high/low anymore.
        do {
            // Search starting from ss+1 to allow calling update_gains()
            value = search<Root>(pos, ss+1, alpha, beta, depth * ONE_PLY);

            // Write PV back to transposition table in case the relevant entries
            // have been overwritten during the search.
            for (int i = 0; i < Min(MultiPV, (int)Rml.size()); i++)
                Rml[i].insert_pv_in_tt(pos);

            // Value cannot be trusted. Break out immediately!
            if (StopRequest)
                break;

            // Send full PV info to GUI if we are going to leave the loop or
            // if we have a fail high/low and we are deep in the search.
            if ((value > alpha && value < beta) || current_search_time() > 2000)
                for (int i = 0; i < Min(UCIMultiPV, (int)Rml.size()); i++)
                    cout << "info"
                         << depth_to_uci(depth * ONE_PLY)
                         << score_to_uci(Rml[i].pv_score, alpha, beta)
                         << speed_to_uci(pos.nodes_searched())
                         << pv_to_uci(Rml[i].pv, i + 1, pos.is_chess960()) << endl;

            // In case of failing high/low increase aspiration window and research,
            // otherwise exit the fail high/low loop.
            if (value >= beta)
            {
                beta = Min(beta + aspirationDelta, VALUE_INFINITE);
                aspirationDelta += aspirationDelta / 2;
            }
            else if (value <= alpha)
            {
                AspirationFailLow = true;
                StopOnPonderhit = false;

                alpha = Max(alpha - aspirationDelta, -VALUE_INFINITE);
                aspirationDelta += aspirationDelta / 2;
            }
            else
                break;

        } while (abs(value) < VALUE_KNOWN_WIN);

        // Collect info about search result
        bestMove = Rml[0].pv[0];
        *ponderMove = Rml[0].pv[1];
        bestValues[depth] = value;
        bestMoveChanges[depth] = Rml.bestMoveChanges;

        // Do we need to pick now the best and the ponder moves ?
        if (SkillLevelEnabled && depth == 1 + SkillLevel)
            do_skill_level(&skillBest, &skillPonder);

        if (LogFile.is_open())
            LogFile << pretty_pv(pos, depth, value, current_search_time(), Rml[0].pv) << endl;

        // Init easyMove after first iteration or drop if differs from the best move
        if (depth == 1 && (Rml.size() == 1 || Rml[0].pv_score > Rml[1].pv_score + EasyMoveMargin))
            easyMove = bestMove;
        else if (bestMove != easyMove)
            easyMove = MOVE_NONE;

        // Check for some early stop condition
        if (!StopRequest && Limits.useTimeManagement())
        {
            // Stop search early if one move seems to be much better than the
            // others or if there is only a single legal move. Also in the latter
            // case we search up to some depth anyway to get a proper score.
            if (   depth >= 7
                && easyMove == bestMove
                && (   Rml.size() == 1
                    ||(   Rml[0].nodes > (pos.nodes_searched() * 85) / 100
                       && current_search_time() > TimeMgr.available_time() / 16)
                    ||(   Rml[0].nodes > (pos.nodes_searched() * 98) / 100
                       && current_search_time() > TimeMgr.available_time() / 32)))
                StopRequest = true;

            // Take in account some extra time if the best move has changed
            if (depth > 4 && depth < 50)
                TimeMgr.pv_instability(bestMoveChanges[depth], bestMoveChanges[depth - 1]);

            // Stop search if most of available time is already consumed. We probably don't
            // have enough time to search the first move at the next iteration anyway.
            if (current_search_time() > (TimeMgr.available_time() * 62) / 100)
                StopRequest = true;

            // If we are allowed to ponder do not stop the search now but keep pondering
            if (StopRequest && Limits.ponder)
            {
                StopRequest = false;
                StopOnPonderhit = true;
            }
        }
    }

    // When using skills overwrite best and ponder moves with the sub-optimal ones
    if (SkillLevelEnabled)
    {
        if (skillBest == MOVE_NONE) // Still unassigned ?
            do_skill_level(&skillBest, &skillPonder);

        bestMove = skillBest;
        *ponderMove = skillPonder;
    }

    return bestMove;
  }


  // search<>() is the main search function for both PV and non-PV nodes and for
  // normal and SplitPoint nodes. When called just after a split point the search
  // is simpler because we have already probed the hash table, done a null move
  // search, and searched the first move before splitting, we don't have to repeat
  // all this work again. We also don't need to store anything to the hash table
  // here: This is taken care of after we return from the split point.

  template <NodeType NT>
  Value search(Position& pos, SearchStack* ss, Value alpha, Value beta, Depth depth) {

    const bool PvNode   = (NT == PV || NT == Root || NT == SplitPointPV);
    const bool SpNode   = (NT == SplitPointPV || NT == SplitPointNonPV);
    const bool RootNode = (NT == Root);

    assert(alpha >= -VALUE_INFINITE && alpha <= VALUE_INFINITE);
    assert(beta > alpha && beta <= VALUE_INFINITE);
    assert(PvNode || alpha == beta - 1);
    assert(pos.thread() >= 0 && pos.thread() < Threads.size());

    Move movesSearched[MAX_MOVES];
    int64_t nodes;
    StateInfo st;
    const TTEntry *tte;
    Key posKey;
    Move ttMove, move, excludedMove, threatMove;
    Depth ext, newDepth;
    ValueType vt;
    Value bestValue, value, oldAlpha;
    Value refinedValue, nullValue, futilityBase, futilityValueScaled; // Non-PV specific
    bool isPvMove, inCheck, singularExtensionNode, givesCheck, captureOrPromotion, dangerous;
    int moveCount = 0, playedMoveCount = 0;
    Thread& thread = Threads[pos.thread()];
    SplitPoint* sp = NULL;

    refinedValue = bestValue = value = -VALUE_INFINITE;
    oldAlpha = alpha;
    inCheck = pos.in_check();
    ss->ply = (ss-1)->ply + 1;

    // Used to send selDepth info to GUI
    if (PvNode && thread.maxPly < ss->ply)
        thread.maxPly = ss->ply;

    // Step 1. Initialize node and poll. Polling can abort search
    if (!SpNode)
    {
        ss->currentMove = ss->bestMove = threatMove = (ss+1)->excludedMove = MOVE_NONE;
        (ss+1)->skipNullMove = false; (ss+1)->reduction = DEPTH_ZERO;
        (ss+2)->killers[0] = (ss+2)->killers[1] = MOVE_NONE;
    }
    else
    {
        sp = ss->sp;
        tte = NULL;
        ttMove = excludedMove = MOVE_NONE;
        threatMove = sp->threatMove;
        goto split_point_start;
    }

    if (pos.thread() == 0 && ++NodesSincePoll > NodesBetweenPolls)
    {
        NodesSincePoll = 0;
        poll(pos);
    }

    // Step 2. Check for aborted search and immediate draw
    if ((   StopRequest
         || pos.is_draw<false>()
         || ss->ply > PLY_MAX) && !RootNode)
        return VALUE_DRAW;

    // Step 3. Mate distance pruning
    if (!RootNode)
    {
        alpha = Max(value_mated_in(ss->ply), alpha);
        beta = Min(value_mate_in(ss->ply+1), beta);
        if (alpha >= beta)
            return alpha;
    }

    // Step 4. Transposition table lookup
    // We don't want the score of a partial search to overwrite a previous full search
    // TT value, so we use a different position key in case of an excluded move.
    excludedMove = ss->excludedMove;
    posKey = excludedMove ? pos.get_exclusion_key() : pos.get_key();
    tte = TT.probe(posKey);
    ttMove = tte ? tte->move() : MOVE_NONE;

    // At PV nodes we check for exact scores, while at non-PV nodes we check for
    // a fail high/low. Biggest advantage at probing at PV nodes is to have a
    // smooth experience in analysis mode. We don't probe at Root nodes otherwise
    // we should also update RootMoveList to avoid bogus output.
    if (!RootNode && tte && (PvNode ? tte->depth() >= depth && tte->type() == VALUE_TYPE_EXACT
                                    : ok_to_use_TT(tte, depth, beta, ss->ply)))
    {
        TT.refresh(tte);
        ss->bestMove = ttMove; // Can be MOVE_NONE
        return value_from_tt(tte->value(), ss->ply);
    }

    // Step 5. Evaluate the position statically and update parent's gain statistics
    if (inCheck)
        ss->eval = ss->evalMargin = VALUE_NONE;
    else if (tte)
    {
        assert(tte->static_value() != VALUE_NONE);

        ss->eval = tte->static_value();
        ss->evalMargin = tte->static_value_margin();
        refinedValue = refine_eval(tte, ss->eval, ss->ply);
    }
    else
    {
        refinedValue = ss->eval = evaluate(pos, ss->evalMargin);
        TT.store(posKey, VALUE_NONE, VALUE_TYPE_NONE, DEPTH_NONE, MOVE_NONE, ss->eval, ss->evalMargin);
    }

    // Save gain for the parent non-capture move
    update_gains(pos, (ss-1)->currentMove, (ss-1)->eval, ss->eval);

    // Step 6. Razoring (is omitted in PV nodes)
    if (   !PvNode
        &&  depth < RazorDepth
        && !inCheck
        &&  refinedValue + razor_margin(depth) < beta
        &&  ttMove == MOVE_NONE
        &&  abs(beta) < VALUE_MATE_IN_PLY_MAX
        && !pos.has_pawn_on_7th(pos.side_to_move()))
    {
        Value rbeta = beta - razor_margin(depth);
        Value v = qsearch<NonPV>(pos, ss, rbeta-1, rbeta, DEPTH_ZERO);
        if (v < rbeta)
            // Logically we should return (v + razor_margin(depth)), but
            // surprisingly this did slightly weaker in tests.
            return v;
    }

    // Step 7. Static null move pruning (is omitted in PV nodes)
    // We're betting that the opponent doesn't have a move that will reduce
    // the score by more than futility_margin(depth) if we do a null move.
    if (   !PvNode
        && !ss->skipNullMove
        &&  depth < RazorDepth
        && !inCheck
        &&  refinedValue - futility_margin(depth, 0) >= beta
        &&  abs(beta) < VALUE_MATE_IN_PLY_MAX
        &&  pos.non_pawn_material(pos.side_to_move()))
        return refinedValue - futility_margin(depth, 0);

    // Step 8. Null move search with verification search (is omitted in PV nodes)
    if (   !PvNode
        && !ss->skipNullMove
        &&  depth > ONE_PLY
        && !inCheck
        &&  refinedValue >= beta
        &&  abs(beta) < VALUE_MATE_IN_PLY_MAX
        &&  pos.non_pawn_material(pos.side_to_move()))
    {
        ss->currentMove = MOVE_NULL;

        // Null move dynamic reduction based on depth
        int R = 3 + (depth >= 5 * ONE_PLY ? depth / 8 : 0);

        // Null move dynamic reduction based on value
        if (refinedValue - PawnValueMidgame > beta)
            R++;

        pos.do_null_move(st);
        (ss+1)->skipNullMove = true;
        nullValue = depth-R*ONE_PLY < ONE_PLY ? -qsearch<NonPV>(pos, ss+1, -beta, -alpha, DEPTH_ZERO)
                                              : - search<NonPV>(pos, ss+1, -beta, -alpha, depth-R*ONE_PLY);
        (ss+1)->skipNullMove = false;
        pos.undo_null_move();

        if (nullValue >= beta)
        {
            // Do not return unproven mate scores
            if (nullValue >= VALUE_MATE_IN_PLY_MAX)
                nullValue = beta;

            if (depth < 6 * ONE_PLY)
                return nullValue;

            // Do verification search at high depths
            ss->skipNullMove = true;
            Value v = search<NonPV>(pos, ss, alpha, beta, depth-R*ONE_PLY);
            ss->skipNullMove = false;

            if (v >= beta)
                return nullValue;
        }
        else
        {
            // The null move failed low, which means that we may be faced with
            // some kind of threat. If the previous move was reduced, check if
            // the move that refuted the null move was somehow connected to the
            // move which was reduced. If a connection is found, return a fail
            // low score (which will cause the reduced move to fail high in the
            // parent node, which will trigger a re-search with full depth).
            threatMove = (ss+1)->bestMove;

            if (   depth < ThreatDepth
                && (ss-1)->reduction
                && threatMove != MOVE_NONE
                && connected_moves(pos, (ss-1)->currentMove, threatMove))
                return beta - 1;
        }
    }

    // Step 9. ProbCut (is omitted in PV nodes)
    // If we have a very good capture (i.e. SEE > seeValues[captured_piece_type])
    // and a reduced search returns a value much above beta, we can (almost) safely
    // prune the previous move.
    if (   !PvNode
        &&  depth >= RazorDepth + ONE_PLY
        && !inCheck
        && !ss->skipNullMove
        &&  excludedMove == MOVE_NONE
        &&  abs(beta) < VALUE_MATE_IN_PLY_MAX)
    {
        Value rbeta = beta + 200;
        Depth rdepth = depth - ONE_PLY - 3 * ONE_PLY;

        assert(rdepth >= ONE_PLY);

        MovePicker mp(pos, ttMove, H, pos.captured_piece_type());
        CheckInfo ci(pos);

        while ((move = mp.get_next_move()) != MOVE_NONE)
            if (pos.pl_move_is_legal(move, ci.pinned))
            {
                pos.do_move(move, st, ci, pos.move_gives_check(move, ci));
                value = -search<NonPV>(pos, ss+1, -rbeta, -rbeta+1, rdepth);
                pos.undo_move(move);
                if (value >= rbeta)
                    return value;
            }
    }

    // Step 10. Internal iterative deepening
    if (   depth >= IIDDepth[PvNode]
        && ttMove == MOVE_NONE
        && (PvNode || (!inCheck && ss->eval + IIDMargin >= beta)))
    {
        Depth d = (PvNode ? depth - 2 * ONE_PLY : depth / 2);

        ss->skipNullMove = true;
        search<PvNode ? PV : NonPV>(pos, ss, alpha, beta, d);
        ss->skipNullMove = false;

        tte = TT.probe(posKey);
        ttMove = tte ? tte->move() : MOVE_NONE;
    }

split_point_start: // At split points actual search starts from here

    // Initialize a MovePicker object for the current position
    MovePickerExt<NT> mp(pos, ttMove, depth, H, ss, PvNode ? -VALUE_INFINITE : beta);
    CheckInfo ci(pos);
    ss->bestMove = MOVE_NONE;
    futilityBase = ss->eval + ss->evalMargin;
    singularExtensionNode =   !RootNode
                           && !SpNode
                           && depth >= SingularExtensionDepth[PvNode]
                           && ttMove != MOVE_NONE
                           && !excludedMove // Do not allow recursive singular extension search
                           && (tte->type() & VALUE_TYPE_LOWER)
                           && tte->depth() >= depth - 3 * ONE_PLY;
    if (SpNode)
    {
        lock_grab(&(sp->lock));
        bestValue = sp->bestValue;
    }

    // Step 11. Loop through moves
    // Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs
    while (   bestValue < beta
           && (move = mp.get_next_move()) != MOVE_NONE
           && !thread.cutoff_occurred())
    {
      assert(move_is_ok(move));

      if (move == excludedMove)
          continue;

      // At PV and SpNode nodes we want all moves to be legal since the beginning
      if ((PvNode || SpNode) && !pos.pl_move_is_legal(move, ci.pinned))
          continue;

      if (SpNode)
      {
          moveCount = ++sp->moveCount;
          lock_release(&(sp->lock));
      }
      else
          moveCount++;

      if (RootNode)
      {
          // This is used by time management
          FirstRootMove = (moveCount == 1);

          // Save the current node count before the move is searched
          nodes = pos.nodes_searched();

          // If it's time to send nodes info, do it here where we have the
          // correct accumulated node counts searched by each thread.
          if (SendSearchedNodes)
          {
              SendSearchedNodes = false;
              cout << "info" << speed_to_uci(pos.nodes_searched()) << endl;
          }

          // For long searches send current move info to GUI
          if (current_search_time() > 2000)
              cout << "info" << depth_to_uci(depth)
                   << " currmove " << move << " currmovenumber " << moveCount << endl;
      }

      // At Root and at first iteration do a PV search on all the moves to score root moves
      isPvMove = (PvNode && moveCount <= (!RootNode ? 1 : depth <= ONE_PLY ? MAX_MOVES : MultiPV));
      givesCheck = pos.move_gives_check(move, ci);
      captureOrPromotion = pos.move_is_capture_or_promotion(move);

      // Step 12. Decide the new search depth
      ext = extension<PvNode>(pos, move, captureOrPromotion, givesCheck, &dangerous);

      // Singular extension search. If all moves but one fail low on a search of
      // (alpha-s, beta-s), and just one fails high on (alpha, beta), then that move
      // is singular and should be extended. To verify this we do a reduced search
      // on all the other moves but the ttMove, if result is lower than ttValue minus
      // a margin then we extend ttMove.
      if (   singularExtensionNode
          && move == ttMove
          && pos.pl_move_is_legal(move, ci.pinned)
          && ext < ONE_PLY)
      {
          Value ttValue = value_from_tt(tte->value(), ss->ply);

          if (abs(ttValue) < VALUE_KNOWN_WIN)
          {
              Value rBeta = ttValue - int(depth);
              ss->excludedMove = move;
              ss->skipNullMove = true;
              Value v = search<NonPV>(pos, ss, rBeta - 1, rBeta, depth / 2);
              ss->skipNullMove = false;
              ss->excludedMove = MOVE_NONE;
              ss->bestMove = MOVE_NONE;
              if (v < rBeta)
                  ext = ONE_PLY;
          }
      }

      // Update current move (this must be done after singular extension search)
      newDepth = depth - ONE_PLY + ext;

      // Step 13. Futility pruning (is omitted in PV nodes)
      if (   !PvNode
          && !captureOrPromotion
          && !inCheck
          && !dangerous
          &&  move != ttMove
          && !move_is_castle(move))
      {
          // Move count based pruning
          if (   moveCount >= futility_move_count(depth)
              && (!threatMove || !connected_threat(pos, move, threatMove))
              && bestValue > VALUE_MATED_IN_PLY_MAX) // FIXME bestValue is racy
          {
              if (SpNode)
                  lock_grab(&(sp->lock));

              continue;
          }

          // Value based pruning
          // We illogically ignore reduction condition depth >= 3*ONE_PLY for predicted depth,
          // but fixing this made program slightly weaker.
          Depth predictedDepth = newDepth - reduction<PvNode>(depth, moveCount);
          futilityValueScaled =  futilityBase + futility_margin(predictedDepth, moveCount)
                               + H.gain(pos.piece_on(move_from(move)), move_to(move));

          if (futilityValueScaled < beta)
          {
              if (SpNode)
              {
                  lock_grab(&(sp->lock));
                  if (futilityValueScaled > sp->bestValue)
                      sp->bestValue = bestValue = futilityValueScaled;
              }
              else if (futilityValueScaled > bestValue)
                  bestValue = futilityValueScaled;

              continue;
          }

          // Prune moves with negative SEE at low depths
          if (   predictedDepth < 2 * ONE_PLY
              && bestValue > VALUE_MATED_IN_PLY_MAX
              && pos.see_sign(move) < 0)
          {
              if (SpNode)
                  lock_grab(&(sp->lock));

              continue;
          }
      }

      // Check for legality only before to do the move
      if (!pos.pl_move_is_legal(move, ci.pinned))
      {
          moveCount--;
          continue;
      }

      ss->currentMove = move;
      if (!SpNode && !captureOrPromotion)
          movesSearched[playedMoveCount++] = move;

      // Step 14. Make the move
      pos.do_move(move, st, ci, givesCheck);

      // Step extra. pv search (only in PV nodes)
      // The first move in list is the expected PV
      if (isPvMove)
          value = newDepth < ONE_PLY ? -qsearch<PV>(pos, ss+1, -beta, -alpha, DEPTH_ZERO)
                                     : - search<PV>(pos, ss+1, -beta, -alpha, newDepth);
      else
      {
          // Step 15. Reduced depth search
          // If the move fails high will be re-searched at full depth.
          bool doFullDepthSearch = true;

          if (    depth > 3 * ONE_PLY
              && !captureOrPromotion
              && !dangerous
              && !move_is_castle(move)
              &&  ss->killers[0] != move
              &&  ss->killers[1] != move
              && (ss->reduction = reduction<PvNode>(depth, moveCount)) != DEPTH_ZERO)
          {
              Depth d = newDepth - ss->reduction;
              alpha = SpNode ? sp->alpha : alpha;

              value = d < ONE_PLY ? -qsearch<NonPV>(pos, ss+1, -(alpha+1), -alpha, DEPTH_ZERO)
                                  : - search<NonPV>(pos, ss+1, -(alpha+1), -alpha, d);

              ss->reduction = DEPTH_ZERO;
              doFullDepthSearch = (value > alpha);
          }

          // Step 16. Full depth search
          if (doFullDepthSearch)
          {
              alpha = SpNode ? sp->alpha : alpha;
              value = newDepth < ONE_PLY ? -qsearch<NonPV>(pos, ss+1, -(alpha+1), -alpha, DEPTH_ZERO)
                                         : - search<NonPV>(pos, ss+1, -(alpha+1), -alpha, newDepth);

              // Step extra. pv search (only in PV nodes)
              // Search only for possible new PV nodes, if instead value >= beta then
              // parent node fails low with value <= alpha and tries another move.
              if (PvNode && value > alpha && (RootNode || value < beta))
                  value = newDepth < ONE_PLY ? -qsearch<PV>(pos, ss+1, -beta, -alpha, DEPTH_ZERO)
                                             : - search<PV>(pos, ss+1, -beta, -alpha, newDepth);
          }
      }

      // Step 17. Undo move
      pos.undo_move(move);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // Step 18. Check for new best move
      if (SpNode)
      {
          lock_grab(&(sp->lock));
          bestValue = sp->bestValue;
          alpha = sp->alpha;
      }

      if (value > bestValue)
      {
          bestValue = value;
          ss->bestMove = move;

          if (  !RootNode
              && PvNode
              && value > alpha
              && value < beta) // We want always alpha < beta
              alpha = value;

          if (SpNode && !thread.cutoff_occurred())
          {
              sp->bestValue = value;
              sp->ss->bestMove = move;
              sp->alpha = alpha;
              sp->is_betaCutoff = (value >= beta);
          }
      }

      if (RootNode)
      {
          // Finished searching the move. If StopRequest is true, the search
          // was aborted because the user interrupted the search or because we
          // ran out of time. In this case, the return value of the search cannot
          // be trusted, and we break out of the loop without updating the best
          // move and/or PV.
          if (StopRequest)
              break;

          // Remember searched nodes counts for this move
          mp.current().nodes += pos.nodes_searched() - nodes;

          // PV move or new best move ?
          if (isPvMove || value > alpha)
          {
              // Update PV
              mp.current().pv_score = value;
              mp.current().extract_pv_from_tt(pos);

              // We record how often the best move has been changed in each
              // iteration. This information is used for time management: When
              // the best move changes frequently, we allocate some more time.
              if (!isPvMove && MultiPV == 1)
                  Rml.bestMoveChanges++;

              // It is critical that sorting is done with a stable algorithm
              // because all the values but the first are usually set to
              // -VALUE_INFINITE and we want to keep the same order for all
              // the moves but the new PV that goes to head.
              sort<RootMove>(Rml.begin(), Rml.begin() + moveCount);

              // Update alpha. In multi-pv we don't use aspiration window, so set
              // alpha equal to minimum score among the PV lines searched so far.
              if (MultiPV > 1)
                  alpha = Rml[Min(moveCount, MultiPV) - 1].pv_score;
              else if (value > alpha)
                  alpha = value;
          }
          else
              // All other moves but the PV are set to the lowest value, this
              // is not a problem when sorting becuase sort is stable and move
              // position in the list is preserved, just the PV is pushed up.
              mp.current().pv_score = -VALUE_INFINITE;

      } // RootNode

      // Step 19. Check for split
      if (   !RootNode
          && !SpNode
          && depth >= Threads.min_split_depth()
          && bestValue < beta
          && Threads.available_slave_exists(pos.thread())
          && !StopRequest
          && !thread.cutoff_occurred())
          Threads.split<FakeSplit>(pos, ss, &alpha, beta, &bestValue, depth,
                                   threatMove, moveCount, &mp, PvNode);
    }

    // Step 20. Check for mate and stalemate
    // All legal moves have been searched and if there are
    // no legal moves, it must be mate or stalemate.
    // If one move was excluded return fail low score.
    if (!SpNode && !moveCount)
        return excludedMove ? oldAlpha : inCheck ? value_mated_in(ss->ply) : VALUE_DRAW;

    // Step 21. Update tables
    // If the search is not aborted, update the transposition table,
    // history counters, and killer moves.
    if (!SpNode && !StopRequest && !thread.cutoff_occurred())
    {
        move = bestValue <= oldAlpha ? MOVE_NONE : ss->bestMove;
        vt   = bestValue <= oldAlpha ? VALUE_TYPE_UPPER
             : bestValue >= beta ? VALUE_TYPE_LOWER : VALUE_TYPE_EXACT;

        TT.store(posKey, value_to_tt(bestValue, ss->ply), vt, depth, move, ss->eval, ss->evalMargin);

        // Update killers and history only for non capture moves that fails high
        if (    bestValue >= beta
            && !pos.move_is_capture_or_promotion(move))
        {
            if (move != ss->killers[0])
            {
                ss->killers[1] = ss->killers[0];
                ss->killers[0] = move;
            }
            update_history(pos, move, depth, movesSearched, playedMoveCount);
        }
    }

    if (SpNode)
    {
        // Here we have the lock still grabbed
        sp->is_slave[pos.thread()] = false;
        sp->nodes += pos.nodes_searched();
        lock_release(&(sp->lock));
    }

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
  }

  // qsearch() is the quiescence search function, which is called by the main
  // search function when the remaining depth is zero (or, to be more precise,
  // less than ONE_PLY).

  template <NodeType NT>
  Value qsearch(Position& pos, SearchStack* ss, Value alpha, Value beta, Depth depth) {

    const bool PvNode = (NT == PV);

    assert(NT == PV || NT == NonPV);
    assert(alpha >= -VALUE_INFINITE && alpha <= VALUE_INFINITE);
    assert(beta >= -VALUE_INFINITE && beta <= VALUE_INFINITE);
    assert(PvNode || alpha == beta - 1);
    assert(depth <= 0);
    assert(pos.thread() >= 0 && pos.thread() < Threads.size());

    StateInfo st;
    Move ttMove, move;
    Value bestValue, value, evalMargin, futilityValue, futilityBase;
    bool inCheck, enoughMaterial, givesCheck, evasionPrunable;
    const TTEntry* tte;
    Depth ttDepth;
    Value oldAlpha = alpha;

    ss->bestMove = ss->currentMove = MOVE_NONE;
    ss->ply = (ss-1)->ply + 1;

    // Check for an instant draw or maximum ply reached
    if (pos.is_draw<true>() || ss->ply > PLY_MAX)
        return VALUE_DRAW;

    // Decide whether or not to include checks, this fixes also the type of
    // TT entry depth that we are going to use. Note that in qsearch we use
    // only two types of depth in TT: DEPTH_QS_CHECKS or DEPTH_QS_NO_CHECKS.
    inCheck = pos.in_check();
    ttDepth = (inCheck || depth >= DEPTH_QS_CHECKS ? DEPTH_QS_CHECKS : DEPTH_QS_NO_CHECKS);

    // Transposition table lookup. At PV nodes, we don't use the TT for
    // pruning, but only for move ordering.
    tte = TT.probe(pos.get_key());
    ttMove = (tte ? tte->move() : MOVE_NONE);

    if (!PvNode && tte && ok_to_use_TT(tte, ttDepth, beta, ss->ply))
    {
        ss->bestMove = ttMove; // Can be MOVE_NONE
        return value_from_tt(tte->value(), ss->ply);
    }

    // Evaluate the position statically
    if (inCheck)
    {
        bestValue = futilityBase = -VALUE_INFINITE;
        ss->eval = evalMargin = VALUE_NONE;
        enoughMaterial = false;
    }
    else
    {
        if (tte)
        {
            assert(tte->static_value() != VALUE_NONE);

            evalMargin = tte->static_value_margin();
            ss->eval = bestValue = tte->static_value();
        }
        else
            ss->eval = bestValue = evaluate(pos, evalMargin);

        // Stand pat. Return immediately if static value is at least beta
        if (bestValue >= beta)
        {
            if (!tte)
                TT.store(pos.get_key(), value_to_tt(bestValue, ss->ply), VALUE_TYPE_LOWER, DEPTH_NONE, MOVE_NONE, ss->eval, evalMargin);

            return bestValue;
        }

        if (PvNode && bestValue > alpha)
            alpha = bestValue;

        // Futility pruning parameters, not needed when in check
        futilityBase = ss->eval + evalMargin + FutilityMarginQS;
        enoughMaterial = pos.non_pawn_material(pos.side_to_move()) > RookValueMidgame;
    }

    // Initialize a MovePicker object for the current position, and prepare
    // to search the moves. Because the depth is <= 0 here, only captures,
    // queen promotions and checks (only if depth >= DEPTH_QS_CHECKS) will
    // be generated.
    MovePicker mp(pos, ttMove, depth, H, move_to((ss-1)->currentMove));
    CheckInfo ci(pos);

    // Loop through the moves until no moves remain or a beta cutoff occurs
    while (   alpha < beta
           && (move = mp.get_next_move()) != MOVE_NONE)
    {
      assert(move_is_ok(move));

      givesCheck = pos.move_gives_check(move, ci);

      // Futility pruning
      if (   !PvNode
          && !inCheck
          && !givesCheck
          &&  move != ttMove
          &&  enoughMaterial
          && !move_is_promotion(move)
          && !pos.move_is_passed_pawn_push(move))
      {
          futilityValue =  futilityBase
                         + piece_value_endgame(pos.piece_on(move_to(move)))
                         + (move_is_ep(move) ? PawnValueEndgame : VALUE_ZERO);

          if (futilityValue < alpha)
          {
              if (futilityValue > bestValue)
                  bestValue = futilityValue;
              continue;
          }

          // Prune moves with negative or equal SEE
          if (   futilityBase < beta
              && depth < DEPTH_ZERO
              && pos.see(move) <= 0)
              continue;
      }

      // Detect non-capture evasions that are candidate to be pruned
      evasionPrunable =   !PvNode
                       && inCheck
                       && bestValue > VALUE_MATED_IN_PLY_MAX
                       && !pos.move_is_capture(move)
                       && !pos.can_castle(pos.side_to_move());

      // Don't search moves with negative SEE values
      if (   !PvNode
          && (!inCheck || evasionPrunable)
          &&  move != ttMove
          && !move_is_promotion(move)
          &&  pos.see_sign(move) < 0)
          continue;

      // Don't search useless checks
      if (   !PvNode
          && !inCheck
          &&  givesCheck
          &&  move != ttMove
          && !pos.move_is_capture_or_promotion(move)
          &&  ss->eval + PawnValueMidgame / 4 < beta
          && !check_is_dangerous(pos, move, futilityBase, beta, &bestValue))
      {
          if (ss->eval + PawnValueMidgame / 4 > bestValue)
              bestValue = ss->eval + PawnValueMidgame / 4;

          continue;
      }

      // Check for legality only before to do the move
      if (!pos.pl_move_is_legal(move, ci.pinned))
          continue;

      // Update current move
      ss->currentMove = move;

      // Make and search the move
      pos.do_move(move, st, ci, givesCheck);
      value = -qsearch<NT>(pos, ss+1, -beta, -alpha, depth-ONE_PLY);
      pos.undo_move(move);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // New best move?
      if (value > bestValue)
      {
          bestValue = value;
          if (value > alpha)
          {
              alpha = value;
              ss->bestMove = move;
          }
       }
    }

    // All legal moves have been searched. A special case: If we're in check
    // and no legal moves were found, it is checkmate.
    if (inCheck && bestValue == -VALUE_INFINITE)
        return value_mated_in(ss->ply);

    // Update transposition table
    ValueType vt = (bestValue <= oldAlpha ? VALUE_TYPE_UPPER : bestValue >= beta ? VALUE_TYPE_LOWER : VALUE_TYPE_EXACT);
    TT.store(pos.get_key(), value_to_tt(bestValue, ss->ply), vt, ttDepth, ss->bestMove, ss->eval, evalMargin);

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
  }


  // check_is_dangerous() tests if a checking move can be pruned in qsearch().
  // bestValue is updated only when returning false because in that case move
  // will be pruned.

  bool check_is_dangerous(Position &pos, Move move, Value futilityBase, Value beta, Value *bestValue)
  {
    Bitboard b, occ, oldAtt, newAtt, kingAtt;
    Square from, to, ksq, victimSq;
    Piece pc;
    Color them;
    Value futilityValue, bv = *bestValue;

    from = move_from(move);
    to = move_to(move);
    them = opposite_color(pos.side_to_move());
    ksq = pos.king_square(them);
    kingAtt = pos.attacks_from<KING>(ksq);
    pc = pos.piece_on(from);

    occ = pos.occupied_squares() & ~(1ULL << from) & ~(1ULL << ksq);
    oldAtt = pos.attacks_from(pc, from, occ);
    newAtt = pos.attacks_from(pc,   to, occ);

    // Rule 1. Checks which give opponent's king at most one escape square are dangerous
    b = kingAtt & ~pos.pieces(them) & ~newAtt & ~(1ULL << to);

    if (!(b && (b & (b - 1))))
        return true;

    // Rule 2. Queen contact check is very dangerous
    if (   piece_type(pc) == QUEEN
        && bit_is_set(kingAtt, to))
        return true;

    // Rule 3. Creating new double threats with checks
    b = pos.pieces(them) & newAtt & ~oldAtt & ~(1ULL << ksq);

    while (b)
    {
        victimSq = pop_1st_bit(&b);
        futilityValue = futilityBase + piece_value_endgame(pos.piece_on(victimSq));

        // Note that here we generate illegal "double move"!
        if (   futilityValue >= beta
            && pos.see_sign(make_move(from, victimSq)) >= 0)
            return true;

        if (futilityValue > bv)
            bv = futilityValue;
    }

    // Update bestValue only if check is not dangerous (because we will prune the move)
    *bestValue = bv;
    return false;
  }


  // connected_moves() tests whether two moves are 'connected' in the sense
  // that the first move somehow made the second move possible (for instance
  // if the moving piece is the same in both moves). The first move is assumed
  // to be the move that was made to reach the current position, while the
  // second move is assumed to be a move from the current position.

  bool connected_moves(const Position& pos, Move m1, Move m2) {

    Square f1, t1, f2, t2;
    Piece p1, p2;
    Square ksq;

    assert(m1 && move_is_ok(m1));
    assert(m2 && move_is_ok(m2));

    // Case 1: The moving piece is the same in both moves
    f2 = move_from(m2);
    t1 = move_to(m1);
    if (f2 == t1)
        return true;

    // Case 2: The destination square for m2 was vacated by m1
    t2 = move_to(m2);
    f1 = move_from(m1);
    if (t2 == f1)
        return true;

    // Case 3: Moving through the vacated square
    p2 = pos.piece_on(f2);
    if (   piece_is_slider(p2)
        && bit_is_set(squares_between(f2, t2), f1))
      return true;

    // Case 4: The destination square for m2 is defended by the moving piece in m1
    p1 = pos.piece_on(t1);
    if (bit_is_set(pos.attacks_from(p1, t1), t2))
        return true;

    // Case 5: Discovered check, checking piece is the piece moved in m1
    ksq = pos.king_square(pos.side_to_move());
    if (    piece_is_slider(p1)
        &&  bit_is_set(squares_between(t1, ksq), f2))
    {
        Bitboard occ = pos.occupied_squares();
        clear_bit(&occ, f2);
        if (bit_is_set(pos.attacks_from(p1, t1, occ), ksq))
            return true;
    }
    return false;
  }


  // value_to_tt() adjusts a mate score from "plies to mate from the root" to
  // "plies to mate from the current ply".  Non-mate scores are unchanged.
  // The function is called before storing a value to the transposition table.

  Value value_to_tt(Value v, int ply) {

    if (v >= VALUE_MATE_IN_PLY_MAX)
      return v + ply;

    if (v <= VALUE_MATED_IN_PLY_MAX)
      return v - ply;

    return v;
  }


  // value_from_tt() is the inverse of value_to_tt(): It adjusts a mate score from
  // the transposition table to a mate score corrected for the current ply.

  Value value_from_tt(Value v, int ply) {

    if (v >= VALUE_MATE_IN_PLY_MAX)
      return v - ply;

    if (v <= VALUE_MATED_IN_PLY_MAX)
      return v + ply;

    return v;
  }


  // connected_threat() tests whether it is safe to forward prune a move or if
  // is somehow connected to the threat move returned by null search.

  bool connected_threat(const Position& pos, Move m, Move threat) {

    assert(move_is_ok(m));
    assert(threat && move_is_ok(threat));
    assert(!pos.move_is_capture_or_promotion(m));
    assert(!pos.move_is_passed_pawn_push(m));

    Square mfrom, mto, tfrom, tto;

    mfrom = move_from(m);
    mto = move_to(m);
    tfrom = move_from(threat);
    tto = move_to(threat);

    // Case 1: Don't prune moves which move the threatened piece
    if (mfrom == tto)
        return true;

    // Case 2: If the threatened piece has value less than or equal to the
    // value of the threatening piece, don't prune moves which defend it.
    if (   pos.move_is_capture(threat)
        && (   piece_value_midgame(pos.piece_on(tfrom)) >= piece_value_midgame(pos.piece_on(tto))
            || piece_type(pos.piece_on(tfrom)) == KING)
        && pos.move_attacks_square(m, tto))
        return true;

    // Case 3: If the moving piece in the threatened move is a slider, don't
    // prune safe moves which block its ray.
    if (   piece_is_slider(pos.piece_on(tfrom))
        && bit_is_set(squares_between(tfrom, tto), mto)
        && pos.see_sign(m) >= 0)
        return true;

    return false;
  }


  // ok_to_use_TT() returns true if a transposition table score
  // can be used at a given point in search.

  bool ok_to_use_TT(const TTEntry* tte, Depth depth, Value beta, int ply) {

    Value v = value_from_tt(tte->value(), ply);

    return   (   tte->depth() >= depth
              || v >= Max(VALUE_MATE_IN_PLY_MAX, beta)
              || v < Min(VALUE_MATED_IN_PLY_MAX, beta))

          && (   ((tte->type() & VALUE_TYPE_LOWER) && v >= beta)
              || ((tte->type() & VALUE_TYPE_UPPER) && v < beta));
  }


  // refine_eval() returns the transposition table score if
  // possible otherwise falls back on static position evaluation.

  Value refine_eval(const TTEntry* tte, Value defaultEval, int ply) {

      assert(tte);

      Value v = value_from_tt(tte->value(), ply);

      if (   ((tte->type() & VALUE_TYPE_LOWER) && v >= defaultEval)
          || ((tte->type() & VALUE_TYPE_UPPER) && v < defaultEval))
          return v;

      return defaultEval;
  }


  // update_history() registers a good move that produced a beta-cutoff
  // in history and marks as failures all the other moves of that ply.

  void update_history(const Position& pos, Move move, Depth depth,
                      Move movesSearched[], int moveCount) {
    Move m;
    Value bonus = Value(int(depth) * int(depth));

    H.update(pos.piece_on(move_from(move)), move_to(move), bonus);

    for (int i = 0; i < moveCount - 1; i++)
    {
        m = movesSearched[i];

        assert(m != move);

        H.update(pos.piece_on(move_from(m)), move_to(m), -bonus);
    }
  }


  // update_gains() updates the gains table of a non-capture move given
  // the static position evaluation before and after the move.

  void update_gains(const Position& pos, Move m, Value before, Value after) {

    if (   m != MOVE_NULL
        && before != VALUE_NONE
        && after != VALUE_NONE
        && pos.captured_piece_type() == PIECE_TYPE_NONE
        && !move_is_special(m))
        H.update_gain(pos.piece_on(move_to(m)), move_to(m), -(before + after));
  }


  // current_search_time() returns the number of milliseconds which have passed
  // since the beginning of the current search.

  int current_search_time(int set) {

    static int searchStartTime;

    if (set)
        searchStartTime = set;

    return get_system_time() - searchStartTime;
  }


  // score_to_uci() converts a value to a string suitable for use with the UCI
  // protocol specifications:
  //
  // cp <x>     The score from the engine's point of view in centipawns.
  // mate <y>   Mate in y moves, not plies. If the engine is getting mated
  //            use negative values for y.

  string score_to_uci(Value v, Value alpha, Value beta) {

    std::stringstream s;

    if (abs(v) < VALUE_MATE - PLY_MAX * ONE_PLY)
        s << " score cp " << int(v) * 100 / int(PawnValueMidgame); // Scale to centipawns
    else
        s << " score mate " << (v > 0 ? VALUE_MATE - v + 1 : -VALUE_MATE - v) / 2;

    s << (v >= beta ? " lowerbound" : v <= alpha ? " upperbound" : "");

    return s.str();
  }


  // speed_to_uci() returns a string with time stats of current search suitable
  // to be sent to UCI gui.

  string speed_to_uci(int64_t nodes) {

    std::stringstream s;
    int t = current_search_time();

    s << " nodes " << nodes
      << " nps " << (t > 0 ? int(nodes * 1000 / t) : 0)
      << " time "  << t;

    return s.str();
  }

  // pv_to_uci() returns a string with information on the current PV line
  // formatted according to UCI specification.

  string pv_to_uci(Move pv[], int pvNum, bool chess960) {

    std::stringstream s;

    s << " multipv " << pvNum << " pv " << set960(chess960);

    for ( ; *pv != MOVE_NONE; pv++)
        s << *pv << " ";

    return s.str();
  }

  // depth_to_uci() returns a string with information on the current depth and
  // seldepth formatted according to UCI specification.

  string depth_to_uci(Depth depth) {

    std::stringstream s;

    // Retrieve max searched depth among threads
    int selDepth = 0;
    for (int i = 0; i < Threads.size(); i++)
        if (Threads[i].maxPly > selDepth)
            selDepth = Threads[i].maxPly;

     s << " depth " << depth / ONE_PLY << " seldepth " << selDepth;

    return s.str();
  }

  string time_to_string(int millisecs) {

    const int MSecMinute = 1000 * 60;
    const int MSecHour   = 1000 * 60 * 60;

    int hours = millisecs / MSecHour;
    int minutes =  (millisecs % MSecHour) / MSecMinute;
    int seconds = ((millisecs % MSecHour) % MSecMinute) / 1000;

    std::stringstream s;

    if (hours)
        s << hours << ':';

    s << std::setfill('0') << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
    return s.str();
  }

  string score_to_string(Value v) {

    std::stringstream s;

    if (v >= VALUE_MATE_IN_PLY_MAX)
        s << "#" << (VALUE_MATE - v + 1) / 2;
    else if (v <= VALUE_MATED_IN_PLY_MAX)
        s << "-#" << (VALUE_MATE + v) / 2;
    else
        s << std::setprecision(2) << std::fixed << std::showpos << float(v) / PawnValueMidgame;

    return s.str();
  }

  // pretty_pv() creates a human-readable string from a position and a PV.
  // It is used to write search information to the log file (which is created
  // when the UCI parameter "Use Search Log" is "true").

  string pretty_pv(Position& pos, int depth, Value value, int time, Move pv[]) {

    const int64_t K = 1000;
    const int64_t M = 1000000;
    const int startColumn = 28;
    const size_t maxLength = 80 - startColumn;

    StateInfo state[PLY_MAX_PLUS_2], *st = state;
    Move* m = pv;
    string san;
    std::stringstream s;
    size_t length = 0;

    // First print depth, score, time and searched nodes...
    s << set960(pos.is_chess960())
      << std::setw(2) << depth
      << std::setw(8) << score_to_string(value)
      << std::setw(8) << time_to_string(time);

    if (pos.nodes_searched() < M)
        s << std::setw(8) << pos.nodes_searched() / 1 << "  ";
    else if (pos.nodes_searched() < K * M)
        s << std::setw(7) << pos.nodes_searched() / K << "K  ";
    else
        s << std::setw(7) << pos.nodes_searched() / M << "M  ";

    // ...then print the full PV line in short algebraic notation
    while (*m != MOVE_NONE)
    {
        san = move_to_san(pos, *m);
        length += san.length() + 1;

        if (length > maxLength)
        {
            length = san.length() + 1;
            s << "\n" + string(startColumn, ' ');
        }
        s << san << ' ';

        pos.do_move(*m++, *st++);
    }

    // Restore original position before to leave
    while (m != pv) pos.undo_move(*--m);

    return s.str();
  }

  // poll() performs two different functions: It polls for user input, and it
  // looks at the time consumed so far and decides if it's time to abort the
  // search.

  void poll(const Position& pos) {

    static int lastInfoTime;
    int t = current_search_time();

    //  Poll for input
    if (input_available())
    {
        // We are line oriented, don't read single chars
        string command;

        if (!std::getline(std::cin, command) || command == "quit")
        {
            // Quit the program as soon as possible
            Limits.ponder = false;
            QuitRequest = StopRequest = true;
            return;
        }
        else if (command == "stop")
        {
            // Stop calculating as soon as possible, but still send the "bestmove"
            // and possibly the "ponder" token when finishing the search.
            Limits.ponder = false;
            StopRequest = true;
        }
        else if (command == "ponderhit")
        {
            // The opponent has played the expected move. GUI sends "ponderhit" if
            // we were told to ponder on the same move the opponent has played. We
            // should continue searching but switching from pondering to normal search.
            Limits.ponder = false;

            if (StopOnPonderhit)
                StopRequest = true;
        }
    }

    // Print search information
    if (t < 1000)
        lastInfoTime = 0;

    else if (lastInfoTime > t)
        // HACK: Must be a new search where we searched less than
        // NodesBetweenPolls nodes during the first second of search.
        lastInfoTime = 0;

    else if (t - lastInfoTime >= 1000)
    {
        lastInfoTime = t;

        dbg_print_mean();
        dbg_print_hit_rate();

        // Send info on searched nodes as soon as we return to root
        SendSearchedNodes = true;
    }

    // Should we stop the search?
    if (Limits.ponder)
        return;

    bool stillAtFirstMove =    FirstRootMove
                           && !AspirationFailLow
                           &&  t > TimeMgr.available_time();

    bool noMoreTime =   t > TimeMgr.maximum_time()
                     || stillAtFirstMove;

    if (   (Limits.useTimeManagement() && noMoreTime)
        || (Limits.maxTime && t >= Limits.maxTime)
        || (Limits.maxNodes && pos.nodes_searched() >= Limits.maxNodes)) // FIXME
        StopRequest = true;
  }


  // wait_for_stop_or_ponderhit() is called when the maximum depth is reached
  // while the program is pondering. The point is to work around a wrinkle in
  // the UCI protocol: When pondering, the engine is not allowed to give a
  // "bestmove" before the GUI sends it a "stop" or "ponderhit" command.
  // We simply wait here until one of these commands is sent, and return,
  // after which the bestmove and pondermove will be printed.

  void wait_for_stop_or_ponderhit() {

    string command;

    // Wait for a command from stdin
    while (   std::getline(std::cin, command)
           && command != "ponderhit" && command != "stop" && command != "quit") {};

    if (command != "ponderhit" && command != "stop")
        QuitRequest = true; // Must be "quit" or getline() returned false
  }


  // When playing with strength handicap choose best move among the MultiPV set
  // using a statistical rule dependent on SkillLevel. Idea by Heinz van Saanen.
  void do_skill_level(Move* best, Move* ponder) {

    assert(MultiPV > 1);

    static RKISS rk;

    // Rml list is already sorted by pv_score in descending order
    int s;
    int max_s = -VALUE_INFINITE;
    int size = Min(MultiPV, (int)Rml.size());
    int max = Rml[0].pv_score;
    int var = Min(max - Rml[size - 1].pv_score, PawnValueMidgame);
    int wk = 120 - 2 * SkillLevel;

    // PRNG sequence should be non deterministic
    for (int i = abs(get_system_time() % 50); i > 0; i--)
        rk.rand<unsigned>();

    // Choose best move. For each move's score we add two terms both dependent
    // on wk, one deterministic and bigger for weaker moves, and one random,
    // then we choose the move with the resulting highest score.
    for (int i = 0; i < size; i++)
    {
        s = Rml[i].pv_score;

        // Don't allow crazy blunders even at very low skills
        if (i > 0 && Rml[i-1].pv_score > s + EasyMoveMargin)
            break;

        // This is our magical formula
        s += ((max - s) * wk + var * (rk.rand<unsigned>() % wk)) / 128;

        if (s > max_s)
        {
            max_s = s;
            *best = Rml[i].pv[0];
            *ponder = Rml[i].pv[1];
        }
    }
  }


  /// RootMove and RootMoveList method's definitions

  RootMove::RootMove() {

    nodes = 0;
    pv_score = non_pv_score = -VALUE_INFINITE;
    pv[0] = MOVE_NONE;
  }

  RootMove& RootMove::operator=(const RootMove& rm) {

    const Move* src = rm.pv;
    Move* dst = pv;

    // Avoid a costly full rm.pv[] copy
    do *dst++ = *src; while (*src++ != MOVE_NONE);

    nodes = rm.nodes;
    pv_score = rm.pv_score;
    non_pv_score = rm.non_pv_score;
    return *this;
  }

  void RootMoveList::init(Position& pos, Move searchMoves[]) {

    Move* sm;
    bestMoveChanges = 0;
    clear();

    // Generate all legal moves and add them to RootMoveList
    for (MoveList<MV_LEGAL> ml(pos); !ml.end(); ++ml)
    {
        // If we have a searchMoves[] list then verify the move
        // is in the list before to add it.
        for (sm = searchMoves; *sm && *sm != ml.move(); sm++) {}

        if (sm != searchMoves && *sm != ml.move())
            continue;

        RootMove rm;
        rm.pv[0] = ml.move();
        rm.pv[1] = MOVE_NONE;
        rm.pv_score = -VALUE_INFINITE;
        push_back(rm);
    }
  }

  // extract_pv_from_tt() builds a PV by adding moves from the transposition table.
  // We consider also failing high nodes and not only VALUE_TYPE_EXACT nodes. This
  // allow to always have a ponder move even when we fail high at root and also a
  // long PV to print that is important for position analysis.

  void RootMove::extract_pv_from_tt(Position& pos) {

    StateInfo state[PLY_MAX_PLUS_2], *st = state;
    TTEntry* tte;
    int ply = 1;

    assert(pv[0] != MOVE_NONE && pos.move_is_pl(pv[0]));

    pos.do_move(pv[0], *st++);

    while (   (tte = TT.probe(pos.get_key())) != NULL
           && tte->move() != MOVE_NONE
           && pos.move_is_pl(tte->move())
           && pos.pl_move_is_legal(tte->move(), pos.pinned_pieces())
           && ply < PLY_MAX
           && (!pos.is_draw<false>() || ply < 2))
    {
        pv[ply] = tte->move();
        pos.do_move(pv[ply++], *st++);
    }
    pv[ply] = MOVE_NONE;

    do pos.undo_move(pv[--ply]); while (ply);
  }

  // insert_pv_in_tt() is called at the end of a search iteration, and inserts
  // the PV back into the TT. This makes sure the old PV moves are searched
  // first, even if the old TT entries have been overwritten.

  void RootMove::insert_pv_in_tt(Position& pos) {

    StateInfo state[PLY_MAX_PLUS_2], *st = state;
    TTEntry* tte;
    Key k;
    Value v, m = VALUE_NONE;
    int ply = 0;

    assert(pv[0] != MOVE_NONE && pos.move_is_pl(pv[0]));

    do {
        k = pos.get_key();
        tte = TT.probe(k);

        // Don't overwrite existing correct entries
        if (!tte || tte->move() != pv[ply])
        {
            v = (pos.in_check() ? VALUE_NONE : evaluate(pos, m));
            TT.store(k, VALUE_NONE, VALUE_TYPE_NONE, DEPTH_NONE, pv[ply], v, m);
        }
        pos.do_move(pv[ply], *st++);

    } while (pv[++ply] != MOVE_NONE);

    do pos.undo_move(pv[--ply]); while (ply);
  }

  // Specializations for MovePickerExt in case of Root node
  MovePickerExt<Root>::MovePickerExt(const Position& p, Move ttm, Depth d,
                                     const History& h, SearchStack* ss, Value b)
                     : MovePicker(p, ttm, d, h, ss, b), cur(-1) {
    Move move;
    Value score = VALUE_ZERO;

    // Score root moves using standard ordering used in main search, the moves
    // are scored according to the order in which they are returned by MovePicker.
    // This is the second order score that is used to compare the moves when
    // the first orders pv_score of both moves are equal.
    while ((move = MovePicker::get_next_move()) != MOVE_NONE)
        for (RootMoveList::iterator rm = Rml.begin(); rm != Rml.end(); ++rm)
            if (rm->pv[0] == move)
            {
                rm->non_pv_score = score--;
                break;
            }

    sort<RootMove>(Rml.begin(), Rml.end());
  }

} // namespace


// ThreadsManager::idle_loop() is where the threads are parked when they have no work
// to do. The parameter 'sp', if non-NULL, is a pointer to an active SplitPoint
// object for which the current thread is the master.

void ThreadsManager::idle_loop(int threadID, SplitPoint* sp) {

  assert(threadID >= 0 && threadID < MAX_THREADS);

  int i;
  bool allFinished;

  while (true)
  {
      // Slave threads can exit as soon as AllThreadsShouldExit raises,
      // master should exit as last one.
      if (allThreadsShouldExit)
      {
          assert(!sp);
          threads[threadID].state = Thread::TERMINATED;
          return;
      }

      // If we are not thinking, wait for a condition to be signaled
      // instead of wasting CPU time polling for work.
      while (   threadID >= activeThreads
             || threads[threadID].state == Thread::INITIALIZING
             || (useSleepingThreads && threads[threadID].state == Thread::AVAILABLE))
      {
          assert(!sp || useSleepingThreads);
          assert(threadID != 0 || useSleepingThreads);

          if (threads[threadID].state == Thread::INITIALIZING)
              threads[threadID].state = Thread::AVAILABLE;

          // Grab the lock to avoid races with Thread::wake_up()
          lock_grab(&threads[threadID].sleepLock);

          // If we are master and all slaves have finished do not go to sleep
          for (i = 0; sp && i < activeThreads && !sp->is_slave[i]; i++) {}
          allFinished = (i == activeThreads);

          if (allFinished || allThreadsShouldExit)
          {
              lock_release(&threads[threadID].sleepLock);
              break;
          }

          // Do sleep here after retesting sleep conditions
          if (threadID >= activeThreads || threads[threadID].state == Thread::AVAILABLE)
              cond_wait(&threads[threadID].sleepCond, &threads[threadID].sleepLock);

          lock_release(&threads[threadID].sleepLock);
      }

      // If this thread has been assigned work, launch a search
      if (threads[threadID].state == Thread::WORKISWAITING)
      {
          assert(!allThreadsShouldExit);

          threads[threadID].state = Thread::SEARCHING;

          // Copy split point position and search stack and call search()
          // with SplitPoint template parameter set to true.
          SearchStack ss[PLY_MAX_PLUS_2];
          SplitPoint* tsp = threads[threadID].splitPoint;
          Position pos(*tsp->pos, threadID);

          memcpy(ss, tsp->ss - 1, 4 * sizeof(SearchStack));
          (ss+1)->sp = tsp;

          if (tsp->pvNode)
              search<SplitPointPV>(pos, ss+1, tsp->alpha, tsp->beta, tsp->depth);
          else
              search<SplitPointNonPV>(pos, ss+1, tsp->alpha, tsp->beta, tsp->depth);

          assert(threads[threadID].state == Thread::SEARCHING);

          threads[threadID].state = Thread::AVAILABLE;

          // Wake up master thread so to allow it to return from the idle loop in
          // case we are the last slave of the split point.
          if (   useSleepingThreads
              && threadID != tsp->master
              && threads[tsp->master].state == Thread::AVAILABLE)
              threads[tsp->master].wake_up();
      }

      // If this thread is the master of a split point and all slaves have
      // finished their work at this split point, return from the idle loop.
      for (i = 0; sp && i < activeThreads && !sp->is_slave[i]; i++) {}
      allFinished = (i == activeThreads);

      if (allFinished)
      {
          // Because sp->slaves[] is reset under lock protection,
          // be sure sp->lock has been released before to return.
          lock_grab(&(sp->lock));
          lock_release(&(sp->lock));

          // In helpful master concept a master can help only a sub-tree, and
          // because here is all finished is not possible master is booked.
          assert(threads[threadID].state == Thread::AVAILABLE);

          threads[threadID].state = Thread::SEARCHING;
          return;
      }
  }
}
