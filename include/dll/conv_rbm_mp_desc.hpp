//=======================================================================
// Copyright (c) 2014-2016 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#pragma once

#include "base_conf.hpp"
#include "contrastive_divergence.hpp"
#include "watcher.hpp"
#include "util/tmp.hpp"

namespace dll {

/*!
 * \brief Describe a Convolutional Restricted Boltzmann Machine with
 * Probabilistic Max Pooling layer.
 *
 * This struct should be used to define a RBM either as standalone or for a DBN.
 * Once configured, the ::layer_t member returns the type of the configured RBM.
 */
template <std::size_t NC_T, std::size_t NV_1, std::size_t NV_2, std::size_t K_T, std::size_t NH_1, std::size_t NH_2, std::size_t C_T, typename... Parameters>
struct conv_rbm_mp_desc {
    static constexpr const std::size_t NV1 = NV_1; ///< The first dimension of the input
    static constexpr const std::size_t NV2 = NV_2; ///< The second dimension of the input
    static constexpr const std::size_t NH1 = NH_1; ///< The first dimension of the output
    static constexpr const std::size_t NH2 = NH_2; ///< The second dimension of the output
    static constexpr const std::size_t NC  = NC_T; ///< The number of input channels
    static constexpr const std::size_t K   = K_T;  ///< The number of filters
    static constexpr const std::size_t C   = C_T;  ///< The output pooling ratio

    /*!
     * \brief A list of all the parameters of the descriptor
     */
    using parameters = cpp::type_list<Parameters...>;

    static constexpr const std::size_t BatchSize    = detail::get_value<batch_size<1>, Parameters...>::value;
    static constexpr const unit_type visible_unit   = detail::get_value<visible<unit_type::BINARY>, Parameters...>::value;
    static constexpr const unit_type hidden_unit    = detail::get_value<hidden<unit_type::BINARY>, Parameters...>::value;
    static constexpr const unit_type pooling_unit   = detail::get_value<pooling<unit_type::BINARY>, Parameters...>::value;
    static constexpr const sparsity_method Sparsity = detail::get_value<sparsity<sparsity_method::NONE>, Parameters...>::value;
    static constexpr const bias_mode Bias           = detail::get_value<bias<bias_mode::SIMPLE>, Parameters...>::value;

    /*! The type used to store the weights */
    using weight = typename detail::get_type<weight_type<double>, Parameters...>::value;

    /*! The type of the trainer to use to train the RBM */
    template <typename RBM, bool Denoising>
    using trainer_t = typename detail::get_template_type_tb<trainer_rbm<cd1_trainer_t>, Parameters...>::template value<RBM, Denoising>;

    /*! The type of the watched to use during training */
    template <typename RBM>
    using watcher_t = typename detail::get_template_type<watcher<default_rbm_watcher>, Parameters...>::template value<RBM>;

    /*! The layer type */
    using layer_t = conv_rbm_mp<conv_rbm_mp_desc<NC_T, NV_1, NV_2, K_T, NH_1, NH_2, C_T, Parameters...>>;

    //Validate all parameters

    static_assert(NV1 > 0, "A matrix of at least 1x1 is necessary for the visible units");
    static_assert(NV2 > 0, "A matrix of at least 1x1 is necessary for the visible units");
    static_assert(NH1 > 0, "A matrix of at least 1x1 is necessary for the hidden units");
    static_assert(NH2 > 0, "A matrix of at least 1x1 is necessary for the hidden units");
    static_assert(NC > 0, "At least one channel is necessary");
    static_assert(K > 0, "At least one base is necessary");
    static_assert(C > 0, "At least one pooling group is necessary");

    static_assert(NV1 >= NH1, "The convolutional filter must be of at least size 1");
    static_assert(NV2 >= NH2, "The convolutional filter must be of at least size 1");

    //Make sure only valid types are passed to the configuration list
    static_assert(
        detail::is_valid<cpp::type_list<
                             momentum_id, batch_size_id, visible_id, hidden_id, pooling_id, dbn_only_id,
                             weight_decay_id, sparsity_id, trainer_rbm_id, watcher_id, bias_id,
                             weight_type_id, shuffle_id, parallel_mode_id, serial_id, verbose_id, nop_id>,
                         Parameters...>::value,
        "Invalid parameters type");

    static_assert(BatchSize > 0, "Batch size must be at least 1");

    static_assert(Sparsity == sparsity_method::NONE || hidden_unit == unit_type::BINARY,
                  "Sparsity only works with binary hidden units");
};

/*!
 * \brief Describe a Convolutional Restricted Boltzmann Machine with
 * Probabilistic Max Pooling layer with square input.
 *
 * This struct should be used to define a RBM either as standalone or for a DBN.
 * Once configured, the ::rbm_t member returns the type of the configured RBM.
 */
template <std::size_t NC_T, std::size_t NV_T, std::size_t K_T, std::size_t NH_T, std::size_t C_T, typename... Parameters>
using conv_rbm_mp_desc_square = conv_rbm_mp_desc<NC_T, NV_T, NV_T, K_T, NH_T, NH_T, C_T, Parameters...>;

} //end of dll namespace
