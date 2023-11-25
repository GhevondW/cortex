#ifndef SRC_CORTEX_INCLUDE_CORTEX_BASIC_FLOW_HPP
#define SRC_CORTEX_INCLUDE_CORTEX_BASIC_FLOW_HPP

#include <cortex/api/flow.hpp>

#include <functional>
#include <memory>

namespace cortex {

/**
 * @brief The `basic_flow` struct provides a basic implementation of the Cortex flow interface.
 */
struct basic_flow : public api::flow {
    /**
     * @brief Creates a new instance of `basic_flow` with the provided execution flow function.
     *
     * @param flow The function representing the execution flow.
     * @return A unique pointer to the created `basic_flow`.
     */
    static std::unique_ptr<api::flow> make(std::function<void(api::suspendable& suspender)> flow);

    /**
     * @brief Destructor for the `basic_flow` class.
     */
    ~basic_flow() noexcept override = default;

private:
    /**
     * @brief Private constructor for creating a `basic_flow` with the provided execution flow function.
     *
     * @param flow The function representing the execution flow.
     */
    explicit basic_flow(std::function<void(api::suspendable& suspender)> flow);

    /**
     * @brief Runs the execution flow using the provided suspendable object.
     *
     * @param suspender A reference to a `suspendable` object that can be used to control the flow.
     */
    void run(api::suspendable& suspender) override;

    /// The function representing the execution flow.
    std::function<void(api::suspendable& suspender)> _flow;
};

} // namespace cortex
#endif
