#ifndef _NNUE_TRAINER_FEATURES_FACTORIZER_HALF_KA_H_
#define _NNUE_TRAINER_FEATURES_FACTORIZER_HALF_KA_H_

#include "factorizer.h"

#include "nnue/features/half_ka.h"
#include "nnue/features/a.h"
#include "nnue/features/half_relative_ka.h"

// Specialization of NNUE evaluation function feature conversion class template for HalfKA
namespace Eval::NNUE::Features {

    // Class template that converts input features into learning features
    // Specialization for HalfKA
    template <Side AssociatedKing>
    class Factorizer<HalfKA<AssociatedKing>> {
    private:
        using FeatureType = HalfKA<AssociatedKing>;

        // The maximum value of the number of indexes whose value is 1 at the same time among the feature values
        static constexpr IndexType kMaxActiveDimensions =
            FeatureType::kMaxActiveDimensions;

        // Type of learning feature
        enum TrainingFeatureType {
            kFeaturesHalfKA,
            kFeaturesA,
            kFeaturesHalfRelativeKA,
            kNumTrainingFeatureTypes,
        };

        // Learning feature information
        static constexpr FeatureProperties kProperties[] = {
            // kFeaturesHalfA
            {true, FeatureType::kDimensions},
            // kFeaturesA
            {true, Factorizer<A>::get_dimensions()},
            // kFeaturesHalfRelativeKA
            {true, Factorizer<HalfRelativeKA<AssociatedKing>>::get_dimensions()},
        };

        static_assert(get_array_length(kProperties) == kNumTrainingFeatureTypes, "");

    public:
        static constexpr std::string get_name() {
            return std::string("Factorizer<") + FeatureType::kName + "> -> " + "A, HalfRelativeKA";
        }

        static constexpr std::string get_factorizers_string() {
            return "  - " + get_name();
        }

        // Get the dimensionality of the learning feature
        static constexpr IndexType get_dimensions() {
            return get_active_dimensions(kProperties);
        }

        // Get index of learning feature and scale of learning rate
        static void append_training_features(
            IndexType base_index, std::vector<TrainingFeature>* training_features) {

            // kFeaturesHalfA
            IndexType index_offset = append_base_feature<FeatureType>(
                kProperties[kFeaturesHalfKA], base_index, training_features);

            const auto sq_k = static_cast<Square>(base_index / PS_END2);
            const auto a = static_cast<IndexType>(base_index % PS_END2);

            // kFeaturesA
            index_offset += inherit_features_if_required<A>(
                index_offset, kProperties[kFeaturesA], a, training_features);

            // kFeaturesHalfRelativeKA
            if (a >= PS_W_PAWN) {
                index_offset += inherit_features_if_required<HalfRelativeKA<AssociatedKing>>(
                    index_offset, kProperties[kFeaturesHalfRelativeKA],
                    HalfRelativeKA<AssociatedKing>::make_index(sq_k, a),
                    training_features);
            }
            else {
                index_offset += skip_features(kProperties[kFeaturesHalfRelativeKA]);
            }

            assert(index_offset == get_dimensions());
        }
    };

    template <Side AssociatedKing>
    constexpr FeatureProperties Factorizer<HalfKA<AssociatedKing>>::kProperties[];

}  // namespace Eval::NNUE::Features

#endif // #ifndef _NNUE_TRAINER_FEATURES_FACTORIZER_HALF_KA_H_
