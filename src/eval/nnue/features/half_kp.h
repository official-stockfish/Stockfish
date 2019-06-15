// NNUE評価関数の入力特徴量HalfKPの定義

#ifndef _NNUE_FEATURES_HALF_KP_H_
#define _NNUE_FEATURES_HALF_KP_H_

#if defined(EVAL_NNUE)

#include "../../../evaluate.h"
#include "features_common.h"

namespace Eval {

namespace NNUE {

namespace Features {

// 特徴量HalfKP：自玉または敵玉の位置と、玉以外の駒の位置の組み合わせ
template <Side AssociatedKing>
class HalfKP {
 public:
  // 特徴量名
  static constexpr const char* kName =
      (AssociatedKing == Side::kFriend) ? "HalfKP(Friend)" : "HalfKP(Enemy)";
  // 評価関数ファイルに埋め込むハッシュ値
  static constexpr std::uint32_t kHashValue =
      0x5D69D5B9u ^ (AssociatedKing == Side::kFriend);
  // 特徴量の次元数
  static constexpr IndexType kDimensions =
      static_cast<IndexType>(SQUARE_NB) * static_cast<IndexType>(fe_end);
  // 特徴量のうち、同時に値が1となるインデックスの数の最大値
  static constexpr IndexType kMaxActiveDimensions = PIECE_NUMBER_KING;
  // 差分計算の代わりに全計算を行うタイミング
  static constexpr TriggerEvent kRefreshTrigger =
      (AssociatedKing == Side::kFriend) ?
      TriggerEvent::kFriendKingMoved : TriggerEvent::kEnemyKingMoved;

  // 特徴量のうち、値が1であるインデックスのリストを取得する
  static void AppendActiveIndices(const Position& pos, Color perspective,
                                  IndexList* active);

  // 特徴量のうち、一手前から値が変化したインデックスのリストを取得する
  static void AppendChangedIndices(const Position& pos, Color perspective,
                                   IndexList* removed, IndexList* added);

  // 玉の位置とBonaPieceから特徴量のインデックスを求める
  static IndexType MakeIndex(Square sq_k, BonaPiece p);

 private:
  // 駒の情報を取得する
  static void GetPieces(const Position& pos, Color perspective,
                        BonaPiece** pieces, Square* sq_target_k);
};

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
