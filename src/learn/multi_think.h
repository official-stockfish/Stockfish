#ifndef _MULTI_THINK_
#define _MULTI_THINK_

#if defined(EVAL_LEARN)

#include <functional>

#include "../misc.h"
#include "../learn/learn.h"
#include "../thread_win32_osx.h"

#include <atomic>

// 棋譜からの学習や、自ら思考させて定跡を生成するときなど、
// 複数スレッドが個別にSearch::think()を呼び出したいときに用いるヘルパクラス。
// このクラスを派生させて用いる。
struct MultiThink
{
	MultiThink() : prng(21120903)
	{
		loop_count = 0;
	}

	// マスタースレッドからこの関数を呼び出すと、スレッドがそれぞれ思考して、
	// 思考終了条件を満たしたところで制御を返す。
	// 他にやってくれること。
	// ・各スレッドがLearner::search(),qsearch()を呼び出しても安全なように
	// 　置換表をスレッドごとに分離してくれる。(終了後、元に戻してくれる。)
	// ・bookはon the flyモードだとthread safeではないので、このモードを一時的に
	// 　オフにしてくれる。
	// [要件]
	// 1) thread_worker()のオーバーライド
	// 2) set_loop_max()でループ回数の設定
	// 3) 定期的にcallbackされる関数を設定する(必要なら)
	//   callback_funcとcallback_interval
	void go_think();

	// 派生クラス側で初期化したいものがあればこれをoverrideしておけば、
	// go_think()で初期化が終わったタイミングで呼び出される。
	// 定跡の読み込みなどはそのタイミングで行うと良い。
	virtual void init() {}

	// go_think()したときにスレッドを生成して呼び出されるthread worker
	// これをoverrideして用いる。
	virtual void thread_worker(size_t thread_id) = 0;

	// go_think()したときにcallback_seconds[秒]ごとにcallbackされる。
	std::function<void()> callback_func;
	uint64_t callback_seconds = 600;

	// workerが処理する(Search::think()を呼び出す)回数を設定する。
	void set_loop_max(uint64_t loop_max_) { loop_max = loop_max_; }
	
	// set_loop_max()で設定した値を取得する。
	uint64_t get_loop_max() const { return loop_max; }

	// [ASYNC] ループカウンターの値を取り出して、取り出し後にループカウンターを加算する。
	// もしループカウンターがloop_maxに達していたらUINT64_MAXを返す。
	// 局面を生成する場合などは、局面を生成するタイミングでこの関数を呼び出すようにしないと、
	// 生成した局面数と、カウンターの値が一致しなくなってしまうので注意すること。
	uint64_t get_next_loop_count() {
		std::unique_lock<Mutex> lk(loop_mutex);
		if (loop_count >= loop_max)
			return UINT64_MAX;
		return loop_count++;
	}

	// [ASYNC] 処理した個数を返す用。呼び出されるごとにインクリメントされたカウンターが返る。
	uint64_t get_done_count() {
		std::unique_lock<Mutex> lk(loop_mutex);
		return ++done_count;
	}

	// worker threadがI/Oにアクセスするときのmutex
	Mutex io_mutex;

protected:
	// 乱数発生器本体
	AsyncPRNG prng;

private:
	// workerが処理する(Search::think()を呼び出す)回数
	std::atomic<uint64_t> loop_max;
	// workerが処理した(Search::think()を呼び出した)回数
	std::atomic<uint64_t> loop_count;
	// 処理した回数を返す用。
	std::atomic<uint64_t> done_count;

	// ↑の変数を変更するときのmutex
	Mutex loop_mutex;

	// スレッドの終了フラグ。
	// vector<bool>にすると複数スレッドから書き換えようとしたときに正しく反映されないことがある…はず。
	typedef uint8_t Flag;
	std::vector<Flag> thread_finished;

};

// idle時間にtaskを処理する仕組み。
// masterは好きなときにpush_task_async()でtaskを渡す。
// slaveは暇なときにon_idle()を実行すると、taskを一つ取り出してqueueがなくなるまで実行を続ける。
// MultiThinkのthread workerをmaster-slave方式で書きたいときに用いると便利。
struct TaskDispatcher
{
	typedef std::function<void(size_t /* thread_id */)> Task;

	// slaveはidle中にこの関数を呼び出す。
	void on_idle(size_t thread_id)
	{
		Task task;
		while ((task = get_task_async()) != nullptr)
			task(thread_id);

		sleep(1);
	}

	// [ASYNC] taskを一つ積む。
	void push_task_async(Task task)
	{
		std::unique_lock<Mutex> lk(task_mutex);
		tasks.push_back(task);
	}

	// task用の配列の要素をsize分だけ事前に確保する。
	void task_reserve(size_t size)
	{
		tasks.reserve(size);
	}

protected:
	// taskの集合
	std::vector<Task> tasks;

	// [ASYNC] taskを一つ取り出す。on_idle()から呼び出される。
	Task get_task_async()
	{
		std::unique_lock<Mutex> lk(task_mutex);
		if (tasks.size() == 0)
			return nullptr;
		Task task = *tasks.rbegin();
		tasks.pop_back();
		return task;
	}

	// tasksにアクセスするとき用のmutex
	Mutex task_mutex;
};

#endif // defined(EVAL_LEARN) && defined(YANEURAOU_2018_OTAFUKU_ENGINE)

#endif
