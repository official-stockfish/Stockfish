/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008 Marco Costalba

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
#include <fstream>
#include <iostream>
#include <sstream>

#include "book.h"
#include "evaluate.h"
#include "history.h"
#include "misc.h"
#include "movepick.h"
#include "san.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "ucioption.h"


////
//// Local definitions
////

namespace {

  /// Types

  // The RootMove class is used for moves at the root at the tree.  For each
  // root move, we store a score, a node count, and a PV (really a refutation
  // in the case of moves which fail low).

  struct RootMove {

    RootMove();
    bool operator<(const RootMove&); // used to sort

    Move move;
    Value score;
    int64_t nodes, cumulativeNodes;
    Move pv[PLY_MAX_PLUS_2];
  };


  // The RootMoveList class is essentially an array of RootMove objects, with
  // a handful of methods for accessing the data in the individual moves.

  class RootMoveList {

  public:
    RootMoveList(Position &pos, Move searchMoves[]);
    inline Move get_move(int moveNum) const;
    inline Value get_move_score(int moveNum) const;
    inline void set_move_score(int moveNum, Value score);
    inline void set_move_nodes(int moveNum, int64_t nodes);
    void set_move_pv(int moveNum, const Move pv[]);
    inline Move get_move_pv(int moveNum, int i) const;
    inline int64_t get_move_cumulative_nodes(int moveNum) const;
    inline int move_count() const;
    Move scan_for_easy_move() const;
    inline void sort();
    void sort_multipv(int n);

  private:
    static const int MaxRootMoves = 500;
    RootMove moves[MaxRootMoves];
    int count;
  };


  /// Constants and variables

  // Minimum number of full depth (i.e. non-reduced) moves at PV and non-PV
  // nodes:
  int LMRPVMoves = 15;
  int LMRNonPVMoves = 4;

  // Depth limit for use of dynamic threat detection:
  Depth ThreatDepth = 5*OnePly;

  // Depth limit for selective search:
  Depth SelectiveDepth = 7*OnePly;

  // Use internal iterative deepening?
  const bool UseIIDAtPVNodes = true;
  const bool UseIIDAtNonPVNodes = false;

  // Internal iterative deepening margin.  At Non-PV moves, when
  // UseIIDAtNonPVNodes is true, we do an internal iterative deepening search
  // when the static evaluation is at most IIDMargin below beta.
  const Value IIDMargin = Value(0x100);

  // Use easy moves?
  const bool UseEasyMove = true;

  // Easy move margin.  An easy move candidate must be at least this much
  // better than the second best move.
  const Value EasyMoveMargin = Value(0x200);

  // Problem margin.  If the score of the first move at iteration N+1 has
  // dropped by more than this since iteration N, the boolean variable
  // "Problem" is set to true, which will make the program spend some extra
  // time looking for a better move.
  const Value ProblemMargin = Value(0x28);

  // No problem margin.  If the boolean "Problem" is true, and a new move
  // is found at the root which is less than NoProblemMargin worse than the
  // best move from the previous iteration, Problem is set back to false.
  const Value NoProblemMargin = Value(0x14);

  // Null move margin.  A null move search will not be done if the approximate
  // evaluation of the position is more than NullMoveMargin below beta.
  const Value NullMoveMargin = Value(0x300);

  // Pruning criterions.  See the code and comments in ok_to_prune() to
  // understand their precise meaning.
  const bool PruneEscapeMoves = false;
  const bool PruneDefendingMoves = false;
  const bool PruneBlockingMoves = false;

  // Use futility pruning?
  bool UseQSearchFutilityPruning = true;
  bool UseFutilityPruning = true;

  // Margins for futility pruning in the quiescence search, at frontier
  // nodes, and at pre-frontier nodes:
  Value FutilityMargin0 = Value(0x80);
  Value FutilityMargin1 = Value(0x100);
  Value FutilityMargin2 = Value(0x300);

  // Razoring
  Depth RazorDepth = 4*OnePly;
  Value RazorMargin = Value(0x300);

  // Last seconds noise filtering (LSN)
  bool UseLSNFiltering = false;
  bool looseOnTime = false;
  int LSNTime = 4 * 1000; // In milliseconds
  Value LSNValue = Value(0x200);

  // Extensions.  Array index 0 is used at non-PV nodes, index 1 at PV nodes.
  Depth CheckExtension[2] = {OnePly, OnePly};
  Depth SingleReplyExtension[2] = {OnePly / 2, OnePly / 2};
  Depth PawnPushTo7thExtension[2] = {OnePly / 2, OnePly / 2};
  Depth PassedPawnExtension[2] = {Depth(0), Depth(0)};
  Depth PawnEndgameExtension[2] = {OnePly, OnePly};
  Depth MateThreatExtension[2] = {Depth(0), Depth(0)};

  // Search depth at iteration 1:
  const Depth InitialDepth = OnePly /*+ OnePly/2*/;

  // Node counters
  int NodesSincePoll;
  int NodesBetweenPolls = 30000;

  // Iteration counter:
  int Iteration;

  // Scores and number of times the best move changed for each iteration:
  Value ValueByIteration[PLY_MAX_PLUS_2];
  int BestMoveChangesByIteration[PLY_MAX_PLUS_2];

  // MultiPV mode:
  int MultiPV = 1;

  // Time managment variables
  int SearchStartTime;
  int MaxNodes, MaxDepth;
  int MaxSearchTime, AbsoluteMaxSearchTime, ExtraSearchTime, TimeAdvantage;
  Move BestRootMove, PonderMove, EasyMove;
  int RootMoveNumber;
  bool InfiniteSearch;
  bool PonderSearch;
  bool StopOnPonderhit;
  bool AbortSearch;
  bool Quit;
  bool FailHigh;
  bool Problem;
  bool PonderingEnabled;
  int ExactMaxTime;

  // Show current line?
  bool ShowCurrentLine = false;

  // Log file
  bool UseLogFile = false;
  std::ofstream LogFile;

  // MP related variables
  Depth MinimumSplitDepth = 4*OnePly;
  int MaxThreadsPerSplitPoint = 4;
  Thread Threads[THREAD_MAX];
  Lock MPLock;
  bool AllThreadsShouldExit = false;
  const int MaxActiveSplitPoints = 8;
  SplitPoint SplitPointStack[THREAD_MAX][MaxActiveSplitPoints];
  bool Idle = true;

#if !defined(_MSC_VER)
  pthread_cond_t WaitCond;
  pthread_mutex_t WaitLock;
#else
  HANDLE SitIdleEvent[THREAD_MAX];
#endif


  /// Functions

  Value id_loop(const Position &pos, Move searchMoves[]);
  Value root_search(Position &pos, SearchStack ss[], RootMoveList &rml);
  Value search_pv(Position &pos, SearchStack ss[], Value alpha, Value beta,
                  Depth depth, int ply, int threadID);
  Value search(Position &pos, SearchStack ss[], Value beta,
               Depth depth, int ply, bool allowNullmove, int threadID);
  Value qsearch(Position &pos, SearchStack ss[], Value alpha, Value beta,
                Depth depth, int ply, int threadID);
  void sp_search(SplitPoint *sp, int threadID);
  void sp_search_pv(SplitPoint *sp, int threadID);
  void init_search_stack(SearchStack ss[]);
  void init_node(const Position &pos, SearchStack ss[], int ply, int threadID);
  void update_pv(SearchStack ss[], int ply);
  void sp_update_pv(SearchStack *pss, SearchStack ss[], int ply);
  bool connected_moves(const Position &pos, Move m1, Move m2);
  Depth extension(const Position &pos, Move m, bool pvNode, bool check,
                  bool singleReply, bool mateThreat);
  bool ok_to_do_nullmove(const Position &pos);
  bool ok_to_prune(const Position &pos, Move m, Move threat, Depth d);
  bool ok_to_use_TT(const TTEntry* tte, Depth depth, Value beta, int ply);
  bool ok_to_history(const Position &pos, Move m);
  void update_history(const Position& pos, Move m, Depth depth,
                      Move movesSearched[], int moveCount);

  bool fail_high_ply_1();
  int current_search_time();
  int nps();
  void poll();
  void ponderhit();
  void print_current_line(SearchStack ss[], int ply, int threadID);
  void wait_for_stop_or_ponderhit();

  void idle_loop(int threadID, SplitPoint *waitSp);
  void init_split_point_stack();
  void destroy_split_point_stack();
  bool thread_should_stop(int threadID);
  bool thread_is_available(int slave, int master);
  bool idle_thread_exists(int master);
  bool split(const Position &pos, SearchStack *ss, int ply,
             Value *alpha, Value *beta, Value *bestValue, Depth depth,
             int *moves, MovePicker *mp, Bitboard dcCandidates, int master,
             bool pvNode);
  void wake_sleeping_threads();

#if !defined(_MSC_VER)
  void *init_thread(void *threadID);
#else
  DWORD WINAPI init_thread(LPVOID threadID);
#endif

}


////
//// Global variables
////

// The main transposition table
TranspositionTable TT = TranspositionTable(TTDefaultSize);


// Number of active threads:
int ActiveThreads = 1;

// Locks.  In principle, there is no need for IOLock to be a global variable,
// but it could turn out to be useful for debugging.
Lock IOLock;

History H;  // Should be made local?


////
//// Functions
////

/// think() is the external interface to Stockfish's search, and is called when
/// the program receives the UCI 'go' command.  It initializes various
/// search-related global variables, and calls root_search()

