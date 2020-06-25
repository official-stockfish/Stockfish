/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2020 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>
#ifndef _MSC_VER
#include <mm_malloc.h>
#endif

#include "types.h"
#include "thread_win32_osx.h"

const std::string engine_info(bool to_uci = false);
const std::string compiler_info();
void prefetch(void* addr);
void start_logger(const std::string& fname);
void* aligned_ttmem_alloc(size_t size, void*& mem);
void aligned_ttmem_free(void* mem); // nop if mem == nullptr

void dbg_hit_on(bool b);
void dbg_hit_on(bool c, bool b);
void dbg_mean_of(int v);
void dbg_print();

typedef std::chrono::milliseconds::rep TimePoint; // A value in milliseconds

static_assert(sizeof(TimePoint) == sizeof(int64_t), "TimePoint should be 64 bits");

inline TimePoint now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>
        (std::chrono::steady_clock::now().time_since_epoch()).count();
}

template<class Entry, int Size>
struct HashTable {
  Entry* operator[](Key key) { return &table[(uint32_t)key & (Size - 1)]; }

private:
  std::vector<Entry> table = std::vector<Entry>(Size); // Allocate on the heap
};


enum SyncCout { IO_LOCK, IO_UNLOCK };
std::ostream& operator<<(std::ostream&, SyncCout);

#define sync_cout std::cout << IO_LOCK
#define sync_endl std::endl << IO_UNLOCK

namespace Utility {

/// Clamp a value between lo and hi. Available in c++17.
template<class T> constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

}

/// xorshift64star Pseudo-Random Number Generator
/// This class is based on original code written and dedicated
/// to the public domain by Sebastiano Vigna (2014).
/// It has the following characteristics:
///
///  -  Outputs 64-bit numbers
///  -  Passes Dieharder and SmallCrush test batteries
///  -  Does not require warm-up, no zeroland to escape
///  -  Internal state is a single 64-bit integer
///  -  Period is 2^64 - 1
///  -  Speed: 1.60 ns/call (Core i7 @3.40GHz)
///
/// For further analysis see
///   <http://vigna.di.unimi.it/ftp/papers/xorshift.pdf>

class PRNG {

  uint64_t s;

  uint64_t rand64() {

    s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
    return s * 2685821657736338717LL;
  }

public:
  PRNG(uint64_t seed) : s(seed) { assert(seed); }

  template<typename T> T rand() { return T(rand64()); }

  /// Special generator used to fast init magic numbers.
  /// Output values only have 1/8th of their bits set on average.
  template<typename T> T sparse_rand()
  { return T(rand64() & rand64() & rand64()); }

  // 0からn-1までの乱数を返す。(一様分布ではないが現実的にはこれで十分)
  uint64_t rand(uint64_t n) { return rand<uint64_t>() % n; }

  // 内部で使用している乱数seedを返す。
  uint64_t get_seed() const { return s; }
};

// 乱数のseedを表示する。(デバッグ用)
inline std::ostream& operator<<(std::ostream& os, PRNG& prng)
{
  os << "PRNG::seed = " << std::hex << prng.get_seed() << std::dec;
  return os;
}

inline uint64_t mul_hi64(uint64_t a, uint64_t b) {
#if defined(__GNUC__) && defined(IS_64BIT)
    __extension__ typedef unsigned __int128 uint128;
    return ((uint128)a * (uint128)b) >> 64;
#else
    uint64_t aL = (uint32_t)a, aH = a >> 32;
    uint64_t bL = (uint32_t)b, bH = b >> 32;
    uint64_t c1 = (aL * bL) >> 32;
    uint64_t c2 = aH * bL + c1;
    uint64_t c3 = aL * bH + (uint32_t)c2;
    return aH * bH + (c2 >> 32) + (c3 >> 32);
#endif
}

/// Under Windows it is not possible for a process to run on more than one
/// logical processor group. This usually means to be limited to use max 64
/// cores. To overcome this, some special platform specific API should be
/// called to set group affinity for each thread. Original code from Texel by
/// Peter Österlund.

namespace WinProcGroup {
  void bindThisThread(size_t idx);
}

// 指定されたミリ秒だけsleepする。
extern void sleep(int ms);

// 現在時刻を文字列化したもを返す。(評価関数の学習時などにログ出力のために用いる)
std::string now_string();

// 途中での終了処理のためのwrapper
static void my_exit()
{
	sleep(3000); // エラーメッセージが出力される前に終了するのはまずいのでwaitを入れておく。
	exit(EXIT_FAILURE);
}

