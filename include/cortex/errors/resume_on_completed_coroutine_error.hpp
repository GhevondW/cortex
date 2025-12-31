#pragma once

#include <stdexcept>

namespace cortex {

struct ResumeOnDoneCoroutineError : std::logic_error {
    using logic_error::logic_error;
};

} // namespace cortex