void think(const Position &pos, bool infinite, bool ponder, int side_to_move,
           int time[], int increment[], int movesToGo, int maxDepth,
           int maxNodes, int maxTime, Move searchMoves[]) {

  // Look for a book move
  if (!infinite && !ponder && get_option_value_bool("OwnBook"))
  {
      Move bookMove;
      if (get_option_value_string("Book File") != OpeningBook.file_name())
      {
          OpeningBook.close();
          OpeningBook.open("book.bin");
      }
      bookMove = OpeningBook.get_move(pos);
      if (bookMove != MOVE_NONE)
      {
          std::cout << "bestmove " << bookMove << std::endl;
          return;
      }
  }

  // Initialize global search variables
  Idle = false;
  SearchStartTime = get_system_time();
  BestRootMove = MOVE_NONE;
  PonderMove = MOVE_NONE;
  EasyMove = MOVE_NONE;
  for (int i = 0; i < THREAD_MAX; i++)
  {
      Threads[i].nodes = 0ULL;
      Threads[i].failHighPly1 = false;
  }
  NodesSincePoll = 0;
  InfiniteSearch = infinite;
  PonderSearch = ponder;
  StopOnPonderhit = false;
  AbortSearch = false;
  Quit = false;
  FailHigh = false;
  Problem = false;
  ExactMaxTime = maxTime;

  // Read UCI option values
  TT.set_size(get_option_value_int("Hash"));
  if (button_was_pressed("Clear Hash"))
      TT.clear();

  PonderingEnabled = get_option_value_bool("Ponder");
  MultiPV = get_option_value_int("MultiPV");

  CheckExtension[1] = Depth(get_option_value_int("Check Extension (PV nodes)"));
  CheckExtension[0] = Depth(get_option_value_int("Check Extension (non-PV nodes)"));

  SingleReplyExtension[1] = Depth(get_option_value_int("Single Reply Extension (PV nodes)"));
  SingleReplyExtension[0] = Depth(get_option_value_int("Single Reply Extension (non-PV nodes)"));

  PawnPushTo7thExtension[1] = Depth(get_option_value_int("Pawn Push to 7th Extension (PV nodes)"));
  PawnPushTo7thExtension[0] = Depth(get_option_value_int("Pawn Push to 7th Extension (non-PV nodes)"));

  PassedPawnExtension[1] = Depth(get_option_value_int("Passed Pawn Extension (PV nodes)"));
  PassedPawnExtension[0] = Depth(get_option_value_int("Passed Pawn Extension (non-PV nodes)"));

  PawnEndgameExtension[1] = Depth(get_option_value_int("Pawn Endgame Extension (PV nodes)"));
  PawnEndgameExtension[0] = Depth(get_option_value_int("Pawn Endgame Extension (non-PV nodes)"));

  MateThreatExtension[1] = Depth(get_option_value_int("Mate Threat Extension (PV nodes)"));
  MateThreatExtension[0] = Depth(get_option_value_int("Mate Threat Extension (non-PV nodes)"));

  LMRPVMoves     = get_option_value_int("Full Depth Moves (PV nodes)") + 1;
  LMRNonPVMoves  = get_option_value_int("Full Depth Moves (non-PV nodes)") + 1;
  ThreatDepth    = get_option_value_int("Threat Depth") * OnePly;
  SelectiveDepth = get_option_value_int("Selective Plies") * OnePly;

  Chess960 = get_option_value_bool("UCI_Chess960");
  ShowCurrentLine = get_option_value_bool("UCI_ShowCurrLine");
  UseLogFile = get_option_value_bool("Use Search Log");
  if (UseLogFile)
      LogFile.open(get_option_value_string("Search Log Filename").c_str(), std::ios::out | std::ios::app);

  UseQSearchFutilityPruning = get_option_value_bool("Futility Pruning (Quiescence Search)");
  UseFutilityPruning = get_option_value_bool("Futility Pruning (Main Search)");

  FutilityMargin0 = value_from_centipawns(get_option_value_int("Futility Margin 0"));
  FutilityMargin1 = value_from_centipawns(get_option_value_int("Futility Margin 1"));
  FutilityMargin2 = value_from_centipawns(get_option_value_int("Futility Margin 2"));

  RazorDepth = (get_option_value_int("Maximum Razoring Depth") + 1) * OnePly;
  RazorMargin = value_from_centipawns(get_option_value_int("Razoring Margin"));

  UseLSNFiltering = get_option_value_bool("LSN filtering");
  LSNTime = get_option_value_int("LSN Time Margin (sec)") * 1000;
  LSNValue = value_from_centipawns(get_option_value_int("LSN Value Margin"));

  MinimumSplitDepth = get_option_value_int("Minimum Split Depth") * OnePly;
  MaxThreadsPerSplitPoint = get_option_value_int("Maximum Number of Threads per Split Point");

  read_weights(pos.side_to_move());

  int newActiveThreads = get_option_value_int("Threads");
  if (newActiveThreads != ActiveThreads)
  {
      ActiveThreads = newActiveThreads;
      init_eval(ActiveThreads);
  }

  // Wake up sleeping threads:
  wake_sleeping_threads();

  for (int i = 1; i < ActiveThreads; i++)
      assert(thread_is_available(i, 0));

  // Set thinking time:
  int myTime = time[side_to_move];
  int myIncrement = increment[side_to_move];
  int oppTime = time[1 - side_to_move];

  TimeAdvantage = myTime - oppTime;

  if (!movesToGo) // Sudden death time control
  { 
      if (increment)
      {
          MaxSearchTime = myTime / 30 + myIncrement;
          AbsoluteMaxSearchTime = Max(myTime / 4, myIncrement - 100);
      } else { // Blitz game without increment
          MaxSearchTime = myTime / 40;
          AbsoluteMaxSearchTime = myTime / 8;
      }
  }
  else // (x moves) / (y minutes)
  {
      if (movesToGo == 1)
      {
          MaxSearchTime = myTime / 2;
          AbsoluteMaxSearchTime = Min(myTime / 2, myTime - 500);
      } else {
          MaxSearchTime = myTime / Min(movesToGo, 20);
          AbsoluteMaxSearchTime = Min((4 * myTime) / movesToGo, myTime / 3);
      }
  }

  if (PonderingEnabled)
  {
      MaxSearchTime += MaxSearchTime / 4;
      MaxSearchTime = Min(MaxSearchTime, AbsoluteMaxSearchTime);
  }

  // Fixed depth or fixed number of nodes?
  MaxDepth = maxDepth;
  if (MaxDepth)
      InfiniteSearch = true; // HACK

  MaxNodes = maxNodes;
  if (MaxNodes)
  {
      NodesBetweenPolls = Min(MaxNodes, 30000);
      InfiniteSearch = true; // HACK
  }
  else
      NodesBetweenPolls = 30000;


  // Write information to search log file:
  if (UseLogFile)
      LogFile << "Searching: " << pos.to_fen() << std::endl
              << "infinite: " << infinite
              << " ponder: " << ponder
              << " time: " << myTime
              << " increment: " << myIncrement
              << " moves to go: " << movesToGo << std::endl;


  // We're ready to start thinking.  Call the iterative deepening loop
  // function:
  if (!looseOnTime)
  {
      Value v = id_loop(pos, searchMoves);
      looseOnTime = (   UseLSNFiltering
                     && myTime < LSNTime
                     && myIncrement == 0
                     && v < -LSNValue);
  }
  else
  {
      looseOnTime = false; // reset for next match
      while (SearchStartTime + myTime + 1000 > get_system_time())
          ; // wait here
      id_loop(pos, searchMoves); // to fail gracefully
  }

  if (UseLogFile)
      LogFile.close();

  if (Quit)
  {
      OpeningBook.close();
      stop_threads();
      quit_eval();
      exit(0);
  }
  Idle = true;
}


/// init_threads() is called during startup.  It launches all helper threads,
/// and initializes the split point stack and the global locks and condition
/// objects.

void init_threads() {

  volatile int i;

#if !defined(_MSC_VER)
  pthread_t pthread[1];
#endif

  for (i = 0; i < THREAD_MAX; i++)
      Threads[i].activeSplitPoints = 0;

  // Initialize global locks:
  lock_init(&MPLock, NULL);
  lock_init(&IOLock, NULL);

  init_split_point_stack();

#if !defined(_MSC_VER)
  pthread_mutex_init(&WaitLock, NULL);
  pthread_cond_init(&WaitCond, NULL);
#else
  for (i = 0; i < THREAD_MAX; i++)
      SitIdleEvent[i] = CreateEvent(0, FALSE, FALSE, 0);
#endif

  // All threads except the main thread should be initialized to idle state
  for (i = 1; i < THREAD_MAX; i++)
  {
      Threads[i].stop = false;
      Threads[i].workIsWaiting = false;
      Threads[i].idle = true;
      Threads[i].running = false;
  }

  // Launch the helper threads
  for(i = 1; i < THREAD_MAX; i++)
  {
#if !defined(_MSC_VER)
      pthread_create(pthread, NULL, init_thread, (void*)(&i));
#else
      DWORD iID[1];
      CreateThread(NULL, 0, init_thread, (LPVOID)(&i), 0, iID);
#endif

      // Wait until the thread has finished launching:
      while (!Threads[i].running);
  }
}


/// stop_threads() is called when the program exits.  It makes all the
/// helper threads exit cleanly.

void stop_threads() {

  ActiveThreads = THREAD_MAX;  // HACK
  Idle = false;  // HACK
  wake_sleeping_threads();
  AllThreadsShouldExit = true;
  for (int i = 1; i < THREAD_MAX; i++)
  {
      Threads[i].stop = true;
      while(Threads[i].running);
  }
  destroy_split_point_stack();
}


/// nodes_searched() returns the total number of nodes searched so far in
/// the current search.

int64_t nodes_searched() {

  int64_t result = 0ULL;
  for (int i = 0; i < ActiveThreads; i++)
      result += Threads[i].nodes;
  return result;
}


namespace {

  // id_loop() is the main iterative deepening loop.  It calls root_search
  // repeatedly with increasing depth until the allocated thinking time has
  // been consumed, the user stops the search, or the maximum search depth is
  // reached.

