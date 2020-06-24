#include "../types.h"

#if defined(EVAL_LEARN)

#include "multi_think.h"
#include "../tt.h"
#include "../uci.h"

#include <thread>

void MultiThink::go_think()
{
	// あとでOptionsの設定を復元するためにコピーで保持しておく。
	auto oldOptions = Options;

	// 定跡を用いる場合、on the flyで行なうとすごく時間がかかる＆ファイルアクセスを行なう部分が
	// thread safeではないので、メモリに丸読みされている状態であることをここで保証する。
	Options["BookOnTheFly"] = std::string("false");

	// 評価関数の読み込み等
	// learnコマンドの場合、評価関数読み込み後に評価関数の値を補正している可能性があるので、
	// メモリの破損チェックは省略する。
	is_ready(true);

	// 派生クラスのinit()を呼び出す。
	init();

	// ループ上限はset_loop_max()で設定されているものとする。
	loop_count = 0;
	done_count = 0;

	// threadをOptions["Threads"]の数だけ生成して思考開始。
	std::vector<std::thread> threads;
	auto thread_num = (size_t)Options["Threads"];

	// worker threadの終了フラグの確保
	thread_finished.resize(thread_num);
	
	// worker threadの起動
	for (size_t i = 0; i < thread_num; ++i)
	{
		thread_finished[i] = 0;
		threads.push_back(std::thread([i, this]
		{ 
			// プロセッサの全スレッドを使い切る。
			WinProcGroup::bindThisThread(i);

			// オーバーライドされている処理を実行
			this->thread_worker(i);

			// スレッドが終了したので終了フラグを立てる
			this->thread_finished[i] = 1;
		}));
	}

	// すべてのthreadの終了待ちを
	// for (auto& th : threads)
	//  th.join();
	// のように書くとスレッドがまだ仕事をしている状態でここに突入するので、
	// その間、callback_func()が呼び出せず、セーブできなくなる。
	// そこで終了フラグを自前でチェックする必要がある。

	// すべてのスレッドが終了したかを判定する関数
	auto threads_done = [&]()
	{
		// ひとつでも終了していなければfalseを返す
		for (auto& f : thread_finished)
			if (!f)
				return false;
		return true;
	};

	// コールバック関数が設定されているならコールバックする。
	auto do_a_callback = [&]()
	{
		if (callback_func)
			callback_func();
	};


	for (uint64_t i = 0 ; ; )
	{
		// 全スレッドが終了していたら、ループを抜ける。
		if (threads_done())
			break;

		sleep(1000);

		// callback_secondsごとにcallback_func()が呼び出される。
		if (++i == callback_seconds)
		{
			do_a_callback();
			// ↑から戻ってきてからカウンターをリセットしているので、
			// do_a_callback()のなかでsave()などにどれだけ時間がかかろうと
			// 次に呼び出すのは、そこから一定時間の経過を要する。
			i = 0;
		}
	}

	// 最後の保存。
	std::cout << std::endl << "finalize..";

	// do_a_callback();
	// →　呼び出し元で保存するはずで、ここでは要らない気がする。

	// 終了したフラグは立っているがスレッドの終了コードの実行中であるということはありうるので
	// join()でその終了を待つ必要がある。
	for (auto& th : threads)
		th.join();

	// 全スレッドが終了しただけでfileの書き出しスレッドなどはまだ動いていて
	// 作業自体は完了していない可能性があるのでスレッドがすべて終了したことだけ出力する。
	std::cout << "all threads are joined." << std::endl;

	// Optionsを書き換えたので復元。
	// 値を代入しないとハンドラが起動しないのでこうやって復元する。
	for (auto& s : oldOptions)
		Options[s.first] = std::string(s.second);

}


#endif // defined(EVAL_LEARN)
