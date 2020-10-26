#ifndef _NNUE_TRAINER_FEATURES_FACTORIZER_H_
#define _NNUE_TRAINER_FEATURES_FACTORIZER_H_

#include "nnue/nnue_common.h"

#include "nnue/trainer/trainer.h"

// NNUE evaluation function feature conversion class template
namespace Eval::NNUE::Features {

    // Class template that converts input features into learning features
    // By default, the learning feature is the same as the original input feature, and specialized as necessary
    template <typename FeatureType>
    class Factorizer {
    public:
        static constexpr std::string get_name() {
            return std::string("No factorizer");
        }

        static constexpr std::string get_factorizers_string() {
            return "  - " + get_name();
        }

        // Get the dimensionality of the learning feature
        static constexpr IndexType get_dimensions() {
            return FeatureType::kDimensions;
        }

        // Get index of learning feature and scale of learning rate
        static void append_training_features(
            IndexType base_index, std::vector<TrainingFeature>* training_features) {

            assert(base_index <FeatureType::kDimensions);
            training_features->emplace_back(base_index);
        }
    };

    // Learning feature information
    struct FeatureProperties {
        bool active;
        IndexType dimensions;
    };

    // Add the original input features to the learning features
    template <typename FeatureType>
    IndexType append_base_feature(
        FeatureProperties properties, IndexType base_index,
        std::vector<TrainingFeature>* training_features) {

        assert(properties.dimensions == FeatureType::kDimensions);
        assert(base_index < FeatureType::kDimensions);
        training_features->emplace_back(base_index);
        return properties.dimensions;
    }

    // If the learning rate scale is not 0, inherit other types of learning features
    template <typename FeatureType>
    IndexType inherit_features_if_required(
        IndexType index_offset, FeatureProperties properties, IndexType base_index,
        std::vector<TrainingFeature>* training_features) {

        if (!properties.active) {
            return 0;
        }

        assert(properties.dimensions == Factorizer<FeatureType>::get_dimensions());
        assert(base_index < FeatureType::kDimensions);

        const auto start = training_features->size();
        Factorizer<FeatureType>::append_training_features(
            base_index, training_features);

        for (auto i = start; i < training_features->size(); ++i) {
            auto& feature = (*training_features)[i];
            assert(feature.get_index() < Factorizer<FeatureType>::get_dimensions());
            feature.shift_index(index_offset);
        }

        return properties.dimensions;
    }

    // Return the index difference as needed, without adding learning features
    // Call instead of InheritFeaturesIfRequired() if there are no corresponding features
    IndexType skip_features(FeatureProperties properties) {
        if (!properties.active)
            return 0;

        return properties.dimensions;
    }

    // Get the dimensionality of the learning feature
    template <std::size_t N>
    constexpr IndexType get_active_dimensions(
        const FeatureProperties (&properties)[N]) {

        static_assert(N > 0, "");

        IndexType dimensions = properties[0].dimensions;

        for (std::size_t i = 1; i < N; ++i) {
            if (properties[i].active) {
                dimensions += properties[i].dimensions;
            }
        }

        return dimensions;
    }

    // get the number of elements in the array
    template <typename T, std::size_t N>
    constexpr std::size_t get_array_length(const T (&/*array*/)[N]) {
        return N;
    }

}  // namespace Eval::NNUE::Features

#endif