  Value id_loop(const Position &pos, Move searchMoves[]) {

    Position p(pos);
    SearchStack ss[PLY_MAX_PLUS_2];

    // searchMoves are verified, copied, scored and sorted
    RootMoveList rml(p, searchMoves);

    // Initialize
    TT.new_search();
    H.clear();
    init_search_stack(ss);

    ValueByIteration[0] = Value(0);
    ValueByIteration[1] = rml.get_move_score(0);
    Iteration = 1;

    EasyMove = rml.scan_for_easy_move();

    // Iterative deepening loop
    while (!AbortSearch && Iteration < PLY_MAX)
    {
        // Initialize iteration
        rml.sort();
        Iteration++;
        BestMoveChangesByIteration[Iteration] = 0;
        if (Iteration <= 5)
            ExtraSearchTime = 0;

        std::cout << "info depth " << Iteration << std::endl;

        // Search to the current depth
        ValueByIteration[Iteration] = root_search(p, ss, rml);

        // Erase the easy move if it differs from the new best move
        if (ss[0].pv[0] != EasyMove)
            EasyMove = MOVE_NONE;

        Problem = false;

        if (!InfiniteSearch)
        {
            // Time to stop?
            bool stopSearch = false;

            // Stop search early if there is only a single legal move:
            if (Iteration >= 6 && rml.move_count() == 1)
                stopSearch = true;

            // Stop search early when the last two iterations returned a mate score
            if (  Iteration >= 6
                && abs(ValueByIteration[Iteration]) >= abs(VALUE_MATE) - 100
                && abs(ValueByIteration[Iteration-1]) >= abs(VALUE_MATE) - 100)
                stopSearch = true;

            // Stop search early if one move seems to be much better than the rest
            int64_t nodes = nodes_searched();
            if (   Iteration >= 8
                && EasyMove == ss[0].pv[0]
                && (  (   rml.get_move_cumulative_nodes(0) > (nodes * 85) / 100
                       && current_search_time() > MaxSearchTime / 16)
                    ||(   rml.get_move_cumulative_nodes(0) > (nodes * 98) / 100
                       && current_search_time() > MaxSearchTime / 32)))
                stopSearch = true;

            // Add some extra time if the best move has changed during the last two iterations
            if (Iteration > 5 && Iteration <= 50)
                ExtraSearchTime = BestMoveChangesByIteration[Iteration]   * (MaxSearchTime / 2)
                                + BestMoveChangesByIteration[Iteration-1] * (MaxSearchTime / 3);

            // If we need some more and we are in time advantage take it
            if (ExtraSearchTime > 0 && TimeAdvantage > 2 * MaxSearchTime)
                ExtraSearchTime += MaxSearchTime / 2;

            // Stop search if most of MaxSearchTime is consumed at the end of the
            // iteration.  We probably don't have enough time to search the first
            // move at the next iteration anyway.
            if (current_search_time() > ((MaxSearchTime + ExtraSearchTime)*80) / 128)
                stopSearch = true;

            if (stopSearch)
            {
                if (!PonderSearch)
                    break;
                else
                    StopOnPonderhit = true;
            }
        }
        // Write PV to transposition table, in case the relevant entries have
        // been overwritten during the search:
        TT.insert_pv(p, ss[0].pv);

        if (MaxDepth && Iteration >= MaxDepth)
            break;
    }

    rml.sort();

    // If we are pondering, we shouldn't print the best move before we
    // are told to do so
    if (PonderSearch)
        wait_for_stop_or_ponderhit();
    else
        // Print final search statistics
        std::cout << "info nodes " << nodes_searched()
                  << " nps " << nps()
                  << " time " << current_search_time()
                  << " hashfull " << TT.full() << std::endl;

    // Print the best move and the ponder move to the standard output
    std::cout << "bestmove " << ss[0].pv[0];
    if (ss[0].pv[1] != MOVE_NONE)
        std::cout << " ponder " << ss[0].pv[1];

    std::cout << std::endl;

    if (UseLogFile)
    {
        UndoInfo u;
        LogFile << "Nodes: " << nodes_searched() << std::endl
                << "Nodes/second: " << nps() << std::endl
                << "Best move: " << move_to_san(p, ss[0].pv[0]) << std::endl;

        p.do_move(ss[0].pv[0], u);
        LogFile << "Ponder move: " << move_to_san(p, ss[0].pv[1])
                << std::endl << std::endl;
    }
    return rml.get_move_score(0);
  }


  // root_search() is the function which searches the root node.  It is
  // similar to search_pv except that it uses a different move ordering
  // scheme (perhaps we should try to use this at internal PV nodes, too?)
  // and prints some information to the standard output.

  Value root_search(Position &pos, SearchStack ss[], RootMoveList &rml) {

    Value alpha = -VALUE_INFINITE;
    Value beta = VALUE_INFINITE, value;
    Bitboard dcCandidates = pos.discovered_check_candidates(pos.side_to_move());

    // Loop through all the moves in the root move list
    for (int i = 0; i <  rml.move_count() && !AbortSearch; i++)
    {
        int64_t nodes;
        Move move;
        UndoInfo u;
        Depth ext, newDepth;

        RootMoveNumber = i + 1;
        FailHigh = false;

        // Remember the node count before the move is searched. The node counts
        // are used to sort the root moves at the next iteration.
        nodes = nodes_searched();

        // Pick the next root move, and print the move and the move number to
        // the standard output.
        move = ss[0].currentMove = rml.get_move(i);
        if (current_search_time() >= 1000)
            std::cout << "info currmove " << move
                      << " currmovenumber " << i + 1 << std::endl;

        // Decide search depth for this move
        ext = extension(pos, move, true, pos.move_is_check(move), false, false);
        newDepth = (Iteration - 2) * OnePly + ext + InitialDepth;

        // Make the move, and search it
        pos.do_move(move, u, dcCandidates);

        if (i < MultiPV)
        {
            value = -search_pv(pos, ss, -beta, VALUE_INFINITE, newDepth, 1, 0);
            // If the value has dropped a lot compared to the last iteration,
            // set the boolean variable Problem to true. This variable is used
            // for time managment: When Problem is true, we try to complete the
            // current iteration before playing a move.
            Problem = (Iteration >= 2 && value <= ValueByIteration[Iteration-1] - ProblemMargin);

            if (Problem && StopOnPonderhit)
                StopOnPonderhit = false;
        } 
        else
        {
            value = -search(pos, ss, -alpha, newDepth, 1, true, 0);
            if (value > alpha)
            {
                // Fail high! Set the boolean variable FailHigh to true, and
                // re-search the move with a big window. The variable FailHigh is
                // used for time managment: We try to avoid aborting the search
                // prematurely during a fail high research.
                FailHigh = true;
                value = -search_pv(pos, ss, -beta, -alpha, newDepth, 1, 0);
            }
        }

        pos.undo_move(move, u);

        // Finished searching the move. If AbortSearch is true, the search
        // was aborted because the user interrupted the search or because we
        // ran out of time. In this case, the return value of the search cannot
        // be trusted, and we break out of the loop without updating the best
        // move and/or PV:
        if (AbortSearch)
            break;

        // Remember the node count for this move. The node counts are used to
        // sort the root moves at the next iteration.
        rml.set_move_nodes(i, nodes_searched() - nodes);

        assert(value >= -VALUE_INFINITE && value <= VALUE_INFINITE);

        if (value <= alpha && i >= MultiPV)
            rml.set_move_score(i, -VALUE_INFINITE);
        else
        {
            // New best move!

            // Update PV
            rml.set_move_score(i, value);
            update_pv(ss, 0);
            rml.set_move_pv(i, ss[0].pv);

            if (MultiPV == 1)
            {
                // We record how often the best move has been changed in each
                // iteration. This information is used for time managment: When
                // the best move changes frequently, we allocate some more time.
                if (i > 0)
                    BestMoveChangesByIteration[Iteration]++;

                // Print search information to the standard output:
                std::cout << "info depth " << Iteration
                          << " score " << value_to_string(value)
                          << " time " << current_search_time()
                          << " nodes " << nodes_searched()
                          << " nps " << nps()
                          << " pv ";

                for (int j = 0; ss[0].pv[j] != MOVE_NONE && j < PLY_MAX; j++)
                    std::cout << ss[0].pv[j] << " ";

                std::cout << std::endl;

                if (UseLogFile)
                    LogFile << pretty_pv(pos, current_search_time(), Iteration, nodes_searched(), value, ss[0].pv)
                            << std::endl;

                alpha = value;

                // Reset the global variable Problem to false if the value isn't too
                // far below the final value from the last iteration.
                if (value > ValueByIteration[Iteration - 1] - NoProblemMargin)
                    Problem = false;
            }
            else // MultiPV > 1
            {
                rml.sort_multipv(i);
                for (int j = 0; j < Min(MultiPV, rml.move_count()); j++)
                {
                    int k;
                    std::cout << "info multipv " << j + 1
                              << " score " << value_to_string(rml.get_move_score(j))
                              << " depth " << ((j <= i)? Iteration : Iteration - 1)
                              << " time " << current_search_time()
                              << " nodes " << nodes_searched()
                              << " nps " << nps()
                              << " pv ";

                    for (k = 0; rml.get_move_pv(j, k) != MOVE_NONE && k < PLY_MAX; k++)
                        std::cout << rml.get_move_pv(j, k) << " ";

                    std::cout << std::endl;
                }
                alpha = rml.get_move_score(Min(i, MultiPV-1));
            }
        }
    }
    return alpha;
  }


  // search_pv() is the main search function for PV nodes.

