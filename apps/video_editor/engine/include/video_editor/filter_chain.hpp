#pragma once

#include <video_editor/filter.hpp>
#include <video_editor/frame_buffer.hpp>

#include <memory>
#include <vector>

namespace cortex::video_editor {

// Composes a sequence of filters and applies them in order. Empty chain is a
// pure copy. For chains with >= 2 filters the chain pings between an internal
// scratch buffer and the caller-supplied output buffer to avoid extra
// allocations per Apply() call.
class FilterChain final {
public:
    FilterChain() = default;

    FilterChain(const FilterChain&) = delete;
    FilterChain& operator=(const FilterChain&) = delete;
    FilterChain(FilterChain&&) noexcept = default;
    FilterChain& operator=(FilterChain&&) noexcept = default;

    void Add(std::unique_ptr<IFilter> filter);
    void Clear() noexcept;

    [[nodiscard]] std::size_t Size() const noexcept {
        return filters_.size();
    }

    [[nodiscard]] bool Empty() const noexcept {
        return filters_.empty();
    }

    // Reads `in`, writes the final result to `out`. May use an internal
    // scratch buffer sized to match in/out dimensions.
    void Apply(const FrameBuffer& in, FrameBuffer& out) const;

private:
    std::vector<std::unique_ptr<IFilter>> filters_;
    mutable FrameBuffer scratch_;
};

} // namespace cortex::video_editor
