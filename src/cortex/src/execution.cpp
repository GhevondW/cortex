#include <cortex/execution.hpp>

namespace cortex {

namespace {
namespace aux {

machine::transfer_t unwind(machine::transfer_t transfer) {
    throw forced_unwind(transfer.fctx);
}

} // namespace aux
} // namespace

execution::~execution() noexcept {
    if (_context != nullptr) {
        [[maybe_unused]] auto res = machine::ontop_context(std::exchange(_context, nullptr), nullptr, aux::unwind);
    }
}

void execution::enable() {
    _context = machine::jump_to_context(_context, nullptr).fctx;
}

execution::execution(machine::context_t context)
    : _context(context) {}

} // namespace cortex
