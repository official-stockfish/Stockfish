// learning routines
//
// 1) Automatic generation of game records
// → "gensfen" command
// 2) Learning evaluation function parameters from the generated game record
// → "learn" command
// → Shuffle in the teacher phase is also an extension of this command.
// Example) "learn shuffle"
// 3) Automatic generation of fixed traces
// → "makebook think" command
// → implemented in extra/book/book.cpp
// 4) Post-station automatic review mode
// → I will not be involved in the engine because it is a problem that the GUI should assist.
// etc..

#if defined(EVAL_LEARN)

#include <filesystem>
#include <random>
#include <regex>

#include "learn.h"
#include "multi_think.h"
#include "../uci.h"

// evaluate header for learning
#include "../eval/evaluate_common.h"

// ----------------------
// constant string based on the settings
// ----------------------

// Character string according to update formula. (Output for debugging.)
// Implemented various update expressions, but concluded that AdaGrad is the best in terms of speed and memory.
#if defined(ADA_GRAD_UPDATE)
#define LEARN_UPDATE "AdaGrad"
#elif defined(SGD_UPDATE)
#define LEARN_UPDATE "SGD"
#endif

#if defined(LOSS_FUNCTION_IS_WINNING_PERCENTAGE)
#define LOSS_FUNCTION "WINNING_PERCENTAGE"
#elif defined(LOSS_FUNCTION_IS_CROSS_ENTOROPY)
#define LOSS_FUNCTION "CROSS_ENTOROPY"
#elif defined(LOSS_FUNCTION_IS_CROSS_ENTOROPY_FOR_VALUE)
#define LOSS_FUNCTION "CROSS_ENTOROPY_FOR_VALUE"
#elif defined(LOSS_FUNCTION_IS_ELMO_METHOD)
#define LOSS_FUNCTION "ELMO_METHOD(WCSC27)"
#endif

// -----------------------------------
// Below, the implementation section.
// -----------------------------------

#include <sstream>
#include <fstream>
#include <unordered_set>
#include <iomanip>
#include <list>
#include <cmath>	// std::exp(),std::pow(),std::log()
#include <cstring>	// memcpy()

#if defined (_OPENMP)
#include <omp.h>
#endif

#if defined(_MSC_VER)
// The C++ filesystem cannot be used unless it is C++17 or later or MSVC.
// I tried to use windows.h, but with g++ of msys2 I can not get the files in the folder well.
// Use dirent.h because there is no help for it.
#include <filesystem>
#elif defined(__GNUC__)
#include <dirent.h>
#endif

#include "../misc.h"
#include "../thread.h"
#include "../position.h"
//#include "../extra/book/book.h"
#include "../tt.h"
#include "multi_think.h"

#if defined(EVAL_NNUE)
#include "../eval/nnue/evaluate_nnue_learner.h"
#include <shared_mutex>
#endif

using namespace std;

//// This is defined in the search section.
//extern Book::BookMoveSelector book;

// Addition and subtraction definition for atomic<T>
// Aligned with atomicAdd() in Apery/learner.hpp.
template <typename T>
T operator += (std::atomic<T>& x, const T rhs)
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

// Phase array: PSVector stands for packed sfen vector.
typedef std::vector<PackedSfenValue> PSVector;

bool use_draw_in_training_data_generation = false;
bool use_draw_in_training = false;
bool use_draw_in_validation = false;
bool use_hash_in_training = true;

// -----------------------------------
// write phase file
// -----------------------------------

// Helper class for exporting Sfen
struct SfenWriter
{
		// File name to write and number of threads to create
	SfenWriter(string filename, int thread_num)
	{
		sfen_buffers_pool.reserve((size_t)thread_num * 10);
		sfen_buffers.resize(thread_num);

		// When performing additional learning, the quality of the teacher generated after learning the evaluation function does not change much and I want to earn more teacher positions.
		// Since it is preferable that old teachers also use it, it has such a specification.
		fs.open(filename, ios::out | ios::binary | ios::app);
		filename_ = filename;

		finished = false;
	}

	~SfenWriter()
	{
		finished = true;
		file_worker_thread.join();
		fs.close();

		// all buffers should be empty since file_worker_thread has written all..
		for (auto p : sfen_buffers) { assert(p == nullptr); }
		assert(sfen_buffers_pool.empty());
	}

	// For each thread, flush the file by this number of phases.
	const size_t SFEN_WRITE_SIZE = 5000;

	// write one by pairing the phase and evaluation value (in packed sfen format)
	void write(size_t thread_id, const PackedSfenValue& psv)
	{
		// We have a buffer for each thread and add it there.
		// If the buffer overflows, write it to a file.

		// This buffer is prepared for each thread.
		auto& buf = sfen_buffers[thread_id];

		// Secure since there is no buf at the first time and immediately after writing the thread buffer.
		if (!buf)
		{
			buf = new PSVector();
			buf->reserve(SFEN_WRITE_SIZE);
		}

		// It is prepared for each thread, so one thread does not call this write() function at the same time.
		// There is no need to exclude at this point.
		buf->push_back(psv);

		if (buf->size() >= SFEN_WRITE_SIZE)
		{
			// If you load it in sfen_buffers_pool, the worker will do the rest.

			// Mutex lock is required when changing the contents of sfen_buffers_pool.
			std::unique_lock<std::mutex> lk(mutex);
			sfen_buffers_pool.push_back(buf);

			buf = nullptr;
			// If you set buf == nullptr, the buffer will be allocated the next time this function is called.
		}
	}

	// Move what remains in the buffer for your thread to a buffer for writing to a file.
	void finalize(size_t thread_id)
	{
		std::unique_lock<std::mutex> lk(mutex);

		auto& buf = sfen_buffers[thread_id];

		// There is a case that buf==nullptr, so that check is necessary.
		if (buf && buf->size() != 0)
			sfen_buffers_pool.push_back(buf);

		buf = nullptr;
	}

	// Start the write_worker thread.
	void start_file_write_worker()
	{
		file_worker_thread = std::thread([&] { this->file_write_worker(); });
	}

	// Dedicated thread to write to file
	void file_write_worker()
	{
		auto output_status = [&]()
		{
			// also output the current time
			sync_cout << endl << sfen_write_count << " sfens , at " << now_string() << sync_endl;

			// This is enough for flush().
			fs.flush();
		};

		while (!finished || sfen_buffers_pool.size())
		{
			vector<PSVector*> buffers;
			{
				std::unique_lock<std::mutex> lk(mutex);

				// copy the whole
				buffers = sfen_buffers_pool;
				sfen_buffers_pool.clear();
			}

			// sleep() if you didn't get anything
			if (!buffers.size())
				sleep(100);
			else
			{
				for (auto ptr : buffers)
				{
					fs.write((const char*)&((*ptr)[0]), sizeof(PackedSfenValue) * ptr->size());

					sfen_write_count += ptr->size();

#if 1
					// Add the processed number here, and if it exceeds save_every, change the file name and reset this counter.
					save_every_counter += ptr->size();
					if (save_every_counter >= save_every)
					{
						save_every_counter = 0;
						// Change the file name.

						fs.close();

						// Sequential number attached to the file
						int n = (int)(sfen_write_count / save_every);
						// Rename the file and open it again. Add ios::app in consideration of overwriting. (Depending on the operation, it may not be necessary.)
						string filename = filename_ + "_" + std::to_string(n);
						fs.open(filename, ios::out | ios::binary | ios::app);
						cout << endl << "output sfen file = " << filename << endl;
					}
#endif

					// Output'.' every time when writing a game record.
					std::cout << ".";

					// Output the number of phases processed every 40 times
					// Finally, the remainder of the teacher phase of each thread is written out, so halfway numbers are displayed, but is it okay?
					// If you overuse the threads to the maximum number of logical cores, the console will be clogged, so it may be a little more loose.
					if ((++time_stamp_count % 40) == 0)
						output_status();

					// Since this memory is unnecessary, release it at this timing.
					delete ptr;
				}
			}
		}

		// Output the time stamp again before the end.
		output_status();
	}

	// Change the file name in this unit.
	uint64_t save_every = UINT64_MAX;

private:

	fstream fs;

	// File name passed in the constructor
	std::string filename_;

	// Add the processed number here, and if it exceeds save_every, change the file name and reset this counter.
	uint64_t save_every_counter = 0;

	// thread to write to the file
	std::thread file_worker_thread;
	// Flag that all threads have finished
	atomic<bool> finished;

	// Counter for time stamp output
	uint64_t time_stamp_count = 0;

	// buffer before writing to file
	// sfen_buffers is the buffer for each thread
	// sfen_buffers_pool is a buffer for writing.
	// After loading the phase in the former buffer by SFEN_WRITE_SIZE, transfer it to the latter.
	std::vector<PSVector*> sfen_buffers;
	std::vector<PSVector*> sfen_buffers_pool;

	// Mutex required to access sfen_buffers_pool
	std::mutex mutex;

	// number of written phases
	uint64_t sfen_write_count = 0;
};

// -----------------------------------
// worker that creates the game record (for each thread)
// -----------------------------------

// Class to generate sfen with multiple threads
struct MultiThinkGenSfen : public MultiThink
{
	MultiThinkGenSfen(int search_depth_, int search_depth2_, SfenWriter& sw_)
		: search_depth(search_depth_), search_depth2(search_depth2_), sw(sw_)
	{
		hash.resize(GENSFEN_HASH_SIZE);

		// Output for confirmation if the same random seed is not drawn when parallelizing and gensfening the PC.
		std::cout << prng << std::endl;
	}

	virtual void thread_worker(size_t thread_id);
	void start_file_write_worker() { sw.start_file_write_worker(); }

	// search_depth = search depth for normal search
	int search_depth;
	int search_depth2;

	// Upper limit of evaluation value of generated situation
	int eval_limit;

	// minimum ply with random move
	int random_move_minply;
	// maximum ply with random move
	int random_move_maxply;
	// Number of random moves in one station
	int random_move_count;
	// Move balls with a probability of 1/N when randomly moving like Apery.
	// When you move the ball again, there is a 1/N chance that it will randomly move once in the opponent's number.
	// Apery has N=2. Specifying 0 here disables this function.
	int random_move_like_apery;

	// For when using multi pv instead of random move.
	// random_multi_pv is the number of candidates for MultiPV.
	// When adopting the move of the candidate move, the difference between the evaluation value of the move of the 1st place and the evaluation value of the move of the Nth place is
	// Must be in the range random_multi_pv_diff.
	// random_multi_pv_depth is the search depth for MultiPV.
	int random_multi_pv;
	int random_multi_pv_diff;
	int random_multi_pv_depth;

	// The minimum and maximum ply (number of steps from the initial phase) of the phase to write out.
	int write_minply;
	int write_maxply;

	// sfen exporter
	SfenWriter& sw;

	// hash to limit the export of the same phase
	// It must be 2**N because it will be used as the mask to calculate hash_index.
	static const uint64_t GENSFEN_HASH_SIZE = 64 * 1024 * 1024;

	vector<Key> hash; // 64MB*sizeof(HASH_KEY) = 512MB
};

