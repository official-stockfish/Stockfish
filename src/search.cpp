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


////
//// Includes
////

#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "book.h"
#include "evaluate.h"
#include "history.h"
#include "misc.h"
#include "movegen.h"
#include "movepick.h"
#include "lock.h"
#include "san.h"
#include "search.h"
#include "timeman.h"
#include "thread.h"
#include "tt.h"
#include "ucioption.h"

using std::cout;
using std::endl;

////
//// Local definitions
////

namespace {

  // Types
  enum NodeType { NonPV, PV };

  // Set to true to force running with one thread.
  // Used for debugging SMP code.
  const bool FakeSplit = false;

  // Fast lookup table of sliding pieces indexed by Piece
  const bool Slidings[18] = { 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1 };
  inline bool piece_is_slider(Piece p) { return Slidings[p]; }

  // ThreadsManager class is used to handle all the threads related stuff in search,
  // init, starting, parking and, the most important, launching a slave thread at a
  // split point are what this class does. All the access to shared thread data is
  // done through this class, so that we avoid using global variables instead.

  class ThreadsManager {
    /* As long as the single ThreadsManager object is defined as a global we don't
       need to explicitly initialize to zero its data members because variables with
       static storage duration are automatically set to zero before enter main()
    */
  public:
    void init_threads();
    void exit_threads();

    int min_split_depth() const { return minimumSplitDepth; }
    int active_threads() const { return activeThreads; }
    void set_active_threads(int cnt) { activeThreads = cnt; }

    void read_uci_options();
    bool available_thread_exists(int master) const;
    bool thread_is_available(int slave, int master) const;
    bool cutoff_at_splitpoint(int threadID) const;
    void wake_sleeping_thread(int threadID);
    void idle_loop(int threadID, SplitPoint* sp);

    template <bool Fake>
    void split(Position& pos, SearchStack* ss, int ply, Value* alpha, const Value beta, Value* bestValue,
               Depth depth, Move threatMove, bool mateThreat, int moveCount, MovePicker* mp, bool pvNode);

  private:
    Depth minimumSplitDepth;
    int maxThreadsPerSplitPoint;
    bool useSleepingThreads;
    int activeThreads;
    volatile bool allThreadsShouldExit;
    Thread threads[MAX_THREADS];
    Lock mpLock, sleepLock[MAX_THREADS];
    WaitCondition sleepCond[MAX_THREADS];
  };


  // RootMove struct is used for moves at the root at the tree. For each root
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
    // equal pv_score but m1 has the higher non_pv_score. In this
    // way we are guaranteed that PV moves are always sorted as first.
    bool operator<(const RootMove& m) const {
      return pv_score != m.pv_score ? pv_score < m.pv_score
                                    : non_pv_score < m.non_pv_score;
    }

    void extract_pv_from_tt(Position& pos);
    void insert_pv_in_tt(Position& pos);
    std::string pv_info_to_uci(const Position& pos, Value alpha, Value beta, int pvLine = 0);

    int64_t nodes;
    Value pv_score;
    Value non_pv_score;
    Move pv[PLY_MAX_PLUS_2];
  };


  // RootMoveList struct is essentially a std::vector<> of RootMove objects,
  // with an handful of methods above the standard ones.

  struct RootMoveList : public std::vector<RootMove> {

    typedef std::vector<RootMove> Base;

    RootMoveList(Position& pos, Move searchMoves[]);
    void set_non_pv_scores(const Position& pos);

    void sort() { insertion_sort<RootMove, Base::iterator>(begin(), end()); }
    void sort_multipv(int n) { insertion_sort<RootMove, Base::iterator>(begin(), begin() + n + 1); }
  };


  // When formatting a move for std::cout we must know if we are in Chess960
  // or not. To keep using the handy operator<<() on the move the trick is to
  // embed this flag in the stream itself. Function-like named enum set960 is
  // used as a custom manipulator and the stream internal general-purpose array,
  // accessed through ios_base::iword(), is used to pass the flag to the move's
  // operator<<() that will use it to properly format castling moves.
  enum set960 {};

  std::ostream& operator<< (std::ostream& os, const set960& m) {

    os.iword(0) = int(m);
    return os;
  }


  /// Adjustments

  // Step 6. Razoring

  // Maximum depth for razoring
  const Depth RazorDepth = 4 * ONE_PLY;

  // Dynamic razoring margin based on depth
  inline Value razor_margin(Depth d) { return Value(0x200 + 0x10 * int(d)); }

  // Maximum depth for use of dynamic threat detection when null move fails low
  const Depth ThreatDepth = 5 * ONE_PLY;

  // Step 9. Internal iterative deepening

  // Minimum depth for use of internal iterative deepening
  const Depth IIDDepth[2] = { 8 * ONE_PLY /* non-PV */, 5 * ONE_PLY /* PV */};

  // At Non-PV nodes we do an internal iterative deepening search
  // when the static evaluation is bigger then beta - IIDMargin.
  const Value IIDMargin = Value(0x100);

  // Step 11. Decide the new search depth

  // Extensions. Configurable UCI options
  // Array index 0 is used at non-PV nodes, index 1 at PV nodes.
  Depth CheckExtension[2], SingleEvasionExtension[2], PawnPushTo7thExtension[2];
  Depth PassedPawnExtension[2], PawnEndgameExtension[2], MateThreatExtension[2];

  // Minimum depth for use of singular extension
  const Depth SingularExtensionDepth[2] = { 8 * ONE_PLY /* non-PV */, 6 * ONE_PLY /* PV */};

  // If the TT move is at least SingularExtensionMargin better then the
  // remaining ones we will extend it.
  const Value SingularExtensionMargin = Value(0x20);

  // Step 12. Futility pruning

  // Futility margin for quiescence search
  const Value FutilityMarginQS = Value(0x80);

  // Futility lookup tables (initialized at startup) and their getter functions
  Value FutilityMarginsMatrix[16][64]; // [depth][moveNumber]
  int FutilityMoveCountArray[32]; // [depth]

  inline Value futility_margin(Depth d, int mn) { return d < 7 * ONE_PLY ? FutilityMarginsMatrix[Max(d, 1)][Min(mn, 63)] : 2 * VALUE_INFINITE; }
  inline int futility_move_count(Depth d) { return d < 16 * ONE_PLY ? FutilityMoveCountArray[d] : 512; }

  // Step 14. Reduced search

  // Reduction lookup tables (initialized at startup) and their getter functions
  int8_t ReductionMatrix[2][64][64]; // [pv][depth][moveNumber]

  template <NodeType PV>
  inline Depth reduction(Depth d, int mn) { return (Depth) ReductionMatrix[PV][Min(d / 2, 63)][Min(mn, 63)]; }

  // Common adjustments

  // Search depth at iteration 1
  const Depth InitialDepth = ONE_PLY;

  // Easy move margin. An easy move candidate must be at least this much
  // better than the second best move.
  const Value EasyMoveMargin = Value(0x200);


  /// Namespace variables

  // Book object
  Book OpeningBook;

  // Iteration counter
  int Iteration;

  // Scores and number of times the best move changed for each iteration
  Value ValueByIteration[PLY_MAX_PLUS_2];
  int BestMoveChangesByIteration[PLY_MAX_PLUS_2];

  // Search window management
  int AspirationDelta;

  // MultiPV mode
  int MultiPV;

  // Time managment variables
  int SearchStartTime, MaxNodes, MaxDepth, ExactMaxTime;
  bool UseTimeManagement, InfiniteSearch, Pondering, StopOnPonderhit;
  bool FirstRootMove, StopRequest, QuitRequest, AspirationFailLow;
  TimeManager TimeMgr;

  // Log file
  bool UseLogFile;
  std::ofstream LogFile;

  // Multi-threads manager object
  ThreadsManager ThreadsMgr;

  // Node counters, used only by thread[0] but try to keep in different cache
  // lines (64 bytes each) from the heavy multi-thread read accessed variables.
  bool SendSearchedNodes;
  int NodesSincePoll;
  int NodesBetweenPolls = 30000;

  // History table
  History H;

  /// Local functions

  Move id_loop(Position& pos, Move searchMoves[], Move* ponderMove);
  Value root_search(Position& pos, SearchStack* ss, Value alpha, Value beta, Depth depth, RootMoveList& rml);

  template <NodeType PvNode, bool SpNode>
  Value search(Position& pos, SearchStack* ss, Value alpha, Value beta, Depth depth, int ply);

  template <NodeType PvNode>
  Value qsearch(Position& pos, SearchStack* ss, Value alpha, Value beta, Depth depth, int ply);

  template <NodeType PvNode>
  inline Value search(Position& pos, SearchStack* ss, Value alpha, Value beta, Depth depth, int ply) {

      return depth < ONE_PLY ? qsearch<PvNode>(pos, ss, alpha, beta, DEPTH_ZERO, ply)
                             : search<PvNode, false>(pos, ss, alpha, beta, depth, ply);
  }

  template <NodeType PvNode>
  Depth extension(const Position& pos, Move m, bool captureOrPromotion, bool moveIsCheck, bool singleEvasion, bool mateThreat, bool* dangerous);

  bool check_is_dangerous(Position &pos, Move move, Value futilityBase, Value beta, Value *bValue);
  bool connected_moves(const Position& pos, Move m1, Move m2);
  bool value_is_mate(Value value);
  Value value_to_tt(Value v, int ply);
  Value value_from_tt(Value v, int ply);
  bool ok_to_use_TT(const TTEntry* tte, Depth depth, Value beta, int ply);
  bool connected_threat(const Position& pos, Move m, Move threat);
  Value refine_eval(const TTEntry* tte, Value defaultEval, int ply);
  void update_history(const Position& pos, Move move, Depth depth, Move movesSearched[], int moveCount);
  void update_killers(Move m, SearchStack* ss);
  void update_gains(const Position& pos, Move move, Value before, Value after);

  int current_search_time();
  std::string value_to_uci(Value v);
  int nps(const Position& pos);
  void poll(const Position& pos);
  void wait_for_stop_or_ponderhit();
  void init_ss_array(SearchStack* ss, int size);

#if !defined(_MSC_VER)
  void* init_thread(void* threadID);
