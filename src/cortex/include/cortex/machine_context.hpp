/**
 * @file machine.hpp
 * @brief Cortex: Boost.Context Wrapper for Machine Context
 *
 * @note This API is based on Boost.Context and includes headers from <boost/context/detail/fcontext.hpp>.
 *
 * @warning The following code is a wrapper around Boost.Context. The original code is copyrighted by Oliver Kowalke and
 * distributed under the Boost Software License, Version 1.0. For the complete license text, see the accompanying file
 * LICENSE_1_0.txt or visit http://www.boost.org/LICENSE_1_0.txt.
 */

#ifndef SRC_CORTEX_INCLUDE_CORTEX_MACHINE_CONTEXT_HPP
#define SRC_CORTEX_INCLUDE_CORTEX_MACHINE_CONTEXT_HPP

#include <boost/context/detail/fcontext.hpp>

namespace cortex {

/**
 * @brief The `machine` struct provides wrappers for Boost.Context functionality using fcontext.
 * @note This API is based on Boost.Context and includes headers from <boost/context/detail/fcontext.hpp>.
 */
struct machine {
    /// Type alias for the fcontext type.
    using context_t = boost::context::detail::fcontext_t;
    /// Type alias for the transfer type.
    using transfer_t = boost::context::detail::transfer_t;

    /**
     * @brief Creates a new machine context.
     *
     * @param sp The stack pointer for the new context.
     * @param size The size of the stack for the new context.
     * @param fn The function to be executed in the new context.
     * @return The new machine context.
     *
     * @details This function creates a new machine context with the specified stack pointer, stack size, and entry
     * function.
     */
    [[nodiscard]] static context_t make_context(void* sp, std::size_t size, void (*fn)(transfer_t));

    /**
     * @brief Jumps to a specified machine context.
     *
     * @param to The target machine context to jump to.
     * @param vp The transfer value to be passed to the target context.
     * @return The transfer result after the jump.
     *
     * @details This function performs a jump to the specified machine context with the provided transfer value.
     */
    [[nodiscard]] static transfer_t jump_to_context(context_t const to, void* vp);

    /**
     * @brief Switches to a specified machine context on top of the current context.
     *
     * @param to The target machine context to switch to.
     * @param vp The transfer value to be passed to the target context.
     * @param fn The function to be executed in the target context.
     * @return The transfer result after the switch.
     *
     * @details This function switches to the specified machine context on top of the current context and executes the
     * provided function.
     */
    [[nodiscard]] static transfer_t ontop_context(context_t const to, void* vp, transfer_t (*fn)(transfer_t));
};

} // namespace cortex

#endif // SRC_CORTEX_INCLUDE_CORTEX_MACHINE_CONTEXT_HPP