//  thread_id    = 0..Threads.size()-1
void MultiThinkGenSfen::thread_worker(size_t thread_id)
{
	// For the time being, it will be treated as a draw at the maximum number of steps to write.
	const int MAX_PLY2 = write_maxply;

	//Maximum StateInfo + Search PV to advance to leaf buffer
	std::vector<StateInfo,AlignedAllocator<StateInfo>> states(MAX_PLY2 + MAX_PLY /* == search_depth + α */);
	StateInfo si;

	// This move. Use this move to advance the stage.
	Move m = MOVE_NONE;

	// end flag
	bool quit = false;

	// repeat until the specified number of times
	while (!quit)
	{
		// It is necessary to set a dependent thread for Position.
		// When parallelizing, Threads (since this is a vector<Thread*>,
		// Do the same for up to Threads[0]...Threads[thread_num-1].
		auto th = Threads[thread_id];

		auto& pos = th->rootPos;
    pos.set(StartFEN, false, &si, th);

    // Test cod for Packed SFEN.
    //{
    //  PackedSfen packed_sfen;
    //  pos.sfen_pack(packed_sfen);
    //  std::cout << pos << std::endl;
    //  pos.set_from_packed_sfen(packed_sfen, &si, th);
    //  std::string actual = pos.fen();
    //  assert(actual == StartFEN);
    //}

		// Refer to the members of BookMoveSelector defined in the search section.
		//auto& book = ::book;

		// Save the situation for one station, and write it out including the winning and losing at the end.
		// The function to write is flush_psv() below this.
		PSVector a_psv;
		a_psv.reserve(MAX_PLY2 + MAX_PLY);

		// Write out the phases loaded in a_psv to a file.
		// lastTurnIsWin: win/loss in the next phase after the final phase in a_psv
		// 1 when winning. -1 when losing. Pass 0 for a draw.
		// Return value: true if the specified number of phases has already been reached and the process ends.
		auto flush_psv = [&](int8_t lastTurnIsWin)
		{
			int8_t isWin = lastTurnIsWin;

			// From the final stage (one step before) to the first stage, give information on the outcome of the game for each stage.
			// The phases stored in a_psv are assumed to be continuous (in order).
			for (auto it = a_psv.rbegin(); it != a_psv.rend(); ++it)
			{
				// If isWin == 0 (draw), multiply by -1 and it will remain 0 (draw)
				isWin = - isWin;
				it->game_result = isWin;

				// When I tried to write out the phase, it reached the specified number of times.
				// Because the counter is added in get_next_loop_count()
				// If you don't call this when the phase is output, the counter goes crazy.
				auto loop_count = get_next_loop_count();
				if (loop_count == UINT64_MAX)
				{
					// Set the end flag.
					quit = true;
					return;
				}

				// Write out one aspect.
				sw.write(thread_id, *it);

#if 0
				pos.set_from_packed_sfen(it->sfen);
				cout << pos << "Win : " << it->isWin << " , " << it->score << endl;
#endif
			}
		};

		// ply flag for whether or not to randomly move by eyes
		vector<bool> random_move_flag;
		{
			// If you want to add a random move, random_move_maxply be sure to enter random_move_count times before the first move.
			// I want you to disperse so much.
			// I'm not sure how best it is. Experimenting under various conditions.

			// Make an array like a[0] = 0 ,a[1] = 1, ...
			// Fisher-Yates shuffle and take out the first N items.
			// Actually, I only want N pieces, so I only need to shuffle the first N pieces with Fisher-Yates.

			vector<int> a;
			a.reserve((size_t)random_move_maxply);

			// random_move_minply ,random_move_maxply is specified by 1 origin,
			// Note that we are handling 0 origin here.
			for (int i = std::max(random_move_minply - 1 , 0) ; i < random_move_maxply; ++i)
				a.push_back(i);

			// In case of Apery random move, insert() may be called random_move_count times.
			// Reserve only the size considering it.
			random_move_flag.resize((size_t)random_move_maxply + random_move_count);

			// A random move that exceeds the size() of a[] cannot be applied, so limit it.
			for (int i = 0 ; i < std::min(random_move_count, (int)a.size()) ; ++i)
			{
				swap(a[i], a[prng.rand((uint64_t)a.size() - i) + i]);
				random_move_flag[a[i]] = true;
			}
		}

		// A counter that keeps track of the number of random moves
		// When random_move_minply == -1, random moves are performed continuously, so use it at this time.
		int random_move_c = 0;

		// ply: steps from the initial stage
		for (int ply = 0; ; ++ply)
		{
			//cout << pos << endl;

			// Current search depth
			// Goto will fly, so declare it first.
			int depth = search_depth + (int)prng.rand(search_depth2 - search_depth + 1);

			// has it reached the length
			if (ply >= MAX_PLY2)
			{
				if (use_draw_in_training_data_generation) {
				// Write out as win/loss = draw.
				// This way it is harder to allow the opponent to enter the ball when I enter (may)
				flush_psv(0);
				}
				break;
			}

      if (pos.is_draw(ply)) {
		  if (use_draw_in_training_data_generation) {
			  // Write if draw.
			  flush_psv(0);
		  }
        break;
      }

			// Isn't all pieces stuck and stuck?
			if (MoveList<LEGAL>(pos).size() == 0)
			{
        // (write up to the previous phase of this phase)
        // Write the positions other than this position if checkmated.
                if (pos.checkers()) // Mate
                    flush_psv(-1);
				else if (use_draw_in_training_data_generation) {
					flush_psv(0); // Stalemate
				}
				break;
			}

			//// constant track
			//if ((m = book.probe(pos)) != MOVE_NONE)
			//{
			//  // Hit the constant track.
			//  // The move was stored in m.

			//  // Do not use the fixed phase for learning.
			//  a_psv.clear();

			//  if (random_move_minply != -1)
			// 		// Random move is performed with a certain probability even in the constant phase.
			// 		goto RANDOM_MOVE;
			//  else
			// 		// When -1 is specified as random_move_minply, it points according to the standard until it goes out of the standard.
			// 		// Prepare an innumerable number of situations that have left the constant as ConsiderationBookMoveCount true using a huge constant
			// 		// Used for purposes such as performing a random move 5 times from there.
			// 		goto DO_MOVE;
			//}

			{
				// search_depth～search_depth2 Evaluation value of hand reading and PV (best responder row)
				// There should be no problem if you narrow the search window.

				auto pv_value1 = search(pos, depth);

				auto value1 = pv_value1.first;
				auto& pv1 = pv_value1.second;

				// For situations where the absolute evaluation value is greater than or equal to this value
				// It doesn't make much sense to use that aspect for learning, so this game ends.
				// Treat this as having won or lost.

				// If you win one move, declarative win, mate_in(2) will be returned here, so it will be the same value as the upper limit of eval_limit,
				// This if expression is always true. The same applies to resign.

				if (abs(value1) >= eval_limit)
				{
					// sync_cout << pos << "eval limit = "<< eval_limit << "over ,move = "<< pv1[0] << sync_endl;

					// If value1 >= eval_limit in this aspect, you win (the turn side of this aspect).
					flush_psv((value1 >= eval_limit) ? 1 : -1);
					break;
				}

				// Verification of a strange move
				if (pv1.size() > 0
					&& (pv1[0] == MOVE_NONE || pv1[0] == MOVE_NULL)
					)
				{
					// MOVE_WIN is checking if it is the declaration victory stage before this
					// The declarative winning move should never come back here.
					// Also, when MOVE_RESIGN, value1 is a one-stop score, which should be the minimum value of eval_limit (-31998)...
					cout << "Error! : " << pos.fen() << m << value1 << endl;
					break;
				}

				// Processing according to each thousand-day hand.

        if (pos.is_draw(0)) {
			if (use_draw_in_training_data_generation) {
				// Write if draw.
				flush_psv(0);
			}
          break;
        }

				// Use PV's move to the leaf node and use the value that evaluated() is called on that leaf node.
				auto evaluate_leaf = [&](Position& pos , vector<Move>& pv)
				{
					auto rootColor = pos.side_to_move();

					int ply2 = ply;
					for (auto m : pv)
					{
						// As a verification for debugging, make sure there are no illegal players in the middle.
						// NULL_MOVE does not come.

						// I tested it out enough so I can comment it out.
#if 1
						// I shouldn't be an illegal player.
						// declarative win and not mated() are tested above so
						// It is guaranteed that MOVE_WIN and MOVE_RESIGN do not come as a reader. (Should...)
						if (!pos.pseudo_legal(m) || !pos.legal(m))
						{
							cout << "Error! : " << pos.fen() << m << endl;
						}
#endif
						pos.do_move(m, states[ply2++]);
						
						//Because the difference calculation of evaluate() cannot be performed unless each node evaluate() is called!
						// If the depth is 8 or more, it seems faster not to calculate this difference.
#if defined(EVAL_NNUE)
            if (depth < 8)
              Eval::evaluate_with_no_return(pos);
#endif  // defined(EVAL_NNUE)
					}

					// reach leaf
					// cout << pos;

					auto v = Eval::evaluate(pos);
					// evaluate() returns the evaluation value on the turn side, so
					// If it's a turn different from root_color, you must invert v and return it.
					if (rootColor != pos.side_to_move())
						v = -v;

					// Rewind.
					// Is it C++x14, and isn't there even foreach to turn in reverse?
					//  for (auto it : boost::adaptors::reverse(pv))

					for (auto it = pv.rbegin(); it != pv.rend(); ++it)
						pos.undo_move(*it);

					return v;
				};

#if 0
				dbg_hit_on(pv_value1.first == leaf_value);
				// gensfen depth 3 eval_limit 32000
				// Total 217749 Hits 203579 hit rate (%) 93.490
				// gensfen depth 6 eval_limit 32000
				// Total 78407 Hits 69190 hit rate (%) 88.245
				// gensfen depth 6 eval_limit 3000
				// Total 53879 Hits 43713 hit rate (%) 81.132

				// Problems such as pruning with moves in the substitution table.
				// This is a little uncomfortable as a teacher...
#endif

				//If depth 0, pv is not obtained, so search again at depth 2.
				if (search_depth <= 0)
				{
					pv_value1 = search(pos, 2);
					pv1 = pv_value1.second;
				}

				// The surroundings of the initial stage are all similar
				// Do not write it out because it can lead to overlearning when used for learning.
				// → comparative experiment should be done
				if (ply < write_minply - 1)
				{
					a_psv.clear();
					goto SKIP_SAVE;
				}

				// Did you just write the same phase?
				// This may include the same aspect as it is generated in parallel on multiple PCs, so
				// It is better to do the same process when reading.
				{
					auto key = pos.key();
					auto hash_index = (size_t)(key & (GENSFEN_HASH_SIZE - 1));
					auto key2 = hash[hash_index];
					if (key == key2)
					{
						// when skipping regarding earlier
						// Clear the saved situation because the win/loss information will be incorrect.
						// anyway, when the hash matches, it's likely that the previous phases also match
						// Not worth writing out.
						a_psv.clear();
						goto SKIP_SAVE;
					}
					hash[hash_index] = key; // Replace with the current key.
				}

				// Temporary saving of the situation.
				{
					a_psv.emplace_back(PackedSfenValue());
					auto &psv = a_psv.back();

					// If pack is requested, write the packed sfen and the evaluation value at that time.
					// The final writing is after winning or losing.
					pos.sfen_pack(psv.sfen);

          //{
          //  std::string before_fen = pos.fen();
          //  pos.set_from_packed_sfen(psv.sfen, &si, th);
          //  std::string after_fen = pos.fen();
          //  assert(before_fen == after_fen);
          //}

					// Get the value of evaluate() as seen from the root color on the leaf node of the PV line.
					//I don't know the goodness and badness of using the return value of search() as it is.
					psv.score = evaluate_leaf(pos, pv1);
					psv.gamePly = ply;

					// Take out the first PV hand. This should be present unless depth 0.
					assert(pv_value1.second.size() >= 1);
					Move pv_move1 = pv_value1.second[0];
					psv.move = pv_move1;
				}

			SKIP_SAVE:;

				// For some reason, I could not get PV (hit the substitution table etc. and got stuck?) so go to the next game.
				// It's a rare case, so you can ignore it.
				if (pv1.size() == 0)
					break;

				// search_depth Advance the phase by hand reading.
				m = pv1[0];
			}

		RANDOM_MOVE:;

			// Phase to randomly choose one from legal hands
			if (
				// 1. Random move of random_move_count times from random_move_minply to random_move_maxply
				(random_move_minply != -1 && ply <(int)random_move_flag.size() && random_move_flag[ply]) ||
				// 2. A mode to perform random move of random_move_count times after leaving the track
				(random_move_minply == -1 && random_move_c <random_move_count))
			{
				++random_move_c;

				// It's not a mate, so there should be one legal hand...
				if (random_multi_pv == 0)
				{
					// normal random move

					MoveList<LEGAL> list(pos);

					// I don't really know the goodness and badness of making this the Apery method.
					if (random_move_like_apery == 0
						|| prng.rand(random_move_like_apery) != 0
					)
					{
						// Normally one move from legal move
						m = list.at((size_t)prng.rand((uint64_t)list.size()));
					}
					else {
						// if you can move the ball, move the ball
						Move moves[8]; // Near 8
						Move* p = &moves[0];
						for (auto& m : list)
							if (type_of(pos.moved_piece(m)) == KING)
								*(p++) = m;
						size_t n = p - &moves[0];
						if (n != 0)
						{
							// move to move the ball
							m = moves[prng.rand(n)];

							// In Apery method, at this time there is a 1/2 chance that the opponent will also move randomly
							if (prng.rand(2) == 0)
							{
								// Is it a simple hack to add a "1" next to random_move_flag[ply]?
								random_move_flag.insert(random_move_flag.begin() + ply + 1, 1, true);
							}
						}
						else
							// Normally one move from legal move
							m = list.at((size_t)prng.rand((uint64_t)list.size()));
					}

					// I put in the code of two handed balls, but if you choose one from legal hands, it should be equivalent to that
					// I decided it's unnecessary because it just makes the code more complicated.
				}
				else {
					// Since the logic becomes complicated, I'm sorry, I will search again with MultiPV here.
					Learner::search(pos, random_multi_pv_depth, random_multi_pv);
					// Select one from the top N hands of root Moves

					auto& rm = pos.this_thread()->rootMoves;

					uint64_t s = min((uint64_t)rm.size(), (uint64_t)random_multi_pv);
					for (uint64_t i = 1; i < s; ++i)
					{
						// The difference from the evaluation value of rm[0] must be within the range of random_multi_pv_diff.
						// It can be assumed that rm[x].score is arranged in descending order.
						if (rm[0].score > rm[i].score + random_multi_pv_diff)
						{
							s = i;
							break;
						}
					}

					m = rm[prng.rand(s)].pv[0];

					// I haven't written one phase yet, but it ended, so the writing process ends and the next game starts.
					if (!is_ok(m))
						break;
				}

				// When trying to evaluate the move from the outcome of the game,
				// There is a random move this time, so try not to fall below this.
				a_psv.clear(); // clear saved aspect
			}

		DO_MOVE:;
			pos.do_move(m, states[ply]);

			// Call node evaluate() for each difference calculation.
			Eval::evaluate_with_no_return(pos);

		} // for (int ply = 0; ; ++ply)

	} // while(!quit)

	sw.finalize(thread_id);
}

// -----------------------------------
// Command to generate a game record (master thread)
// -----------------------------------