  Value search_pv(Position &pos, SearchStack ss[], Value alpha, Value beta,
                  Depth depth, int ply, int threadID) {

    assert(alpha >= -VALUE_INFINITE && alpha <= VALUE_INFINITE);
    assert(beta > alpha && beta <= VALUE_INFINITE);
    assert(ply >= 0 && ply < PLY_MAX);
    assert(threadID >= 0 && threadID < ActiveThreads);

    // Initialize, and make an early exit in case of an aborted search,
    // an instant draw, maximum ply reached, etc.
    if (AbortSearch || thread_should_stop(threadID))
        return Value(0);

    if (depth < OnePly)
        return qsearch(pos, ss, alpha, beta, Depth(0), ply, threadID);

    init_node(pos, ss, ply, threadID);

    if (pos.is_draw())
        return VALUE_DRAW;

    EvalInfo ei;

    if (ply >= PLY_MAX - 1)
        return evaluate(pos, ei, threadID);

    // Mate distance pruning
    Value oldAlpha = alpha;
    alpha = Max(value_mated_in(ply), alpha);
    beta = Min(value_mate_in(ply+1), beta);
    if (alpha >= beta)
        return alpha;

    // Transposition table lookup. At PV nodes, we don't use the TT for
    // pruning, but only for move ordering.
    const TTEntry* tte = TT.retrieve(pos);
    Move ttMove = (tte ? tte->move() : MOVE_NONE);

    // Go with internal iterative deepening if we don't have a TT move
    if (UseIIDAtPVNodes && ttMove == MOVE_NONE && depth >= 5*OnePly)
    {
        search_pv(pos, ss, alpha, beta, depth-2*OnePly, ply, threadID);
        ttMove = ss[ply].pv[ply];
    }

    // Initialize a MovePicker object for the current position, and prepare
    // to search all moves
    MovePicker mp = MovePicker(pos, true, ttMove, ss[ply].mateKiller,
                               ss[ply].killer1, ss[ply].killer2, depth);

    Move move, movesSearched[256];
    int moveCount = 0;
    Value value, bestValue = -VALUE_INFINITE;
    Bitboard dcCandidates = mp.discovered_check_candidates();
    bool isCheck = pos.is_check();
    bool mateThreat =   MateThreatExtension[1] > Depth(0)
                     && pos.has_mate_threat(opposite_color(pos.side_to_move()));

    // Loop through all legal moves until no moves remain or a beta cutoff
    // occurs.
    while (   alpha < beta
           && (move = mp.get_next_move()) != MOVE_NONE
           && !thread_should_stop(threadID))
    {
      assert(move_is_ok(move));

      bool singleReply = (isCheck && mp.number_of_moves() == 1);
      bool moveIsCheck = pos.move_is_check(move, dcCandidates);
      bool moveIsCapture = pos.move_is_capture(move);
      bool moveIsPassedPawnPush = pos.move_is_passed_pawn_push(move);

      movesSearched[moveCount++] = ss[ply].currentMove = move;

      if (moveIsCapture)
          ss[ply].currentMoveCaptureValue = pos.midgame_value_of_piece_on(move_to(move));
      else if (move_is_ep(move))
          ss[ply].currentMoveCaptureValue = PawnValueMidgame;
      else
          ss[ply].currentMoveCaptureValue = Value(0);

      // Decide the new search depth
      Depth ext = extension(pos, move, true, moveIsCheck, singleReply, mateThreat);
      Depth newDepth = depth - OnePly + ext;

      // Make and search the move
      UndoInfo u;
      pos.do_move(move, u, dcCandidates);

      if (moveCount == 1) // The first move in list is the PV
          value = -search_pv(pos, ss, -beta, -alpha, newDepth, ply+1, threadID);
      else
      {
        // Try to reduce non-pv search depth by one ply if move seems not problematic,
        // if the move fails high will be re-searched at full depth.
        if (    depth >= 2*OnePly
            &&  ext == Depth(0)
            &&  moveCount >= LMRPVMoves
            && !moveIsCapture
            && !move_promotion(move)
            && !moveIsPassedPawnPush
            && !move_is_castle(move)
            &&  move != ss[ply].killer1
            &&  move != ss[ply].killer2)
        {
            ss[ply].reduction = OnePly;
            value = -search(pos, ss, -alpha, newDepth-OnePly, ply+1, true, threadID);
        }
        else
            value = alpha + 1; // Just to trigger next condition

        if (value > alpha) // Go with full depth pv search
        {
            ss[ply].reduction = Depth(0);
            value = -search(pos, ss, -alpha, newDepth, ply+1, true, threadID);
            if (value > alpha && value < beta)
            {
                // When the search fails high at ply 1 while searching the first
                // move at the root, set the flag failHighPly1. This is used for
                // time managment:  We don't want to stop the search early in
                // such cases, because resolving the fail high at ply 1 could
                // result in a big drop in score at the root.
                if (ply == 1 && RootMoveNumber == 1)
                    Threads[threadID].failHighPly1 = true;

                // A fail high occurred. Re-search at full window (pv search)
                value = -search_pv(pos, ss, -beta, -alpha, newDepth, ply+1, threadID);
                Threads[threadID].failHighPly1 = false;
          }
        }
      }
      pos.undo_move(move, u);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // New best move?
      if (value > bestValue)
      {
          bestValue = value;
          if (value > alpha)
          {
              alpha = value;
              update_pv(ss, ply);
              if (value == value_mate_in(ply + 1))
                  ss[ply].mateKiller = move;
          }
          // If we are at ply 1, and we are searching the first root move at
          // ply 0, set the 'Problem' variable if the score has dropped a lot
          // (from the computer's point of view) since the previous iteration:
          if (Iteration >= 2 && -value <= ValueByIteration[Iteration-1] - ProblemMargin)
              Problem = true;
      }

      // Split?
      if (   ActiveThreads > 1
          && bestValue < beta
          && depth >= MinimumSplitDepth
          && Iteration <= 99
          && idle_thread_exists(threadID)
          && !AbortSearch
          && !thread_should_stop(threadID)
          && split(pos, ss, ply, &alpha, &beta, &bestValue, depth,
                   &moveCount, &mp, dcCandidates, threadID, true))
          break;
    }

    // All legal moves have been searched.  A special case: If there were
    // no legal moves, it must be mate or stalemate:
    if (moveCount == 0)
        return (isCheck ? value_mated_in(ply) : VALUE_DRAW);

    // If the search is not aborted, update the transposition table,
    // history counters, and killer moves.
    if (AbortSearch || thread_should_stop(threadID))
        return bestValue;

    if (bestValue <= oldAlpha)
        TT.store(pos, value_to_tt(bestValue, ply), depth, MOVE_NONE, VALUE_TYPE_UPPER);

    else if (bestValue >= beta)
    {
        Move m = ss[ply].pv[ply];
        if (ok_to_history(pos, m)) // Only non capture moves are considered
        {
            update_history(pos, m, depth, movesSearched, moveCount);
            if (m != ss[ply].killer1)
            {
                ss[ply].killer2 = ss[ply].killer1;
                ss[ply].killer1 = m;
            }
        }
        TT.store(pos, value_to_tt(bestValue, ply), depth, m, VALUE_TYPE_LOWER);
    }
    else
        TT.store(pos, value_to_tt(bestValue, ply), depth, ss[ply].pv[ply], VALUE_TYPE_EXACT);

    return bestValue;
  }


  // search() is the search function for zero-width nodes.

  Value search(Position &pos, SearchStack ss[], Value beta, Depth depth,
               int ply, bool allowNullmove, int threadID) {

    assert(beta >= -VALUE_INFINITE && beta <= VALUE_INFINITE);
    assert(ply >= 0 && ply < PLY_MAX);
    assert(threadID >= 0 && threadID < ActiveThreads);

    EvalInfo ei;

    // Initialize, and make an early exit in case of an aborted search,
    // an instant draw, maximum ply reached, etc.
    if (AbortSearch || thread_should_stop(threadID))
        return Value(0);

    if (depth < OnePly)
        return qsearch(pos, ss, beta-1, beta, Depth(0), ply, threadID);

    init_node(pos, ss, ply, threadID);

    if (pos.is_draw())
        return VALUE_DRAW;

    if (ply >= PLY_MAX - 1)
        return evaluate(pos, ei, threadID);

    // Mate distance pruning
    if (value_mated_in(ply) >= beta)
        return beta;

    if (value_mate_in(ply + 1) < beta)
        return beta - 1;

    // Transposition table lookup
    const TTEntry* tte = TT.retrieve(pos);
    Move ttMove = (tte ? tte->move() : MOVE_NONE);

    if (tte && ok_to_use_TT(tte, depth, beta, ply))
    {
        ss[ply].currentMove = ttMove; // can be MOVE_NONE ?
        return value_from_tt(tte->value(), ply);
    }

    Value approximateEval = quick_evaluate(pos);
    bool mateThreat = false;
    bool isCheck = pos.is_check();

    // Null move search
    if (    allowNullmove
        && !isCheck
        &&  ok_to_do_nullmove(pos)
        &&  approximateEval >= beta - NullMoveMargin)
    {
        ss[ply].currentMove = MOVE_NULL;

        UndoInfo u;
        pos.do_null_move(u);
        Value nullValue = -search(pos, ss, -(beta-1), depth-4*OnePly, ply+1, false, threadID);
        pos.undo_null_move(u);

        if (nullValue >= beta)
        {
            if (depth < 6 * OnePly)
                return beta;

            // Do zugzwang verification search
            Value v = search(pos, ss, beta, depth-5*OnePly, ply, false, threadID);
            if (v >= beta)
                return beta;
        } else {
            // The null move failed low, which means that we may be faced with
            // some kind of threat.  If the previous move was reduced, check if
            // the move that refuted the null move was somehow connected to the
            // move which was reduced.  If a connection is found, return a fail
            // low score (which will cause the reduced move to fail high in the
            // parent node, which will trigger a re-search with full depth).
            if (nullValue == value_mated_in(ply + 2))
                mateThreat = true;

            ss[ply].threatMove = ss[ply + 1].currentMove;
            if (   depth < ThreatDepth
                && ss[ply - 1].reduction
                && connected_moves(pos, ss[ply - 1].currentMove, ss[ply].threatMove))
                return beta - 1;
        }
    }
    // Null move search not allowed, try razoring
    else if (  (approximateEval < beta - RazorMargin && depth < RazorDepth)
             ||(approximateEval < beta - PawnValueMidgame && depth <= OnePly))
    {
        Value v = qsearch(pos, ss, beta-1, beta, Depth(0), ply, threadID);
        if (v < beta)
            return v;
    }

    // Go with internal iterative deepening if we don't have a TT move
    if (UseIIDAtNonPVNodes && ttMove == MOVE_NONE && depth >= 8*OnePly &&
        evaluate(pos, ei, threadID) >= beta - IIDMargin)
    {
        search(pos, ss, beta, Min(depth/2, depth-2*OnePly), ply, false, threadID);
        ttMove = ss[ply].pv[ply];
    }

    // Initialize a MovePicker object for the current position, and prepare
    // to search all moves:
    MovePicker mp = MovePicker(pos, false, ttMove, ss[ply].mateKiller,
                               ss[ply].killer1, ss[ply].killer2, depth);

    Move move, movesSearched[256];
    int moveCount = 0;
    Value value, bestValue = -VALUE_INFINITE;
    Bitboard dcCandidates = mp.discovered_check_candidates();
    Value futilityValue = VALUE_NONE;
    bool useFutilityPruning =   UseFutilityPruning
                             && depth < SelectiveDepth
                             && !isCheck;

    // Loop through all legal moves until no moves remain or a beta cutoff
    // occurs.
    while (   bestValue < beta
           && (move = mp.get_next_move()) != MOVE_NONE
           && !thread_should_stop(threadID))
    {
      assert(move_is_ok(move));

      bool singleReply = (isCheck && mp.number_of_moves() == 1);
      bool moveIsCheck = pos.move_is_check(move, dcCandidates);
      bool moveIsCapture = pos.move_is_capture(move);
      bool moveIsPassedPawnPush = pos.move_is_passed_pawn_push(move);

      movesSearched[moveCount++] = ss[ply].currentMove = move;

      // Decide the new search depth
      Depth ext = extension(pos, move, false, moveIsCheck, singleReply, mateThreat);
      Depth newDepth = depth - OnePly + ext;

      // Futility pruning
      if (    useFutilityPruning
          &&  ext == Depth(0)
          && !moveIsCapture
          && !moveIsPassedPawnPush
          && !move_promotion(move))
      {
          if (   moveCount >= 2 + int(depth)
              && ok_to_prune(pos, move, ss[ply].threatMove, depth))
              continue;

          if (depth < 3 * OnePly && approximateEval < beta)
          {
              if (futilityValue == VALUE_NONE)
                  futilityValue =  evaluate(pos, ei, threadID)
                                + (depth < 2 * OnePly ? FutilityMargin1 : FutilityMargin2);

              if (futilityValue < beta)
              {
                  if (futilityValue > bestValue)
                      bestValue = futilityValue;
                  continue;
              }
          }
      }

      // Make and search the move
      UndoInfo u;
      pos.do_move(move, u, dcCandidates);

      // Try to reduce non-pv search depth by one ply if move seems not problematic,
      // if the move fails high will be re-searched at full depth.
      if (   depth >= 2*OnePly
          && ext == Depth(0)
          && moveCount >= LMRNonPVMoves
          && !moveIsCapture
          && !move_promotion(move)
          && !moveIsPassedPawnPush
          && !move_is_castle(move)
          &&  move != ss[ply].killer1
          &&  move != ss[ply].killer2)
      {
          ss[ply].reduction = OnePly;
          value = -search(pos, ss, -(beta-1), newDepth-OnePly, ply+1, true, threadID);
      }
      else
        value = beta; // Just to trigger next condition

      if (value >= beta) // Go with full depth non-pv search
      {
          ss[ply].reduction = Depth(0);
          value = -search(pos, ss, -(beta-1), newDepth, ply+1, true, threadID);
      }
      pos.undo_move(move, u);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // New best move?
      if (value > bestValue)
      {
        bestValue = value;
        if (value >= beta)
            update_pv(ss, ply);

        if (value == value_mate_in(ply + 1))
            ss[ply].mateKiller = move;
      }

      // Split?
      if (   ActiveThreads > 1
          && bestValue < beta
          && depth >= MinimumSplitDepth
          && Iteration <= 99
          && idle_thread_exists(threadID)
          && !AbortSearch
          && !thread_should_stop(threadID)
          && split(pos, ss, ply, &beta, &beta, &bestValue, depth, &moveCount,
                   &mp, dcCandidates, threadID, false))
        break;
    }

    // All legal moves have been searched.  A special case: If there were
    // no legal moves, it must be mate or stalemate.
    if (moveCount == 0)
        return (pos.is_check() ? value_mated_in(ply) : VALUE_DRAW);

    // If the search is not aborted, update the transposition table,
    // history counters, and killer moves.
    if (AbortSearch || thread_should_stop(threadID))
        return bestValue;

    if (bestValue < beta)
        TT.store(pos, value_to_tt(bestValue, ply), depth, MOVE_NONE, VALUE_TYPE_UPPER);
    else
    {
        Move m = ss[ply].pv[ply];
        if (ok_to_history(pos, m)) // Only non capture moves are considered
        {
            update_history(pos, m, depth, movesSearched, moveCount);
            if (m != ss[ply].killer1)
            {
                ss[ply].killer2 = ss[ply].killer1;
                ss[ply].killer1 = m;
            }
        }
        TT.store(pos, value_to_tt(bestValue, ply), depth, m, VALUE_TYPE_LOWER);
    }
    return bestValue;
  }


