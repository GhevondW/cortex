#ifndef SRC_CORTEX_INCLUDE_CORTEX_ERROR_HPP
#define SRC_CORTEX_INCLUDE_CORTEX_ERROR_HPP

#include <stdexcept>
#include <string>

namespace cortex {

/**
 * @brief The `error` class is the base exception type for the Cortex library.
 * It inherits from std::exception and provides a custom error message.
 */
class error : public std::exception {
public:
    /**
     * @brief Constructor to create an `error` with a custom error message.
     *
     * @tparam StringLike Type that can be converted to std::string (e.g., const char*, std::string, etc.).
     * @param str The error message.
     */
    template <typename StringLike>
    explicit error(StringLike&& str)
        : _what(std::forward<StringLike>(str)) {}

    /**
     * @brief Returns a pointer to the C-style string describing the error.
     *
     * @return A pointer to the C-style string.
     */
    [[nodiscard]] const char* what() const noexcept override {
        return _what.c_str();
    }

private:
    /// The custom error message associated with the exception.
    std::string _what;
};

}; // namespace cortex

#endif