// Command to generate a game record
void gen_sfen(Position&, istringstream& is)
{
	// number of threads (given by USI setoption)
	uint32_t thread_num = (uint32_t)Options["Threads"];

	// Number of generated game records default = 8 billion phases (Ponanza specification)
	uint64_t loop_max = 8000000000UL;

	// Stop the generation when the evaluation value reaches this value.
	int eval_limit = 3000;

	// search depth
	int search_depth = 3;
	int search_depth2 = INT_MIN;

	// minimum ply, maximum ply and number of random moves
	int random_move_minply = 1;
	int random_move_maxply = 24;
	int random_move_count = 5;
	// A function to move the random move mainly like Apery
	// If this is set to 3, the ball will move with a probability of 1/3.
	int random_move_like_apery = 0;
	// If you search with multipv instead of random move and choose from among them randomly, set random_multi_pv = 1 or more.
	int random_multi_pv = 0;
	int random_multi_pv_diff = 32000;
	int random_multi_pv_depth = INT_MIN;

	// The minimum and maximum ply (number of steps from the initial phase) of the phase to write out.
	int write_minply = 16;
	int write_maxply = 400;

	// File name to write
	string output_file_name = "generated_kifu.bin";

	string token;

	// When hit to eval hash, as a evaluation value near the initial stage, if a hash collision occurs and a large value is written
	// When eval_limit is set small, eval_limit will be exceeded every time in the initial phase, and phase generation will not proceed.
	// Therefore, eval hash needs to be disabled.
	// After that, when the hash of the eval hash collides, the evaluation value of a strange value is used, and it may be unpleasant to use it for the teacher.
	bool use_eval_hash = false;

	// Save to file in this unit.
	// File names are serialized like file_1.bin, file_2.bin.
	uint64_t save_every = UINT64_MAX;

	// Add a random number to the end of the file name.
	bool random_file_name = false;

	while (true)
	{
		token = "";
		is >> token;
		if (token == "")
			break;

		if (token == "depth")
			is >> search_depth;
		else if (token == "depth2")
			is >> search_depth2;
		else if (token == "loop")
			is >> loop_max;
		else if (token == "output_file_name")
			is >> output_file_name;
		else if (token == "eval_limit")
		{
			is >> eval_limit;
			// Limit the maximum to a one-stop score. (Otherwise you might not end the loop)
			eval_limit = std::min(eval_limit, (int)mate_in(2));
		}
		else if (token == "random_move_minply")
			is >> random_move_minply;
		else if (token == "random_move_maxply")
			is >> random_move_maxply;
		else if (token == "random_move_count")
			is >> random_move_count;
		else if (token == "random_move_like_apery")
			is >> random_move_like_apery;
		else if (token == "random_multi_pv")
			is >> random_multi_pv;
		else if (token == "random_multi_pv_diff")
			is >> random_multi_pv_diff;
		else if (token == "random_multi_pv_depth")
			is >> random_multi_pv_depth;
		else if (token == "write_minply")
			is >> write_minply;
		else if (token == "write_maxply")
			is >> write_maxply;
		else if (token == "use_eval_hash")
			is >> use_eval_hash;
		else if (token == "save_every")
			is >> save_every;
		else if (token == "random_file_name")
			is >> random_file_name;
		else
			cout << "Error! : Illegal token " << token << endl;
	}

#if defined(USE_GLOBAL_OPTIONS)
	// Save it for later restore.
	auto oldGlobalOptions = GlobalOptions;
	GlobalOptions.use_eval_hash = use_eval_hash;
#endif

	// If search depth2 is not set, leave it the same as search depth.
	if (search_depth2 == INT_MIN)
		search_depth2 = search_depth;
	if (random_multi_pv_depth == INT_MIN)
		random_multi_pv_depth = search_depth;

	if (random_file_name)
	{
		// Give a random number to output_file_name at this point.
    std::random_device seed_gen;
    PRNG r(seed_gen());
		// Just in case, reassign the random numbers.
		for(int i=0;i<10;++i)
			r.rand(1);
		auto to_hex = [](uint64_t u){
			std::stringstream ss;
			ss << std::hex << u;
			return ss.str();
		};
		// I don't want to wear 64bit numbers by accident, so I'm going to make a 64bit number 2 just in case.
		output_file_name = output_file_name + "_" + to_hex(r.rand<uint64_t>()) + to_hex(r.rand<uint64_t>());
	}

	std::cout << "gensfen : " << endl
		<< "  search_depth = " << search_depth << " to " << search_depth2 << endl
		<< "  loop_max = " << loop_max << endl
		<< "  eval_limit = " << eval_limit << endl
		<< "  thread_num (set by USI setoption) = " << thread_num << endl
		<< "  book_moves (set by USI setoption) = " << Options["BookMoves"] << endl
		<< "  random_move_minply     = " << random_move_minply << endl
		<< "  random_move_maxply     = " << random_move_maxply << endl
		<< "  random_move_count      = " << random_move_count << endl
		<< "  random_move_like_apery = " << random_move_like_apery << endl
		<< "  random_multi_pv        = " << random_multi_pv << endl
		<< "  random_multi_pv_diff   = " << random_multi_pv_diff << endl
		<< "  random_multi_pv_depth  = " << random_multi_pv_depth << endl
		<< "  write_minply           = " << write_minply << endl
		<< "  write_maxply           = " << write_maxply << endl
		<< "  output_file_name       = " << output_file_name << endl
		<< "  use_eval_hash          = " << use_eval_hash << endl
		<< "  save_every             = " << save_every << endl
		<< "  random_file_name       = " << random_file_name << endl;

	// Create and execute threads as many as Options["Threads"].
	{
		SfenWriter sw(output_file_name, thread_num);
		sw.save_every = save_every;

		MultiThinkGenSfen multi_think(search_depth, search_depth2, sw);
		multi_think.set_loop_max(loop_max);
		multi_think.eval_limit = eval_limit;
		multi_think.random_move_minply = random_move_minply;
		multi_think.random_move_maxply = random_move_maxply;
		multi_think.random_move_count = random_move_count;
		multi_think.random_move_like_apery = random_move_like_apery;
		multi_think.random_multi_pv = random_multi_pv;
		multi_think.random_multi_pv_diff = random_multi_pv_diff;
		multi_think.random_multi_pv_depth = random_multi_pv_depth;
		multi_think.write_minply = write_minply;
		multi_think.write_maxply = write_maxply;
		multi_think.start_file_write_worker();
		multi_think.go_think();

		// Since we are joining with the destructor of SfenWriter, please give a message that it has finished after the join
		// Enclose this in a block because it should be displayed.
	}

	std::cout << "gensfen finished." << endl;

#if defined(USE_GLOBAL_OPTIONS)
	// Restore Global Options.
	GlobalOptions = oldGlobalOptions;
#endif

}

// -----------------------------------
// command to learn from the generated game (learn)
// -----------------------------------

// ordinary sigmoid function
double sigmoid(double x)
{
	return 1.0 / (1.0 + std::exp(-x));
}

// A function that converts the evaluation value to the winning rate [0,1]
double winning_percentage(double value)
{
	// 1/(1+10^(-Eval/4))
	// = 1/(1+e^(-Eval/4*ln(10))
	// = sigmoid(Eval/4*ln(10))
	return sigmoid(value / PawnValueEg / 4.0 * log(10.0));
}
double dsigmoid(double x)
{
	// Sigmoid function
	// f(x) = 1/(1+exp(-x))
	// the first derivative is
	// f'(x) = df/dx = f(x)・{ 1-f(x)}
	// becomes

	return sigmoid(x) * (1.0 - sigmoid(x));
}

// When the objective function is the sum of squares of the difference in winning percentage
#if defined (LOSS_FUNCTION_IS_WINNING_PERCENTAGE)
// function to calculate the gradient
double calc_grad(Value deep, Value shallow, PackedSfenValue& psv)
{
	// The square of the win rate difference minimizes it in the objective function.
	// Objective function J = 1/2m Σ (win_rate(shallow)-win_rate(deep) )^2
	// However, σ is a sigmoid function that converts the evaluation value into the difference in the winning percentage.
	// m is the number of samples. shallow is the evaluation value for a shallow search (qsearch()). deep is the evaluation value for deep search.
	// If W is the feature vector (parameter of the evaluation function) and Xi and Yi are teachers
	// shallow = W*Xi // * is the Hadamard product, transposing W and meaning X
	// f(Xi) = win_rate(W*Xi)
	// If σ(i th deep) = Yi,
	// J = m/2 Σ (f(Xi)-Yi )^2
	// becomes a common expression.
	// W is a vector, and if we write the jth element as Wj, from the chain rule
	// ∂J/∂Wj = ∂J/∂f ・∂f/∂W ・∂W/∂Wj
	// = 1/m Σ (f(Xi)-y) ・f'(Xi) ・ 1

	// 1/m will be multiplied later, but the contents of Σ can be retained in the array as the value of the gradient.
	// f'(Xi) = win_rate'(shallow) = sigmoid'(shallow/600) = dsigmoid(shallow / 600) / 600
	// This /600 at the end is adjusted by the learning rate, so do not write it..
	// Also, the coefficient of 1/m is unnecessary if you use the update formula that has the automatic gradient adjustment function like Adam and AdaGrad.
	// Therefore, it is not necessary to save it in memory.

	double p = winning_percentage(deep);
	double q = winning_percentage(shallow);
	return (q - p) * dsigmoid(double(shallow) / 600.0);
}
#endif

#if defined (LOSS_FUNCTION_IS_CROSS_ENTOROPY)
double calc_grad(Value deep, Value shallow, const PackedSfenValue& psv)
{
	// Objective function with cross entropy

	// For the concept and nature of cross entropy,
	// http://nnadl-ja.github.io/nnadl_site_ja/chap3.html#the_cross-entropy_cost_function
	// http://postd.cc/visual-information-theory-3/
	// Refer to etc.

	// Objective function design)
	// We want to make the distribution of p closer to the distribution of q → Think of it as the problem of minimizing the cross entropy between the probability distributions of p and q.
	// J = H(p,q) =-Σ p(x) log(q(x)) = -p log q-(1-p) log(1-q)
	// x

	// p is a constant and q is a Wi function (q = σ(W・Xi) ).
	// ∂J/∂Wi = -p・q'/q-(1-p)(1-q)'/(1-q)
	// = ...
	// = q-p.

	double p = winning_percentage(deep);
	double q = winning_percentage(shallow);

	return q - p;
}
#endif

#if defined ( LOSS_FUNCTION_IS_CROSS_ENTOROPY_FOR_VALUE )
double calc_grad(Value deep, Value shallow, const PackedSfenValue& psv)
{
	// Version that does not pass the winning percentage function
	// This, unless EVAL_LIMIT is set low, trying to match the evaluation value with the shape of the end stage
	// eval may exceed the range of eval.
	return shallow - deep;
}
#endif

#if defined ( LOSS_FUNCTION_IS_ELMO_METHOD )

// A constant used in elmo (WCSC27). Adjustment required.
// Since elmo does not internally divide the expression, the value is different.
// You can set this value with the learn command.
// 0.33 is equivalent to the constant (0.5) used in elmo (WCSC27)
double ELMO_LAMBDA = 0.33;
double ELMO_LAMBDA2 = 0.33;
double ELMO_LAMBDA_LIMIT = 32000;

double calc_grad(Value deep, Value shallow , const PackedSfenValue& psv)
{
	// elmo (WCSC27) method
	// Correct with the actual game wins and losses.

	const double q = winning_percentage(shallow);
	const double p = winning_percentage(deep);

	// Use 1 as the correction term if the expected win rate is 1, 0 if you lose, and 0.5 if you draw.
	// game_result = 1,0,-1 so add 1 and divide by 2.
	const double t = double(psv.game_result + 1) / 2;

	// If the evaluation value in deep search exceeds ELMO_LAMBDA_LIMIT, apply ELMO_LAMBDA2 instead of ELMO_LAMBDA.
	const double lambda = (abs(deep) >= ELMO_LAMBDA_LIMIT) ? ELMO_LAMBDA2 : ELMO_LAMBDA;

	// Use the actual win rate as a correction term.
	// This is the idea of ​​elmo (WCSC27), modern O-parts.
	const double grad = lambda * (q - p) + (1.0 - lambda) * (q - t);

	return grad;
}

// Calculate cross entropy during learning
// The individual cross entropy of the win/loss term and win rate term of the elmo expression is returned to the arguments cross_entropy_eval and cross_entropy_win.
void calc_cross_entropy(Value deep, Value shallow, const PackedSfenValue& psv,
	double& cross_entropy_eval, double& cross_entropy_win, double& cross_entropy,
	double& entropy_eval, double& entropy_win, double& entropy)
{
	const double p /* teacher_winrate */ = winning_percentage(deep);
	const double q /* eval_winrate    */ = winning_percentage(shallow);
	const double t = double(psv.game_result + 1) / 2;

	constexpr double epsilon = 0.000001;

	// If the evaluation value in deep search exceeds ELMO_LAMBDA_LIMIT, apply ELMO_LAMBDA2 instead of ELMO_LAMBDA.
	const double lambda = (abs(deep) >= ELMO_LAMBDA_LIMIT) ? ELMO_LAMBDA2 : ELMO_LAMBDA;

	const double m = (1.0 - lambda) * t + lambda * p;

	cross_entropy_eval =
		(-p * std::log(q + epsilon) - (1.0 - p) * std::log(1.0 - q + epsilon));
	cross_entropy_win =
		(-t * std::log(q + epsilon) - (1.0 - t) * std::log(1.0 - q + epsilon));
	entropy_eval =
		(-p * std::log(p + epsilon) - (1.0 - p) * std::log(1.0 - p + epsilon));
	entropy_win =
		(-t * std::log(t + epsilon) - (1.0 - t) * std::log(1.0 - t + epsilon));

	cross_entropy =
		(-m * std::log(q + epsilon) - (1.0 - m) * std::log(1.0 - q + epsilon));
	entropy =
		(-m * std::log(m + epsilon) - (1.0 - m) * std::log(1.0 - m + epsilon));
}

#endif


// Other variations may be prepared as the objective function..


double calc_grad(Value shallow, const PackedSfenValue& psv) {
	return calc_grad((Value)psv.score, shallow, psv);
}

// Sfen reader
struct SfenReader
{
	SfenReader(int thread_num) : prng((std::random_device())())
	{
		packed_sfens.resize(thread_num);
		total_read = 0;
		total_done = 0;
		last_done = 0;
		next_update_weights = 0;
		save_count = 0;
		end_of_files = false;
		no_shuffle = false;
		stop_flag = false;

		hash.resize(READ_SFEN_HASH_SIZE);
	}

	~SfenReader()
	{
		if (file_worker_thread.joinable())
			file_worker_thread.join();

		for (auto p : packed_sfens)
			delete p;
		for (auto p : packed_sfens_pool)
			delete p;
	}

	// number of phases used for calculation such as mse
	// mini-batch size = 1M is standard, so 0.2% of that should be negligible in terms of time.
	//Since search() is performed with depth = 1 in calculation of move match rate, simple comparison is not possible...
	const uint64_t sfen_for_mse_size = 2000;