  // qsearch() is the quiescence search function, which is called by the main
  // search function when the remaining depth is zero (or, to be more precise,
  // less than OnePly).

  Value qsearch(Position &pos, SearchStack ss[], Value alpha, Value beta,
                Depth depth, int ply, int threadID) {

    assert(alpha >= -VALUE_INFINITE && alpha <= VALUE_INFINITE);
    assert(beta >= -VALUE_INFINITE && beta <= VALUE_INFINITE);
    assert(depth <= 0);
    assert(ply >= 0 && ply < PLY_MAX);
    assert(threadID >= 0 && threadID < ActiveThreads);

    EvalInfo ei;

    // Initialize, and make an early exit in case of an aborted search,
    // an instant draw, maximum ply reached, etc.
    if (AbortSearch || thread_should_stop(threadID))
        return Value(0);

    init_node(pos, ss, ply, threadID);

    if (pos.is_draw())
        return VALUE_DRAW;

    // Transposition table lookup
    const TTEntry* tte = TT.retrieve(pos);
    if (tte && ok_to_use_TT(tte, depth, beta, ply))
        return value_from_tt(tte->value(), ply);

    // Evaluate the position statically
    Value staticValue = evaluate(pos, ei, threadID);

    if (ply == PLY_MAX - 1)
        return staticValue;

    // Initialize "stand pat score", and return it immediately if it is
    // at least beta.
    Value bestValue = (pos.is_check() ? -VALUE_INFINITE : staticValue);

    if (bestValue >= beta)
        return bestValue;

    if (bestValue > alpha)
        alpha = bestValue;

    // Initialize a MovePicker object for the current position, and prepare
    // to search the moves.  Because the depth is <= 0 here, only captures,
    // queen promotions and checks (only if depth == 0) will be generated.
    MovePicker mp = MovePicker(pos, false, MOVE_NONE, MOVE_NONE, MOVE_NONE,
                               MOVE_NONE, depth);
    Move move;
    int moveCount = 0;
    Bitboard dcCandidates = mp.discovered_check_candidates();
    bool isCheck = pos.is_check();

    // Loop through the moves until no moves remain or a beta cutoff
    // occurs.
    while (   alpha < beta
           && (move = mp.get_next_move()) != MOVE_NONE)
    {
      assert(move_is_ok(move));

      bool moveIsCheck = pos.move_is_check(move, dcCandidates);
      bool moveIsPassedPawnPush = pos.move_is_passed_pawn_push(move);

      moveCount++;
      ss[ply].currentMove = move;

      // Futility pruning
      if (    UseQSearchFutilityPruning
          && !isCheck
          && !moveIsCheck
          && !move_promotion(move)
          && !moveIsPassedPawnPush
          &&  beta - alpha == 1
          &&  pos.non_pawn_material(pos.side_to_move()) > RookValueMidgame)
      {
          Value futilityValue = staticValue
                              + Max(pos.midgame_value_of_piece_on(move_to(move)),
                                    pos.endgame_value_of_piece_on(move_to(move)))
                              + FutilityMargin0
                              + ei.futilityMargin;

          if (futilityValue < alpha)
          {
              if (futilityValue > bestValue)
                  bestValue = futilityValue;
              continue;
          }
      }

      // Don't search captures and checks with negative SEE values.
      if (   !isCheck
          && !move_promotion(move)
          && (pos.midgame_value_of_piece_on(move_from(move)) >
              pos.midgame_value_of_piece_on(move_to(move)))
          &&  pos.see(move) < 0)
          continue;

      // Make and search the move.
      UndoInfo u;
      pos.do_move(move, u, dcCandidates);
      Value value = -qsearch(pos, ss, -beta, -alpha, depth-OnePly, ply+1, threadID);
      pos.undo_move(move, u);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // New best move?
      if (value > bestValue)
      {
          bestValue = value;
          if (value > alpha)
          {
              alpha = value;
              update_pv(ss, ply);
          }
       }
    }

    // All legal moves have been searched.  A special case: If we're in check
    // and no legal moves were found, it is checkmate:
    if (pos.is_check() && moveCount == 0) // Mate!
        return value_mated_in(ply);

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    // Update transposition table
    TT.store(pos, value_to_tt(bestValue, ply), depth, MOVE_NONE, VALUE_TYPE_EXACT);

    return bestValue;
  }


  // sp_search() is used to search from a split point.  This function is called
  // by each thread working at the split point.  It is similar to the normal
  // search() function, but simpler.  Because we have already probed the hash
  // table, done a null move search, and searched the first move before
  // splitting, we don't have to repeat all this work in sp_search().  We
  // also don't need to store anything to the hash table here:  This is taken
  // care of after we return from the split point.

  void sp_search(SplitPoint *sp, int threadID) {

    assert(threadID >= 0 && threadID < ActiveThreads);
    assert(ActiveThreads > 1);

    Position pos = Position(sp->pos);
    SearchStack *ss = sp->sstack[threadID];
    Value value;
    Move move;
    bool isCheck = pos.is_check();
    bool useFutilityPruning =    UseFutilityPruning
                              && sp->depth < SelectiveDepth
                              && !isCheck;

    while (    sp->bestValue < sp->beta
           && !thread_should_stop(threadID)
           && (move = sp->mp->get_next_move(sp->lock)) != MOVE_NONE)
    {
      assert(move_is_ok(move));

      bool moveIsCheck = pos.move_is_check(move, sp->dcCandidates);
      bool moveIsCapture = pos.move_is_capture(move);
      bool moveIsPassedPawnPush = pos.move_is_passed_pawn_push(move);

      lock_grab(&(sp->lock));
      int moveCount = ++sp->moves;
      lock_release(&(sp->lock));

      ss[sp->ply].currentMove = move;

      // Decide the new search depth.
      Depth ext = extension(pos, move, false, moveIsCheck, false, false);
      Depth newDepth = sp->depth - OnePly + ext;

      // Prune?
      if (    useFutilityPruning
          &&  ext == Depth(0)
          && !moveIsCapture
          && !moveIsPassedPawnPush
          && !move_promotion(move)
          &&  moveCount >= 2 + int(sp->depth)
          &&  ok_to_prune(pos, move, ss[sp->ply].threatMove, sp->depth))
        continue;

      // Make and search the move.
      UndoInfo u;
      pos.do_move(move, u, sp->dcCandidates);

      // Try to reduce non-pv search depth by one ply if move seems not problematic,
      // if the move fails high will be re-searched at full depth.
      if (    ext == Depth(0)
          &&  moveCount >= LMRNonPVMoves
          && !moveIsCapture
          && !moveIsPassedPawnPush
          && !move_promotion(move)
          && !move_is_castle(move)
          &&  move != ss[sp->ply].killer1
          &&  move != ss[sp->ply].killer2)
      {
          ss[sp->ply].reduction = OnePly;
          value = -search(pos, ss, -(sp->beta-1), newDepth - OnePly, sp->ply+1, true, threadID);
      }
      else
          value = sp->beta; // Just to trigger next condition

      if (value >= sp->beta) // Go with full depth non-pv search
      {
          ss[sp->ply].reduction = Depth(0);
          value = -search(pos, ss, -(sp->beta - 1), newDepth, sp->ply+1, true, threadID);
      }
      pos.undo_move(move, u);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      if (thread_should_stop(threadID))
          break;

      // New best move?
      lock_grab(&(sp->lock));
      if (value > sp->bestValue && !thread_should_stop(threadID))
      {
          sp->bestValue = value;
          if (sp->bestValue >= sp->beta)
          {
              sp_update_pv(sp->parentSstack, ss, sp->ply);
              for (int i = 0; i < ActiveThreads; i++)
                  if (i != threadID && (i == sp->master || sp->slaves[i]))
                      Threads[i].stop = true;

              sp->finished = true;
        }
      }
      lock_release(&(sp->lock));
    }

    lock_grab(&(sp->lock));

    // If this is the master thread and we have been asked to stop because of
    // a beta cutoff higher up in the tree, stop all slave threads:
    if (sp->master == threadID && thread_should_stop(threadID))
        for (int i = 0; i < ActiveThreads; i++)
            if (sp->slaves[i])
                Threads[i].stop = true;

    sp->cpus--;
    sp->slaves[threadID] = 0;

    lock_release(&(sp->lock));
  }


  // sp_search_pv() is used to search from a PV split point.  This function
  // is called by each thread working at the split point.  It is similar to
  // the normal search_pv() function, but simpler.  Because we have already
  // probed the hash table and searched the first move before splitting, we
  // don't have to repeat all this work in sp_search_pv().  We also don't
  // need to store anything to the hash table here:  This is taken care of
  // after we return from the split point.

