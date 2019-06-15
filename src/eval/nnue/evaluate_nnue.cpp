// NNUE評価関数の計算に関するコード

#include <fstream>
#include <iostream>

#include "../../evaluate.h"
#include "../../position.h"
#include "../../misc.h"
#include "../../uci.h"

#include "evaluate_nnue.h"

namespace Eval {

namespace NNUE {

// 入力特徴量変換器
AlignedPtr<FeatureTransformer> feature_transformer;

// 評価関数
AlignedPtr<Network> network;

// 評価関数ファイル名
const char* const kFileName = "nn.bin";

// 評価関数の構造を表す文字列を取得する
std::string GetArchitectureString() {
  return "Features=" + FeatureTransformer::GetStructureString() +
      ",Network=" + Network::GetStructureString();
}

namespace {

namespace Detail {

// 評価関数パラメータを初期化する
template <typename T>
void Initialize(AlignedPtr<T>& pointer) {
  pointer.reset(reinterpret_cast<T*>(aligned_malloc(sizeof(T), alignof(T))));
  std::memset(pointer.get(), 0, sizeof(T));
}

// 評価関数パラメータを読み込む
template <typename T>
bool ReadParameters(std::istream& stream, const AlignedPtr<T>& pointer) {
  std::uint32_t header;
  stream.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!stream || header != T::GetHashValue()) return false;
  return pointer->ReadParameters(stream);
}

// 評価関数パラメータを書き込む
template <typename T>
bool WriteParameters(std::ostream& stream, const AlignedPtr<T>& pointer) {
  constexpr std::uint32_t header = T::GetHashValue();
  stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
  return pointer->WriteParameters(stream);
}

}  // namespace Detail

// 評価関数パラメータを初期化する
void Initialize() {
  Detail::Initialize(feature_transformer);
  Detail::Initialize(network);
}

}  // namespace

// ヘッダを読み込む
bool ReadHeader(std::istream& stream,
  std::uint32_t* hash_value, std::string* architecture) {
  std::uint32_t version, size;
  stream.read(reinterpret_cast<char*>(&version), sizeof(version));
  stream.read(reinterpret_cast<char*>(hash_value), sizeof(*hash_value));
  stream.read(reinterpret_cast<char*>(&size), sizeof(size));
  if (!stream || version != kVersion) return false;
  architecture->resize(size);
  stream.read(&(*architecture)[0], size);
  return !stream.fail();
}

// ヘッダを書き込む
bool WriteHeader(std::ostream& stream,
  std::uint32_t hash_value, const std::string& architecture) {
  stream.write(reinterpret_cast<const char*>(&kVersion), sizeof(kVersion));
  stream.write(reinterpret_cast<const char*>(&hash_value), sizeof(hash_value));
  const std::uint32_t size = static_cast<std::uint32_t>(architecture.size());
  stream.write(reinterpret_cast<const char*>(&size), sizeof(size));
  stream.write(architecture.data(), size);
  return !stream.fail();
}

// 評価関数パラメータを読み込む
bool ReadParameters(std::istream& stream) {
  std::uint32_t hash_value;
  std::string architecture;
  if (!ReadHeader(stream, &hash_value, &architecture)) return false;
  if (hash_value != kHashValue) return false;
  if (!Detail::ReadParameters(stream, feature_transformer)) return false;
  if (!Detail::ReadParameters(stream, network)) return false;
  return stream && stream.peek() == std::ios::traits_type::eof();
}

// 評価関数パラメータを書き込む
bool WriteParameters(std::ostream& stream) {
  if (!WriteHeader(stream, kHashValue, GetArchitectureString())) return false;
  if (!Detail::WriteParameters(stream, feature_transformer)) return false;
  if (!Detail::WriteParameters(stream, network)) return false;
  return !stream.fail();
}

// 差分計算ができるなら進める
static void UpdateAccumulatorIfPossible(const Position& pos) {
  feature_transformer->UpdateAccumulatorIfPossible(pos);
}

// 評価値を計算する
static Value ComputeScore(const Position& pos, bool refresh = false) {
  auto& accumulator = pos.state()->accumulator;
  if (!refresh && accumulator.computed_score) {
    return accumulator.score;
  }

  alignas(kCacheLineSize) TransformedFeatureType
      transformed_features[FeatureTransformer::kBufferSize];
  feature_transformer->Transform(pos, transformed_features, refresh);
  alignas(kCacheLineSize) char buffer[Network::kBufferSize];
  const auto output = network->Propagate(transformed_features, buffer);

  // VALUE_MAX_EVALより大きな値が返ってくるとaspiration searchがfail highして
  // 探索が終わらなくなるのでVALUE_MAX_EVAL以下であることを保証すべき。

  // この現象が起きても、対局時に秒固定などだとそこで探索が打ち切られるので、
  // 1つ前のiterationのときの最善手がbestmoveとして指されるので見かけ上、
  // 問題ない。このVALUE_MAX_EVALが返ってくるような状況は、ほぼ詰みの局面であり、
  // そのような詰みの局面が出現するのは終盤で形勢に大差がついていることが多いので
  // 勝敗にはあまり影響しない。

  // しかし、教師生成時などdepth固定で探索するときに探索から戻ってこなくなるので
  // そのスレッドの計算時間を無駄にする。またdepth固定対局でtime-outするようになる。

  auto score = static_cast<Value>(output[0] / FV_SCALE);

  // 1) ここ、下手にclipすると学習時には影響があるような気もするが…。
  // 2) accumulator.scoreは、差分計算の時に用いないので書き換えて問題ない。
  score = Math::clamp(score , -VALUE_MAX_EVAL , VALUE_MAX_EVAL);

  accumulator.score = score;
  accumulator.computed_score = true;
  return accumulator.score;
}

}  // namespace NNUE