	// Load the phase for calculation such as mse.
	void read_for_mse()
	{
		auto th = Threads.main();
		Position& pos = th->rootPos;
		for (uint64_t i = 0; i < sfen_for_mse_size; ++i)
		{
			PackedSfenValue ps;
			if (!read_to_thread_buffer(0, ps))
			{
				cout << "Error! read packed sfen , failed." << endl;
				break;
			}
			sfen_for_mse.push_back(ps);

			// Get the hash key.
			StateInfo si;
			pos.set_from_packed_sfen(ps.sfen,&si,th);
			sfen_for_mse_hash.insert(pos.key());
		}
	}

	void read_validation_set(const string file_name, int eval_limit)
	{
		ifstream fs(file_name, ios::binary);

		while (fs)
		{
			PackedSfenValue p;
			if (fs.read((char*)&p, sizeof(PackedSfenValue)))
			{
				if (eval_limit < abs(p.score))
					continue;
				if (!use_draw_in_validation && p.game_result == 0)
					continue;
				sfen_for_mse.push_back(p);
			} else {
				break;
			}
		}
	}

	// Number of phases buffered by each thread 0.1M phases. 4M phase at 40HT
	const size_t THREAD_BUFFER_SIZE = 10 * 1000;

	// Buffer for reading files (If this is made larger, the shuffle becomes larger and the phases may vary.
	// If it is too large, the memory consumption will increase.
	// SFEN_READ_SIZE is a multiple of THREAD_BUFFER_SIZE.
	const size_t SFEN_READ_SIZE = LEARN_SFEN_READ_SIZE;

	// [ASYNC] Thread returns one aspect. Otherwise returns false.
	bool read_to_thread_buffer(size_t thread_id, PackedSfenValue& ps)
	{
		// If there are any positions left in the thread buffer, retrieve one and return it.
		auto& thread_ps = packed_sfens[thread_id];

		// Fill the read buffer if there is no remaining buffer, but if it doesn't even exist, finish.
		if ((thread_ps == nullptr || thread_ps->size() == 0) // If the buffer is empty, fill it.
			&& !read_to_thread_buffer_impl(thread_id))
			return false;

		// read_to_thread_buffer_impl() returned true,
		// Since the filling of the thread buffer with the phase has been completed successfully
		// thread_ps->rbegin() is alive.

		ps = *(thread_ps->rbegin());
		thread_ps->pop_back();

		// If you've run out of buffers, call delete yourself to free this buffer.
		if (thread_ps->size() == 0)
		{

			delete thread_ps;
			thread_ps = nullptr;
		}

		return true;
	}

	// [ASYNC] Read some aspects into thread buffer.
	bool read_to_thread_buffer_impl(size_t thread_id)
	{
		while (true)
		{
			{
				std::unique_lock<std::mutex> lk(mutex);
				// If you can fill from the file buffer, that's fine.
				if (packed_sfens_pool.size() != 0)
				{
					// It seems that filling is possible, so fill and finish.

					packed_sfens[thread_id] = packed_sfens_pool.front();
					packed_sfens_pool.pop_front();

					total_read += THREAD_BUFFER_SIZE;

					return true;
				}
			}

			// The file to read is already gone. No more use.
			if (end_of_files)
				return false;

			// Waiting for file worker to fill packed_sfens_pool.
			// The mutex isn't locked, so it should fill up soon.
			sleep(1);
		}

	}

	// Start a thread that loads the phase file in the background.
	void start_file_read_worker()
	{
		file_worker_thread = std::thread([&] { this->file_read_worker(); });
	}

	// for file read-only threads
	void file_read_worker()
	{
		auto open_next_file = [&]()
		{
			if (fs.is_open())
				fs.close();

			// no more
			if (filenames.size() == 0)
				return false;

			// Get the next file name.
			string filename = *filenames.rbegin();
			filenames.pop_back();

			fs.open(filename, ios::in | ios::binary);
			cout << "open filename = " << filename << endl;
			assert(fs);

			return true;
		};

		while (true)
		{
			// Wait for the buffer to run out.
			// This size() is read only, so you don't need to lock it.
			while (!stop_flag && packed_sfens_pool.size() >= SFEN_READ_SIZE / THREAD_BUFFER_SIZE)
				sleep(100);
			if (stop_flag)
				return;

			PSVector sfens;
			sfens.reserve(SFEN_READ_SIZE);

			// Read from the file into the file buffer.
			while (sfens.size() < SFEN_READ_SIZE)
			{
				PackedSfenValue p;
				if (fs.read((char*)&p, sizeof(PackedSfenValue)))
				{
					sfens.push_back(p);
				} else
				{
					// read failure
					if (!open_next_file())
					{
						// There was no next file. Abon.
						cout << "..end of files." << endl;
						end_of_files = true;
						return;
					}
				}
			}

			// Shuffle the read phase data.
			// random shuffle by Fisher-Yates algorithm

			if (!no_shuffle)
			{
				auto size = sfens.size();
				for (size_t i = 0; i < size; ++i)
					swap(sfens[i], sfens[(size_t)(prng.rand((uint64_t)size - i) + i)]);
			}

			// Divide this by THREAD_BUFFER_SIZE. There should be size pieces.
			// SFEN_READ_SIZE shall be a multiple of THREAD_BUFFER_SIZE.
			assert((SFEN_READ_SIZE % THREAD_BUFFER_SIZE)==0);

			auto size = size_t(SFEN_READ_SIZE / THREAD_BUFFER_SIZE);
			std::vector<PSVector*> ptrs;
			ptrs.reserve(size);

			for (size_t i = 0; i < size; ++i)
			{
				// Delete this pointer on the receiving side.
				PSVector* ptr = new PSVector();
				ptr->resize(THREAD_BUFFER_SIZE);
				memcpy(&((*ptr)[0]), &sfens[i * THREAD_BUFFER_SIZE], sizeof(PackedSfenValue) * THREAD_BUFFER_SIZE);

				ptrs.push_back(ptr);
			}

			// Since sfens is ready, look at the occasion and copy
			{
				std::unique_lock<std::mutex> lk(mutex);

				// You can ignore this time because you just copy the pointer...
				// The mutex lock is required because the contents of packed_sfens_pool are changed.

				for (size_t i = 0; i < size; ++i)
					packed_sfens_pool.push_back(ptrs[i]);
			}
		}
	}

	// sfen files
	vector<string> filenames;

	// number of phases read (file to memory buffer)
	atomic<uint64_t> total_read;

	// number of processed phases
	atomic<uint64_t> total_done;

	// number of cases processed so far
	uint64_t last_done;

	// If total_read exceeds this value, update_weights() and calculate mse.
	uint64_t next_update_weights;

	uint64_t save_count;

	// Do not shuffle when reading the phase.
	bool no_shuffle;

	bool stop_flag;

	// Determine if it is a phase for calculating rmse.
	// (The computational aspects of rmse should not be used for learning.)
	bool is_for_rmse(Key key) const
	{
			return sfen_for_mse_hash.count(key) != 0;
	}

	// hash to limit the reading of the same situation
	// Is there too many 64 million phases? Or Not really..
	// It must be 2**N because it will be used as the mask to calculate hash_index.
	static const uint64_t READ_SFEN_HASH_SIZE = 64 * 1024 * 1024;
	vector<Key> hash; // 64MB*8 = 512MB

	// test phase for mse calculation
	PSVector sfen_for_mse;

protected:

	// worker thread reading file in background
	std::thread file_worker_thread;

	// Random number to shuffle when reading the phase
	PRNG prng;

	// Did you read the files and reached the end?
	atomic<bool> end_of_files;


	// handle of sfen file
	std::fstream fs;

	// sfen for each thread
	// (When the thread is used up, the thread should call delete to release it.)
	std::vector<PSVector*> packed_sfens;

	// Mutex when accessing packed_sfens_pool
	std::mutex mutex;

	// pool of sfen. The worker thread read from the file is added here.
	// Each worker thread fills its own packed_sfens[thread_id] from here.
	// * Lock and access the mutex.
	std::list<PSVector*> packed_sfens_pool;

	// Hold the hash key so that the mse calculation phase is not used for learning.
	std::unordered_set<Key> sfen_for_mse_hash;
};

// Class to generate sfen with multiple threads
struct LearnerThink: public MultiThink
{
	LearnerThink(SfenReader& sr_):sr(sr_),stop_flag(false), save_only_once(false)
	{
#if defined ( LOSS_FUNCTION_IS_ELMO_METHOD )
		learn_sum_cross_entropy_eval = 0.0;
		learn_sum_cross_entropy_win = 0.0;
		learn_sum_cross_entropy = 0.0;
		learn_sum_entropy_eval = 0.0;
		learn_sum_entropy_win = 0.0;
		learn_sum_entropy = 0.0;
#endif
#if defined(EVAL_NNUE)
		newbob_scale = 1.0;
		newbob_decay = 1.0;
		newbob_num_trials = 2;
		best_loss = std::numeric_limits<double>::infinity();
		latest_loss_sum = 0.0;
		latest_loss_count = 0;
#endif
	}

	virtual void thread_worker(size_t thread_id);

	// Start a thread that loads the phase file in the background.
	void start_file_read_worker() { sr.start_file_read_worker(); }

	// save merit function parameters to a file
	bool save(bool is_final=false);

	// sfen reader
	SfenReader& sr;

	// Learning iteration counter
	uint64_t epoch = 0;

	// Mini batch size size. Be sure to set it on the side that uses this class.
	uint64_t mini_batch_size = 1000*1000;

	bool stop_flag;

	// Discount rate
	double discount_rate;

	// Option to exclude early stage from learning
	int reduction_gameply;

	// Option not to learn kk/kkp/kpp/kppp
	std::array<bool,4> freeze;

	// If the absolute value of the evaluation value of the deep search of the teacher phase exceeds this value, discard the teacher phase.
	int eval_limit;

	// Flag whether to dig a folder each time the evaluation function is saved.
	// If true, do not dig the folder.
	bool save_only_once;

	// --- loss calculation

#if defined (LOSS_FUNCTION_IS_ELMO_METHOD)
	// For calculation of learning data loss
	atomic<double> learn_sum_cross_entropy_eval;
	atomic<double> learn_sum_cross_entropy_win;
	atomic<double> learn_sum_cross_entropy;
	atomic<double> learn_sum_entropy_eval;
	atomic<double> learn_sum_entropy_win;
	atomic<double> learn_sum_entropy;
#endif

#if defined(EVAL_NNUE)
	shared_timed_mutex nn_mutex;
	double newbob_scale;
	double newbob_decay;
	int newbob_num_trials;
	double best_loss;
	double latest_loss_sum;
	uint64_t latest_loss_count;
	std::string best_nn_directory;
#endif

	uint64_t eval_save_interval;
	uint64_t loss_output_interval;
	uint64_t mirror_percentage;

	// Loss calculation.
	// done: Number of phases targeted this time
	void calc_loss(size_t thread_id , uint64_t done);

	// Define the loss calculation in ↑ as a task and execute it
	TaskDispatcher task_dispatcher;
};