#else
  DWORD WINAPI init_thread(LPVOID threadID);
#endif

}


////
//// Functions
////

/// init_threads(), exit_threads() and nodes_searched() are helpers to
/// give accessibility to some TM methods from outside of current file.

void init_threads() { ThreadsMgr.init_threads(); }
void exit_threads() { ThreadsMgr.exit_threads(); }


/// init_search() is called during startup. It initializes various lookup tables

void init_search() {

  int d;  // depth (ONE_PLY == 2)
  int hd; // half depth (ONE_PLY == 1)
  int mc; // moveCount

  // Init reductions array
  for (hd = 1; hd < 64; hd++) for (mc = 1; mc < 64; mc++)
  {
      double    pvRed = log(double(hd)) * log(double(mc)) / 3.0;
      double nonPVRed = 0.33 + log(double(hd)) * log(double(mc)) / 2.25;
      ReductionMatrix[PV][hd][mc]    = (int8_t) (   pvRed >= 1.0 ? floor(   pvRed * int(ONE_PLY)) : 0);
      ReductionMatrix[NonPV][hd][mc] = (int8_t) (nonPVRed >= 1.0 ? floor(nonPVRed * int(ONE_PLY)) : 0);
  }

  // Init futility margins array
  for (d = 1; d < 16; d++) for (mc = 0; mc < 64; mc++)
      FutilityMarginsMatrix[d][mc] = Value(112 * int(log(double(d * d) / 2) / log(2.0) + 1.001) - 8 * mc + 45);

  // Init futility move count array
  for (d = 0; d < 32; d++)
      FutilityMoveCountArray[d] = int(3.001 + 0.25 * pow(d, 2.0));
}


/// perft() is our utility to verify move generation is bug free. All the legal
/// moves up to given depth are generated and counted and the sum returned.

int perft(Position& pos, Depth depth)
{
    MoveStack mlist[MOVES_MAX];
    StateInfo st;
    Move m;
    int sum = 0;

    // Generate all legal moves
    MoveStack* last = generate_moves(pos, mlist);

    // If we are at the last ply we don't need to do and undo
    // the moves, just to count them.
    if (depth <= ONE_PLY)
        return int(last - mlist);

    // Loop through all legal moves
    CheckInfo ci(pos);
    for (MoveStack* cur = mlist; cur != last; cur++)
    {
        m = cur->move;
        pos.do_move(m, st, ci, pos.move_is_check(m, ci));
        sum += perft(pos, depth - ONE_PLY);
        pos.undo_move(m);
    }
    return sum;
}


/// think() is the external interface to Stockfish's search, and is called when
/// the program receives the UCI 'go' command. It initializes various
/// search-related global variables, and calls root_search(). It returns false
/// when a quit command is received during the search.

bool think(Position& pos, bool infinite, bool ponder, int time[], int increment[],
           int movesToGo, int maxDepth, int maxNodes, int maxTime, Move searchMoves[]) {

  // Initialize global search variables
  StopOnPonderhit = StopRequest = QuitRequest = AspirationFailLow = SendSearchedNodes = false;
  NodesSincePoll = 0;
  SearchStartTime = get_system_time();
  ExactMaxTime = maxTime;
  MaxDepth = maxDepth;
  MaxNodes = maxNodes;
  InfiniteSearch = infinite;
  Pondering = ponder;
  UseTimeManagement = !ExactMaxTime && !MaxDepth && !MaxNodes && !InfiniteSearch;

  // Look for a book move, only during games, not tests
  if (UseTimeManagement && Options["OwnBook"].value<bool>())
  {
      if (Options["Book File"].value<std::string>() != OpeningBook.file_name())
          OpeningBook.open(Options["Book File"].value<std::string>());

      Move bookMove = OpeningBook.get_move(pos, Options["Best Book Move"].value<bool>());
      if (bookMove != MOVE_NONE)
      {
          if (Pondering)
              wait_for_stop_or_ponderhit();

          cout << "bestmove " << bookMove << endl;
          return !QuitRequest;
      }
  }

  // Read UCI option values
  TT.set_size(Options["Hash"].value<int>());
  if (Options["Clear Hash"].value<bool>())
  {
      Options["Clear Hash"].set_value("false");
      TT.clear();
  }

  CheckExtension[1]         = Options["Check Extension (PV nodes)"].value<Depth>();
  CheckExtension[0]         = Options["Check Extension (non-PV nodes)"].value<Depth>();
  SingleEvasionExtension[1] = Options["Single Evasion Extension (PV nodes)"].value<Depth>();
  SingleEvasionExtension[0] = Options["Single Evasion Extension (non-PV nodes)"].value<Depth>();
  PawnPushTo7thExtension[1] = Options["Pawn Push to 7th Extension (PV nodes)"].value<Depth>();
  PawnPushTo7thExtension[0] = Options["Pawn Push to 7th Extension (non-PV nodes)"].value<Depth>();
  PassedPawnExtension[1]    = Options["Passed Pawn Extension (PV nodes)"].value<Depth>();
  PassedPawnExtension[0]    = Options["Passed Pawn Extension (non-PV nodes)"].value<Depth>();
  PawnEndgameExtension[1]   = Options["Pawn Endgame Extension (PV nodes)"].value<Depth>();
  PawnEndgameExtension[0]   = Options["Pawn Endgame Extension (non-PV nodes)"].value<Depth>();
  MateThreatExtension[1]    = Options["Mate Threat Extension (PV nodes)"].value<Depth>();
  MateThreatExtension[0]    = Options["Mate Threat Extension (non-PV nodes)"].value<Depth>();
  MultiPV                   = Options["MultiPV"].value<int>();
  UseLogFile                = Options["Use Search Log"].value<bool>();

  read_evaluation_uci_options(pos.side_to_move());

  // Set the number of active threads
  ThreadsMgr.read_uci_options();
  init_eval(ThreadsMgr.active_threads());

  // Wake up needed threads
  for (int i = 1; i < ThreadsMgr.active_threads(); i++)
      ThreadsMgr.wake_sleeping_thread(i);

  // Set thinking time
  int myTime = time[pos.side_to_move()];
  int myIncrement = increment[pos.side_to_move()];
  if (UseTimeManagement)
      TimeMgr.init(myTime, myIncrement, movesToGo, pos.startpos_ply_counter());

  // Set best NodesBetweenPolls interval to avoid lagging under
  // heavy time pressure.
  if (MaxNodes)
      NodesBetweenPolls = Min(MaxNodes, 30000);
  else if (myTime && myTime < 1000)
      NodesBetweenPolls = 1000;
  else if (myTime && myTime < 5000)
      NodesBetweenPolls = 5000;
  else
      NodesBetweenPolls = 30000;

  // Write search information to log file
  if (UseLogFile)
  {
      std::string name = Options["Search Log Filename"].value<std::string>();
      LogFile.open(name.c_str(), std::ios::out | std::ios::app);

      LogFile << "Searching: "  << pos.to_fen()
              << "\ninfinite: " << infinite
              << " ponder: "    << ponder
              << " time: "      << myTime
              << " increment: " << myIncrement
              << " moves to go: " << movesToGo << endl;
  }

  // We're ready to start thinking. Call the iterative deepening loop function
  Move ponderMove = MOVE_NONE;
  Move bestMove = id_loop(pos, searchMoves, &ponderMove);

  // Print final search statistics
  cout << "info nodes " << pos.nodes_searched()
       << " nps " << nps(pos)
       << " time " << current_search_time() << endl;

  if (UseLogFile)
  {
      LogFile << "\nNodes: " << pos.nodes_searched()
              << "\nNodes/second: " << nps(pos)
              << "\nBest move: " << move_to_san(pos, bestMove);

      StateInfo st;
      pos.do_move(bestMove, st);
      LogFile << "\nPonder move: "
              << move_to_san(pos, ponderMove) // Works also with MOVE_NONE
              << endl;

      LogFile.close();
  }

  // This makes all the threads to go to sleep
  ThreadsMgr.set_active_threads(1);

  // If we are pondering or in infinite search, we shouldn't print the
  // best move before we are told to do so.
  if (!StopRequest && (Pondering || InfiniteSearch))
      wait_for_stop_or_ponderhit();

  // Could be both MOVE_NONE when searching on a stalemate position
  cout << "bestmove " << bestMove << " ponder " << ponderMove << endl;

  return !QuitRequest;
}


namespace {

  // id_loop() is the main iterative deepening loop. It calls root_search
  // repeatedly with increasing depth until the allocated thinking time has
  // been consumed, the user stops the search, or the maximum search depth is
  // reached.

