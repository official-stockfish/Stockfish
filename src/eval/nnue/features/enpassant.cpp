// NNUE評価関数の入力特徴量Kの定義

#if defined(EVAL_NNUE)

#include "enpassant.h"
#include "index_list.h"

namespace Eval {

  namespace NNUE {

    namespace Features {

      // 特徴量のうち、値が1であるインデックスのリストを取得する
      void EnPassant::AppendActiveIndices(
        const Position& pos, Color perspective, IndexList* active) {
        // コンパイラの警告を回避するため、配列サイズが小さい場合は何もしない
        if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions) return;

        auto epSquare = pos.state()->epSquare;
        if (epSquare == SQ_NONE) {
          return;
        }

        if (perspective == BLACK) {
          epSquare = Inv(epSquare);
        }

        auto file = file_of(epSquare);
        active->push_back(file);
      }

      // 特徴量のうち、一手前から値が変化したインデックスのリストを取得する
      void EnPassant::AppendChangedIndices(
        const Position& pos, Color perspective,
        IndexList* removed, IndexList* added) {
        // Not implemented.
        assert(false);
      }

    }  // namespace Features

  }  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