void LearnerThink::calc_loss(size_t thread_id, uint64_t done)
{
	// There is no point in hitting the replacement table, so at this timing the generation of the replacement table is updated.
	// It doesn't matter if you have disabled the substitution table.
	TT.new_search();


#if defined(EVAL_NNUE)
	std::cout << "PROGRESS: " << now_string() << ", ";
	std::cout << sr.total_done << " sfens";
	std::cout << ", iteration " << epoch;
	std::cout << ", eta = " << Eval::get_eta() << ", ";
#endif

#if !defined(LOSS_FUNCTION_IS_ELMO_METHOD)
	double sum_error = 0;
	double sum_error2 = 0;
	double sum_error3 = 0;
#endif

#if defined (LOSS_FUNCTION_IS_ELMO_METHOD)
	// For calculation of verification data loss
	atomic<double> test_sum_cross_entropy_eval,test_sum_cross_entropy_win,test_sum_cross_entropy;
	atomic<double> test_sum_entropy_eval,test_sum_entropy_win,test_sum_entropy;
	test_sum_cross_entropy_eval = 0;
	test_sum_cross_entropy_win = 0;
	test_sum_cross_entropy = 0;
	test_sum_entropy_eval = 0;
	test_sum_entropy_win = 0;
	test_sum_entropy = 0;

	// norm for learning
	atomic<double> sum_norm;
	sum_norm = 0;
#endif

	// The number of times the pv first move of deep search matches the pv first move of search(1).
	atomic<int> move_accord_count;
	move_accord_count = 0;

	// Display the value of eval() in the initial stage of Hirate and see the shaking.
	auto th = Threads[thread_id];
	auto& pos = th->rootPos;
	StateInfo si;
  pos.set(StartFEN, false, &si, th);
  std::cout << "hirate eval = " << Eval::evaluate(pos);

	//Eval::print_eval_stat(pos);

	// It's better to parallelize here, but it's a bit troublesome because the search before slave has not finished.
	// I created a mechanism to call task, so I will use it.

	// The number of tasks to do.
	atomic<int> task_count;
	task_count = (int)sr.sfen_for_mse.size();
	task_dispatcher.task_reserve(task_count);

	// Create a task to search for the situation and give it to each thread.
	for (const auto& ps : sr.sfen_for_mse)
	{
		// Assign work to each thread using TaskDispatcher.
		// A task definition for that.
		// It is not possible to capture pos used in ↑, so specify the variables you want to capture one by one.
		auto task = [&ps,&test_sum_cross_entropy_eval,&test_sum_cross_entropy_win,&test_sum_cross_entropy,&test_sum_entropy_eval,&test_sum_entropy_win,&test_sum_entropy, &sum_norm,&task_count ,&move_accord_count](size_t thread_id)
		{
			// Does C++ properly capture a new ps instance for each loop?.
			auto th = Threads[thread_id];
			auto& pos = th->rootPos;
			StateInfo si;
			if (pos.set_from_packed_sfen(ps.sfen ,&si, th) != 0)
			{
				// Unfortunately, as an sfen for rmse calculation, an invalid sfen was drawn.
				cout << "Error! : illegal packed sfen " << pos.fen() << endl;
			}

			// Evaluation value for shallow search
			// The value of evaluate() may be used, but when calculating loss, learn_cross_entropy and
			// Use qsearch() because it is difficult to compare the values.
			// EvalHash has been disabled in advance. (If not, the same value will be returned every time)
			auto r = qsearch(pos);

			auto shallow_value = r.first;
			{
				const auto rootColor = pos.side_to_move();
				const auto pv = r.second;
				std::vector<StateInfo,AlignedAllocator<StateInfo>> states(pv.size());
				for (size_t i = 0; i < pv.size(); ++i)
				{
					pos.do_move(pv[i], states[i]);
					Eval::evaluate_with_no_return(pos);
				}
				shallow_value = (rootColor == pos.side_to_move()) ? Eval::evaluate(pos) : -Eval::evaluate(pos);
				for (auto it = pv.rbegin(); it != pv.rend(); ++it)
					pos.undo_move(*it);
			}

			// Evaluation value of deep search
			auto deep_value = (Value)ps.score;

			// Note) This code does not consider when eval_limit is specified in the learn command.

			// --- error calculation

#if !defined(LOSS_FUNCTION_IS_ELMO_METHOD)
			auto grad = calc_grad(deep_value, shallow_value, ps);

			// something like rmse
			sum_error += grad*grad;
			// Add the absolute value of the gradient
			sum_error2 += abs(grad);
			// Add the absolute value of the difference between the evaluation values
			sum_error3 += abs(shallow_value - deep_value);
#endif

			// --- calculation of cross entropy

			// For the time being, regarding the win rate and loss terms only in the elmo method
			// Calculate and display the cross entropy.

#if defined (LOSS_FUNCTION_IS_ELMO_METHOD)
			double test_cross_entropy_eval, test_cross_entropy_win, test_cross_entropy;
			double test_entropy_eval, test_entropy_win, test_entropy;
			calc_cross_entropy(deep_value, shallow_value, ps, test_cross_entropy_eval, test_cross_entropy_win, test_cross_entropy, test_entropy_eval, test_entropy_win, test_entropy);
			// The total cross entropy need not be abs() by definition.
			test_sum_cross_entropy_eval += test_cross_entropy_eval;
			test_sum_cross_entropy_win += test_cross_entropy_win;
			test_sum_cross_entropy += test_cross_entropy;
			test_sum_entropy_eval += test_entropy_eval;
			test_sum_entropy_win += test_entropy_win;
			test_sum_entropy += test_entropy;
			sum_norm += (double)abs(shallow_value);
#endif

			// Determine if the teacher's move and the score of the shallow search match
			{
				auto r = search(pos,1);
				if ((uint16_t)r.second[0] == ps.move)
					move_accord_count.fetch_add(1, std::memory_order_relaxed);
			}

			// Reduced one task because I did it
			--task_count;
		};

		// Throw the defined task to slave.
		task_dispatcher.push_task_async(task);
	}

	// join yourself as a slave
	task_dispatcher.on_idle(thread_id);

	// wait for all tasks to complete
	while (task_count)
		sleep(1);

#if !defined(LOSS_FUNCTION_IS_ELMO_METHOD)
	// rmse = root mean square error: mean square error
	// mae = mean absolute error: mean absolute error
	auto dsig_rmse = std::sqrt(sum_error / (sfen_for_mse.size() + epsilon));
	auto dsig_mae = sum_error2 / (sfen_for_mse.size() + epsilon);
	auto eval_mae = sum_error3 / (sfen_for_mse.size() + epsilon);
	cout << " , dsig rmse = " << dsig_rmse << " , dsig mae = " << dsig_mae
		<< " , eval mae = " << eval_mae;
#endif

#if defined ( LOSS_FUNCTION_IS_ELMO_METHOD )
#if defined(EVAL_NNUE)
	latest_loss_sum += test_sum_cross_entropy - test_sum_entropy;
	latest_loss_count += sr.sfen_for_mse.size();
#endif

// learn_cross_entropy may be called train cross entropy in the world of machine learning,
// When omitting the acronym, it is nice to be able to distinguish it from test cross entropy(tce) by writing it as lce.

	if (sr.sfen_for_mse.size() && done)
	{
		cout
			<< " , test_cross_entropy_eval = "  << test_sum_cross_entropy_eval / sr.sfen_for_mse.size()
			<< " , test_cross_entropy_win = "   << test_sum_cross_entropy_win / sr.sfen_for_mse.size()
			<< " , test_entropy_eval = "        << test_sum_entropy_eval / sr.sfen_for_mse.size()
			<< " , test_entropy_win = "         << test_sum_entropy_win / sr.sfen_for_mse.size()
			<< " , test_cross_entropy = "       << test_sum_cross_entropy / sr.sfen_for_mse.size()
			<< " , test_entropy = "             << test_sum_entropy / sr.sfen_for_mse.size()
			<< " , norm = "						<< sum_norm
			<< " , move accuracy = "			<< (move_accord_count * 100.0 / sr.sfen_for_mse.size()) << "%";
		if (done != static_cast<uint64_t>(-1))
		{
			cout
				<< " , learn_cross_entropy_eval = " << learn_sum_cross_entropy_eval / done
				<< " , learn_cross_entropy_win = "  << learn_sum_cross_entropy_win / done
				<< " , learn_entropy_eval = "       << learn_sum_entropy_eval / done
				<< " , learn_entropy_win = "        << learn_sum_entropy_win / done
				<< " , learn_cross_entropy = "      << learn_sum_cross_entropy / done
				<< " , learn_entropy = "            << learn_sum_entropy / done;
		}
		cout << endl;
	}
	else {
		cout << "Error! : sr.sfen_for_mse.size() = " << sr.sfen_for_mse.size() << " ,  done = " << done << endl;
	}

	// Clear 0 for next time.
	learn_sum_cross_entropy_eval = 0.0;
	learn_sum_cross_entropy_win = 0.0;
	learn_sum_cross_entropy = 0.0;
	learn_sum_entropy_eval = 0.0;
	learn_sum_entropy_win = 0.0;
	learn_sum_entropy = 0.0;
#else
	<< endl;
#endif
}


void LearnerThink::thread_worker(size_t thread_id)
{
#if defined(_OPENMP)
	omp_set_num_threads((int)Options["Threads"]);
#endif

	auto th = Threads[thread_id];
	auto& pos = th->rootPos;

	while (true)
	{
	// display mse (this is sometimes done only for thread 0)
	// Immediately after being read from the file...

#if defined(EVAL_NNUE)
		// Lock the evaluation function so that it is not used during updating.
		shared_lock<shared_timed_mutex> read_lock(nn_mutex, defer_lock);
		if (sr.next_update_weights <= sr.total_done ||
		    (thread_id != 0 && !read_lock.try_lock()))
#else
		if (sr.next_update_weights <= sr.total_done)
#endif
		{
			if (thread_id != 0)
			{
				// Wait except thread_id == 0.

				if (stop_flag)
					break;

				// I want to parallelize rmse calculation etc., so if task() is loaded, process it.
				task_dispatcher.on_idle(thread_id);
				continue;
			}
			else
			{
				// Only thread_id == 0 performs the following update process.

				// The weight array is not updated for the first time.
				if (sr.next_update_weights == 0)
				{
					sr.next_update_weights += mini_batch_size;
					continue;
				}

#if !defined(EVAL_NNUE)
				// Output the current time. Output every time.
				std::cout << sr.total_done << " sfens , at " << now_string() << std::endl;

				// Reflect the gradient in the weight array at this timing. The calculation of the gradient is just right for each 1M phase in terms of mini-batch.
				Eval::update_weights(epoch , freeze);

				// Display epoch and current eta for debugging.
				std::cout << "epoch = " << epoch << " , eta = " << Eval::get_eta() << std::endl;
#else
				{
					// update parameters

					// Lock the evaluation function so that it is not used during updating.
					lock_guard<shared_timed_mutex> write_lock(nn_mutex);
					Eval::NNUE::UpdateParameters(epoch);
				}
#endif
				++epoch;

				// Save once every 1 billion phases.

				// However, the elapsed time during update_weights() and calc_rmse() is ignored.
				if (++sr.save_count * mini_batch_size >= eval_save_interval)
				{
					sr.save_count = 0;

					// During this time, as the gradient calculation proceeds, the value becomes too large and I feel annoyed, so stop other threads.
					const bool converged = save();
					if (converged)
					{
						stop_flag = true;
						sr.stop_flag = true;
						break;
					}
				}

				// Calculate rmse. This is done for samples of 10,000 phases.
				// If you do with 40 cores, update_weights every 1 million phases
				// I don't think it's so good to be tiring.
				static uint64_t loss_output_count = 0;
				if (++loss_output_count * mini_batch_size >= loss_output_interval)
				{
					loss_output_count = 0;

					// Number of cases processed this time
					uint64_t done = sr.total_done - sr.last_done;

					// loss calculation
					calc_loss(thread_id , done);

#if defined(EVAL_NNUE)
					Eval::NNUE::CheckHealth();
#endif

					// Make a note of how far you have totaled.
					sr.last_done = sr.total_done;
				}

				// Next time, I want you to do this series of processing again when you process only mini_batch_size.
				sr.next_update_weights += mini_batch_size;

				// Since I was waiting for the update of this sr.next_update_weights except the main thread,
				// Once this value is updated, it will start moving again.
			}
		}

		PackedSfenValue ps;
	RetryRead:;
		if (!sr.read_to_thread_buffer(thread_id, ps))
		{
			// ran out of thread pool for my thread.
			// Because there are almost no phases left,
			// Terminate all other threads.

			stop_flag = true;
			break;
		}

		// The evaluation value exceeds the learning target value.
		// Ignore this aspect information.
		if (eval_limit <abs(ps.score))
			goto RetryRead;


		if (!use_draw_in_training && ps.game_result == 0)
			goto RetryRead;


		// Skip over the opening phase
		if (ps.gamePly < prng.rand(reduction_gameply))
			goto RetryRead;

#if 0
		auto sfen = pos.sfen_unpack(ps.data);
		pos.set(sfen);
#endif
		// ↑ Since it is slow when passing through sfen, I made a dedicated function.
		StateInfo si;
		const bool mirror = prng.rand(100) < mirror_percentage;
		if (pos.set_from_packed_sfen(ps.sfen,&si,th,mirror) != 0)
		{
			// I got a strange sfen. Should be debugged!
			// Since it is an illegal sfen, it may not be displayed with pos.sfen(), but it is better than not.
			cout << "Error! : illigal packed sfen = " << pos.fen() << endl;
			goto RetryRead;
		}
#if !defined(EVAL_NNUE)
		{
			auto key = pos.key();
			// Exclude the phase used for rmse calculation.
			if (sr.is_for_rmse(key) && use_hash_in_training)
				goto RetryRead;

			// Exclude the most recently used aspect.
			auto hash_index = size_t(key & (sr.READ_SFEN_HASH_SIZE - 1));
			auto key2 = sr.hash[hash_index];
			if (key == key2 && use_hash_in_training)
				goto RetryRead;
			sr.hash[hash_index] = key; // Replace with the current key.
		}
#endif

		// There is a possibility that all the pieces are blocked and stuck.
		// Also, the declaration win phase is excluded from learning because you cannot go to leaf with PV moves.
		// (shouldn't write out such teacher aspect itself, but may have written it out with an old generation routine)
	// Skip the position if there are no legal moves (=checkmated or stalemate).
		if (MoveList<LEGAL>(pos).size() == 0)
			goto RetryRead;

		// I can read it, so try displaying it.
		//		cout << pos << value << endl;

		// Evaluation value of shallow search (qsearch)
		auto r = qsearch(pos);
		auto pv = r.second;

		// Evaluation value of deep search
		auto deep_value = (Value)ps.score;

		// I feel that the mini batch has a better gradient.
		// Go to the leaf node as it is, add only to the gradient array, and later try AdaGrad at the time of rmse aggregation.

		auto rootColor = pos.side_to_move();

		// If the initial PV is different, it is better not to use it for learning.
		// If it is the result of searching a completely different place, it may become noise.
		// It may be better not to study where the difference in evaluation values ​​is too large.

#if 0
		// If you do this, about 13% of the phases will be excluded from the learning target. Good and bad are subtle.
		if (pv.size() >= 1 && (uint16_t)pv[0] != ps.move)
		{
			// dbg_hit_on(false);
			continue;
		}
#endif

#if 0
		// It may be better not to study where the difference in evaluation values ​​is too large.
		// → It's okay because it passes the win rate function... About 30% of the phases are out of the scope of learning...
		if (abs((int16_t)r.first - ps.score) >= Eval::PawnValue * 4)
		{
//			dbg_hit_on(false);
			continue;
		}
		//		dbg_hit_on(true);
#endif

		int ply = 0;

		// A helper function that adds the gradient to the current phase.
		auto pos_add_grad = [&]() {
			// Use the value of evaluate in leaf as shallow_value.
			// Using the return value of qsearch() as shallow_value,
			// If PV is interrupted in the middle, the phase where evaluate() is called to calculate the gradient, and
			// I don't think this is a very desirable property, as the aspect that gives that gradient will be different.
			// I have turned off the substitution table, but since the pv array has not been updated due to one stumbling block etc...

			Value shallow_value = (rootColor == pos.side_to_move()) ? Eval::evaluate(pos) : -Eval::evaluate(pos);

#if defined (LOSS_FUNCTION_IS_ELMO_METHOD)
			// Calculate loss for training data
			double learn_cross_entropy_eval, learn_cross_entropy_win, learn_cross_entropy;
			double learn_entropy_eval, learn_entropy_win, learn_entropy;
			calc_cross_entropy(deep_value, shallow_value, ps, learn_cross_entropy_eval, learn_cross_entropy_win, learn_cross_entropy, learn_entropy_eval, learn_entropy_win, learn_entropy);
			learn_sum_cross_entropy_eval += learn_cross_entropy_eval;
			learn_sum_cross_entropy_win += learn_cross_entropy_win;
			learn_sum_cross_entropy += learn_cross_entropy;
			learn_sum_entropy_eval += learn_entropy_eval;
			learn_sum_entropy_win += learn_entropy_win;
			learn_sum_entropy += learn_entropy;
#endif

#if !defined(EVAL_NNUE)
			// Slope
			double dj_dw = calc_grad(deep_value, shallow_value, ps);

			// Add jd_dw as the gradient (∂J/∂Wj) for the feature vector currently appearing in the leaf node.

			// If it is not PV termination, apply a discount rate.
			if (discount_rate != 0 && ply != (int)pv.size())
				dj_dw *= discount_rate;

			// Since we have reached leaf, add the gradient to the features that appear in this phase.
			// Update based on gradient later.
			Eval::add_grad(pos, rootColor, dj_dw, freeze);
#else
			const double example_weight =
			    (discount_rate != 0 && ply != (int)pv.size()) ? discount_rate : 1.0;
			Eval::NNUE::AddExample(pos, rootColor, ps, example_weight);
#endif

			// Since the processing is completed, the counter of the processed number is incremented
			sr.total_done++;
		};

		StateInfo state[MAX_PLY]; // PV of qsearch cannot be so long.
		bool illegal_move = false;
		for (auto m : pv)
		{
			// I shouldn't be an illegal player.
			// An illegal move sometimes comes here...
			if (!pos.pseudo_legal(m) || !pos.legal(m))
			{
				//cout << pos << m << endl;
				//assert(false);
				illegal_move = true;
				break;
			}

			// Processing when adding the gradient to the node on each PV.
			//If discount_rate is 0, this process is not performed.
			if (discount_rate != 0)
				pos_add_grad();

			pos.do_move(m, state[ply++]);

			// Since the value of evaluate in leaf is used, the difference is updated.
			Eval::evaluate_with_no_return(pos);
		}

		if (illegal_move) {
			sync_cout << "An illical move was detected... Excluded the position from the learning data..." << sync_endl;
			continue;
		}

		// Since we have reached the end phase of PV, add the slope here.
		pos_add_grad();

		// rewind the phase
		for (auto it = pv.rbegin(); it != pv.rend(); ++it)
			pos.undo_move(*it);

#if 0
		// When adding the gradient to the root phase
		shallow_value = (rootColor == pos.side_to_move()) ? Eval::evaluate(pos) : -Eval::evaluate(pos);
		dj_dw = calc_grad(deep_value, shallow_value, ps);
		Eval::add_grad(pos, rootColor, dj_dw , without_kpp);
#endif

	}

}

