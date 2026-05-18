// Standalone NNUE embedding for universal binary builds

#include "../evaluate.h"

extern const unsigned char gEmbeddedNNUEData[] =
#ifdef __has_embed
  {
    #embed EvalFileDefaultName
};
const unsigned int padding = 0;
#else
    #include "network_dump.inc"
  ;
const unsigned int padding = 1;  // trailing NUL byte
#endif
extern const unsigned int gEmbeddedNNUESize = sizeof(gEmbeddedNNUEData) - padding;