  Move id_loop(Position& pos, Move searchMoves[], Move* ponderMove) {

    SearchStack ss[PLY_MAX_PLUS_2];
    Depth depth;
    Move EasyMove = MOVE_NONE;
    Value value, alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;

    // Moves to search are verified, scored and sorted
    RootMoveList rml(pos, searchMoves);

    // Handle special case of searching on a mate/stale position
    if (rml.size() == 0)
    {
        Value s = (pos.is_check() ? -VALUE_MATE : VALUE_DRAW);

        cout << "info depth " << 1
             << " score " << value_to_uci(s) << endl;

        return MOVE_NONE;
    }

    // Initialize
    TT.new_search();
    H.clear();
    init_ss_array(ss, PLY_MAX_PLUS_2);
    ValueByIteration[1] = rml[0].pv_score;
    Iteration = 1;

    // Send initial RootMoveList scoring (iteration 1)
    cout << set960(pos.is_chess960()) // Is enough to set once at the beginning
         << "info depth " << Iteration
         << "\n" << rml[0].pv_info_to_uci(pos, alpha, beta) << endl;

    // Is one move significantly better than others after initial scoring ?
    if (   rml.size() == 1
        || rml[0].pv_score > rml[1].pv_score + EasyMoveMargin)
        EasyMove = rml[0].pv[0];

    // Iterative deepening loop
    while (Iteration < PLY_MAX)
    {
        // Initialize iteration
        Iteration++;
        BestMoveChangesByIteration[Iteration] = 0;

        cout << "info depth " << Iteration << endl;

        // Calculate dynamic aspiration window based on previous iterations
        if (MultiPV == 1 && Iteration >= 6 && abs(ValueByIteration[Iteration - 1]) < VALUE_KNOWN_WIN)
        {
            int prevDelta1 = ValueByIteration[Iteration - 1] - ValueByIteration[Iteration - 2];
            int prevDelta2 = ValueByIteration[Iteration - 2] - ValueByIteration[Iteration - 3];

            AspirationDelta = Max(abs(prevDelta1) + abs(prevDelta2) / 2, 16);
            AspirationDelta = (AspirationDelta + 7) / 8 * 8; // Round to match grainSize

            alpha = Max(ValueByIteration[Iteration - 1] - AspirationDelta, -VALUE_INFINITE);
            beta  = Min(ValueByIteration[Iteration - 1] + AspirationDelta,  VALUE_INFINITE);
        }

        depth = (Iteration - 2) * ONE_PLY + InitialDepth;

        // Search to the current depth, rml is updated and sorted
        value = root_search(pos, ss, alpha, beta, depth, rml);

        if (StopRequest)
            break; // Value cannot be trusted. Break out immediately!

        //Save info about search result
        ValueByIteration[Iteration] = value;

        // Drop the easy move if differs from the new best move
        if (rml[0].pv[0] != EasyMove)
            EasyMove = MOVE_NONE;

        if (UseTimeManagement)
        {
            // Time to stop?
            bool stopSearch = false;

            // Stop search early if there is only a single legal move,
            // we search up to Iteration 6 anyway to get a proper score.
            if (Iteration >= 6 && rml.size() == 1)
                stopSearch = true;

            // Stop search early when the last two iterations returned a mate score
            if (  Iteration >= 6
                && abs(ValueByIteration[Iteration]) >= abs(VALUE_MATE) - 100
                && abs(ValueByIteration[Iteration-1]) >= abs(VALUE_MATE) - 100)
                stopSearch = true;

            // Stop search early if one move seems to be much better than the others
            if (   Iteration >= 8
                && EasyMove == rml[0].pv[0]
                && (  (   rml[0].nodes > (pos.nodes_searched() * 85) / 100
                       && current_search_time() > TimeMgr.available_time() / 16)
                    ||(   rml[0].nodes > (pos.nodes_searched() * 98) / 100
                       && current_search_time() > TimeMgr.available_time() / 32)))
                stopSearch = true;

            // Add some extra time if the best move has changed during the last two iterations
            if (Iteration > 5 && Iteration <= 50)
                TimeMgr.pv_instability(BestMoveChangesByIteration[Iteration],
                                       BestMoveChangesByIteration[Iteration-1]);

            // Stop search if most of MaxSearchTime is consumed at the end of the
            // iteration. We probably don't have enough time to search the first
            // move at the next iteration anyway.
            if (current_search_time() > (TimeMgr.available_time() * 80) / 128)
                stopSearch = true;

            if (stopSearch)
            {
                if (Pondering)
                    StopOnPonderhit = true;
                else
                    break;
            }
        }

        if (MaxDepth && Iteration >= MaxDepth)
            break;
    }

    *ponderMove = rml[0].pv[1];
    return rml[0].pv[0];
  }


  // root_search() is the function which searches the root node. It is
  // similar to search_pv except that it prints some information to the
  // standard output and handles the fail low/high loops.

  Value root_search(Position& pos, SearchStack* ss, Value alpha,
                    Value beta, Depth depth, RootMoveList& rml) {
    StateInfo st;
    CheckInfo ci(pos);
    int64_t nodes;
    Move move;
    Depth ext, newDepth;
    Value value, oldAlpha;
    bool isCheck, moveIsCheck, captureOrPromotion, dangerous;
    int researchCountFH, researchCountFL;

    researchCountFH = researchCountFL = 0;
    oldAlpha = alpha;
    isCheck = pos.is_check();

    // Step 1. Initialize node (polling is omitted at root)
    ss->currentMove = ss->bestMove = MOVE_NONE;

    // Step 2. Check for aborted search (omitted at root)
    // Step 3. Mate distance pruning (omitted at root)
    // Step 4. Transposition table lookup (omitted at root)

    // Step 5. Evaluate the position statically
    // At root we do this only to get reference value for child nodes
    ss->evalMargin = VALUE_NONE;
    ss->eval = isCheck ? VALUE_NONE : evaluate(pos, ss->evalMargin);

    // Step 6. Razoring (omitted at root)
    // Step 7. Static null move pruning (omitted at root)
    // Step 8. Null move search with verification search (omitted at root)
    // Step 9. Internal iterative deepening (omitted at root)

    // Step extra. Fail low loop
    // We start with small aspiration window and in case of fail low, we research
    // with bigger window until we are not failing low anymore.
    while (1)
    {
        // Sort the moves before to (re)search
        rml.set_non_pv_scores(pos);
        rml.sort();

        // Step 10. Loop through all moves in the root move list
        for (int i = 0; i < (int)rml.size() && !StopRequest; i++)
        {
            // This is used by time management
            FirstRootMove = (i == 0);

            // Save the current node count before the move is searched
            nodes = pos.nodes_searched();

            // If it's time to send nodes info, do it here where we have the
            // correct accumulated node counts searched by each thread.
            if (SendSearchedNodes)
            {
                SendSearchedNodes = false;
                cout << "info nodes " << nodes
                     << " nps " << nps(pos)
                     << " time " << current_search_time() << endl;
            }

            // Pick the next root move, and print the move and the move number to
            // the standard output.
            move = ss->currentMove = rml[i].pv[0];

            if (current_search_time() >= 1000)
                cout << "info currmove " << move
                     << " currmovenumber " << i + 1 << endl;

            moveIsCheck = pos.move_is_check(move);
            captureOrPromotion = pos.move_is_capture_or_promotion(move);

            // Step 11. Decide the new search depth
            ext = extension<PV>(pos, move, captureOrPromotion, moveIsCheck, false, false, &dangerous);
            newDepth = depth + ext;

            // Step 12. Futility pruning (omitted at root)

            // Step extra. Fail high loop
            // If move fails high, we research with bigger window until we are not failing
            // high anymore.
            value = -VALUE_INFINITE;

            while (1)
            {
                // Step 13. Make the move
                pos.do_move(move, st, ci, moveIsCheck);

                // Step extra. pv search
                // We do pv search for first moves (i < MultiPV)
                // and for fail high research (value > alpha)
                if (i < MultiPV || value > alpha)
                {
                    // Aspiration window is disabled in multi-pv case
                    if (MultiPV > 1)
                        alpha = -VALUE_INFINITE;

                    // Full depth PV search, done on first move or after a fail high
                    value = -search<PV>(pos, ss+1, -beta, -alpha, newDepth, 1);
                }
                else
                {
                    // Step 14. Reduced search
                    // if the move fails high will be re-searched at full depth
                    bool doFullDepthSearch = true;

                    if (    depth >= 3 * ONE_PLY
                        && !dangerous
                        && !captureOrPromotion
                        && !move_is_castle(move))
                    {
                        ss->reduction = reduction<PV>(depth, i - MultiPV + 2);
                        if (ss->reduction)
                        {
                            assert(newDepth-ss->reduction >= ONE_PLY);

                            // Reduced depth non-pv search using alpha as upperbound
                            value = -search<NonPV>(pos, ss+1, -(alpha+1), -alpha, newDepth-ss->reduction, 1);
                            doFullDepthSearch = (value > alpha);
                        }
                        ss->reduction = DEPTH_ZERO; // Restore original reduction
                    }

                    // Step 15. Full depth search
                    if (doFullDepthSearch)
                    {
                        // Full depth non-pv search using alpha as upperbound
                        value = -search<NonPV>(pos, ss+1, -(alpha+1), -alpha, newDepth, 1);

                        // If we are above alpha then research at same depth but as PV
                        // to get a correct score or eventually a fail high above beta.
                        if (value > alpha)
                            value = -search<PV>(pos, ss+1, -beta, -alpha, newDepth, 1);
                    }
                }

                // Step 16. Undo move
                pos.undo_move(move);

                // Can we exit fail high loop ?
                if (StopRequest || value < beta)
                    break;

                // We are failing high and going to do a research. It's important to update
                // the score before research in case we run out of time while researching.
                ss->bestMove = move;
                rml[i].pv_score = value;
                rml[i].extract_pv_from_tt(pos);

                // Inform GUI that PV has changed
                cout << rml[i].pv_info_to_uci(pos, alpha, beta) << endl;

                // Prepare for a research after a fail high, each time with a wider window
                beta = Min(beta + AspirationDelta * (1 << researchCountFH), VALUE_INFINITE);
                researchCountFH++;

            } // End of fail high loop

            // Finished searching the move. If AbortSearch is true, the search
            // was aborted because the user interrupted the search or because we
            // ran out of time. In this case, the return value of the search cannot
            // be trusted, and we break out of the loop without updating the best
            // move and/or PV.
            if (StopRequest)
                break;

            // Remember searched nodes counts for this move
            rml[i].nodes += pos.nodes_searched() - nodes;

            assert(value >= -VALUE_INFINITE && value <= VALUE_INFINITE);
            assert(value < beta);

            // Step 17. Check for new best move
            if (value <= alpha && i >= MultiPV)
                rml[i].pv_score = -VALUE_INFINITE;
            else
            {
                // PV move or new best move!

                // Update PV
                ss->bestMove = move;
                rml[i].pv_score = value;
                rml[i].extract_pv_from_tt(pos);

                // We record how often the best move has been changed in each
                // iteration. This information is used for time managment: When
                // the best move changes frequently, we allocate some more time.
                if (MultiPV == 1 && i > 0)
                    BestMoveChangesByIteration[Iteration]++;

                // Inform GUI that PV has changed, in case of multi-pv UCI protocol
                // requires we send all the PV lines properly sorted.
                rml.sort_multipv(i);

                for (int j = 0; j < Min(MultiPV, (int)rml.size()); j++)
                    cout << rml[j].pv_info_to_uci(pos, alpha, beta, j) << endl;

                // Update alpha. In multi-pv we don't use aspiration window
                if (MultiPV == 1)
                {
                    // Raise alpha to setup proper non-pv search upper bound
                    if (value > alpha)
                        alpha = value;
                }
                else // Set alpha equal to minimum score among the PV lines
                    alpha = rml[Min(i, MultiPV - 1)].pv_score;

            } // PV move or new best move

            assert(alpha >= oldAlpha);

            AspirationFailLow = (alpha == oldAlpha);

            if (AspirationFailLow && StopOnPonderhit)
                StopOnPonderhit = false;

        } // Root moves loop

        // Can we exit fail low loop ?
        if (StopRequest || !AspirationFailLow)
            break;

        // Prepare for a research after a fail low, each time with a wider window
        oldAlpha = alpha = Max(alpha - AspirationDelta * (1 << researchCountFL), -VALUE_INFINITE);
        researchCountFL++;

    } // Fail low loop

    // Sort the moves before to return
    rml.sort();

    // Write PV lines to transposition table, in case the relevant entries
    // have been overwritten during the search.
    for (int i = 0; i < MultiPV; i++)
        rml[i].insert_pv_in_tt(pos);

    return alpha;
  }