// msys2、Windows Subsystem for Linuxなどのgcc/clangでコンパイルした場合、
// C++のstd::ifstreamで::read()は、一発で2GB以上のファイルの読み書きが出来ないのでそのためのwrapperである。
//
// read_file_to_memory()の引数のcallback_funcは、ファイルがオープン出来た時点でそのファイルサイズを引数として
// callbackされるので、バッファを確保して、その先頭ポインタを返す関数を渡すと、そこに読み込んでくれる。
// これらの関数は、ファイルが見つからないときなどエラーの際には非0を返す。
//
// また、callbackされた関数のなかでバッファが確保できなかった場合や、想定していたファイルサイズと異なった場合は、
// nullptrを返せば良い。このとき、read_file_to_memory()は、読み込みを中断し、エラーリターンする。

int read_file_to_memory(std::string filename, std::function<void* (uint64_t)> callback_func);
int write_memory_to_file(std::string filename, void* ptr, uint64_t size);

// --------------------
//    PRNGのasync版
// --------------------

// PRNGのasync版
struct AsyncPRNG
{
  AsyncPRNG(uint64_t seed) : prng(seed) { assert(seed); }
  // [ASYNC] 乱数を一つ取り出す。
  template<typename T> T rand() {
    std::unique_lock<std::mutex> lk(mutex);
    return prng.rand<T>();
  }

  // [ASYNC] 0からn-1までの乱数を返す。(一様分布ではないが現実的にはこれで十分)
  uint64_t rand(uint64_t n) {
    std::unique_lock<std::mutex> lk(mutex);
    return prng.rand(n);
  }

  // 内部で使用している乱数seedを返す。
  uint64_t get_seed() const { return prng.get_seed(); }

protected:
  std::mutex mutex;
  PRNG prng;
};

// 乱数のseedを表示する。(デバッグ用)
inline std::ostream& operator<<(std::ostream& os, AsyncPRNG& prng)
{
  os << "AsyncPRNG::seed = " << std::hex << prng.get_seed() << std::dec;
  return os;
}

// --------------------
//       Math
// --------------------

// 進行度の計算や学習で用いる数学的な関数
namespace Math {
	// シグモイド関数
	//  = 1.0 / (1.0 + std::exp(-x))
	double sigmoid(double x);

	// シグモイド関数の微分
	//  = sigmoid(x) * (1.0 - sigmoid(x))
	double dsigmoid(double x);

	// vを[lo,hi]の間に収まるようにクリップする。
	// ※　Stockfishではこの関数、bitboard.hに書いてある。
	template<class T> constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
		return v < lo ? lo : v > hi ? hi : v;
	}

}

// --------------------
//       Path
// --------------------

// C#にあるPathクラス的なもの。ファイル名の操作。
// C#のメソッド名に合わせておく。
struct Path
{
	// path名とファイル名を結合して、それを返す。
	// folder名のほうは空文字列でないときに、末尾に'/'か'\\'がなければそれを付与する。
	static std::string Combine(const std::string& folder, const std::string& filename)
	{
		if (folder.length() >= 1 && *folder.rbegin() != '/' && *folder.rbegin() != '\\')
			return folder + "/" + filename;

		return folder + filename;
	}

	// full path表現から、(フォルダ名を除いた)ファイル名の部分を取得する。
	static std::string GetFileName(const std::string& path)
	{
		// "\"か"/"か、どちらを使ってあるかはわからない。
		auto path_index1 = path.find_last_of("\\") + 1;
		auto path_index2 = path.find_last_of("/") + 1;
		auto path_index = std::max(path_index1, path_index2);

		return path.substr(path_index);
	}
};

extern void* aligned_malloc(size_t size, size_t align);
static void aligned_free(void* ptr) { _mm_free(ptr); }

// alignasを指定しているのにnewのときに無視される＆STLのコンテナがメモリ確保するときに無視するので、
// そのために用いるカスタムアロケーター。
template <typename T>
class AlignedAllocator {
public:
  using value_type = T;

  AlignedAllocator() {}
  AlignedAllocator(const AlignedAllocator&) {}
  AlignedAllocator(AlignedAllocator&&) {}

  template <typename U> AlignedAllocator(const AlignedAllocator<U>&) {}

  T* allocate(std::size_t n) { return (T*)aligned_malloc(n * sizeof(T), alignof(T)); }
  void deallocate(T* p, std::size_t n) { aligned_free(p); }
};

// --------------------
//  Dependency Wrapper
// --------------------

namespace Dependency
{
  // Linux環境ではgetline()したときにテキストファイルが'\r\n'だと
  // '\r'が末尾に残るのでこの'\r'を除去するためにwrapperを書く。
  // そのため、fstreamに対してgetline()を呼び出すときは、
  // std::getline()ではなく単にgetline()と書いて、この関数を使うべき。
  extern bool getline(std::ifstream& fs, std::string& s);

  // フォルダを作成する。
  // カレントフォルダ相対で指定する。dir_nameに日本語は使っていないものとする。
  // 成功すれば0、失敗すれば非0が返る。
  extern int mkdir(std::string dir_name);
}

#endif // #ifndef MISC_H_INCLUDED
