#ifndef SRC_CORTEX_INCLUDE_CORTEX_API_DISABLER_HPP
#define SRC_CORTEX_INCLUDE_CORTEX_API_DISABLER_HPP

namespace cortex::api {

/**
 * @brief The `disabler` interface provides a mechanism to stop the current execution flow.
 * It is designed to be used in scenarios where pausing or disabling the current execution is necessary.
 */
struct disabler {
    /**
     * @brief Default constructor for the `disabler` class.
     */
    disabler() = default;

    /**
     * @brief Virtual destructor for proper cleanup in derived classes.
     */
    virtual ~disabler() = default;

    /**
     * @brief Pure virtual function that must be implemented by derived classes.
     * It is responsible for stopping the current execution.
     */
    virtual void disable() = 0;
};

}; // namespace cortex::api

#endif
