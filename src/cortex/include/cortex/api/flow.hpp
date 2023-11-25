#ifndef SRC_CORTEX_INCLUDE_CORTEX_API_FLOW_HPP
#define SRC_CORTEX_INCLUDE_CORTEX_API_FLOW_HPP

#include <cortex/api/suspendable.hpp>

namespace cortex::api {

/**
 * @brief The `flow` interface represents the current execution's flow or code.
 * It provides a mechanism for executing code and includes a method to run the code with the option to disable
 * execution.
 */
struct flow {
    /**
     * @brief Default constructor for the `flow` class.
     */
    flow() = default;

    /**
     * @brief Deleted copy constructor to prevent unintended copying of `flow` objects.
     */
    flow(const flow&) = delete;

    /**
     * @brief Deleted move constructor to prevent unintended moving of `flow` objects.
     */
    flow(flow&&) = delete;

    /**
     * @brief Deleted copy assignment operator to prevent unintended copying of `flow` objects.
     */
    flow& operator=(const flow&) = delete;

    /**
     * @brief Deleted move assignment operator to prevent unintended moving of `flow` objects.
     */
    flow& operator=(flow&&) = delete;

    /**
     * @brief Virtual destructor for proper cleanup in derived classes.
     */
    virtual ~flow() noexcept = default;

    /**
     * @brief Pure virtual function that must be implemented by derived classes.
     * It is responsible for running the execution flow with an option to disable execution.
     *
     * @param suspender A reference to a `suspendable` object that can be used to disable the execution flow if needed.
     * @internal Consider making it noexcept.
     */
    virtual void run(api::suspendable& suspender) = 0;
};

} // namespace cortex::api

#endif
