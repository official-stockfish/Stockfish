#ifndef _MULTI_THINK_
#define _MULTI_THINK_

#if defined(EVAL_LEARN)

#include <functional>

#include "../misc.h"
#include "../learn/learn.h"
#include "../thread_win32_osx.h"

#include <atomic>

// Learning from a game record, when making yourself think and generating a fixed track, etc.
// Helper class used when multiple threads want to call Search::think() individually.
// Derive and use this class.
struct MultiThink
{
	MultiThink() : prng(21120903)
	{
		loop_count = 0;
	}

	// Call this function from the master thread, each thread will think,
	// Return control when the thought ending condition is satisfied.
	// Do something else.
	// ・It is safe for each thread to call Learner::search(),qsearch()
	// Separates the substitution table for each thread. (It will be restored after the end.)
	// ・Book is not thread safe when in on the fly mode, so temporarily change this mode.
	// Turn it off.
	// [Requirements]
	// 1) Override thread_worker()
	// 2) Set the loop count with set_loop_max()
	// 3) set a function to be called back periodically (if necessary)
	// callback_func and callback_interval
	void go_think();

	// If there is something you want to initialize on the derived class side, override this,
	// Called when initialization is completed with go_think().
	// It is better to read the fixed trace at that timing.
	virtual void init() {}

	// A thread worker that is called by creating a thread when you go_think()
	// Override and use this.
	virtual void thread_worker(size_t thread_id) = 0;

	// Called back every callback_seconds [seconds] when go_think().
	std::function<void()> callback_func;
	uint64_t callback_seconds = 600;

	// Set the number of times worker processes (calls Search::think()).
	void set_loop_max(uint64_t loop_max_) { loop_max = loop_max_; }

	// Get the value set by set_loop_max().
	uint64_t get_loop_max() const { return loop_max; }

	// [ASYNC] Take the value of the loop counter and add the loop counter after taking it out.
	// If the loop counter has reached loop_max, return UINT64_MAX.
	// If you want to generate a phase, you must call this function at the time of generating the phase,
	// Please note that the number of generated phases and the value of the counter will not match.
	uint64_t get_next_loop_count() {
		std::unique_lock<std::mutex> lk(loop_mutex);
		if (loop_count >= loop_max)
			return UINT64_MAX;
		return loop_count++;
	}

	// [ASYNC] For returning the processed number. Each time it is called, it returns a counter that is incremented.
	uint64_t get_done_count() {
		std::unique_lock<std::mutex> lk(loop_mutex);
		return ++done_count;
	}

	// Mutex when worker thread accesses I/O
	std::mutex io_mutex;

protected:
	// Random number generator body
	AsyncPRNG prng;

private:
	// number of times worker processes (calls Search::think())
	std::atomic<uint64_t> loop_max;
	// number of times the worker has processed (calls Search::think())
	std::atomic<uint64_t> loop_count;
	// To return the number of times it has been processed.
	std::atomic<uint64_t> done_count;

	// Mutex when changing the variables in ↑
	std::mutex loop_mutex;

	// Thread end flag.
	// vector<bool> may not be reflected properly when trying to rewrite from multiple threads...
	typedef uint8_t Flag;
	std::vector<Flag> thread_finished;

};

// Mechanism to process task during idle time.
// master passes the task with push_task_async() whenever you like.
// When slave executes on_idle() in its spare time, it retrieves one task and continues execution until there is no queue.
// Convenient to use when you want to write MultiThink thread worker in master-slave method.
struct TaskDispatcher
{
	typedef std::function<void(size_t /* thread_id */)> Task;

	// slave calls this function during idle.
	void on_idle(size_t thread_id)
	{
		Task task;
		while ((task = get_task_async()) != nullptr)
			task(thread_id);

		sleep(1);
	}

	// Stack [ASYNC] task.
	void push_task_async(Task task)
	{
		std::unique_lock<std::mutex> lk(task_mutex);
		tasks.push_back(task);
	}

	// Allocate size array elements for task in advance.
	void task_reserve(size_t size)
	{
		tasks.reserve(size);
	}

protected:
	// set of tasks
	std::vector<Task> tasks;

	// Take out one [ASYNC] task. Called from on_idle().
	Task get_task_async()
	{
		std::unique_lock<std::mutex> lk(task_mutex);
		if (tasks.size() == 0)
			return nullptr;
		Task task = *tasks.rbegin();
		tasks.pop_back();
		return task;
	}

	// a mutex for accessing tasks
	std::mutex task_mutex;
};

#endif // defined(EVAL_LEARN) && defined(YANEURAOU_2018_OTAFUKU_ENGINE)

#endif
