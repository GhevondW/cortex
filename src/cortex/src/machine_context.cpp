#include <cortex/machine_context.hpp>

namespace cortex {

machine::context_t machine::make_context(void* sp, std::size_t size, void (*fn)(transfer_t)) {
    return boost::context::detail::make_fcontext(sp, size, fn);
}

machine::transfer_t machine::jump_to_context(context_t const to, void* vp) {
    return boost::context::detail::jump_fcontext(to, vp);
}

machine::transfer_t machine::ontop_context(context_t const to, void* vp, transfer_t (*fn)(transfer_t)) {
    return boost::context::detail::ontop_fcontext(to, vp, fn);
}

} // namespace cortex
