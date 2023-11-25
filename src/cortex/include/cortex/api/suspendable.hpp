#ifndef SRC_CORTEX_INCLUDE_CORTEX_API_SUSPENDABLE_HPP
#define SRC_CORTEX_INCLUDE_CORTEX_API_SUSPENDABLE_HPP

namespace cortex::api {

/**
 * @brief The `suspendable` interface provides a mechanism to stop the current execution flow.
 * It is designed to be used in scenarios where pausing or disabling the current execution is necessary.
 */
struct suspendable {
    /**
     * @brief Default constructor for the `suspendable` class.
     */
    suspendable() = default;

    /**
     * @brief Virtual destructor for proper cleanup in derived classes.
     */
    virtual ~suspendable() = default;

    /**
     * @brief Pure virtual function that must be implemented by derived classes.
     * It is responsible for stopping the current execution.
     */
    virtual void suspend() = 0;
};

}; // namespace cortex::api

#endif