// Write evaluation function file.
bool LearnerThink::save(bool is_final)
{
	// Calculate and output check sum before saving. (To check if it matches the next time)
	std::cout << "Check Sum = "<< std::hex << Eval::calc_check_sum() << std::dec << std::endl;

	// Each time you save, change the extension part of the file name like "0","1","2",..
	// (Because I want to compare the winning rate for each evaluation function parameter later)

	if (save_only_once)
	{
		// When EVAL_SAVE_ONLY_ONCE is defined,
		// Do not dig a subfolder because I want to save it only once.
		Eval::save_eval("");
	}
	else if (is_final) {
		Eval::save_eval("final");
		return true;
	}
	else {
		static int dir_number = 0;
		const std::string dir_name = std::to_string(dir_number++);
		Eval::save_eval(dir_name);
#if defined(EVAL_NNUE)
		if (newbob_decay != 1.0 && latest_loss_count > 0) {
			static int trials = newbob_num_trials;
			const double latest_loss = latest_loss_sum / latest_loss_count;
			latest_loss_sum = 0.0;
			latest_loss_count = 0;
			cout << "loss: " << latest_loss;
			if (latest_loss < best_loss) {
				cout << " < best (" << best_loss << "), accepted" << endl;
				best_loss = latest_loss;
				best_nn_directory = Path::Combine((std::string)Options["EvalSaveDir"], dir_name);
				trials = newbob_num_trials;
			} else {
				cout << " >= best (" << best_loss << "), rejected" << endl;
				if (best_nn_directory.empty()) {
					cout << "WARNING: no improvement from initial model" << endl;
				} else {
					cout << "restoring parameters from " << best_nn_directory << endl;
					Eval::NNUE::RestoreParameters(best_nn_directory);
				}
				if (--trials > 0 && !is_final) {
					cout << "reducing learning rate scale from " << newbob_scale
					     << " to " << (newbob_scale * newbob_decay)
					     << " (" << trials << " more trials)" << endl;
					newbob_scale *= newbob_decay;
					Eval::NNUE::SetGlobalLearningRateScale(newbob_scale);
				}
			}
			if (trials == 0) {
				cout << "converged" << endl;
				return true;
			}
		}
#endif
	}
	return false;
}

// Shuffle_files(), shuffle_files_quick() subcontracting, writing part.
// output_file_name: Name of the file to write
// prng: random number
// afs: fstream of each teacher phase file
// a_count: The number of teacher positions inherent in each file.
void shuffle_write(const string& output_file_name , PRNG& prng , vector<fstream>& afs , vector<uint64_t>& a_count)
{
	uint64_t total_sfen_count = 0;
	for (auto c : a_count)
		total_sfen_count += c;

	// number of exported phases
	uint64_t write_sfen_count = 0;

	// Output the progress on the screen for each phase.
	const uint64_t buffer_size = 10000000;

	auto print_status = [&]()
	{
		// Output progress every 10M phase or when all writing is completed
		if (((write_sfen_count % buffer_size) == 0) ||
			(write_sfen_count == total_sfen_count))
			cout << write_sfen_count << " / " << total_sfen_count << endl;
	};


	cout << endl <<  "write : " << output_file_name << endl;

	fstream fs(output_file_name, ios::out | ios::binary);

	// total teacher positions
	uint64_t sum = 0;
	for (auto c : a_count)
		sum += c;

	while (sum != 0)
	{
		auto r = prng.rand(sum);

		// Aspects stored in fs[0] file ... Aspects stored in fs[1] file ...
		//Think of it as a series like, and determine in which file r is pointing.
		// The contents of the file are shuffled, so you can take the next element from that file.
		// Each file has a_count[x] phases, so this process can be written as follows.

		uint64_t n = 0;
		while (a_count[n] <= r)
			r -= a_count[n++];

		// This confirms n. Before you forget it, reduce the remaining number.

		--a_count[n];
		--sum;

		PackedSfenValue psv;
		// It's better to read and write all at once until the performance is not so good...
		if (afs[n].read((char*)&psv, sizeof(PackedSfenValue)))
		{
			fs.write((char*)&psv, sizeof(PackedSfenValue));
			++write_sfen_count;
			print_status();
		}
	}
	print_status();
	fs.close();
	cout << "done!" << endl;
}

// Subcontracting the teacher shuffle "learn shuffle" command.
// output_file_name: name of the output file where the shuffled teacher positions will be written
void shuffle_files(const vector<string>& filenames , const string& output_file_name , uint64_t buffer_size )
{
	// The destination folder is
	// tmp/ for temporary writing

	// Temporary file is written to tmp/ folder for each buffer_size phase.
	// For example, if buffer_size = 20M, you need a buffer of 20M*40bytes = 800MB.
	// In a PC with a small memory, it would be better to reduce this.
	// However, if the number of files increases too much, it will not be possible to open at the same time due to OS restrictions.
	// There should have been a limit of 512 per process on Windows, so you can open here as 500,
	// The current setting is 500 files x 20M = 10G = 10 billion phases.

	PSVector buf;
	buf.resize(buffer_size);
	// ↑ buffer, a marker that indicates how much you have used
	uint64_t buf_write_marker = 0;

	// File name to write (incremental counter because it is a serial number)
	uint64_t write_file_count = 0;

	// random number to shuffle
	PRNG prng((std::random_device())());

	// generate the name of the temporary file
	auto make_filename = [](uint64_t i)
	{
		return "tmp/" + to_string(i) + ".bin";
	};

	// Exported files in tmp/ folder, number of teacher positions stored in each
	vector<uint64_t> a_count;

	auto write_buffer = [&](uint64_t size)
	{
		// shuffle from buf[0] to buf[size-1]
		for (uint64_t i = 0; i < size; ++i)
			swap(buf[i], buf[(uint64_t)(prng.rand(size - i) + i)]);

		// write to a file
		fstream fs;
		fs.open(make_filename(write_file_count++), ios::out | ios::binary);
		fs.write((char*)&buf[0], size * sizeof(PackedSfenValue));
		fs.close();
		a_count.push_back(size);

		buf_write_marker = 0;
		cout << ".";
	};

	Dependency::mkdir("tmp");

	// Shuffle and export as a 10M phase shredded file.
	for (auto filename : filenames)
	{
		fstream fs(filename, ios::in | ios::binary);
		cout << endl << "open file = " << filename;
		while (fs.read((char*)&buf[buf_write_marker], sizeof(PackedSfenValue)))
			if (++buf_write_marker == buffer_size)
				write_buffer(buffer_size);

		// Read in units of sizeof(PackedSfenValue),
		// Ignore the last remaining fraction. (Fails in fs.read, so exit while)
		// (The remaining fraction seems to be half-finished data that was created because it was stopped halfway during teacher generation.)

	}

	if (buf_write_marker != 0)
		write_buffer(buf_write_marker);

	// Only shuffled files have been written write_file_count.
	// As a second pass, if you open all of them at the same time, select one at random and load one phase at a time
	// Now you have shuffled.

	// Original file for shirt full + tmp file + file to write requires 3 times the storage capacity of the original file.
	// 1 billion SSD is not enough for shuffling because it is 400GB for 10 billion phases.
	// If you want to delete (or delete by hand) the original file at this point after writing to tmp,
	// The storage capacity is about twice that of the original file.
	// So, maybe we should have an option to delete the original file.

	// Files are opened at the same time. It is highly possible that this will exceed FOPEN_MAX.
	// In that case, rather than adjusting buffer_size to reduce the number of files.

	vector<fstream> afs;
	for (uint64_t i = 0; i < write_file_count; ++i)
		afs.emplace_back(fstream(make_filename(i),ios::in | ios::binary));

	// Throw to the subcontract function and end.
	shuffle_write(output_file_name, prng, afs, a_count);
}

// Subcontracting the teacher shuffle "learn shuffleq" command.
// This is written in 1 pass.
// output_file_name: name of the output file where the shuffled teacher positions will be written
void shuffle_files_quick(const vector<string>& filenames, const string& output_file_name)
{
	// number of phases read
	uint64_t read_sfen_count = 0;

	// random number to shuffle
	PRNG prng((std::random_device())());

	// number of files
	size_t file_count = filenames.size();

	// Number of teacher positions stored in each file in filenames
	vector<uint64_t> a_count(file_count);

	// Count the number of teacher aspects in each file.
	vector<fstream> afs(file_count);

	for (size_t i = 0; i <file_count ;++i)
	{
		auto filename = filenames[i];
		auto& fs = afs[i];

		fs.open(filename, ios::in | ios::binary);
		fs.seekg(0, fstream::end);
		uint64_t eofPos = (uint64_t)fs.tellg();
		fs.clear(); // Otherwise, the next seek may fail.
		fs.seekg(0, fstream::beg);
		uint64_t begPos = (uint64_t)fs.tellg();
		uint64_t file_size = eofPos - begPos;
		uint64_t sfen_count = file_size / sizeof(PackedSfenValue);
		a_count[i] = sfen_count;

		// Output the number of sfen stored in each file.
		cout << filename << " = " << sfen_count << " sfens." << endl;
	}

	// Since we know the file size of each file,
	// open them all at once (already open),
	// Select one at a time and load one phase at a time
	// Now you have shuffled.

	// Throw to the subcontract function and end.
	shuffle_write(output_file_name, prng, afs, a_count);
}

// Subcontracting the teacher shuffle "learn shufflem" command.
// Read the whole memory and write it out with the specified file name.
void shuffle_files_on_memory(const vector<string>& filenames,const string output_file_name)
{
	PSVector buf;

	for (auto filename : filenames)
	{
		std::cout << "read : " << filename << std::endl;
		read_file_to_memory(filename, [&buf](uint64_t size) {
			assert((size % sizeof(PackedSfenValue)) == 0);
			// Expand the buffer and read after the last end.
			uint64_t last = buf.size();
			buf.resize(last + size / sizeof(PackedSfenValue));
			return (void*)&buf[last];
		});
	}

	// shuffle from buf[0] to buf[size-1]
	PRNG prng((std::random_device())());
	uint64_t size = (uint64_t)buf.size();
	std::cout << "shuffle buf.size() = " << size << std::endl;
	for (uint64_t i = 0; i < size; ++i)
		swap(buf[i], buf[(uint64_t)(prng.rand(size - i) + i)]);

	std::cout << "write : " << output_file_name << endl;

	// If the file to be written exceeds 2GB, it cannot be written in one shot with fstream::write, so use wrapper.
	write_memory_to_file(output_file_name, (void*)&buf[0], (uint64_t)sizeof(PackedSfenValue)*(uint64_t)buf.size());

	std::cout << "..shuffle_on_memory done." << std::endl;
}

