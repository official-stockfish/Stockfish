#ifndef _NNUE_TRAINER_FEATURES_FACTORIZER_HALF_KP_H_
#define _NNUE_TRAINER_FEATURES_FACTORIZER_HALF_KP_H_

#include "factorizer.h"

#include "nnue/features/half_kp.h"
#include "nnue/features/p.h"
#include "nnue/features/half_relative_kp.h"

// Specialization of NNUE evaluation function feature conversion class template for HalfKP
namespace Eval::NNUE::Features {

    // Class template that converts input features into learning features
    // Specialization for HalfKP
    template <Side AssociatedKing>
    class Factorizer<HalfKP<AssociatedKing>> {
    private:
        using FeatureType = HalfKP<AssociatedKing>;

        // The maximum value of the number of indexes whose value is 1 at the same time among the feature values
        static constexpr IndexType kMaxActiveDimensions =
            FeatureType::kMaxActiveDimensions;

        // Type of learning feature
        enum TrainingFeatureType {
            kFeaturesHalfKP,
            kFeaturesHalfK,
            kFeaturesP,
            kFeaturesHalfRelativeKP,
            kNumTrainingFeatureTypes,
        };

        // Learning feature information
        static constexpr FeatureProperties kProperties[] = {
            // kFeaturesHalfKP
            {true, FeatureType::kDimensions},
            // kFeaturesHalfK
            {true, SQUARE_NB},
            // kFeaturesP
            {true, Factorizer<P>::get_dimensions()},
            // kFeaturesHalfRelativeKP
            {true, Factorizer<HalfRelativeKP<AssociatedKing>>::get_dimensions()},
        };

        static_assert(get_array_length(kProperties) == kNumTrainingFeatureTypes, "");

    public:
        static constexpr std::string get_name() {
            return std::string("Factorizer<") + FeatureType::kName + ">";
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

            // kFeaturesHalfKP
            IndexType index_offset = append_base_feature<FeatureType>(
                kProperties[kFeaturesHalfKP], base_index, training_features);

            const auto sq_k = static_cast<Square>(base_index / PS_END);
            const auto p = static_cast<IndexType>(base_index % PS_END);

            // kFeaturesHalfK
            {
                const auto& properties = kProperties[kFeaturesHalfK];
                if (properties.active) {
                    training_features->emplace_back(index_offset + sq_k);
                    index_offset += properties.dimensions;
                }
            }

            // kFeaturesP
            index_offset += inherit_features_if_required<P>(
                index_offset, kProperties[kFeaturesP], p, training_features);
            // kFeaturesHalfRelativeKP
            if (p >= PS_W_PAWN) {
                index_offset += inherit_features_if_required<HalfRelativeKP<AssociatedKing>>(
                    index_offset, kProperties[kFeaturesHalfRelativeKP],
                    HalfRelativeKP<AssociatedKing>::make_index(sq_k, p),
                    training_features);
            }
            else {
                index_offset += skip_features(kProperties[kFeaturesHalfRelativeKP]);
            }

            assert(index_offset == get_dimensions());
        }
    };

    template <Side AssociatedKing>
    constexpr FeatureProperties Factorizer<HalfKP<AssociatedKing>>::kProperties[];

}  // namespace Eval::NNUE::Features

#endif
