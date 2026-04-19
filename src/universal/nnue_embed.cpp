// Standalone NNUE embedding for universal binary builds

#include "../evaluate.h"

extern const unsigned char gEmbeddedNNUEBigData[] = {
#embed EvalFileDefaultNameBig
};
extern const unsigned int         gEmbeddedNNUEBigSize = sizeof(gEmbeddedNNUEBigData);
extern const unsigned char* const gEmbeddedNNUEBigEnd = gEmbeddedNNUEBigData + gEmbeddedNNUEBigSize;

extern const unsigned char gEmbeddedNNUESmallData[] = {
#embed EvalFileDefaultNameSmall
};
extern const unsigned int         gEmbeddedNNUESmallSize = sizeof(gEmbeddedNNUESmallData);
extern const unsigned char* const gEmbeddedNNUESmallEnd =
  gEmbeddedNNUESmallData + gEmbeddedNNUESmallSize;