  void sp_search_pv(SplitPoint *sp, int threadID) {

    assert(threadID >= 0 && threadID < ActiveThreads);
    assert(ActiveThreads > 1);

    Position pos = Position(sp->pos);
    SearchStack *ss = sp->sstack[threadID];
    Value value;
    Move move;

    while (    sp->alpha < sp->beta
           && !thread_should_stop(threadID)
           && (move = sp->mp->get_next_move(sp->lock)) != MOVE_NONE)
    {
      bool moveIsCheck = pos.move_is_check(move, sp->dcCandidates);
      bool moveIsCapture = pos.move_is_capture(move);
      bool moveIsPassedPawnPush = pos.move_is_passed_pawn_push(move);

      assert(move_is_ok(move));

      ss[sp->ply].currentMoveCaptureValue = move_is_ep(move)?
        PawnValueMidgame : pos.midgame_value_of_piece_on(move_to(move));

      lock_grab(&(sp->lock));
      int moveCount = ++sp->moves;
      lock_release(&(sp->lock));

      ss[sp->ply].currentMove = move;

      // Decide the new search depth.
      Depth ext = extension(pos, move, true, moveIsCheck, false, false);
      Depth newDepth = sp->depth - OnePly + ext;

      // Make and search the move.
      UndoInfo u;
      pos.do_move(move, u, sp->dcCandidates);

      // Try to reduce non-pv search depth by one ply if move seems not problematic,
      // if the move fails high will be re-searched at full depth.
      if (    ext == Depth(0)
          &&  moveCount >= LMRPVMoves
          && !moveIsCapture
          && !moveIsPassedPawnPush
          && !move_promotion(move)
          && !move_is_castle(move)
          &&  move != ss[sp->ply].killer1
          &&  move != ss[sp->ply].killer2)
      {
          ss[sp->ply].reduction = OnePly;
          value = -search(pos, ss, -sp->alpha, newDepth - OnePly, sp->ply+1, true, threadID);
      }
      else
          value = sp->alpha + 1; // Just to trigger next condition

      if (value > sp->alpha) // Go with full depth non-pv search
      {
          ss[sp->ply].reduction = Depth(0);
          value = -search(pos, ss, -sp->alpha, newDepth, sp->ply+1, true, threadID);

          if (value > sp->alpha && value < sp->beta)
          {
              // When the search fails high at ply 1 while searching the first
              // move at the root, set the flag failHighPly1.  This is used for
              // time managment:  We don't want to stop the search early in
              // such cases, because resolving the fail high at ply 1 could
              // result in a big drop in score at the root.
              if (sp->ply == 1 && RootMoveNumber == 1)
                  Threads[threadID].failHighPly1 = true;

              value = -search_pv(pos, ss, -sp->beta, -sp->alpha, newDepth, sp->ply+1, threadID);
              Threads[threadID].failHighPly1 = false;
        }
      }
      pos.undo_move(move, u);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      if (thread_should_stop(threadID))
          break;

      // New best move?
      lock_grab(&(sp->lock));
      if (value > sp->bestValue && !thread_should_stop(threadID))
      {
          sp->bestValue = value;
          if (value > sp->alpha)
          {
              sp->alpha = value;
              sp_update_pv(sp->parentSstack, ss, sp->ply);
              if (value == value_mate_in(sp->ply + 1))
                  ss[sp->ply].mateKiller = move;

              if(value >= sp->beta)
              {
                  for(int i = 0; i < ActiveThreads; i++)
                      if(i != threadID && (i == sp->master || sp->slaves[i]))
                          Threads[i].stop = true;

                  sp->finished = true;
              }
        }
        // If we are at ply 1, and we are searching the first root move at
        // ply 0, set the 'Problem' variable if the score has dropped a lot
        // (from the computer's point of view) since the previous iteration:
        if (Iteration >= 2 && -value <= ValueByIteration[Iteration-1] - ProblemMargin)
            Problem = true;
      }
      lock_release(&(sp->lock));
    }

    lock_grab(&(sp->lock));

    // If this is the master thread and we have been asked to stop because of
    // a beta cutoff higher up in the tree, stop all slave threads:
    if (sp->master == threadID && thread_should_stop(threadID))
        for (int i = 0; i < ActiveThreads; i++)
            if (sp->slaves[i])
                Threads[i].stop = true;

    sp->cpus--;
    sp->slaves[threadID] = 0;

    lock_release(&(sp->lock));
  }


  /// The RootMove class

  // Constructor

  RootMove::RootMove() {
    nodes = cumulativeNodes = 0ULL;
  }

  // RootMove::operator<() is the comparison function used when
  // sorting the moves.  A move m1 is considered to be better
  // than a move m2 if it has a higher score, or if the moves
  // have equal score but m1 has the higher node count.

  bool RootMove::operator<(const RootMove& m) {

    if (score != m.score)
        return (score < m.score);

    return nodes <= m.nodes;
  }

  /// The RootMoveList class

  // Constructor

  RootMoveList::RootMoveList(Position& pos, Move searchMoves[]) : count(0) {

    MoveStack mlist[MaxRootMoves];
    bool includeAllMoves = (searchMoves[0] == MOVE_NONE);

    // Generate all legal moves
    int lm_count = generate_legal_moves(pos, mlist);

    // Add each move to the moves[] array
    for (int i = 0; i < lm_count; i++)
    {
        bool includeMove = includeAllMoves;

        for (int k = 0; !includeMove && searchMoves[k] != MOVE_NONE; k++)
            includeMove = (searchMoves[k] == mlist[i].move);

        if (includeMove)
        {
            // Find a quick score for the move
            UndoInfo u;
            SearchStack ss[PLY_MAX_PLUS_2];

            moves[count].move = mlist[i].move;
            moves[count].nodes = 0ULL;
            pos.do_move(moves[count].move, u);
            moves[count].score = -qsearch(pos, ss, -VALUE_INFINITE, VALUE_INFINITE,
                                          Depth(0), 1, 0);
            pos.undo_move(moves[count].move, u);
            moves[count].pv[0] = moves[i].move;
            moves[count].pv[1] = MOVE_NONE; // FIXME
            count++;
        }
    }
    sort();
  }


  // Simple accessor methods for the RootMoveList class

  inline Move RootMoveList::get_move(int moveNum) const {
    return moves[moveNum].move;
  }

  inline Value RootMoveList::get_move_score(int moveNum) const {
    return moves[moveNum].score;
  }

  inline void RootMoveList::set_move_score(int moveNum, Value score) {
    moves[moveNum].score = score;
  }

  inline void RootMoveList::set_move_nodes(int moveNum, int64_t nodes) {
    moves[moveNum].nodes = nodes;
    moves[moveNum].cumulativeNodes += nodes;
  }

  void RootMoveList::set_move_pv(int moveNum, const Move pv[]) {
    int j;
    for(j = 0; pv[j] != MOVE_NONE; j++)
      moves[moveNum].pv[j] = pv[j];
    moves[moveNum].pv[j] = MOVE_NONE;
  }

  inline Move RootMoveList::get_move_pv(int moveNum, int i) const {
    return moves[moveNum].pv[i];
  }

  inline int64_t RootMoveList::get_move_cumulative_nodes(int moveNum) const {
    return moves[moveNum].cumulativeNodes;
  }

  inline int RootMoveList::move_count() const {
    return count;
  }


  // RootMoveList::scan_for_easy_move() is called at the end of the first
  // iteration, and is used to detect an "easy move", i.e. a move which appears
  // to be much bester than all the rest.  If an easy move is found, the move
  // is returned, otherwise the function returns MOVE_NONE.  It is very
  // important that this function is called at the right moment:  The code
  // assumes that the first iteration has been completed and the moves have
  // been sorted. This is done in RootMoveList c'tor.

  Move RootMoveList::scan_for_easy_move() const {

    assert(count);

    if (count == 1)
        return get_move(0);

    // moves are sorted so just consider the best and the second one
    if (get_move_score(0) > get_move_score(1) + EasyMoveMargin)
        return get_move(0);

    return MOVE_NONE;
  }

  // RootMoveList::sort() sorts the root move list at the beginning of a new
  // iteration.

  inline void RootMoveList::sort() {

    sort_multipv(count - 1); // all items
  }


  // RootMoveList::sort_multipv() sorts the first few moves in the root move
  // list by their scores and depths. It is used to order the different PVs
  // correctly in MultiPV mode.

  void RootMoveList::sort_multipv(int n) {

    for (int i = 1; i <= n; i++)
    {
      RootMove rm = moves[i];
      int j;
      for (j = i; j > 0 && moves[j-1] < rm; j--)
          moves[j] = moves[j-1];
      moves[j] = rm;
    }
  }


  // init_search_stack() initializes a search stack at the beginning of a
  // new search from the root.

  void init_search_stack(SearchStack ss[]) {
    for(int i = 0; i < 3; i++) {
      ss[i].pv[i] = MOVE_NONE;
      ss[i].pv[i+1] = MOVE_NONE;
      ss[i].currentMove = MOVE_NONE;
      ss[i].mateKiller = MOVE_NONE;
      ss[i].killer1 = MOVE_NONE;
      ss[i].killer2 = MOVE_NONE;
      ss[i].threatMove = MOVE_NONE;
      ss[i].reduction = Depth(0);
    }
  }


  // init_node() is called at the beginning of all the search functions
  // (search(), search_pv(), qsearch(), and so on) and initializes the search
  // stack object corresponding to the current node.  Once every
  // NodesBetweenPolls nodes, init_node() also calls poll(), which polls
  // for user input and checks whether it is time to stop the search.

  void init_node(const Position &pos, SearchStack ss[], int ply, int threadID) {
    assert(ply >= 0 && ply < PLY_MAX);
    assert(threadID >= 0 && threadID < ActiveThreads);

    Threads[threadID].nodes++;

    if(threadID == 0) {
      NodesSincePoll++;
      if(NodesSincePoll >= NodesBetweenPolls) {
        poll();
        NodesSincePoll = 0;
      }
    }

    ss[ply].pv[ply] = ss[ply].pv[ply+1] = ss[ply].currentMove = MOVE_NONE;
    ss[ply+2].mateKiller = MOVE_NONE;
    ss[ply+2].killer1 = ss[ply+2].killer2 = MOVE_NONE;
    ss[ply].threatMove = MOVE_NONE;
    ss[ply].reduction = Depth(0);
    ss[ply].currentMoveCaptureValue = Value(0);

    if(Threads[threadID].printCurrentLine)
      print_current_line(ss, ply, threadID);
  }


  // update_pv() is called whenever a search returns a value > alpha.  It
  // updates the PV in the SearchStack object corresponding to the current
  // node.

  void update_pv(SearchStack ss[], int ply) {
    assert(ply >= 0 && ply < PLY_MAX);

    ss[ply].pv[ply] = ss[ply].currentMove;
    int p;
    for(p = ply + 1; ss[ply+1].pv[p] != MOVE_NONE; p++)
      ss[ply].pv[p] = ss[ply+1].pv[p];
    ss[ply].pv[p] = MOVE_NONE;
  }


  // sp_update_pv() is a variant of update_pv for use at split points.  The
  // difference between the two functions is that sp_update_pv also updates
  // the PV at the parent node.

  void sp_update_pv(SearchStack *pss, SearchStack ss[], int ply) {
    assert(ply >= 0 && ply < PLY_MAX);

    ss[ply].pv[ply] = pss[ply].pv[ply] = ss[ply].currentMove;
    int p;
    for(p = ply + 1; ss[ply+1].pv[p] != MOVE_NONE; p++)
      ss[ply].pv[p] = pss[ply].pv[p] = ss[ply+1].pv[p];
    ss[ply].pv[p] = pss[ply].pv[p] = MOVE_NONE;
  }