void convert_bin(const vector<string>& filenames, const string& output_file_name, const int ply_minimum, const int ply_maximum, const int interpolate_eval)
{
	std::fstream fs;
	uint64_t data_size=0;
	uint64_t filtered_size = 0;
	auto th = Threads.main();
	auto &tpos = th->rootPos;
	// convert plain rag to packed sfenvalue for Yaneura king
	fs.open(output_file_name, ios::app | ios::binary);
	StateListPtr states;
	for (auto filename : filenames) {
		std::cout << "convert " << filename << " ... ";
		std::string line;
		ifstream ifs;
		ifs.open(filename);
		PackedSfenValue p;
		data_size = 0;
		filtered_size = 0;
		p.gamePly = 1; // Not included in apery format. Should be initialized
		bool ignore_flag = false;
		while (std::getline(ifs, line)) {
			std::stringstream ss(line);
			std::string token;
			std::string value;
			ss >> token;
			if (token == "fen") {
			  states = StateListPtr(new std::deque<StateInfo>(1)); // Drop old and create a new one
			  tpos.set(line.substr(4), false, &states->back(), Threads.main());
			  tpos.sfen_pack(p.sfen);
			}
			else if (token == "move") {
				ss >> value;
				p.move = UCI::to_move(tpos, value);
			}
			else if (token == "score") {
				ss >> p.score;
			}
			else if (token == "ply") {
				int temp;
				ss >> temp;
				if(temp < ply_minimum || temp > ply_maximum){
				  ignore_flag = true;
				}
				p.gamePly = uint16_t(temp); // No cast here?
				if (interpolate_eval != 0){
				  p.score = min(3000, interpolate_eval * temp);
				}
			}
			else if (token == "result") {
				int temp;
				ss >> temp;
				p.game_result = int8_t(temp); // Do you need a cast here?
				if (interpolate_eval){
				  p.score = p.score * p.game_result;
				}
			}
			else if (token == "e") {
			  if(!ignore_flag){
				fs.write((char*)&p, sizeof(PackedSfenValue));
				data_size+=1;
				// debug
				// std::cout<<tpos<<std::endl;
				// std::cout<<p.score<<","<<int(p.gamePly)<<","<<int(p.game_result)<<std::endl;
			  }else{
			    ignore_flag = false;
			    filtered_size += 1;
			  }
				
			}
		}
		std::cout << "done" << data_size <<" parsed " << filtered_size<<" is filtered"<< std::endl;
		ifs.close();
	}
	std::cout << "all done" << std::endl;
	fs.close();
}

static inline void ltrim(std::string &s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
		return !std::isspace(ch);
	}));
}

static inline void rtrim(std::string &s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
		return !std::isspace(ch);
	}).base(), s.end());
}

static inline void trim(std::string &s) {
	ltrim(s);
	rtrim(s);
}

int parse_game_result_from_pgn_extract(std::string result) {
	// White Win
	if (result == "\"1-0\"") {
		return 1;
	}
	// Black Win
	else if (result == "\"0-1\"") {
		return -1;
	}
	// Draw
	else {
		return 0;
	}
}

// 0.25 -->  0.25 * PawnValueEg
// #-4  --> -mate_in(4)
// #3   -->  mate_in(3)
// -M4  --> -mate_in(4)
// +M3  -->  mate_in(3)
Value parse_score_from_pgn_extract(std::string eval, bool& success) {
	success = true;

	if (eval.substr(0, 1) == "#") {
		if (eval.substr(1, 1) == "-") {
			return -mate_in(stoi(eval.substr(2, eval.length() - 2)));
		}
		else {
			return mate_in(stoi(eval.substr(1, eval.length() - 1)));
		}
	}
	else if (eval.substr(0, 2) == "-M") {
		//std::cout << "eval=" << eval << std::endl;
		return -mate_in(stoi(eval.substr(2, eval.length() - 2)));
	}
	else if (eval.substr(0, 2) == "+M") {
		//std::cout << "eval=" << eval << std::endl;
		return mate_in(stoi(eval.substr(2, eval.length() - 2)));
	}
	else {
		char *endptr;
		double value = strtod(eval.c_str(), &endptr);

		if (*endptr != '\0') {
			success = false;
			return VALUE_ZERO;
		}
		else {
			return Value(value * static_cast<double>(PawnValueEg));
		}
	}
}

void convert_bin_from_pgn_extract(const vector<string>& filenames, const string& output_file_name, const bool pgn_eval_side_to_move)
{
	std::cout << "pgn_eval_side_to_move=" << pgn_eval_side_to_move << std::endl;

	auto th = Threads.main();
	auto &pos = th->rootPos;

	std::fstream ofs;
	ofs.open(output_file_name, ios::out | ios::binary);

	int game_count = 0;
	int fen_count = 0;

	for (auto filename : filenames) {
		std::cout << now_string() << " convert " << filename << std::endl;
		ifstream ifs;
		ifs.open(filename);

		int game_result = 0;

		std::string line;
		while (std::getline(ifs, line)) {

			if (line.empty()) {
				continue;
			}

			else if (line.substr(0, 1) == "[") {
				std::regex pattern_result(R"(\[Result (.+?)\])");
				std::smatch match;

				// example: [Result "1-0"]
				if (std::regex_search(line, match, pattern_result)) {
					game_result = parse_game_result_from_pgn_extract(match.str(1));
					//std::cout << "game_result=" << game_result << std::endl;

					game_count++;
					if (game_count % 10000 == 0) {
						std::cout << now_string() << " game_count=" << game_count << ", fen_count=" << fen_count << std::endl;
					}
				}

				continue;
			}

			else {
				int gamePly = 0;
				bool first = true;

				PackedSfenValue psv;
				memset((char*)&psv, 0, sizeof(PackedSfenValue));

				auto itr = line.cbegin();

				while (true) {
					gamePly++;

					std::regex pattern_bracket(R"(\{(.+?)\})");

					std::regex pattern_eval1(R"(\[\%eval (.+?)\])");
					std::regex pattern_eval2(R"((.+?)\/)");

					// very slow
					//std::regex pattern_eval1(R"(\[\%eval (#?[+-]?(?:\d+\.?\d*|\.\d+))\])");
					//std::regex pattern_eval2(R"((#?[+-]?(?:\d+\.?\d*|\.\d+)\/))");

					std::regex pattern_move(R"((.+?)\{)");
					std::smatch match;

					// example: { [%eval 0.25] [%clk 0:10:00] }
					// example: { +0.71/22 1.2s }
					// example: { book }
					if (!std::regex_search(itr, line.cend(), match, pattern_bracket)) {
						break;
					}

					itr += match.position(0) + match.length(0);
					std::string str_eval_clk = match.str(1);
					trim(str_eval_clk);
					//std::cout << "str_eval_clk="<< str_eval_clk << std::endl;

					if (str_eval_clk == "book") {
						//std::cout << "book" << std::endl;

						// example: { rnbqkbnr/pppppppp/8/8/8/4P3/PPPP1PPP/RNBQKBNR b KQkq - 0 1 }
						if (!std::regex_search(itr, line.cend(), match, pattern_bracket)) {
							break;
						}
						itr += match.position(0) + match.length(0);
						continue;
					}

					// example: [%eval 0.25]
					// example: [%eval #-4]
					// example: [%eval #3]
					// example: +0.71/
					if (std::regex_search(str_eval_clk, match, pattern_eval1) ||
						std::regex_search(str_eval_clk, match, pattern_eval2)) {
						std::string str_eval = match.str(1);
						trim(str_eval);
						//std::cout << "str_eval=" << str_eval << std::endl;

						bool success = false;
						psv.score = Math::clamp(parse_score_from_pgn_extract(str_eval, success), -VALUE_MATE , VALUE_MATE);
						//std::cout << "success=" << success << ", psv.score=" << psv.score << std::endl;

						if (!success) {
							//std::cout << "str_eval=" << str_eval << std::endl;
							//std::cout << "success=" << success << ", psv.score=" << psv.score << std::endl;
							break;
						}
					}
					else {
						break;
					}

					if (first) {
						first = false;
					}
					else {
						psv.gamePly = gamePly;
						psv.game_result = game_result;

						if (pos.side_to_move() == BLACK) {
							if (!pgn_eval_side_to_move) {
								psv.score *= -1;
							}
							psv.game_result *= -1;
						}

#if 0
						std::cout << "write: "
								  << "score=" << psv.score
								  << ", move=" << psv.move
								  << ", gamePly=" << psv.gamePly
								  << ", game_result=" << (int)psv.game_result
								  << std::endl;
#endif

						ofs.write((char*)&psv, sizeof(PackedSfenValue));
						memset((char*)&psv, 0, sizeof(PackedSfenValue));

						fen_count++;
					}

					// example: { rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 1 }
					if (!std::regex_search(itr, line.cend(), match, pattern_bracket)) {
						break;
					}

					itr += match.position(0) + match.length(0);
					std::string str_fen = match.str(1);
					trim(str_fen);
					//std::cout << "str_fen=" << str_fen << std::endl;

					StateInfo si;
					pos.set(str_fen, false, &si, th);
					pos.sfen_pack(psv.sfen);

					// example: d7d5 {
					if (!std::regex_search(itr, line.cend(), match, pattern_move)) {
						break;
					}

					itr += match.position(0) + match.length(0) - 1;
					std::string str_move = match.str(1);
					trim(str_move);
					//std::cout << "str_move=" << str_move << std::endl;
					psv.move = UCI::to_move(pos, str_move);
				}

				game_result = 0;
			}
		}
	}

	std::cout << now_string() << " game_count=" << game_count << ", fen_count=" << fen_count << std::endl;
	std::cout << now_string() << " all done" << std::endl;
	ofs.close();
}

//void convert_plain(const vector<string>& filenames , const string& output_file_name)
//{
//	Position tpos;
//	std::ofstream ofs;
//	ofs.open(output_file_name, ios::app);
//	for (auto filename : filenames) {
//		std::cout << "convert " << filename << " ... ";
//
// 		// Just convert packedsfenvalue to text
//		std::fstream fs;
//		fs.open(filename, ios::in | ios::binary);
//		PackedSfenValue p;
//		while (true)
//		{
//			if (fs.read((char*)&p, sizeof(PackedSfenValue))) {
// 				// write as plain text
//				ofs << "sfen " << tpos.sfen_unpack(p.sfen) << std::endl;
//				ofs << "move " << to_usi_string(Move(p.move)) << std::endl;
//				ofs << "score " << p.score << std::endl;
//				ofs << "ply " << int(p.gamePly) << std::endl;
//				ofs << "result " << int(p.game_result) << std::endl;
//				ofs << "e" << std::endl;
//			}
//			else {
//				break;
//			}
//		}
//		fs.close();
//		std::cout << "done" << std::endl;
//	}
//	ofs.close();
//	std::cout << "all done" << std::endl;
//}

