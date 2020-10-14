// Definition of input features and network structure used in NNUE evaluation function

#ifndef K_P_CR_256X2_32_32_H
#define K_P_CR_256X2_32_32_H

#include "nnue/features/feature_set.h"
#include "nnue/features/k.h"
#include "nnue/features/p.h"
#include "nnue/features/castling_right.h"

#include "nnue/layers/input_slice.h"
#include "nnue/layers/affine_transform.h"
#include "nnue/layers/clipped_relu.h"

namespace Eval::NNUE {

    // Input features used in evaluation function
    using RawFeatures = Features::FeatureSet<Features::K, Features::P,
        Features::CastlingRight>;

    // Number of input feature dimensions after conversion
    constexpr IndexType kTransformedFeatureDimensions = 256;

    namespace Layers {

        // define network structure
        using InputLayer = InputSlice<kTransformedFeatureDimensions * 2>;
        using HiddenLayer1 = ClippedReLU<AffineTransform<InputLayer, 32>>;
        using HiddenLayer2 = ClippedReLU<AffineTransform<HiddenLayer1, 32>>;
        using OutputLayer = AffineTransform<HiddenLayer2, 1>;

    }  // namespace Layers

    using Network = Layers::OutputLayer;

}  // namespace Eval::NNUE
#endif // K_P_CR_256X2_32_32_H