  // connected_moves() tests whether two moves are 'connected' in the sense
  // that the first move somehow made the second move possible (for instance
  // if the moving piece is the same in both moves).  The first move is
  // assumed to be the move that was made to reach the current position, while
  // the second move is assumed to be a move from the current position.

  bool connected_moves(const Position &pos, Move m1, Move m2) {
    Square f1, t1, f2, t2;

    assert(move_is_ok(m1));
    assert(move_is_ok(m2));

    if(m2 == MOVE_NONE)
      return false;

    // Case 1: The moving piece is the same in both moves.
    f2 = move_from(m2);
    t1 = move_to(m1);
    if(f2 == t1)
      return true;

    // Case 2: The destination square for m2 was vacated by m1.
    t2 = move_to(m2);
    f1 = move_from(m1);
    if(t2 == f1)
      return true;

    // Case 3: Moving through the vacated square:
    if(piece_is_slider(pos.piece_on(f2)) &&
       bit_is_set(squares_between(f2, t2), f1))
      return true;

    // Case 4: The destination square for m2 is attacked by the moving piece
    // in m1:
    if(pos.piece_attacks_square(t1, t2))
      return true;

    // Case 5: Discovered check, checking piece is the piece moved in m1:
    if(piece_is_slider(pos.piece_on(t1)) &&
       bit_is_set(squares_between(t1, pos.king_square(pos.side_to_move())),
                  f2) &&
       !bit_is_set(squares_between(t2, pos.king_square(pos.side_to_move())),
                   t2)) {
      Bitboard occ = pos.occupied_squares();
      Color us = pos.side_to_move();
      Square ksq = pos.king_square(us);
      clear_bit(&occ, f2);
      if(pos.type_of_piece_on(t1) == BISHOP) {
        if(bit_is_set(bishop_attacks_bb(ksq, occ), t1))
          return true;
      }
      else if(pos.type_of_piece_on(t1) == ROOK) {
        if(bit_is_set(rook_attacks_bb(ksq, occ), t1))
          return true;
      }
      else {
        assert(pos.type_of_piece_on(t1) == QUEEN);
        if(bit_is_set(queen_attacks_bb(ksq, occ), t1))
          return true;
      }
    }

    return false;
  }


  // extension() decides whether a move should be searched with normal depth,
  // or with extended depth.  Certain classes of moves (checking moves, in
  // particular) are searched with bigger depth than ordinary moves.

  Depth extension(const Position &pos, Move m, bool pvNode,
                  bool check, bool singleReply, bool mateThreat) {

    Depth result = Depth(0);

    if (check)
        result += CheckExtension[pvNode];

    if (singleReply)
        result += SingleReplyExtension[pvNode];

    if (pos.move_is_pawn_push_to_7th(m))
        result += PawnPushTo7thExtension[pvNode];

    if (pos.move_is_passed_pawn_push(m))
        result += PassedPawnExtension[pvNode];

    if (mateThreat)
        result += MateThreatExtension[pvNode];

    if (   pos.midgame_value_of_piece_on(move_to(m)) >= RookValueMidgame
        && (  pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK)
            - pos.midgame_value_of_piece_on(move_to(m)) == Value(0))
        && !move_promotion(m))
        result += PawnEndgameExtension[pvNode];
    
    if (   pvNode
        && pos.move_is_capture(m)
        && pos.type_of_piece_on(move_to(m)) != PAWN
        && pos.see(m) >= 0)
        result += OnePly/2;

