//=======================================================================
// Copyright (c) 2014-2017 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

/*!
 * \file
 * \brief Implementation of a Restricted Boltzmann Machine
 */

#pragma once

#include "cpp_utils/assert.hpp"     //Assertions
#include "cpp_utils/stop_watch.hpp" //Performance counter

#include "etl/etl.hpp"

#include "dll/rbm/standard_rbm.hpp"
#include "dll/base_traits.hpp"
#include "dll/layer_traits.hpp"

#include "dll/util/converter.hpp" //converter

namespace dll {

/*!
 * \brief Standard version of Restricted Boltzmann Machine
 *
 * This follows the definition of a RBM by Geoffrey Hinton.
 */
template <typename Desc>
struct rbm final : public standard_rbm<rbm<Desc>, Desc> {
    using desc      = Desc;                          ///< The layer descriptor
    using weight    = typename desc::weight;         ///< The weight type
    using this_type = rbm<desc>;                     ///< The type of this layer
    using base_type = standard_rbm<this_type, desc>; ///< The base type

    using input_t      = typename rbm_base_traits<this_type>::input_t;
    using output_t     = typename rbm_base_traits<this_type>::output_t;
    using input_one_t  = typename rbm_base_traits<this_type>::input_one_t;
    using output_one_t = typename rbm_base_traits<this_type>::output_one_t;

    static constexpr const std::size_t num_visible = desc::num_visible; ///< The number of visible units
    static constexpr const std::size_t num_hidden  = desc::num_hidden;  ///< The number of hidden units
    static constexpr const std::size_t batch_size  = desc::BatchSize;  ///< The mini-batch size

    static constexpr const unit_type visible_unit = desc::visible_unit; ///< The type of visible units
    static constexpr const unit_type hidden_unit  = desc::hidden_unit;  ///< The type of hidden units

    static constexpr bool dbn_only = rbm_layer_traits<this_type>::is_dbn_only();

    using w_type = etl::fast_matrix<weight, num_visible, num_hidden>; ///< The type used to store weights
    using b_type = etl::fast_vector<weight, num_hidden>;              ///< The type used to store hidden biases
    using c_type = etl::fast_vector<weight, num_visible>;             ///< The type used to store visible biases

    //Weights and biases
    w_type w; //!< Weights
    b_type b; //!< Hidden biases
    c_type c; //!< Visible biases

    //Backup weights and biases
    std::unique_ptr<w_type> bak_w; //!< Backup Weights
    std::unique_ptr<b_type> bak_b; //!< Backup Hidden biases
    std::unique_ptr<c_type> bak_c; //!< Backup Visible biases

    //Reconstruction data
    conditional_fast_matrix_t<!dbn_only, weight, num_visible> v1; //!< State of the visible units

    conditional_fast_matrix_t<!dbn_only, weight, num_hidden> h1_a; //!< Activation probabilities of hidden units after first CD-step
    conditional_fast_matrix_t<!dbn_only, weight, num_hidden> h1_s; //!< Sampled value of hidden units after first CD-step

    conditional_fast_matrix_t<!dbn_only, weight, num_visible> v2_a; //!< Activation probabilities of visible units after first CD-step
    conditional_fast_matrix_t<!dbn_only, weight, num_visible> v2_s; //!< Sampled value of visible units after first CD-step

    conditional_fast_matrix_t<!dbn_only, weight, num_hidden> h2_a; //!< Activation probabilities of hidden units after last CD-step
    conditional_fast_matrix_t<!dbn_only, weight, num_hidden> h2_s; //!< Sampled value of hidden units after last CD-step

    /*!
     * \brief Initialize a RBM with basic weights.
     *
     * The weights are initialized from a normal distribution of
     * zero-mean and 0.1 variance.
     */
    rbm()
            : standard_rbm<rbm<Desc>, Desc>(), b(0.0), c(0.0) {
        //Initialize the weights with a zero-mean and unit variance Gaussian distribution
        w = etl::normal_generator<weight>() * 0.1;
    }

    /*!
     * \brief Return the size of the input of this layer
     * \return the number of elements input into this layer
     */
    static constexpr std::size_t input_size() noexcept {
        return num_visible;
    }

    /*!
     * \brief Return the size of the output of this layer
     * \return the number of elements output from this layer
     */
    static constexpr std::size_t output_size() noexcept {
        return num_hidden;
    }

