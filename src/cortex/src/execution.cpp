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

void execution::resume() {
    assert(_context);

    machine::transfer_t transfer = machine::jump_to_context(_context, nullptr);

    _context = transfer.fctx;

    if (transfer.data != nullptr) { // Exception is happened.
        std::rethrow_exception(*static_cast<std::exception_ptr*>(transfer.data));
    }
}

execution::execution(machine::context_t context)
    : _context(context) {}

} // namespace cortex
