// Definition of input features and network structure used in NNUE evaluation function

#ifndef NNUE_HALFKP_256X2_32_32_H_INCLUDED
#define NNUE_HALFKP_256X2_32_32_H_INCLUDED

#include "../features/feature_set.h"
#include "../features/half_kp.h"

#include "../layers/input_slice.h"
#include "../layers/affine_transform.h"
#include "../layers/clipped_relu.h"

namespace Eval::NNUE {

// Input features used in evaluation function
using RawFeatures = Features::FeatureSet<
    Features::HalfKP<Features::Side::kFriend>>;

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

#endif // #ifndef NNUE_HALFKP_256X2_32_32_H_INCLUDED