    /*!
     * \brief Return the number of parameters of this layer
     * \return the number of parameters of this layer
     */
    static constexpr std::size_t parameters() noexcept {
        return num_visible * num_hidden;
    }

    static std::string to_short_string() {
        return "RBM: " + std::to_string(num_visible) + "(" + to_string(visible_unit) + ") -> " + std::to_string(num_hidden) + "(" + to_string(hidden_unit) + ")";
    }

    template<typename DRBM>
    static void dyn_init(DRBM& dyn){
        dyn.init_layer(num_visible, num_hidden);
        dyn.batch_size  = batch_size;
    }

    void prepare_input(input_one_t& input) const {
        // Need to initialize the dimensions of the matrix
        input = input_one_t(num_visible);
    }

    /*!
     * \brief Adapt the errors, called before backpropagation of the errors.
     *
     * This must be used by layers that have both an activation fnction and a non-linearity.
     *
     * \param context the training context
     */
    template<typename C>
    void adapt_errors(C& context) const {
        static_assert(
            hidden_unit == unit_type::BINARY || hidden_unit == unit_type::RELU || hidden_unit == unit_type::SOFTMAX,
            "Only (C)RBM with binary, softmax or RELU hidden unit are supported");

        static constexpr const function activation_function =
            hidden_unit == unit_type::BINARY
                ? function::SIGMOID
                : (hidden_unit == unit_type::SOFTMAX ? function::SOFTMAX : function::RELU);

        context.errors = f_derivative<activation_function>(context.output) >> context.errors;
    }

    /*!
     * \brief Backpropagate the errors to the previous layers
     * \param output The ETL expression into which write the output
     * \param context The training context
     */
    template<typename H, typename C>
    void backward_batch(H&& output, C& context) const {
        // The reshape has no overhead, so better than SFINAE for nothing
        constexpr const auto Batch = etl::decay_traits<H>::template dim<0>();
        etl::reshape<Batch, num_visible>(output) = context.errors * etl::transpose(w);
    }

    /*!
     * \brief Compute the gradients for this layer, if any
     * \param context The trainng context
     */
    template<typename C>
    void compute_gradients(C& context) const {
        context.w_grad = batch_outer(context.input, context.errors);
        context.b_grad = etl::sum_l(context.errors);
    }
};

/*!
 * \brief Simple traits to pass information around from the real
 * class to the CRTP class.
 */
template <typename Desc>
struct rbm_base_traits<rbm<Desc>> {
    using desc      = Desc;
    using weight    = typename desc::weight;

    using input_one_t  = etl::dyn_vector<weight>;
    using output_one_t = etl::dyn_vector<weight>;
    using input_t      = std::vector<input_one_t>;
    using output_t     = std::vector<output_one_t>;
};

//Allow odr-use of the constexpr static members

template <typename Desc>
const std::size_t rbm<Desc>::num_visible;

template <typename Desc>
const std::size_t rbm<Desc>::num_hidden;

// Declare the traits for the RBM

template<typename Desc>
struct layer_base_traits<rbm<Desc>> {
    static constexpr bool is_neural     = true;                                         ///< Indicates if the layer is a neural layer
    static constexpr bool is_dense      = true;                                         ///< Indicates if the layer is dense
    static constexpr bool is_conv       = false;                                        ///< Indicates if the layer is convolutional
    static constexpr bool is_deconv     = false;                                        ///< Indicates if the layer is deconvolutional
    static constexpr bool is_standard   = false;                                        ///< Indicates if the layer is standard
    static constexpr bool is_rbm        = true;                                         ///< Indicates if the layer is RBM
    static constexpr bool is_pooling    = false;                                        ///< Indicates if the layer is a pooling layer
    static constexpr bool is_unpooling  = false;                                        ///< Indicates if the layer is an unpooling laye
    static constexpr bool is_transform  = false;                                        ///< Indicates if the layer is a transform layer
    static constexpr bool is_patches    = false;                                        ///< Indicates if the layer is a patches layer
    static constexpr bool is_augment    = false;                                        ///< Indicates if the layer is an augment layer
    static constexpr bool is_dynamic    = false;                                        ///< Indicates if the layer is dynamic
    static constexpr bool pretrain_last = Desc::hidden_unit != dll::unit_type::SOFTMAX; ///< Indicates if the layer is dynamic
    static constexpr bool sgd_supported = true;                                         ///< Indicates if the layer is supported by SGD
};

template<typename Desc>
struct rbm_layer_base_traits<rbm<Desc>> {
    using param = typename Desc::parameters;

