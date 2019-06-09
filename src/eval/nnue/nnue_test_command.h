// NNUE評価関数に関するUSI拡張コマンドのインターフェイス

#ifndef _NNUE_TEST_COMMAND_H_
#define _NNUE_TEST_COMMAND_H_

#include "../../config.h"

#if defined(ENABLE_TEST_CMD) && defined(EVAL_NNUE)

namespace Eval {

namespace NNUE {

// NNUE評価関数に関するUSI拡張コマンド
void TestCommand(Position& pos, std::istream& stream);

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(ENABLE_TEST_CMD) && defined(EVAL_NNUE)

#endif