  // search<>() is the main search function for both PV and non-PV nodes and for
  // normal and SplitPoint nodes. When called just after a split point the search
  // is simpler because we have already probed the hash table, done a null move
  // search, and searched the first move before splitting, we don't have to repeat
  // all this work again. We also don't need to store anything to the hash table
  // here: This is taken care of after we return from the split point.

  template <NodeType PvNode, bool SpNode>
  Value search(Position& pos, SearchStack* ss, Value alpha, Value beta, Depth depth, int ply) {

    assert(alpha >= -VALUE_INFINITE && alpha <= VALUE_INFINITE);
    assert(beta > alpha && beta <= VALUE_INFINITE);
    assert(PvNode || alpha == beta - 1);
    assert(ply > 0 && ply < PLY_MAX);
    assert(pos.thread() >= 0 && pos.thread() < ThreadsMgr.active_threads());

    Move movesSearched[MOVES_MAX];
    StateInfo st;
    const TTEntry *tte;
    Key posKey;
    Move ttMove, move, excludedMove, threatMove;
    Depth ext, newDepth;
    ValueType vt;
    Value bestValue, value, oldAlpha;
    Value refinedValue, nullValue, futilityBase, futilityValueScaled; // Non-PV specific
    bool isCheck, singleEvasion, singularExtensionNode, moveIsCheck, captureOrPromotion, dangerous;
    bool mateThreat = false;
    int moveCount = 0;
    int threadID = pos.thread();
    SplitPoint* sp = NULL;
    refinedValue = bestValue = value = -VALUE_INFINITE;
    oldAlpha = alpha;
    isCheck = pos.is_check();

    if (SpNode)
    {
        sp = ss->sp;
        tte = NULL;
        ttMove = excludedMove = MOVE_NONE;
        threatMove = sp->threatMove;
        mateThreat = sp->mateThreat;
        goto split_point_start;
    }
    else {} // Hack to fix icc's "statement is unreachable" warning

    // Step 1. Initialize node and poll. Polling can abort search
    ss->currentMove = ss->bestMove = threatMove = MOVE_NONE;
    (ss+2)->killers[0] = (ss+2)->killers[1] = (ss+2)->mateKiller = MOVE_NONE;

    if (threadID == 0 && ++NodesSincePoll > NodesBetweenPolls)
    {
        NodesSincePoll = 0;
        poll(pos);
    }

    // Step 2. Check for aborted search and immediate draw
    if (   StopRequest
        || ThreadsMgr.cutoff_at_splitpoint(threadID)
        || pos.is_draw()
        || ply >= PLY_MAX - 1)
        return VALUE_DRAW;

    // Step 3. Mate distance pruning
    alpha = Max(value_mated_in(ply), alpha);
    beta = Min(value_mate_in(ply+1), beta);
    if (alpha >= beta)
        return alpha;

    // Step 4. Transposition table lookup

    // We don't want the score of a partial search to overwrite a previous full search
    // TT value, so we use a different position key in case of an excluded move exists.
    excludedMove = ss->excludedMove;
    posKey = excludedMove ? pos.get_exclusion_key() : pos.get_key();

    tte = TT.retrieve(posKey);
    ttMove = tte ? tte->move() : MOVE_NONE;

    // At PV nodes, we don't use the TT for pruning, but only for move ordering.
    // This is to avoid problems in the following areas:
    //
    // * Repetition draw detection
    // * Fifty move rule detection
    // * Searching for a mate
    // * Printing of full PV line
    if (!PvNode && tte && ok_to_use_TT(tte, depth, beta, ply))
    {
        TT.refresh(tte);
        ss->bestMove = ttMove; // Can be MOVE_NONE
        return value_from_tt(tte->value(), ply);
    }

    // Step 5. Evaluate the position statically and
    // update gain statistics of parent move.
    if (isCheck)
        ss->eval = ss->evalMargin = VALUE_NONE;
    else if (tte)
    {
        assert(tte->static_value() != VALUE_NONE);

        ss->eval = tte->static_value();
        ss->evalMargin = tte->static_value_margin();
        refinedValue = refine_eval(tte, ss->eval, ply);
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
        && !isCheck
        &&  refinedValue < beta - razor_margin(depth)
        &&  ttMove == MOVE_NONE
        && !value_is_mate(beta)
        && !pos.has_pawn_on_7th(pos.side_to_move()))
    {
        Value rbeta = beta - razor_margin(depth);
        Value v = qsearch<NonPV>(pos, ss, rbeta-1, rbeta, DEPTH_ZERO, ply);
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
        && !isCheck
        &&  refinedValue >= beta + futility_margin(depth, 0)
        && !value_is_mate(beta)
        &&  pos.non_pawn_material(pos.side_to_move()))
        return refinedValue - futility_margin(depth, 0);

    // Step 8. Null move search with verification search (is omitted in PV nodes)
    if (   !PvNode
        && !ss->skipNullMove
        &&  depth > ONE_PLY
        && !isCheck
        &&  refinedValue >= beta
        && !value_is_mate(beta)
        &&  pos.non_pawn_material(pos.side_to_move()))
    {
        ss->currentMove = MOVE_NULL;

        // Null move dynamic reduction based on depth
        int R = 3 + (depth >= 5 * ONE_PLY ? depth / 8 : 0);

        // Null move dynamic reduction based on value
        if (refinedValue - beta > PawnValueMidgame)
            R++;

        pos.do_null_move(st);
        (ss+1)->skipNullMove = true;
        nullValue = -search<NonPV>(pos, ss+1, -beta, -alpha, depth-R*ONE_PLY, ply+1);
        (ss+1)->skipNullMove = false;
        pos.undo_null_move();

        if (nullValue >= beta)
        {
            // Do not return unproven mate scores
            if (nullValue >= value_mate_in(PLY_MAX))
                nullValue = beta;

            if (depth < 6 * ONE_PLY)
                return nullValue;

            // Do verification search at high depths
            ss->skipNullMove = true;
            Value v = search<NonPV>(pos, ss, alpha, beta, depth-R*ONE_PLY, ply);
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
            if (nullValue == value_mated_in(ply + 2))
                mateThreat = true;

            threatMove = (ss+1)->bestMove;
            if (   depth < ThreatDepth
                && (ss-1)->reduction
                && threatMove != MOVE_NONE
                && connected_moves(pos, (ss-1)->currentMove, threatMove))
                return beta - 1;
        }
    }

    // Step 9. Internal iterative deepening
    if (    depth >= IIDDepth[PvNode]
        &&  ttMove == MOVE_NONE
        && (PvNode || (!isCheck && ss->eval >= beta - IIDMargin)))
    {
        Depth d = (PvNode ? depth - 2 * ONE_PLY : depth / 2);

        ss->skipNullMove = true;
        search<PvNode>(pos, ss, alpha, beta, d, ply);
        ss->skipNullMove = false;

        ttMove = ss->bestMove;
        tte = TT.retrieve(posKey);
    }

    // Expensive mate threat detection (only for PV nodes)
    if (PvNode)
        mateThreat = pos.has_mate_threat();

split_point_start: // At split points actual search starts from here

    // Initialize a MovePicker object for the current position
    // FIXME currently MovePicker() c'tor is needless called also in SplitPoint
    MovePicker mpBase(pos, ttMove, depth, H, ss, (PvNode ? -VALUE_INFINITE : beta));
    MovePicker& mp = SpNode ? *sp->mp : mpBase;
    CheckInfo ci(pos);
    ss->bestMove = MOVE_NONE;
    singleEvasion = !SpNode && isCheck && mp.number_of_evasions() == 1;
    futilityBase = ss->eval + ss->evalMargin;
    singularExtensionNode =  !SpNode
                           && depth >= SingularExtensionDepth[PvNode]
                           && tte
                           && tte->move()
                           && !excludedMove // Do not allow recursive singular extension search
                           && (tte->type() & VALUE_TYPE_LOWER)
                           && tte->depth() >= depth - 3 * ONE_PLY;
    if (SpNode)
    {
        lock_grab(&(sp->lock));
        bestValue = sp->bestValue;
    }

    // Step 10. Loop through moves
    // Loop through all legal moves until no moves remain or a beta cutoff occurs
    while (   bestValue < beta
           && (move = mp.get_next_move()) != MOVE_NONE
           && !ThreadsMgr.cutoff_at_splitpoint(threadID))
    {
      assert(move_is_ok(move));

      if (SpNode)
      {
          moveCount = ++sp->moveCount;
          lock_release(&(sp->lock));
      }
      else if (move == excludedMove)
          continue;
      else
          movesSearched[moveCount++] = move;

      moveIsCheck = pos.move_is_check(move, ci);
      captureOrPromotion = pos.move_is_capture_or_promotion(move);

      // Step 11. Decide the new search depth
      ext = extension<PvNode>(pos, move, captureOrPromotion, moveIsCheck, singleEvasion, mateThreat, &dangerous);

      // Singular extension search. If all moves but one fail low on a search of (alpha-s, beta-s),
      // and just one fails high on (alpha, beta), then that move is singular and should be extended.
      // To verify this we do a reduced search on all the other moves but the ttMove, if result is
      // lower then ttValue minus a margin then we extend ttMove.
      if (   singularExtensionNode
          && move == tte->move()
          && ext < ONE_PLY)
      {
          Value ttValue = value_from_tt(tte->value(), ply);

          if (abs(ttValue) < VALUE_KNOWN_WIN)
          {
              Value b = ttValue - SingularExtensionMargin;
              ss->excludedMove = move;
              ss->skipNullMove = true;
              Value v = search<NonPV>(pos, ss, b - 1, b, depth / 2, ply);
              ss->skipNullMove = false;
              ss->excludedMove = MOVE_NONE;
              ss->bestMove = MOVE_NONE;
              if (v < b)
                  ext = ONE_PLY;
          }
      }

      // Update current move (this must be done after singular extension search)
      ss->currentMove = move;
      newDepth = depth - ONE_PLY + ext;

      // Step 12. Futility pruning (is omitted in PV nodes)
      if (   !PvNode
          && !captureOrPromotion
          && !isCheck
          && !dangerous
          &&  move != ttMove
          && !move_is_castle(move))
      {
          // Move count based pruning
          if (   moveCount >= futility_move_count(depth)
              && !(threatMove && connected_threat(pos, move, threatMove))
              && bestValue > value_mated_in(PLY_MAX)) // FIXME bestValue is racy
          {
              if (SpNode)
                  lock_grab(&(sp->lock));

              continue;
          }

          // Value based pruning
          // We illogically ignore reduction condition depth >= 3*ONE_PLY for predicted depth,
          // but fixing this made program slightly weaker.
          Depth predictedDepth = newDepth - reduction<NonPV>(depth, moveCount);
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
              && bestValue > value_mated_in(PLY_MAX)
              && pos.see_sign(move) < 0)
          {
              if (SpNode)
                  lock_grab(&(sp->lock));

              continue;
          }
      }

      // Step 13. Make the move
      pos.do_move(move, st, ci, moveIsCheck);

      // Step extra. pv search (only in PV nodes)
      // The first move in list is the expected PV
      if (PvNode && moveCount == 1)
          value = -search<PV>(pos, ss+1, -beta, -alpha, newDepth, ply+1);
      else
      {
          // Step 14. Reduced depth search
          // If the move fails high will be re-searched at full depth.
          bool doFullDepthSearch = true;

          if (    depth >= 3 * ONE_PLY
              && !captureOrPromotion
              && !dangerous
              && !move_is_castle(move)
              &&  ss->killers[0] != move
              &&  ss->killers[1] != move)
          {
              ss->reduction = reduction<PvNode>(depth, moveCount);

              if (ss->reduction)
              {
                  alpha = SpNode ? sp->alpha : alpha;
                  Depth d = newDepth - ss->reduction;
                  value = -search<NonPV>(pos, ss+1, -(alpha+1), -alpha, d, ply+1);

                  doFullDepthSearch = (value > alpha);
              }
              ss->reduction = DEPTH_ZERO; // Restore original reduction
          }

          // Step 15. Full depth search
          if (doFullDepthSearch)
          {
              alpha = SpNode ? sp->alpha : alpha;
              value = -search<NonPV>(pos, ss+1, -(alpha+1), -alpha, newDepth, ply+1);

              // Step extra. pv search (only in PV nodes)
              // Search only for possible new PV nodes, if instead value >= beta then
              // parent node fails low with value <= alpha and tries another move.
              if (PvNode && value > alpha && value < beta)
                  value = -search<PV>(pos, ss+1, -beta, -alpha, newDepth, ply+1);
          }
      }

      // Step 16. Undo move
      pos.undo_move(move);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // Step 17. Check for new best move
      if (SpNode)
      {
          lock_grab(&(sp->lock));
          bestValue = sp->bestValue;
          alpha = sp->alpha;
      }

      if (value > bestValue && !(SpNode && ThreadsMgr.cutoff_at_splitpoint(threadID)))
      {
          bestValue = value;

          if (SpNode)
              sp->bestValue = value;

          if (value > alpha)
          {
              if (PvNode && value < beta) // We want always alpha < beta
              {
                  alpha = value;

                  if (SpNode)
                      sp->alpha = value;
              }
              else if (SpNode)
                  sp->betaCutoff = true;

              if (value == value_mate_in(ply + 1))
                  ss->mateKiller = move;

              ss->bestMove = move;

              if (SpNode)
                  sp->parentSstack->bestMove = move;
          }
      }

      // Step 18. Check for split
      if (   !SpNode
          && depth >= ThreadsMgr.min_split_depth()
          && ThreadsMgr.active_threads() > 1
          && bestValue < beta
          && ThreadsMgr.available_thread_exists(threadID)
          && !StopRequest
          && !ThreadsMgr.cutoff_at_splitpoint(threadID)
          && Iteration <= 99)
          ThreadsMgr.split<FakeSplit>(pos, ss, ply, &alpha, beta, &bestValue, depth,
                                      threatMove, mateThreat, moveCount, &mp, PvNode);
    }

