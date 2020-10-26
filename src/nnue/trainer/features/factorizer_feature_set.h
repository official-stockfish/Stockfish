#ifndef _NNUE_TRAINER_FEATURES_FACTORIZER_FEATURE_SET_H_
#define _NNUE_TRAINER_FEATURES_FACTORIZER_FEATURE_SET_H_

#include "factorizer.h"

#include "nnue/features/feature_set.h"

// Specialization for feature set of feature conversion class template of NNUE evaluation function
namespace Eval::NNUE::Features {

    // Class template that converts input features into learning features
    // Specialization for FeatureSet
    template <typename FirstFeatureType, typename... RemainingFeatureTypes>
    class Factorizer<FeatureSet<FirstFeatureType, RemainingFeatureTypes...>> {
    private:
        using Head = Factorizer<FeatureSet<FirstFeatureType>>;
        using Tail = Factorizer<FeatureSet<RemainingFeatureTypes...>>;

    public:
        // number of dimensions of original input features
        static constexpr IndexType kBaseDimensions =
            FeatureSet<FirstFeatureType, RemainingFeatureTypes...>::kDimensions;

        static constexpr std::string get_factorizers_string() {
            std::string str = "  - ";
            str += Head::get_name();
            str += '\n';
            str += Tail::get_factorizers_string();
            return str;
        }

        // Get the dimensionality of the learning feature
        static constexpr IndexType get_dimensions() {
            return Head::get_dimensions() + Tail::get_dimensions();
        }

        // Get index of learning feature and scale of learning rate
        static void append_training_features(
            IndexType base_index, std::vector<TrainingFeature>* training_features,
            IndexType base_dimensions = kBaseDimensions) {

            assert(base_index < kBaseDimensions);

            constexpr auto boundary = FeatureSet<RemainingFeatureTypes...>::kDimensions;

            if (base_index < boundary) {
                Tail::append_training_features(
                    base_index, training_features, base_dimensions);
            }
            else {
                const auto start = training_features->size();

                Head::append_training_features(
                    base_index - boundary, training_features, base_dimensions);

                for (auto i = start; i < training_features->size(); ++i) {
                    auto& feature = (*training_features)[i];
                    const auto index = feature.get_index();

                    assert(index < Head::get_dimensions() ||
                               (index >= base_dimensions &&
                                index < base_dimensions +
                                        Head::get_dimensions() - Head::kBaseDimensions));

                    if (index < Head::kBaseDimensions) {
                        feature.shift_index(Tail::kBaseDimensions);
                    }
                    else {
                        feature.shift_index(Tail::get_dimensions() - Tail::kBaseDimensions);
                    }
                }
            }
        }
    };

    // Class template that converts input features into learning features
    // Specialization when FeatureSet has one template argument
    template <typename FeatureType>
    class Factorizer<FeatureSet<FeatureType>> {
    public:
        // number of dimensions of original input features
        static constexpr IndexType kBaseDimensions = FeatureType::kDimensions;

        static constexpr std::string get_name() {
            return FeatureType::kName;
        }

        static constexpr std::string get_factorizers_string() {
            return "  - " + get_name();
        }

        // Get the dimensionality of the learning feature
        static constexpr IndexType get_dimensions() {
            return Factorizer<FeatureType>::get_dimensions();
        }

        // Get index of learning feature and scale of learning rate
        static void append_training_features(
            IndexType base_index, std::vector<TrainingFeature>* training_features,
            IndexType base_dimensions = kBaseDimensions) {

            assert(base_index < kBaseDimensions);

            const auto start = training_features->size();

            Factorizer<FeatureType>::append_training_features(
                base_index, training_features);

            for (auto i = start; i < training_features->size(); ++i) {
                auto& feature = (*training_features)[i];
                assert(feature.get_index() < Factorizer<FeatureType>::get_dimensions());
                if (feature.get_index() >= kBaseDimensions) {
                    feature.shift_index(base_dimensions - kBaseDimensions);
                }
            }
        }
    };

}  // namespace Eval::NNUE::Features

#endif
