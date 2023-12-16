#include <cortex/basic_flow.hpp>

namespace cortex {

std::unique_ptr<api::flow> basic_flow::make(std::function<void(api::suspendable& suspender)> in_flow) {
    return std::unique_ptr<basic_flow>(new basic_flow(std::move(in_flow)));
}

basic_flow::basic_flow(std::function<void(api::suspendable& suspender)> in_flow)
    : _flow(std::move(in_flow)) {}

void basic_flow::run(api::suspendable& suspender) {
    _flow(suspender);
}

} // namespace cortex