    return Min(result, OnePly);
  }


  // ok_to_do_nullmove() looks at the current position and decides whether
  // doing a 'null move' should be allowed.  In order to avoid zugzwang
  // problems, null moves are not allowed when the side to move has very
  // little material left.  Currently, the test is a bit too simple:  Null
  // moves are avoided only when the side to move has only pawns left.  It's
  // probably a good idea to avoid null moves in at least some more
  // complicated endgames, e.g. KQ vs KR.  FIXME

  bool ok_to_do_nullmove(const Position &pos) {
    if(pos.non_pawn_material(pos.side_to_move()) == Value(0))
      return false;
    return true;
  }


  // ok_to_prune() tests whether it is safe to forward prune a move.  Only
  // non-tactical moves late in the move list close to the leaves are
  // candidates for pruning.

  bool ok_to_prune(const Position &pos, Move m, Move threat, Depth d) {
    Square mfrom, mto, tfrom, tto;

    assert(move_is_ok(m));
    assert(threat == MOVE_NONE || move_is_ok(threat));
    assert(!move_promotion(m));
    assert(!pos.move_is_check(m));
    assert(!pos.move_is_capture(m));
    assert(!pos.move_is_passed_pawn_push(m));
    assert(d >= OnePly);

    mfrom = move_from(m);
    mto = move_to(m);
    tfrom = move_from(threat);
    tto = move_to(threat);

    // Case 1: Castling moves are never pruned.
    if(move_is_castle(m))
      return false;

    // Case 2: Don't prune moves which move the threatened piece
    if(!PruneEscapeMoves && threat != MOVE_NONE && mfrom == tto)
      return false;

    // Case 3: If the threatened piece has value less than or equal to the
    // value of the threatening piece, don't prune move which defend it.
    if(!PruneDefendingMoves && threat != MOVE_NONE
       && (piece_value_midgame(pos.piece_on(tfrom))
           >= piece_value_midgame(pos.piece_on(tto)))
       && pos.move_attacks_square(m, tto))
      return false;

    // Case 4: Don't prune moves with good history.
    if(!H.ok_to_prune(pos.piece_on(move_from(m)), m, d))
      return false;

    // Case 5: If the moving piece in the threatened move is a slider, don't
    // prune safe moves which block its ray.
    if(!PruneBlockingMoves && threat != MOVE_NONE
       && piece_is_slider(pos.piece_on(tfrom))
       && bit_is_set(squares_between(tfrom, tto), mto) && pos.see(m) >= 0)
      return false;

    return true;
  }


  // ok_to_use_TT() returns true if a transposition table score
  // can be used at a given point in search.

  bool ok_to_use_TT(const TTEntry* tte, Depth depth, Value beta, int ply) {

    Value v = value_from_tt(tte->value(), ply);

    return   (   tte->depth() >= depth
              || v >= Max(value_mate_in(100), beta)
              || v < Min(value_mated_in(100), beta))

          && (   (is_lower_bound(tte->type()) && v >= beta)
              || (is_upper_bound(tte->type()) && v < beta));
  }


  // ok_to_history() returns true if a move m can be stored
  // in history. Should be a non capturing move.

  bool ok_to_history(const Position& pos, Move m) {

    return    pos.square_is_empty(move_to(m))
          && !move_promotion(m)
          && !move_is_ep(m);
  }


  // update_history() registers a good move that produced a beta-cutoff
  // in history and marks as failures all the other moves of that ply.

  void update_history(const Position& pos, Move m, Depth depth,
                      Move movesSearched[], int moveCount) {

    H.success(pos.piece_on(move_from(m)), m, depth);

    for (int i = 0; i < moveCount - 1; i++)
        if (ok_to_history(pos, movesSearched[i]) && m != movesSearched[i])
            H.failure(pos.piece_on(move_from(movesSearched[i])), movesSearched[i]);
  }

  // fail_high_ply_1() checks if some thread is currently resolving a fail
  // high at ply 1 at the node below the first root node.  This information
  // is used for time managment.

  bool fail_high_ply_1() {
    for(int i = 0; i < ActiveThreads; i++)
      if(Threads[i].failHighPly1)
        return true;
    return false;
  }


  // current_search_time() returns the number of milliseconds which have passed
  // since the beginning of the current search.

  int current_search_time() {
    return get_system_time() - SearchStartTime;
  }


  // nps() computes the current nodes/second count.

  int nps() {
    int t = current_search_time();
    return (t > 0)? int((nodes_searched() * 1000) / t) : 0;
  }


  // poll() performs two different functions:  It polls for user input, and it
  // looks at the time consumed so far and decides if it's time to abort the
  // search.

  void poll() {

    static int lastInfoTime;
    int t = current_search_time();

    //  Poll for input
    if (Bioskey())
    {
        // We are line oriented, don't read single chars
        std::string command;
        if (!std::getline(std::cin, command))
            command = "quit";

        if (command == "quit")
        {
            AbortSearch = true;
            PonderSearch = false;
            Quit = true;
        }
        else if(command == "stop")
        {
            AbortSearch = true;
            PonderSearch = false;
        }
        else if(command == "ponderhit")
            ponderhit();
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
        lock_grab(&IOLock);
        if (dbg_show_mean)
            dbg_print_mean();

        if (dbg_show_hit_rate)
            dbg_print_hit_rate();

        std::cout << "info nodes " << nodes_searched() << " nps " << nps()
                  << " time " << t << " hashfull " << TT.full() << std::endl;
        lock_release(&IOLock);
        if (ShowCurrentLine)
            Threads[0].printCurrentLine = true;
    }
    // Should we stop the search?
    if (PonderSearch)
        return;

    bool overTime =     t > AbsoluteMaxSearchTime
                     || (RootMoveNumber == 1 && t > MaxSearchTime + ExtraSearchTime)
                     || (  !FailHigh && !fail_high_ply_1() && !Problem
                         && t > 6*(MaxSearchTime + ExtraSearchTime));

    if (   (Iteration >= 2 && (!InfiniteSearch && overTime))
        || (ExactMaxTime && t >= ExactMaxTime)
        || (Iteration >= 3 && MaxNodes && nodes_searched() >= MaxNodes))
        AbortSearch = true;
  }


  // ponderhit() is called when the program is pondering (i.e. thinking while
  // it's the opponent's turn to move) in order to let the engine know that
  // it correctly predicted the opponent's move.

  void ponderhit() {
    int t = current_search_time();
    PonderSearch = false;
    if(Iteration >= 2 &&
       (!InfiniteSearch && (StopOnPonderhit ||
                            t > AbsoluteMaxSearchTime ||
                            (RootMoveNumber == 1 &&
                             t > MaxSearchTime + ExtraSearchTime) ||
                            (!FailHigh && !fail_high_ply_1() && !Problem &&
                             t > 6*(MaxSearchTime + ExtraSearchTime)))))
      AbortSearch = true;
  }


  // print_current_line() prints the current line of search for a given
  // thread.  Called when the UCI option UCI_ShowCurrLine is 'true'.

  void print_current_line(SearchStack ss[], int ply, int threadID) {
    assert(ply >= 0 && ply < PLY_MAX);
    assert(threadID >= 0 && threadID < ActiveThreads);

    if(!Threads[threadID].idle) {
      lock_grab(&IOLock);
      std::cout << "info currline " << (threadID + 1);
      for(int p = 0; p < ply; p++)
        std::cout << " " << ss[p].currentMove;
      std::cout << std::endl;
      lock_release(&IOLock);
    }
    Threads[threadID].printCurrentLine = false;
    if(threadID + 1 < ActiveThreads)
      Threads[threadID + 1].printCurrentLine = true;
  }


  // wait_for_stop_or_ponderhit() is called when the maximum depth is reached
  // while the program is pondering.  The point is to work around a wrinkle in
  // the UCI protocol:  When pondering, the engine is not allowed to give a
  // "bestmove" before the GUI sends it a "stop" or "ponderhit" command.
  // We simply wait here until one of these commands is sent, and return,
  // after which the bestmove and pondermove will be printed (in id_loop()).

  void wait_for_stop_or_ponderhit() {
    std::string command;

    while(true) {
      if(!std::getline(std::cin, command))
        command = "quit";

      if(command == "quit") {
        OpeningBook.close();
        stop_threads();
        quit_eval();
        exit(0);
      }
      else if(command == "ponderhit" || command == "stop")
        break;
    }
  }


  // idle_loop() is where the threads are parked when they have no work to do.
  // The parameter "waitSp", if non-NULL, is a pointer to an active SplitPoint
  // object for which the current thread is the master.

  void idle_loop(int threadID, SplitPoint *waitSp) {
    assert(threadID >= 0 && threadID < THREAD_MAX);

    Threads[threadID].running = true;

    while(true) {
      if(AllThreadsShouldExit && threadID != 0)
        break;

      // If we are not thinking, wait for a condition to be signaled instead
      // of wasting CPU time polling for work:
      while(threadID != 0 && (Idle || threadID >= ActiveThreads)) {
#if !defined(_MSC_VER)
        pthread_mutex_lock(&WaitLock);
        if(Idle || threadID >= ActiveThreads)
          pthread_cond_wait(&WaitCond, &WaitLock);
        pthread_mutex_unlock(&WaitLock);
#else
        WaitForSingleObject(SitIdleEvent[threadID], INFINITE);
#endif
      }

      // If this thread has been assigned work, launch a search:
      if(Threads[threadID].workIsWaiting) {
        Threads[threadID].workIsWaiting = false;
        if(Threads[threadID].splitPoint->pvNode)
          sp_search_pv(Threads[threadID].splitPoint, threadID);
        else
          sp_search(Threads[threadID].splitPoint, threadID);
        Threads[threadID].idle = true;
      }

      // If this thread is the master of a split point and all threads have
      // finished their work at this split point, return from the idle loop:
      if(waitSp != NULL && waitSp->cpus == 0)
        return;
    }

    Threads[threadID].running = false;
  }


  // init_split_point_stack() is called during program initialization, and
  // initializes all split point objects.

  void init_split_point_stack() {
    for(int i = 0; i < THREAD_MAX; i++)
      for(int j = 0; j < MaxActiveSplitPoints; j++) {
        SplitPointStack[i][j].parent = NULL;
        lock_init(&(SplitPointStack[i][j].lock), NULL);
      }
  }


  // destroy_split_point_stack() is called when the program exits, and
  // destroys all locks in the precomputed split point objects.

  void destroy_split_point_stack() {
    for(int i = 0; i < THREAD_MAX; i++)
      for(int j = 0; j < MaxActiveSplitPoints; j++)
        lock_destroy(&(SplitPointStack[i][j].lock));
  }


  // thread_should_stop() checks whether the thread with a given threadID has
  // been asked to stop, directly or indirectly.  This can happen if a beta
  // cutoff has occured in thre thread's currently active split point, or in
  // some ancestor of the current split point.

  bool thread_should_stop(int threadID) {
    assert(threadID >= 0 && threadID < ActiveThreads);

    SplitPoint *sp;

    if(Threads[threadID].stop)
      return true;
    if(ActiveThreads <= 2)
      return false;
    for(sp = Threads[threadID].splitPoint; sp != NULL; sp = sp->parent)
      if(sp->finished) {
        Threads[threadID].stop = true;
        return true;
      }
    return false;
  }


  // thread_is_available() checks whether the thread with threadID "slave" is
  // available to help the thread with threadID "master" at a split point.  An
  // obvious requirement is that "slave" must be idle.  With more than two
  // threads, this is not by itself sufficient:  If "slave" is the master of
  // some active split point, it is only available as a slave to the other
  // threads which are busy searching the split point at the top of "slave"'s
  // split point stack (the "helpful master concept" in YBWC terminology).

  bool thread_is_available(int slave, int master) {
    assert(slave >= 0 && slave < ActiveThreads);
    assert(master >= 0 && master < ActiveThreads);
    assert(ActiveThreads > 1);

    if(!Threads[slave].idle || slave == master)
      return false;

    if(Threads[slave].activeSplitPoints == 0)
      // No active split points means that the thread is available as a slave
      // for any other thread.
      return true;

    if(ActiveThreads == 2)
      return true;

    // Apply the "helpful master" concept if possible.
    if(SplitPointStack[slave][Threads[slave].activeSplitPoints-1].slaves[master])
      return true;

    return false;
  }


  // idle_thread_exists() tries to find an idle thread which is available as
  // a slave for the thread with threadID "master".

  bool idle_thread_exists(int master) {
    assert(master >= 0 && master < ActiveThreads);
    assert(ActiveThreads > 1);

    for(int i = 0; i < ActiveThreads; i++)
      if(thread_is_available(i, master))
        return true;
    return false;
  }


  // split() does the actual work of distributing the work at a node between
  // several threads at PV nodes.  If it does not succeed in splitting the
  // node (because no idle threads are available, or because we have no unused
  // split point objects), the function immediately returns false.  If
  // splitting is possible, a SplitPoint object is initialized with all the
  // data that must be copied to the helper threads (the current position and
  // search stack, alpha, beta, the search depth, etc.), and we tell our
  // helper threads that they have been assigned work.  This will cause them
  // to instantly leave their idle loops and call sp_search_pv().  When all
  // threads have returned from sp_search_pv (or, equivalently, when
  // splitPoint->cpus becomes 0), split() returns true.

  bool split(const Position &p, SearchStack *sstck, int ply,
             Value *alpha, Value *beta, Value *bestValue,
             Depth depth, int *moves,
             MovePicker *mp, Bitboard dcCandidates, int master, bool pvNode) {
    assert(p.is_ok());
    assert(sstck != NULL);
    assert(ply >= 0 && ply < PLY_MAX);
    assert(*bestValue >= -VALUE_INFINITE && *bestValue <= *alpha);
    assert(!pvNode || *alpha < *beta);
    assert(*beta <= VALUE_INFINITE);
    assert(depth > Depth(0));
    assert(master >= 0 && master < ActiveThreads);
    assert(ActiveThreads > 1);

    SplitPoint *splitPoint;
    int i;

    lock_grab(&MPLock);

    // If no other thread is available to help us, or if we have too many
    // active split points, don't split:
    if(!idle_thread_exists(master) ||
       Threads[master].activeSplitPoints >= MaxActiveSplitPoints) {
      lock_release(&MPLock);
      return false;
    }

    // Pick the next available split point object from the split point stack:
    splitPoint = SplitPointStack[master] + Threads[master].activeSplitPoints;
    Threads[master].activeSplitPoints++;

    // Initialize the split point object:
    splitPoint->parent = Threads[master].splitPoint;
    splitPoint->finished = false;
    splitPoint->ply = ply;
    splitPoint->depth = depth;
    splitPoint->alpha = pvNode? *alpha : (*beta - 1);
    splitPoint->beta = *beta;
    splitPoint->pvNode = pvNode;
    splitPoint->dcCandidates = dcCandidates;
    splitPoint->bestValue = *bestValue;
    splitPoint->master = master;
    splitPoint->mp = mp;
    splitPoint->moves = *moves;
    splitPoint->cpus = 1;
    splitPoint->pos.copy(p);
    splitPoint->parentSstack = sstck;
    for(i = 0; i < ActiveThreads; i++)
      splitPoint->slaves[i] = 0;

    // Copy the current position and the search stack to the master thread:
    memcpy(splitPoint->sstack[master], sstck, (ply+1)*sizeof(SearchStack));
    Threads[master].splitPoint = splitPoint;

    // Make copies of the current position and search stack for each thread:
    for(i = 0; i < ActiveThreads && splitPoint->cpus < MaxThreadsPerSplitPoint;
        i++)
      if(thread_is_available(i, master)) {
        memcpy(splitPoint->sstack[i], sstck, (ply+1)*sizeof(SearchStack));
        Threads[i].splitPoint = splitPoint;
        splitPoint->slaves[i] = 1;
        splitPoint->cpus++;
      }

    // Tell the threads that they have work to do.  This will make them leave
    // their idle loop.
    for(i = 0; i < ActiveThreads; i++)
      if(i == master || splitPoint->slaves[i]) {
        Threads[i].workIsWaiting = true;
        Threads[i].idle = false;
        Threads[i].stop = false;
      }

    lock_release(&MPLock);

    // Everything is set up.  The master thread enters the idle loop, from
    // which it will instantly launch a search, because its workIsWaiting
    // slot is 'true'.  We send the split point as a second parameter to the
    // idle loop, which means that the main thread will return from the idle
    // loop when all threads have finished their work at this split point
    // (i.e. when // splitPoint->cpus == 0).
    idle_loop(master, splitPoint);

    // We have returned from the idle loop, which means that all threads are
    // finished.  Update alpha, beta and bestvalue, and return:
    lock_grab(&MPLock);
    if(pvNode) *alpha = splitPoint->alpha;
    *beta = splitPoint->beta;
    *bestValue = splitPoint->bestValue;
    Threads[master].stop = false;
    Threads[master].idle = false;
    Threads[master].activeSplitPoints--;
    Threads[master].splitPoint = splitPoint->parent;
    lock_release(&MPLock);

    return true;
  }


  // wake_sleeping_threads() wakes up all sleeping threads when it is time
  // to start a new search from the root.

  void wake_sleeping_threads() {
    if(ActiveThreads > 1) {
      for(int i = 1; i < ActiveThreads; i++) {
        Threads[i].idle = true;
        Threads[i].workIsWaiting = false;
      }
#if !defined(_MSC_VER)
      pthread_mutex_lock(&WaitLock);
      pthread_cond_broadcast(&WaitCond);
      pthread_mutex_unlock(&WaitLock);
#else
      for(int i = 1; i < THREAD_MAX; i++)
        SetEvent(SitIdleEvent[i]);
#endif
    }
  }


  // init_thread() is the function which is called when a new thread is
  // launched.  It simply calls the idle_loop() function with the supplied
  // threadID.  There are two versions of this function; one for POSIX threads
  // and one for Windows threads.

#if !defined(_MSC_VER)

  void *init_thread(void *threadID) {
    idle_loop(*(int *)threadID, NULL);
    return NULL;
  }

#else

  DWORD WINAPI init_thread(LPVOID threadID) {
    idle_loop(*(int *)threadID, NULL);
    return NULL;
  }

#endif

}