    // Step 19. Check for mate and stalemate
    // All legal moves have been searched and if there are
    // no legal moves, it must be mate or stalemate.
    // If one move was excluded return fail low score.
    if (!SpNode && !moveCount)
        return excludedMove ? oldAlpha : isCheck ? value_mated_in(ply) : VALUE_DRAW;

    // Step 20. Update tables
    // If the search is not aborted, update the transposition table,
    // history counters, and killer moves.
    if (!SpNode && !StopRequest && !ThreadsMgr.cutoff_at_splitpoint(threadID))
    {
        move = bestValue <= oldAlpha ? MOVE_NONE : ss->bestMove;
        vt   = bestValue <= oldAlpha ? VALUE_TYPE_UPPER
             : bestValue >= beta ? VALUE_TYPE_LOWER : VALUE_TYPE_EXACT;

        TT.store(posKey, value_to_tt(bestValue, ply), vt, depth, move, ss->eval, ss->evalMargin);

        // Update killers and history only for non capture moves that fails high
        if (    bestValue >= beta
            && !pos.move_is_capture_or_promotion(move))
        {
            update_history(pos, move, depth, movesSearched, moveCount);
            update_killers(move, ss);
        }
    }

    if (SpNode)
    {
        // Here we have the lock still grabbed
        sp->slaves[threadID] = 0;
        sp->nodes += pos.nodes_searched();
        lock_release(&(sp->lock));
    }

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
  }

  // qsearch() is the quiescence search function, which is called by the main
  // search function when the remaining depth is zero (or, to be more precise,
  // less than ONE_PLY).

  template <NodeType PvNode>
  Value qsearch(Position& pos, SearchStack* ss, Value alpha, Value beta, Depth depth, int ply) {

    assert(alpha >= -VALUE_INFINITE && alpha <= VALUE_INFINITE);
    assert(beta >= -VALUE_INFINITE && beta <= VALUE_INFINITE);
    assert(PvNode || alpha == beta - 1);
    assert(depth <= 0);
    assert(ply > 0 && ply < PLY_MAX);
    assert(pos.thread() >= 0 && pos.thread() < ThreadsMgr.active_threads());

    StateInfo st;
    Move ttMove, move;
    Value bestValue, value, evalMargin, futilityValue, futilityBase;
    bool isCheck, enoughMaterial, moveIsCheck, evasionPrunable;
    const TTEntry* tte;
    Depth ttDepth;
    Value oldAlpha = alpha;

    ss->bestMove = ss->currentMove = MOVE_NONE;

    // Check for an instant draw or maximum ply reached
    if (pos.is_draw() || ply >= PLY_MAX - 1)
        return VALUE_DRAW;

    // Decide whether or not to include checks, this fixes also the type of
    // TT entry depth that we are going to use. Note that in qsearch we use
    // only two types of depth in TT: DEPTH_QS_CHECKS or DEPTH_QS_NO_CHECKS.
    isCheck = pos.is_check();
    ttDepth = (isCheck || depth >= DEPTH_QS_CHECKS ? DEPTH_QS_CHECKS : DEPTH_QS_NO_CHECKS);

    // Transposition table lookup. At PV nodes, we don't use the TT for
    // pruning, but only for move ordering.
    tte = TT.retrieve(pos.get_key());
    ttMove = (tte ? tte->move() : MOVE_NONE);

    if (!PvNode && tte && ok_to_use_TT(tte, ttDepth, beta, ply))
    {
        ss->bestMove = ttMove; // Can be MOVE_NONE
        return value_from_tt(tte->value(), ply);
    }

    // Evaluate the position statically
    if (isCheck)
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

        update_gains(pos, (ss-1)->currentMove, (ss-1)->eval, ss->eval);

        // Stand pat. Return immediately if static value is at least beta
        if (bestValue >= beta)
        {
            if (!tte)
                TT.store(pos.get_key(), value_to_tt(bestValue, ply), VALUE_TYPE_LOWER, DEPTH_NONE, MOVE_NONE, ss->eval, evalMargin);

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
    MovePicker mp(pos, ttMove, depth, H);
    CheckInfo ci(pos);

    // Loop through the moves until no moves remain or a beta cutoff occurs
    while (   alpha < beta
           && (move = mp.get_next_move()) != MOVE_NONE)
    {
      assert(move_is_ok(move));

      moveIsCheck = pos.move_is_check(move, ci);

      // Futility pruning
      if (   !PvNode
          && !isCheck
          && !moveIsCheck
          &&  move != ttMove
          &&  enoughMaterial
          && !move_is_promotion(move)
          && !pos.move_is_passed_pawn_push(move))
      {
          futilityValue =  futilityBase
                         + pos.endgame_value_of_piece_on(move_to(move))
                         + (move_is_ep(move) ? PawnValueEndgame : VALUE_ZERO);

          if (futilityValue < alpha)
          {
              if (futilityValue > bestValue)
                  bestValue = futilityValue;
              continue;
          }
      }

      // Detect non-capture evasions that are candidate to be pruned
      evasionPrunable =   isCheck
                       && bestValue > value_mated_in(PLY_MAX)
                       && !pos.move_is_capture(move)
                       && !pos.can_castle(pos.side_to_move());

      // Don't search moves with negative SEE values
      if (   !PvNode
          && (!isCheck || evasionPrunable)
          &&  move != ttMove
          && !move_is_promotion(move)
          &&  pos.see_sign(move) < 0)
          continue;

      // Don't search useless checks
      if (   !PvNode
          && !isCheck
          &&  moveIsCheck
          &&  move != ttMove
          && !pos.move_is_capture_or_promotion(move)
          &&  ss->eval + PawnValueMidgame / 4 < beta
          && !check_is_dangerous(pos, move, futilityBase, beta, &bestValue))
      {
          if (ss->eval + PawnValueMidgame / 4 > bestValue)
              bestValue = ss->eval + PawnValueMidgame / 4;

          continue;
      }

      // Update current move
      ss->currentMove = move;

      // Make and search the move
      pos.do_move(move, st, ci, moveIsCheck);
      value = -qsearch<PvNode>(pos, ss+1, -beta, -alpha, depth-ONE_PLY, ply+1);
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
    if (isCheck && bestValue == -VALUE_INFINITE)
        return value_mated_in(ply);

    // Update transposition table
    ValueType vt = (bestValue <= oldAlpha ? VALUE_TYPE_UPPER : bestValue >= beta ? VALUE_TYPE_LOWER : VALUE_TYPE_EXACT);
    TT.store(pos.get_key(), value_to_tt(bestValue, ply), vt, ttDepth, ss->bestMove, ss->eval, evalMargin);

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
    b = kingAtt & ~pos.pieces_of_color(them) & ~newAtt & ~(1ULL << to);

    if (!(b && (b & (b - 1))))
        return true;

    // Rule 2. Queen contact check is very dangerous
    if (   type_of_piece(pc) == QUEEN
        && bit_is_set(kingAtt, to))
        return true;

    // Rule 3. Creating new double threats with checks
    b = pos.pieces_of_color(them) & newAtt & ~oldAtt & ~(1ULL << ksq);

    while (b)
    {
        victimSq = pop_1st_bit(&b);
        futilityValue = futilityBase + pos.endgame_value_of_piece_on(victimSq);

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
    Piece p;

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
    if (   piece_is_slider(pos.piece_on(f2))
        && bit_is_set(squares_between(f2, t2), f1))
      return true;

    // Case 4: The destination square for m2 is defended by the moving piece in m1
    p = pos.piece_on(t1);
    if (bit_is_set(pos.attacks_from(p, t1), t2))
        return true;

    // Case 5: Discovered check, checking piece is the piece moved in m1
    if (    piece_is_slider(p)
        &&  bit_is_set(squares_between(t1, pos.king_square(pos.side_to_move())), f2)
        && !bit_is_set(squares_between(t1, pos.king_square(pos.side_to_move())), t2))
    {
        // discovered_check_candidates() works also if the Position's side to
        // move is the opposite of the checking piece.
        Color them = opposite_color(pos.side_to_move());
        Bitboard dcCandidates = pos.discovered_check_candidates(them);

        if (bit_is_set(dcCandidates, f2))
            return true;
    }
    return false;
  }


  // value_is_mate() checks if the given value is a mate one eventually
  // compensated for the ply.

  bool value_is_mate(Value value) {

    assert(abs(value) <= VALUE_INFINITE);

    return   value <= value_mated_in(PLY_MAX)
          || value >= value_mate_in(PLY_MAX);
  }


  // value_to_tt() adjusts a mate score from "plies to mate from the root" to
  // "plies to mate from the current ply".  Non-mate scores are unchanged.
  // The function is called before storing a value to the transposition table.

  Value value_to_tt(Value v, int ply) {

    if (v >= value_mate_in(PLY_MAX))
      return v + ply;

    if (v <= value_mated_in(PLY_MAX))
      return v - ply;

    return v;
  }


  // value_from_tt() is the inverse of value_to_tt(): It adjusts a mate score from
  // the transposition table to a mate score corrected for the current ply.

  Value value_from_tt(Value v, int ply) {

    if (v >= value_mate_in(PLY_MAX))
      return v - ply;

    if (v <= value_mated_in(PLY_MAX))
      return v + ply;

    return v;
  }


  // extension() decides whether a move should be searched with normal depth,
  // or with extended depth. Certain classes of moves (checking moves, in
  // particular) are searched with bigger depth than ordinary moves and in
  // any case are marked as 'dangerous'. Note that also if a move is not
  // extended, as example because the corresponding UCI option is set to zero,
  // the move is marked as 'dangerous' so, at least, we avoid to prune it.
  template <NodeType PvNode>
  Depth extension(const Position& pos, Move m, bool captureOrPromotion, bool moveIsCheck,
                  bool singleEvasion, bool mateThreat, bool* dangerous) {

    assert(m != MOVE_NONE);

    Depth result = DEPTH_ZERO;
    *dangerous = moveIsCheck | singleEvasion | mateThreat;

    if (*dangerous)
    {
        if (moveIsCheck && pos.see_sign(m) >= 0)
            result += CheckExtension[PvNode];

        if (singleEvasion)
            result += SingleEvasionExtension[PvNode];

        if (mateThreat)
            result += MateThreatExtension[PvNode];
    }

    if (pos.type_of_piece_on(move_from(m)) == PAWN)
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
        && pos.type_of_piece_on(move_to(m)) != PAWN
        && (  pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK)
            - pos.midgame_value_of_piece_on(move_to(m)) == VALUE_ZERO)
        && !move_is_promotion(m)
        && !move_is_ep(m))
    {
        result += PawnEndgameExtension[PvNode];
        *dangerous = true;
    }

    if (   PvNode
        && captureOrPromotion
        && pos.type_of_piece_on(move_to(m)) != PAWN
        && pos.see_sign(m) >= 0)
    {
        result += ONE_PLY / 2;
        *dangerous = true;
    }

    return Min(result, ONE_PLY);
  }


  // connected_threat() tests whether it is safe to forward prune a move or if
  // is somehow coonected to the threat move returned by null search.

  bool connected_threat(const Position& pos, Move m, Move threat) {

    assert(move_is_ok(m));
    assert(threat && move_is_ok(threat));
    assert(!pos.move_is_check(m));
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
    // value of the threatening piece, don't prune move which defend it.
    if (   pos.move_is_capture(threat)
        && (   pos.midgame_value_of_piece_on(tfrom) >= pos.midgame_value_of_piece_on(tto)
            || pos.type_of_piece_on(tfrom) == KING)
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
              || v >= Max(value_mate_in(PLY_MAX), beta)
              || v < Min(value_mated_in(PLY_MAX), beta))

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

    H.success(pos.piece_on(move_from(move)), move_to(move), depth);

    for (int i = 0; i < moveCount - 1; i++)
    {
        m = movesSearched[i];

        assert(m != move);

        if (!pos.move_is_capture_or_promotion(m))
            H.failure(pos.piece_on(move_from(m)), move_to(m), depth);
    }
  }


  // update_killers() add a good move that produced a beta-cutoff
  // among the killer moves of that ply.

  void update_killers(Move m, SearchStack* ss) {

    if (m == ss->killers[0])
        return;

    ss->killers[1] = ss->killers[0];
    ss->killers[0] = m;
  }


  // update_gains() updates the gains table of a non-capture move given
  // the static position evaluation before and after the move.

  void update_gains(const Position& pos, Move m, Value before, Value after) {

    if (   m != MOVE_NULL
        && before != VALUE_NONE
        && after != VALUE_NONE
        && pos.captured_piece_type() == PIECE_TYPE_NONE
        && !move_is_special(m))
        H.set_gain(pos.piece_on(move_to(m)), move_to(m), -(before + after));
  }


  // init_ss_array() does a fast reset of the first entries of a SearchStack
  // array and of all the excludedMove and skipNullMove entries.

  void init_ss_array(SearchStack* ss, int size) {

    for (int i = 0; i < size; i++, ss++)
    {
        ss->excludedMove = MOVE_NONE;
        ss->skipNullMove = false;
        ss->reduction = DEPTH_ZERO;
        ss->sp = NULL;

        if (i < 3)
            ss->killers[0] = ss->killers[1] = ss->mateKiller = MOVE_NONE;
    }
  }


  // value_to_uci() converts a value to a string suitable for use with the UCI
  // protocol specifications:
  //
  // cp <x>     The score from the engine's point of view in centipawns.
  // mate <y>   Mate in y moves, not plies. If the engine is getting mated
  //            use negative values for y.

  std::string value_to_uci(Value v) {

    std::stringstream s;

    if (abs(v) < VALUE_MATE - PLY_MAX * ONE_PLY)
      s << "cp " << int(v) * 100 / int(PawnValueMidgame); // Scale to centipawns
    else
      s << "mate " << (v > 0 ? (VALUE_MATE - v + 1) / 2 : -(VALUE_MATE + v) / 2 );

    return s.str();
  }


  // current_search_time() returns the number of milliseconds which have passed
  // since the beginning of the current search.

  int current_search_time() {

    return get_system_time() - SearchStartTime;
  }


  // nps() computes the current nodes/second count

  int nps(const Position& pos) {

    int t = current_search_time();
    return (t > 0 ? int((pos.nodes_searched() * 1000) / t) : 0);
  }


  // poll() performs two different functions: It polls for user input, and it
  // looks at the time consumed so far and decides if it's time to abort the
  // search.

  void poll(const Position& pos) {

    static int lastInfoTime;
    int t = current_search_time();

    //  Poll for input
    if (data_available())
    {
        // We are line oriented, don't read single chars
        std::string command;

        if (!std::getline(std::cin, command))
            command = "quit";

        if (command == "quit")
        {
            // Quit the program as soon as possible
            Pondering = false;
            QuitRequest = StopRequest = true;
            return;
        }
        else if (command == "stop")
        {
            // Stop calculating as soon as possible, but still send the "bestmove"
            // and possibly the "ponder" token when finishing the search.
            Pondering = false;
            StopRequest = true;
        }
        else if (command == "ponderhit")
        {
            // The opponent has played the expected move. GUI sends "ponderhit" if
            // we were told to ponder on the same move the opponent has played. We
            // should continue searching but switching from pondering to normal search.
            Pondering = false;

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

        if (dbg_show_mean)
            dbg_print_mean();

        if (dbg_show_hit_rate)
            dbg_print_hit_rate();

        // Send info on searched nodes as soon as we return to root
        SendSearchedNodes = true;
    }

    // Should we stop the search?
    if (Pondering)
        return;

    bool stillAtFirstMove =    FirstRootMove
                           && !AspirationFailLow
                           &&  t > TimeMgr.available_time();

    bool noMoreTime =   t > TimeMgr.maximum_time()
                     || stillAtFirstMove;

    if (   (UseTimeManagement && noMoreTime)
        || (ExactMaxTime && t >= ExactMaxTime)
        || (MaxNodes && pos.nodes_searched() >= MaxNodes)) // FIXME
        StopRequest = true;
  }


  // wait_for_stop_or_ponderhit() is called when the maximum depth is reached
  // while the program is pondering. The point is to work around a wrinkle in
  // the UCI protocol: When pondering, the engine is not allowed to give a
  // "bestmove" before the GUI sends it a "stop" or "ponderhit" command.
  // We simply wait here until one of these commands is sent, and return,
  // after which the bestmove and pondermove will be printed.

  void wait_for_stop_or_ponderhit() {

    std::string command;

    while (true)
    {
        // Wait for a command from stdin
        if (!std::getline(std::cin, command))
            command = "quit";

        if (command == "quit")
        {
            QuitRequest = true;
            break;
        }
        else if (command == "ponderhit" || command == "stop")
            break;
    }
  }


  // init_thread() is the function which is called when a new thread is
  // launched. It simply calls the idle_loop() function with the supplied
  // threadID. There are two versions of this function; one for POSIX
  // threads and one for Windows threads.

#if !defined(_MSC_VER)

  void* init_thread(void* threadID) {

    ThreadsMgr.idle_loop(*(int*)threadID, NULL);
    return NULL;
  }

#else

  DWORD WINAPI init_thread(LPVOID threadID) {

    ThreadsMgr.idle_loop(*(int*)threadID, NULL);
    return 0;
  }

#endif


  /// The ThreadsManager class


  // read_uci_options() updates number of active threads and other internal
  // parameters according to the UCI options values. It is called before
  // to start a new search.

  void ThreadsManager::read_uci_options() {

    maxThreadsPerSplitPoint = Options["Maximum Number of Threads per Split Point"].value<int>();
    minimumSplitDepth       = Options["Minimum Split Depth"].value<int>() * ONE_PLY;
    useSleepingThreads      = Options["Use Sleeping Threads"].value<bool>();
    activeThreads           = Options["Threads"].value<int>();
  }


  // idle_loop() is where the threads are parked when they have no work to do.
  // The parameter 'sp', if non-NULL, is a pointer to an active SplitPoint
  // object for which the current thread is the master.

  void ThreadsManager::idle_loop(int threadID, SplitPoint* sp) {

    assert(threadID >= 0 && threadID < MAX_THREADS);

    int i;
    bool allFinished = false;

    while (true)
    {
        // Slave threads can exit as soon as AllThreadsShouldExit raises,
        // master should exit as last one.
        if (allThreadsShouldExit)
        {
            assert(!sp);
            threads[threadID].state = THREAD_TERMINATED;
            return;
        }

        // If we are not thinking, wait for a condition to be signaled
        // instead of wasting CPU time polling for work.
        while (   threadID >= activeThreads || threads[threadID].state == THREAD_INITIALIZING
               || (useSleepingThreads && threads[threadID].state == THREAD_AVAILABLE))
        {
            assert(!sp || useSleepingThreads);
            assert(threadID != 0 || useSleepingThreads);

            if (threads[threadID].state == THREAD_INITIALIZING)
                threads[threadID].state = THREAD_AVAILABLE;

            // Grab the lock to avoid races with wake_sleeping_thread()
            lock_grab(&sleepLock[threadID]);

            // If we are master and all slaves have finished do not go to sleep
            for (i = 0; sp && i < activeThreads && !sp->slaves[i]; i++) {}
            allFinished = (i == activeThreads);

            if (allFinished || allThreadsShouldExit)
            {
                lock_release(&sleepLock[threadID]);
                break;
            }

            // Do sleep here after retesting sleep conditions
            if (threadID >= activeThreads || threads[threadID].state == THREAD_AVAILABLE)
                cond_wait(&sleepCond[threadID], &sleepLock[threadID]);

            lock_release(&sleepLock[threadID]);
        }

        // If this thread has been assigned work, launch a search
        if (threads[threadID].state == THREAD_WORKISWAITING)
        {
            assert(!allThreadsShouldExit);

            threads[threadID].state = THREAD_SEARCHING;

            // Here we call search() with SplitPoint template parameter set to true
            SplitPoint* tsp = threads[threadID].splitPoint;
            Position pos(*tsp->pos, threadID);
            SearchStack* ss = tsp->sstack[threadID] + 1;
            ss->sp = tsp;

            if (tsp->pvNode)
                search<PV, true>(pos, ss, tsp->alpha, tsp->beta, tsp->depth, tsp->ply);
            else
                search<NonPV, true>(pos, ss, tsp->alpha, tsp->beta, tsp->depth, tsp->ply);

            assert(threads[threadID].state == THREAD_SEARCHING);

            threads[threadID].state = THREAD_AVAILABLE;

            // Wake up master thread so to allow it to return from the idle loop in
            // case we are the last slave of the split point.
            if (useSleepingThreads && threadID != tsp->master && threads[tsp->master].state == THREAD_AVAILABLE)
                wake_sleeping_thread(tsp->master);
        }

        // If this thread is the master of a split point and all slaves have
        // finished their work at this split point, return from the idle loop.
        for (i = 0; sp && i < activeThreads && !sp->slaves[i]; i++) {}
        allFinished = (i == activeThreads);

        if (allFinished)
        {
            // Because sp->slaves[] is reset under lock protection,
            // be sure sp->lock has been released before to return.
            lock_grab(&(sp->lock));
            lock_release(&(sp->lock));

            // In helpful master concept a master can help only a sub-tree, and
            // because here is all finished is not possible master is booked.
            assert(threads[threadID].state == THREAD_AVAILABLE);

            threads[threadID].state = THREAD_SEARCHING;
            return;
        }
    }
  }


  // init_threads() is called during startup. It launches all helper threads,
  // and initializes the split point stack and the global locks and condition
  // objects.

  void ThreadsManager::init_threads() {

    int i, arg[MAX_THREADS];
    bool ok;

    // Initialize global locks
    lock_init(&mpLock);

    for (i = 0; i < MAX_THREADS; i++)
    {
        lock_init(&sleepLock[i]);
        cond_init(&sleepCond[i]);
    }

    // Initialize splitPoints[] locks
    for (i = 0; i < MAX_THREADS; i++)
        for (int j = 0; j < MAX_ACTIVE_SPLIT_POINTS; j++)
            lock_init(&(threads[i].splitPoints[j].lock));

    // Will be set just before program exits to properly end the threads
    allThreadsShouldExit = false;

    // Threads will be put all threads to sleep as soon as created
    activeThreads = 1;

    // All threads except the main thread should be initialized to THREAD_INITIALIZING
    threads[0].state = THREAD_SEARCHING;
    for (i = 1; i < MAX_THREADS; i++)
        threads[i].state = THREAD_INITIALIZING;

    // Launch the helper threads
    for (i = 1; i < MAX_THREADS; i++)
    {
        arg[i] = i;

#if !defined(_MSC_VER)
        pthread_t pthread[1];
        ok = (pthread_create(pthread, NULL, init_thread, (void*)(&arg[i])) == 0);
        pthread_detach(pthread[0]);
#else
        ok = (CreateThread(NULL, 0, init_thread, (LPVOID)(&arg[i]), 0, NULL) != NULL);
#endif
        if (!ok)
        {
            cout << "Failed to create thread number " << i << endl;
            exit(EXIT_FAILURE);
        }

        // Wait until the thread has finished launching and is gone to sleep
        while (threads[i].state == THREAD_INITIALIZING) {}
    }
  }


  // exit_threads() is called when the program exits. It makes all the
  // helper threads exit cleanly.

  void ThreadsManager::exit_threads() {

    allThreadsShouldExit = true; // Let the woken up threads to exit idle_loop()

    // Wake up all the threads and waits for termination
    for (int i = 1; i < MAX_THREADS; i++)
    {
        wake_sleeping_thread(i);
        while (threads[i].state != THREAD_TERMINATED) {}
    }

    // Now we can safely destroy the locks
    for (int i = 0; i < MAX_THREADS; i++)
        for (int j = 0; j < MAX_ACTIVE_SPLIT_POINTS; j++)
            lock_destroy(&(threads[i].splitPoints[j].lock));

    lock_destroy(&mpLock);

    // Now we can safely destroy the wait conditions
    for (int i = 0; i < MAX_THREADS; i++)
    {
        lock_destroy(&sleepLock[i]);
        cond_destroy(&sleepCond[i]);
    }
  }


  // cutoff_at_splitpoint() checks whether a beta cutoff has occurred in
  // the thread's currently active split point, or in some ancestor of
  // the current split point.

  bool ThreadsManager::cutoff_at_splitpoint(int threadID) const {

    assert(threadID >= 0 && threadID < activeThreads);

    SplitPoint* sp = threads[threadID].splitPoint;

    for ( ; sp && !sp->betaCutoff; sp = sp->parent) {}
    return sp != NULL;
  }


  // thread_is_available() checks whether the thread with threadID "slave" is
  // available to help the thread with threadID "master" at a split point. An
  // obvious requirement is that "slave" must be idle. With more than two
  // threads, this is not by itself sufficient:  If "slave" is the master of
  // some active split point, it is only available as a slave to the other
  // threads which are busy searching the split point at the top of "slave"'s
  // split point stack (the "helpful master concept" in YBWC terminology).

  bool ThreadsManager::thread_is_available(int slave, int master) const {

    assert(slave >= 0 && slave < activeThreads);
    assert(master >= 0 && master < activeThreads);
    assert(activeThreads > 1);

    if (threads[slave].state != THREAD_AVAILABLE || slave == master)
        return false;

    // Make a local copy to be sure doesn't change under our feet
    int localActiveSplitPoints = threads[slave].activeSplitPoints;

    // No active split points means that the thread is available as
    // a slave for any other thread.
    if (localActiveSplitPoints == 0 || activeThreads == 2)
        return true;

    // Apply the "helpful master" concept if possible. Use localActiveSplitPoints
    // that is known to be > 0, instead of threads[slave].activeSplitPoints that
    // could have been set to 0 by another thread leading to an out of bound access.
    if (threads[slave].splitPoints[localActiveSplitPoints - 1].slaves[master])
        return true;

    return false;
  }


  // available_thread_exists() tries to find an idle thread which is available as
  // a slave for the thread with threadID "master".

  bool ThreadsManager::available_thread_exists(int master) const {

    assert(master >= 0 && master < activeThreads);
    assert(activeThreads > 1);

    for (int i = 0; i < activeThreads; i++)
        if (thread_is_available(i, master))
            return true;

    return false;
  }


  // split() does the actual work of distributing the work at a node between
  // several available threads. If it does not succeed in splitting the
  // node (because no idle threads are available, or because we have no unused
  // split point objects), the function immediately returns. If splitting is
  // possible, a SplitPoint object is initialized with all the data that must be
  // copied to the helper threads and we tell our helper threads that they have
  // been assigned work. This will cause them to instantly leave their idle loops and
  // call search().When all threads have returned from search() then split() returns.

  template <bool Fake>
  void ThreadsManager::split(Position& pos, SearchStack* ss, int ply, Value* alpha,
                             const Value beta, Value* bestValue, Depth depth, Move threatMove,
                             bool mateThreat, int moveCount, MovePicker* mp, bool pvNode) {
    assert(pos.is_ok());
    assert(ply > 0 && ply < PLY_MAX);
    assert(*bestValue >= -VALUE_INFINITE);
    assert(*bestValue <= *alpha);
    assert(*alpha < beta);
    assert(beta <= VALUE_INFINITE);
    assert(depth > DEPTH_ZERO);
    assert(pos.thread() >= 0 && pos.thread() < activeThreads);
    assert(activeThreads > 1);

    int i, master = pos.thread();
    Thread& masterThread = threads[master];

    lock_grab(&mpLock);

    // If no other thread is available to help us, or if we have too many
    // active split points, don't split.
    if (   !available_thread_exists(master)
        || masterThread.activeSplitPoints >= MAX_ACTIVE_SPLIT_POINTS)
    {
        lock_release(&mpLock);
        return;
    }

    // Pick the next available split point object from the split point stack
    SplitPoint& splitPoint = masterThread.splitPoints[masterThread.activeSplitPoints++];

    // Initialize the split point object
    splitPoint.parent = masterThread.splitPoint;
    splitPoint.master = master;
    splitPoint.betaCutoff = false;
    splitPoint.ply = ply;
    splitPoint.depth = depth;
    splitPoint.threatMove = threatMove;
    splitPoint.mateThreat = mateThreat;
    splitPoint.alpha = *alpha;
    splitPoint.beta = beta;
    splitPoint.pvNode = pvNode;
    splitPoint.bestValue = *bestValue;
    splitPoint.mp = mp;
    splitPoint.moveCount = moveCount;
    splitPoint.pos = &pos;
    splitPoint.nodes = 0;
    splitPoint.parentSstack = ss;
    for (i = 0; i < activeThreads; i++)
        splitPoint.slaves[i] = 0;

    masterThread.splitPoint = &splitPoint;

    // If we are here it means we are not available
    assert(masterThread.state != THREAD_AVAILABLE);

    int workersCnt = 1; // At least the master is included

    // Allocate available threads setting state to THREAD_BOOKED
    for (i = 0; !Fake && i < activeThreads && workersCnt < maxThreadsPerSplitPoint; i++)
        if (thread_is_available(i, master))
        {
            threads[i].state = THREAD_BOOKED;
            threads[i].splitPoint = &splitPoint;
            splitPoint.slaves[i] = 1;
            workersCnt++;
        }

    assert(Fake || workersCnt > 1);

    // We can release the lock because slave threads are already booked and master is not available
    lock_release(&mpLock);

    // Tell the threads that they have work to do. This will make them leave
    // their idle loop. But before copy search stack tail for each thread.
    for (i = 0; i < activeThreads; i++)
        if (i == master || splitPoint.slaves[i])
        {
            memcpy(splitPoint.sstack[i], ss - 1, 4 * sizeof(SearchStack));

            assert(i == master || threads[i].state == THREAD_BOOKED);

            threads[i].state = THREAD_WORKISWAITING; // This makes the slave to exit from idle_loop()

            if (useSleepingThreads && i != master)
                wake_sleeping_thread(i);
        }

    // Everything is set up. The master thread enters the idle loop, from
    // which it will instantly launch a search, because its state is
    // THREAD_WORKISWAITING.  We send the split point as a second parameter to the
    // idle loop, which means that the main thread will return from the idle
    // loop when all threads have finished their work at this split point.
    idle_loop(master, &splitPoint);

    // We have returned from the idle loop, which means that all threads are
    // finished. Update alpha and bestValue, and return.
    lock_grab(&mpLock);

    *alpha = splitPoint.alpha;
    *bestValue = splitPoint.bestValue;
    masterThread.activeSplitPoints--;
    masterThread.splitPoint = splitPoint.parent;
    pos.set_nodes_searched(pos.nodes_searched() + splitPoint.nodes);

    lock_release(&mpLock);
  }


  // wake_sleeping_thread() wakes up the thread with the given threadID
  // when it is time to start a new search.

  void ThreadsManager::wake_sleeping_thread(int threadID) {

     lock_grab(&sleepLock[threadID]);
     cond_signal(&sleepCond[threadID]);
     lock_release(&sleepLock[threadID]);
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

  // extract_pv_from_tt() builds a PV by adding moves from the transposition table.
  // We consider also failing high nodes and not only VALUE_TYPE_EXACT nodes. This
  // allow to always have a ponder move even when we fail high at root and also a
  // long PV to print that is important for position analysis.

  void RootMove::extract_pv_from_tt(Position& pos) {

    StateInfo state[PLY_MAX_PLUS_2], *st = state;
    TTEntry* tte;
    int ply = 1;

    assert(pv[0] != MOVE_NONE && move_is_legal(pos, pv[0]));

    pos.do_move(pv[0], *st++);

    while (   (tte = TT.retrieve(pos.get_key())) != NULL
           && tte->move() != MOVE_NONE
           && move_is_legal(pos, tte->move())
           && ply < PLY_MAX
           && (!pos.is_draw() || ply < 2))
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

    assert(pv[0] != MOVE_NONE && move_is_legal(pos, pv[0]));

    do {
        k = pos.get_key();
        tte = TT.retrieve(k);

        // Don't overwrite exsisting correct entries
        if (!tte || tte->move() != pv[ply])
        {
            v = (pos.is_check() ? VALUE_NONE : evaluate(pos, m));
            TT.store(k, VALUE_NONE, VALUE_TYPE_NONE, DEPTH_NONE, pv[ply], v, m);
        }
        pos.do_move(pv[ply], *st++);

    } while (pv[++ply] != MOVE_NONE);

    do pos.undo_move(pv[--ply]); while (ply);
  }

  // pv_info_to_uci() returns a string with information on the current PV line
  // formatted according to UCI specification and eventually writes the info
  // to a log file. It is called at each iteration or after a new pv is found.

  std::string RootMove::pv_info_to_uci(const Position& pos, Value alpha, Value beta, int pvLine) {

    std::stringstream s, l;
    Move* m = pv;

    while (*m != MOVE_NONE)
        l << *m++ << " ";

    s << "info depth " << Iteration // FIXME
      << " seldepth " << int(m - pv)
      << " multipv " << pvLine + 1
      << " score " << value_to_uci(pv_score)
      << (pv_score >= beta ? " lowerbound" : pv_score <= alpha ? " upperbound" : "")
      << " time "  << current_search_time()
      << " nodes " << pos.nodes_searched()
      << " nps "   << nps(pos)
      << " pv "    << l.str();

    if (UseLogFile && pvLine == 0)
    {
        ValueType t = pv_score >= beta  ? VALUE_TYPE_LOWER :
                      pv_score <= alpha ? VALUE_TYPE_UPPER : VALUE_TYPE_EXACT;

        LogFile << pretty_pv(pos, current_search_time(), Iteration, pv_score, t, pv) << endl;
    }
    return s.str();
  }


  RootMoveList::RootMoveList(Position& pos, Move searchMoves[]) {

    SearchStack ss[PLY_MAX_PLUS_2];
    MoveStack mlist[MOVES_MAX];
    StateInfo st;
    Move* sm;

    // Initialize search stack
    init_ss_array(ss, PLY_MAX_PLUS_2);
    ss[0].eval = ss[0].evalMargin = VALUE_NONE;

    // Generate all legal moves
    MoveStack* last = generate_moves(pos, mlist);

    // Add each move to the RootMoveList's vector
    for (MoveStack* cur = mlist; cur != last; cur++)
    {
        // If we have a searchMoves[] list then verify cur->move
        // is in the list before to add it.
        for (sm = searchMoves; *sm && *sm != cur->move; sm++) {}

        if (searchMoves[0] && *sm != cur->move)
            continue;

        // Find a quick score for the move and add to the list
        pos.do_move(cur->move, st);

        RootMove rm;
        rm.pv[0] = ss[0].currentMove = cur->move;
        rm.pv[1] = MOVE_NONE;
        rm.pv_score = -qsearch<PV>(pos, ss+1, -VALUE_INFINITE, VALUE_INFINITE, DEPTH_ZERO, 1);
        push_back(rm);

        pos.undo_move(cur->move);
    }
    sort();
  }

  // Score root moves using the standard way used in main search, the moves
  // are scored according to the order in which are returned by MovePicker.
  // This is the second order score that is used to compare the moves when
  // the first order pv scores of both moves are equal.

  void RootMoveList::set_non_pv_scores(const Position& pos)
  {
      Move move;
      Value score = VALUE_ZERO;
      MovePicker mp(pos, MOVE_NONE, ONE_PLY, H);

      while ((move = mp.get_next_move()) != MOVE_NONE)
          for (Base::iterator it = begin(); it != end(); ++it)
              if (it->pv[0] == move)
              {
                  it->non_pv_score = score--;
                  break;
              }
  }

} // namespace