// Learning from the generated game record
void learn(Position&, istringstream& is)
{
	auto thread_num = (int)Options["Threads"];
	SfenReader sr(thread_num);

	LearnerThink learn_think(sr);
	vector<string> filenames;

	// mini_batch_size 1M aspect by default. This can be increased.
	auto mini_batch_size = LEARN_MINI_BATCH_SIZE;

	// Number of loops (read the game record file this number of times)
	int loop = 1;

	// Game file storage folder (get game file with relative path from here)
	string base_dir;

	string target_dir;

	// If 0, it will be the default value.
	double eta1 = 0.0;
	double eta2 = 0.0;
	double eta3 = 0.0;
	uint64_t eta1_epoch = 0; // eta2 is not applied by default
	uint64_t eta2_epoch = 0; // eta3 is not applied by default

#if defined(USE_GLOBAL_OPTIONS)
	// Save it for later restore.
	auto oldGlobalOptions = GlobalOptions;
	// If you hit the eval hash, you can not calculate rmse etc. so turn it off.
	GlobalOptions.use_eval_hash = false;
	// If you hit the replacement table, pruning may occur at the previous evaluation value, so turn it off.
	GlobalOptions.use_hash_probe = false;
#endif

	// --- Function that only shuffles the teacher aspect

	// normal shuffle
	bool shuffle_normal = false;
	uint64_t buffer_size = 20000000;
	// fast shuffling assuming each file is shuffled
	bool shuffle_quick = false;
	// A function to read the entire file in memory and shuffle it. (Requires file size memory)
	bool shuffle_on_memory = false;
	// Conversion of packed sfen. In plain, it consists of sfen(string), evaluation value (integer), move (eg 7g7f, string), result (loss-1, win 1, draw 0)
	bool use_convert_plain = false;
	// convert plain format teacher to Yaneura King's bin
	bool use_convert_bin = false;
	int ply_minimum = 0;
	int ply_maximum = 114514;
	bool interpolate_eval = 0;
	// convert teacher in pgn-extract format to Yaneura King's bin
	bool use_convert_bin_from_pgn_extract = false;
	bool pgn_eval_side_to_move = false;
	// File name to write in those cases (default is "shuffled_sfen.bin")
	string output_file_name = "shuffled_sfen.bin";

	// If the absolute value of the evaluation value in the deep search of the teacher phase exceeds this value, that phase is discarded.
	int eval_limit = 32000;

	// Flag to save the evaluation function file only once near the end.
	bool save_only_once = false;

	// Shuffle about what you are pre-reading on the teacher aspect. (Shuffle of about 10 million phases)
	// Turn on if you want to pass a pre-shuffled file.
	bool no_shuffle = false;

#if defined (LOSS_FUNCTION_IS_ELMO_METHOD)
	// elmo lambda
	ELMO_LAMBDA = 0.33;
	ELMO_LAMBDA2 = 0.33;
	ELMO_LAMBDA_LIMIT = 32000;
#endif

	// Discount rate. If this is set to a value other than 0, the slope will be added even at other than the PV termination. (At that time, apply this discount rate)
	double discount_rate = 0;

	// if (gamePly <rand(reduction_gameply)) continue;
	// An option to exclude the early stage from the learning target moderately like
	// If set to 1, rand(1)==0, so nothing is excluded.
	int reduction_gameply = 1;

	// Optional item that does not let you learn KK/KKP/KPP/KPPP
	array<bool,4> freeze = {};

#if defined(EVAL_NNUE)
	uint64_t nn_batch_size = 1000;
	double newbob_decay = 1.0;
	int newbob_num_trials = 2;
	string nn_options;
#endif

	uint64_t eval_save_interval = LEARN_EVAL_SAVE_INTERVAL;
	uint64_t loss_output_interval = 0;
	uint64_t mirror_percentage = 0;

	string validation_set_file_name;

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

		// Specify the number of loops
		else if (option == "loop")      is >> loop;

		// Game file storage folder (get game file with relative path from here)
		else if (option == "basedir")   is >> base_dir;

		// Mini batch size
		else if (option == "batchsize") is >> mini_batch_size;

		// learning rate
		else if (option == "eta")        is >> eta1;
		else if (option == "eta1")       is >> eta1; // alias
		else if (option == "eta2")       is >> eta2;
		else if (option == "eta3")       is >> eta3;
		else if (option == "eta1_epoch") is >> eta1_epoch;
		else if (option == "eta2_epoch") is >> eta2_epoch;
		else if (option == "use_draw_in_training_data_generation") is >> use_draw_in_training_data_generation;
		else if (option == "use_draw_in_training") is >> use_draw_in_training;
		else if (option == "use_draw_in_validation") is >> use_draw_in_validation;
		else if (option == "use_hash_in_training") is >> use_hash_in_training;
		// Discount rate
		else if (option == "discount_rate") is >> discount_rate;

		// No learning of KK/KKP/KPP/KPPP.
		else if (option == "freeze_kk")    is >> freeze[0];
		else if (option == "freeze_kkp")   is >> freeze[1];
		else if (option == "freeze_kpp")   is >> freeze[2];

#if defined(EVAL_KPPT) || defined(EVAL_KPP_KKPT) || defined(EVAL_KPP_KKPT_FV_VAR) || defined(EVAL_NABLA)

#elif defined(EVAL_KPPPT) || defined(EVAL_KPPP_KKPT) || defined(EVAL_HELICES)
		else if (option == "freeze_kppp")  is >> freeze[3];
#elif defined(EVAL_KKPP_KKPT) || defined(EVAL_KKPPT)
		else if (option == "freeze_kkpp")  is >> freeze[3];
#endif

#if defined (LOSS_FUNCTION_IS_ELMO_METHOD)
		// LAMBDA
		else if (option == "lambda")       is >> ELMO_LAMBDA;
		else if (option == "lambda2")      is >> ELMO_LAMBDA2;
		else if (option == "lambda_limit") is >> ELMO_LAMBDA_LIMIT;

#endif
		else if (option == "reduction_gameply") is >> reduction_gameply;

		// shuffle related
		else if (option == "shuffle")	shuffle_normal = true;
		else if (option == "buffer_size") is >> buffer_size;
		else if (option == "shuffleq")	shuffle_quick = true;
		else if (option == "shufflem")	shuffle_on_memory = true;
		else if (option == "output_file_name") is >> output_file_name;

		else if (option == "eval_limit") is >> eval_limit;
		else if (option == "save_only_once") save_only_once = true;
		else if (option == "no_shuffle") no_shuffle = true;

#if defined(EVAL_NNUE)
		else if (option == "nn_batch_size") is >> nn_batch_size;
		else if (option == "newbob_decay") is >> newbob_decay;
		else if (option == "newbob_num_trials") is >> newbob_num_trials;
		else if (option == "nn_options") is >> nn_options;
#endif
		else if (option == "eval_save_interval") is >> eval_save_interval;
		else if (option == "loss_output_interval") is >> loss_output_interval;
		else if (option == "mirror_percentage") is >> mirror_percentage;
		else if (option == "validation_set_file_name") is >> validation_set_file_name;

		// Rabbit convert related
		else if (option == "convert_plain") use_convert_plain = true;
		else if (option == "convert_bin") use_convert_bin = true;
		else if (option == "interpolate_eval") is >> interpolate_eval;
		else if (option == "convert_bin_from_pgn-extract") use_convert_bin_from_pgn_extract = true;
		else if (option == "pgn_eval_side_to_move") is >> pgn_eval_side_to_move;

		// Otherwise, it's a filename.
		else
			filenames.push_back(option);
	}
	if (loss_output_interval == 0)
		loss_output_interval = LEARN_RMSE_OUTPUT_INTERVAL * mini_batch_size;

	cout << "learn command , ";

	// Issue a warning if OpenMP is disabled.
#if !defined(_OPENMP)
	cout << "Warning! OpenMP disabled." << endl;
#endif

	// Display learning game file
	if (target_dir != "")
	{
		string kif_base_dir = Path::Combine(base_dir, target_dir);

		// Remove this folder. Keep it relative to base_dir.
#if defined(_MSC_VER)
		// If you use std::tr2, warning C4996 will appear, so suppress it.
		// * std::tr2 issued a deprecation warning by default under std:c++14, and was deleted by default in /std:c++17.
		#pragma warning(push)
		#pragma warning(disable:4996)

		namespace sys = std::filesystem;
		sys::path p(kif_base_dir); // Origin of enumeration
		std::for_each(sys::directory_iterator(p), sys::directory_iterator(),
			[&](const sys::path& p) {
			if (sys::is_regular_file(p))
				filenames.push_back(Path::Combine(target_dir, p.filename().generic_string()));
		});
		#pragma warning(pop)

#elif defined(__GNUC__)

		auto ends_with = [](std::string const & value, std::string const & ending)
		{
			if (ending.size() > value.size()) return false;
			return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
		};

		// It can't be helped, so read it using dirent.h.
		DIR *dp; // pointer to directory
		dirent* entry; // entry point returned by readdir()

		dp = opendir(kif_base_dir.c_str());
		if (dp != NULL)
		{
			do {
				entry = readdir(dp);
				// Only list files ending with ".bin"
				// →I hate this restriction when generating files with serial numbers...
				if (entry != NULL  && ends_with(entry->d_name, ".bin")  )
				{
					//cout << entry->d_name << endl;
					filenames.push_back(Path::Combine(target_dir, entry->d_name));
				}
			} while (entry != NULL);
			closedir(dp);
		}
#endif
	}

	cout << "learn from ";
	for (auto s : filenames)
		cout << s << " , ";
	cout << endl;
	if (!validation_set_file_name.empty())
	{
		cout << "validation set  : " << validation_set_file_name << endl;
	}

	cout << "base dir        : " << base_dir   << endl;
	cout << "target dir      : " << target_dir << endl;

	// shuffle mode
	if (shuffle_normal)
	{
		cout << "buffer_size     : " << buffer_size << endl;
		cout << "shuffle mode.." << endl;
		shuffle_files(filenames,output_file_name , buffer_size);
		return;
	}
	if (shuffle_quick)
	{
		cout << "quick shuffle mode.." << endl;
		shuffle_files_quick(filenames, output_file_name);
		return;
	}
	if (shuffle_on_memory)
	{
		cout << "shuffle on memory.." << endl;
		shuffle_files_on_memory(filenames,output_file_name);
		return;
	}
	//if (use_convert_plain)
	//{
	// 		is_ready(true);
	//  cout << "convert_plain.." << endl;
	//  convert_plain(filenames,output_file_name);
	//  return;
	//
	//}
	if (use_convert_bin)
	{
	  	is_ready(true);
		cout << "convert_bin.." << endl;
		convert_bin(filenames,output_file_name, ply_minimum, ply_maximum, interpolate_eval);
		return;
		
	}
	if (use_convert_bin_from_pgn_extract)
	{
		is_ready(true);
		cout << "convert_bin_from_pgn-extract.." << endl;
		convert_bin_from_pgn_extract(filenames, output_file_name, pgn_eval_side_to_move);
		return;
	}

	cout << "loop              : " << loop << endl;
	cout << "eval_limit        : " << eval_limit << endl;
	cout << "save_only_once    : " << (save_only_once ? "true" : "false") << endl;
	cout << "no_shuffle        : " << (no_shuffle ? "true" : "false") << endl;

	// Insert the file name for the number of loops.
	for (int i = 0; i < loop; ++i)
		// sfen reader, I'll read it in reverse order so I'll reverse it here. I'm sorry.
		for (auto it = filenames.rbegin(); it != filenames.rend(); ++it)
			sr.filenames.push_back(Path::Combine(base_dir, *it));

#if !defined(EVAL_NNUE)
	cout << "Gradient Method   : " << LEARN_UPDATE      << endl;
#endif
	cout << "Loss Function     : " << LOSS_FUNCTION     << endl;
	cout << "mini-batch size   : " << mini_batch_size   << endl;
#if defined(EVAL_NNUE)
	cout << "nn_batch_size     : " << nn_batch_size     << endl;
	cout << "nn_options        : " << nn_options        << endl;
#endif
	cout << "learning rate     : " << eta1 << " , " << eta2 << " , " << eta3 << endl;
	cout << "eta_epoch         : " << eta1_epoch << " , " << eta2_epoch << endl;
#if defined(EVAL_NNUE)
	if (newbob_decay != 1.0) {
		cout << "scheduling        : newbob with decay = " << newbob_decay
		     << ", " << newbob_num_trials << " trials" << endl;
	} else {
		cout << "scheduling        : default" << endl;
	}
#endif
	cout << "discount rate     : " << discount_rate     << endl;

	// If reduction_gameply is set to 0, rand(0) will be divided by 0, so correct it to 1.
	reduction_gameply = max(reduction_gameply, 1);
	cout << "reduction_gameply : " << reduction_gameply << endl;

#if defined (LOSS_FUNCTION_IS_ELMO_METHOD)
	cout << "LAMBDA            : " << ELMO_LAMBDA       << endl;
	cout << "LAMBDA2           : " << ELMO_LAMBDA2      << endl;
	cout << "LAMBDA_LIMIT      : " << ELMO_LAMBDA_LIMIT << endl;
#endif
	cout << "mirror_percentage : " << mirror_percentage << endl;
	cout << "eval_save_interval  : " << eval_save_interval << " sfens" << endl;
	cout << "loss_output_interval: " << loss_output_interval << " sfens" << endl;

#if defined(EVAL_KPPT) || defined(EVAL_KPP_KKPT) || defined(EVAL_KPP_KKPT_FV_VAR) || defined(EVAL_NABLA)
	cout << "freeze_kk/kkp/kpp      : " << freeze[0] << " , " << freeze[1] << " , " << freeze[2] << endl;
#elif defined(EVAL_KPPPT) || defined(EVAL_KPPP_KKPT) || defined(EVAL_HELICES)
	cout << "freeze_kk/kkp/kpp/kppp : " << freeze[0] << " , " << freeze[1] << " , " << freeze[2] << " , " << freeze[3] << endl;
#elif defined(EVAL_KKPP_KKPT) || defined(EVAL_KKPPT)
	cout << "freeze_kk/kkp/kpp/kkpp : " << freeze[0] << " , " << freeze[1] << " , " << freeze[2] << " , " << freeze[3] << endl;
#endif

	// -----------------------------------
	// various initialization
	// -----------------------------------

	cout << "init.." << endl;

	// Read evaluation function parameters
	is_ready(true);

#if !defined(EVAL_NNUE)
	cout << "init_grad.." << endl;

	// Initialize gradient array of merit function parameters
	Eval::init_grad(eta1,eta1_epoch,eta2,eta2_epoch,eta3);
#else
	cout << "init_training.." << endl;
	Eval::NNUE::InitializeTraining(eta1,eta1_epoch,eta2,eta2_epoch,eta3);
	Eval::NNUE::SetBatchSize(nn_batch_size);
	Eval::NNUE::SetOptions(nn_options);
	if (newbob_decay != 1.0 && !Options["SkipLoadingEval"]) {
		learn_think.best_nn_directory = std::string(Options["EvalDir"]);
	}
#endif

#if 0
	// A test to give a gradient of 1.0 to the initial stage of Hirate.
	pos.set_hirate();
	cout << Eval::evaluate(pos) << endl;
	//Eval::print_eval_stat(pos);
	Eval::add_grad(pos, BLACK, 32.0 , false);
	Eval::update_weights(1);
	pos.state()->sum.p[2][0] = VALUE_NOT_EVALUATED;
	cout << Eval::evaluate(pos) << endl;
	//Eval::print_eval_stat(pos);
#endif

	cout << "init done." << endl;

	// Reflect other option settings.
	learn_think.discount_rate = discount_rate;
	learn_think.eval_limit = eval_limit;
	learn_think.save_only_once = save_only_once;
	learn_think.sr.no_shuffle = no_shuffle;
	learn_think.freeze = freeze;
	learn_think.reduction_gameply = reduction_gameply;
#if defined(EVAL_NNUE)
	learn_think.newbob_scale = 1.0;
	learn_think.newbob_decay = newbob_decay;
	learn_think.newbob_num_trials = newbob_num_trials;
#endif
	learn_think.eval_save_interval = eval_save_interval;
	learn_think.loss_output_interval = loss_output_interval;
	learn_think.mirror_percentage = mirror_percentage;

	// Start a thread that loads the phase file in the background
	// (If this is not started, mse cannot be calculated.)
	learn_think.start_file_read_worker();

	learn_think.mini_batch_size = mini_batch_size;

	if (validation_set_file_name.empty()) {
	// Get about 10,000 data for mse calculation.
		sr.read_for_mse();
	} else {
		sr.read_validation_set(validation_set_file_name, eval_limit);
	}

	// Calculate rmse once at this point (timing of 0 sfen)
	// sr.calc_rmse();
#if defined(EVAL_NNUE)
	if (newbob_decay != 1.0) {
		learn_think.calc_loss(0, -1);
		learn_think.best_loss = learn_think.latest_loss_sum / learn_think.latest_loss_count;
		learn_think.latest_loss_sum = 0.0;
		learn_think.latest_loss_count = 0;
		cout << "initial loss: " << learn_think.best_loss << endl;
	}
#endif

	// -----------------------------------
	// start learning evaluation function parameters
	// -----------------------------------

	// Start learning.
	learn_think.go_think();

	// Save once at the end.
	learn_think.save(true);

#if defined(USE_GLOBAL_OPTIONS)
	// Restore Global Options.
	GlobalOptions = oldGlobalOptions;
#endif
}


} // namespace Learner

#if defined(GENSFEN2019)
#include "gensfen2019.cpp"
#endif


#endif // EVAL_LEARN
