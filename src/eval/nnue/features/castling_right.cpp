// NNUE評価関数の入力特徴量Kの定義

#if defined(EVAL_NNUE)

#include "castling_right.h"
#include "index_list.h"

namespace Eval {

  namespace NNUE {

    namespace Features {

      // 特徴量のうち、値が1であるインデックスのリストを取得する
      void CastlingRight::AppendActiveIndices(
        const Position& pos, Color perspective, IndexList* active) {
        // コンパイラの警告を回避するため、配列サイズが小さい場合は何もしない
        if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions) return;

        int castling_rights = pos.state()->castlingRights;
        int relative_castling_rights;
        if (perspective == WHITE) {
          relative_castling_rights = castling_rights;
        }
        else {
          // Invert the perspective.
          relative_castling_rights = ((castling_rights & 3) << 2)
            & ((castling_rights >> 2) & 3);
        }

        for (int i = 0; i < kDimensions; ++i) {
          if (relative_castling_rights & (i << 1)) {
            active->push_back(i);
          }
        }
      }

      // 特徴量のうち、一手前から値が変化したインデックスのリストを取得する
      void CastlingRight::AppendChangedIndices(
        const Position& pos, Color perspective,
        IndexList* removed, IndexList* added) {

        int previous_castling_rights = pos.state()->previous->castlingRights;
        int current_castling_rights = pos.state()->castlingRights;
        int relative_previous_castling_rights;
        int relative_current_castling_rights;
        if (perspective == WHITE) {
          relative_previous_castling_rights = previous_castling_rights;
          relative_current_castling_rights = current_castling_rights;
        }
        else {
          // Invert the perspective.
          relative_previous_castling_rights = ((previous_castling_rights & 3) << 2)
            & ((previous_castling_rights >> 2) & 3);
          relative_current_castling_rights = ((current_castling_rights & 3) << 2)
            & ((current_castling_rights >> 2) & 3);
        }

        for (int i = 0; i < kDimensions; ++i) {
          if ((relative_previous_castling_rights & (i << 1)) &&
            (relative_current_castling_rights & (i << 1)) == 0) {
            removed->push_back(i);
          }
        }
      }

    }  // namespace Features

  }  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