    static constexpr bool has_momentum       = param::template contains<momentum>();                            ///< Does the RBM has momentum
    static constexpr bool has_clip_gradients = param::template contains<clip_gradients>();                      ///< Does the RBM has gradient clipping
    static constexpr bool is_parallel_mode   = param::template contains<parallel_mode>();                       ///< Does the RBM is in parallel
    static constexpr bool is_serial          = param::template contains<serial>();                              ///< Does the RBM is in serial mode
    static constexpr bool is_verbose         = param::template contains<verbose>();                             ///< Does the RBM is verbose
    static constexpr bool has_shuffle        = param::template contains<shuffle>();                             ///< Does the RBM has shuffle
    static constexpr bool is_dbn_only        = param::template contains<dbn_only>();                            ///< Does the RBM is only used inside a DBN
    static constexpr bool has_init_weights   = param::template contains<init_weights>();                        ///< Does the RBM use weights initialization
    static constexpr bool has_free_energy    = param::template contains<free_energy>();                         ///< Does the RBM displays the free energy
    static constexpr auto sparsity_method    = get_value_l<sparsity<dll::sparsity_method::NONE>, param>::value; ///< The RBM's sparsity method
    static constexpr auto bias_mode          = get_value_l<bias<dll::bias_mode::NONE>, param>::value;           ///< The RBM's sparsity bias mode
    static constexpr auto decay              = get_value_l<weight_decay<dll::decay_type::NONE>, param>::value;  ///< The RMB's sparsity decay type
    static constexpr bool has_sparsity       = sparsity_method != dll::sparsity_method::NONE;                   ///< Does the RBM has sparsity
};

/*!
 * \brief specialization of sgd_context for rbm
 */
template <typename DBN, typename Desc>
struct sgd_context<DBN, rbm<Desc>> {
    using layer_t = rbm<Desc>;
    using weight  = typename layer_t::weight;

    static constexpr auto num_visible = layer_t::num_visible;
    static constexpr auto num_hidden  = layer_t::num_hidden;

    static constexpr auto batch_size = DBN::batch_size;

    etl::fast_matrix<weight, num_visible, num_hidden> w_grad;
    etl::fast_matrix<weight, num_hidden> b_grad;

    etl::fast_matrix<weight, num_visible, num_hidden> w_inc;
    etl::fast_matrix<weight, num_hidden> b_inc;

    etl::fast_matrix<weight, batch_size, num_visible> input;
    etl::fast_matrix<weight, batch_size, num_hidden> output;
    etl::fast_matrix<weight, batch_size, num_hidden> errors;

    sgd_context()
            : w_inc(0.0), b_inc(0.0), output(0.0), errors(0.0) {}
};

/*!
 * \brief specialization of cg_context for rbm
 */
template <typename Desc>
struct cg_context<rbm<Desc>> {
    using rbm_t  = rbm<Desc>;
    using weight = typename rbm_t::weight;

    static constexpr const bool is_trained = true;

    static constexpr const std::size_t num_visible = rbm_t::num_visible;
    static constexpr const std::size_t num_hidden  = rbm_t::num_hidden;

    etl::fast_matrix<weight, num_visible, num_hidden> gr_w_incs;
    etl::fast_vector<weight, num_hidden> gr_b_incs;

    etl::fast_matrix<weight, num_visible, num_hidden> gr_w_best;
    etl::fast_vector<weight, num_hidden> gr_b_best;

    etl::fast_matrix<weight, num_visible, num_hidden> gr_w_best_incs;
    etl::fast_vector<weight, num_hidden> gr_b_best_incs;

    etl::fast_matrix<weight, num_visible, num_hidden> gr_w_df0;
    etl::fast_vector<weight, num_hidden> gr_b_df0;

    etl::fast_matrix<weight, num_visible, num_hidden> gr_w_df3;
    etl::fast_vector<weight, num_hidden> gr_b_df3;

    etl::fast_matrix<weight, num_visible, num_hidden> gr_w_s;
    etl::fast_vector<weight, num_hidden> gr_b_s;

    etl::fast_matrix<weight, num_visible, num_hidden> gr_w_tmp;
    etl::fast_vector<weight, num_hidden> gr_b_tmp;

    std::vector<etl::dyn_vector<weight>> gr_probs_a;
    std::vector<etl::dyn_vector<weight>> gr_probs_s;
};

} //end of dll namespace
