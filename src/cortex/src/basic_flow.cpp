#include <cortex/basic_flow.hpp>

namespace cortex {

std::unique_ptr<api::flow> basic_flow::make(std::function<void(api::suspendable& suspender)> flow) {
    return std::unique_ptr<basic_flow>(new basic_flow(std::move(flow)));
}

basic_flow::basic_flow(std::function<void(api::suspendable& suspender)> flow)
    : _flow(std::move(flow)) {}

void basic_flow::run(api::suspendable& suspender) {
    _flow(suspender);
}

} // namespace cortex
