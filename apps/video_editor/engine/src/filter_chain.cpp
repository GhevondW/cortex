#include <video_editor/filter_chain.hpp>

#include <cassert>
#include <utility>

namespace cortex::video_editor {

void FilterChain::Add(std::unique_ptr<IFilter> filter) {
    if (filter) {
        filters_.push_back(std::move(filter));
    }
}

void FilterChain::Clear() noexcept {
    filters_.clear();
}

void FilterChain::Apply(const FrameBuffer& in, FrameBuffer& out) const {
    assert(in.Width() == out.Width() && in.Height() == out.Height());

    if (filters_.empty()) {
        out.CopyFrom(in);
        return;
    }

    if (filters_.size() == 1) {
        filters_.front()->Apply(in, out);
        return;
    }

    if (scratch_.Width() != in.Width() || scratch_.Height() != in.Height()) {
        scratch_ = FrameBuffer(in.Width(), in.Height());
    }

    // Ping-pong: pick the final write target so the very last filter writes
    // directly into `out`. For N filters there are N reads/writes:
    //   read    write
    //   in   -> ping  (filter 0)
    //   ping -> pong  (filter 1)
    //   ...
    //   ?    -> out   (filter N-1)
    const FrameBuffer* read = &in;
    FrameBuffer* ping = &scratch_;
    FrameBuffer* pong = &out;

    const std::size_t n = filters_.size();
    for (std::size_t i = 0; i < n; ++i) {
        const bool last = (i + 1 == n);
        FrameBuffer* write = last ? &out : ((i % 2 == 0) ? ping : pong);
        filters_[i]->Apply(*read, *write);
        read = write;
    }
}

} // namespace cortex::video_editor