#if defined(USE_EVAL_HASH)
// HashTableに評価値を保存するために利用するクラス
struct alignas(16) ScoreKeyValue {
#if defined(USE_SSE2)
  ScoreKeyValue() = default;
  ScoreKeyValue(const ScoreKeyValue& other) {
    static_assert(sizeof(ScoreKeyValue) == sizeof(__m128i),
                  "sizeof(ScoreKeyValue) should be equal to sizeof(__m128i)");
    _mm_store_si128(&as_m128i, other.as_m128i);
  }
  ScoreKeyValue& operator=(const ScoreKeyValue& other) {
    _mm_store_si128(&as_m128i, other.as_m128i);
    return *this;
  }
#endif

  // evaluate hashでatomicに操作できる必要があるのでそのための操作子
  void encode() {
#if defined(USE_SSE2)
    // ScoreKeyValue は atomic にコピーされるので key が合っていればデータも合っている。
#else
    key ^= score;
#endif
  }
  // decode()はencode()の逆変換だが、xorなので逆変換も同じ変換。
  void decode() { encode(); }

  union {
    struct {
      std::uint64_t key;
      std::uint64_t score;
    };
#if defined(USE_SSE2)
    __m128i as_m128i;
#endif
  };
};

// シンプルなHashTableの実装。
// Sizeは2のべき乗。
template <typename T, size_t Size>
struct HashTable {
  HashTable() { clear(); }
  T* operator [] (const Key k) { return entries_ + (static_cast<size_t>(k) & (Size - 1)); }
  void clear() { memset(entries_, 0, sizeof(T)*Size); }

  // Size が 2のべき乗であることのチェック
  static_assert((Size & (Size - 1)) == 0, "");

 private:
  T entries_[Size];
};

// evaluateしたものを保存しておくHashTable(俗にいうehash)

#if !defined(USE_LARGE_EVAL_HASH)
// 134MB(魔女のAVX2以外の時の設定)
struct EvaluateHashTable : HashTable<ScoreKeyValue, 0x800000> {};
#else
// prefetch有りなら大きいほうが良いのでは…。
// →　あまり変わらないし、メモリもったいないのでデフォルトでは↑の設定で良いか…。
// 1GB(魔女のAVX2の時の設定)
struct EvaluateHashTable : HashTable<ScoreKeyValue, 0x4000000> {};
#endif

EvaluateHashTable g_evalTable;

// prefetchする関数も用意しておく。
void prefetch_evalhash(const Key key) {
  constexpr auto mask = ~((u64)0x1f);
  prefetch((void*)((u64)g_evalTable[key] & mask));
}
#endif

// 評価関数ファイルを読み込む
// benchコマンドなどでOptionsを保存して復元するのでこのときEvalDirが変更されたことになって、
// 評価関数の再読込の必要があるというフラグを立てるため、この関数は2度呼び出されることがある。
void load_eval() {
  NNUE::Initialize();

#if defined(EVAL_LEARN)
  if (!Options["SkipLoadingEval"])
#endif
  {
    const std::string dir_name = Options["EvalDir"];
    const std::string file_name = Path::Combine(dir_name, NNUE::kFileName);
    std::ifstream stream(file_name, std::ios::binary);
    const bool result = NNUE::ReadParameters(stream);

//    ASSERT(result);
	if (!result)
	{
		// 読み込みエラーのとき終了してくれないと困る。
		std::cout << "Error! : failed to read " << NNUE::kFileName << std::endl;
		my_exit();
	}
  }
}

// 初期化
void init() {
}

// 評価関数。差分計算ではなく全計算する。
// Position::set()で一度だけ呼び出される。(以降は差分計算)
// 手番側から見た評価値を返すので注意。(他の評価関数とは設計がこの点において異なる)
// なので、この関数の最適化は頑張らない。
Value compute_eval(const Position& pos) {
  return NNUE::ComputeScore(pos, true);
}

// 評価関数
Value NNUE::evaluate(const Position& pos) {
  const auto& accumulator = pos.state()->accumulator;
  if (accumulator.computed_score) {
    return accumulator.score;
  }

#if defined(USE_GLOBAL_OPTIONS)
  // GlobalOptionsでeval hashを用いない設定になっているなら
  // eval hashへの照会をskipする。
  if (!GlobalOptions.use_eval_hash) {
    ASSERT_LV5(pos.state()->materialValue == Eval::material(pos));
    return NNUE::ComputeScore(pos);
  }
#endif

#if defined(USE_EVAL_HASH)
  // evaluate hash tableにはあるかも。
  const Key key = pos.state()->key();
  ScoreKeyValue entry = *g_evalTable[key];
  entry.decode();
  if (entry.key == key) {
    // あった！
    return Value(entry.score);
  }
#endif

  Value score = NNUE::ComputeScore(pos);
#if defined(USE_EVAL_HASH)
  // せっかく計算したのでevaluate hash tableに保存しておく。
  entry.key = key;
  entry.score = score;
  entry.encode();
  *g_evalTable[key] = entry;
#endif

  return score;
}

// 差分計算ができるなら進める
void evaluate_with_no_return(const Position& pos) {
  NNUE::UpdateAccumulatorIfPossible(pos);
}

// 現在の局面の評価値の内訳を表示する
void print_eval_stat(Position& /*pos*/) {
  std::cout << "--- EVAL STAT: not implemented" << std::endl;
}

}  // namespace Eval
