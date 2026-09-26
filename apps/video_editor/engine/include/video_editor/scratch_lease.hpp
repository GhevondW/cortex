#pragma once

#include <video_editor/frame_buffer.hpp>

namespace cortex::video_editor {

// Borrows a filter's cached scratch frame for one Apply() call. Filters yield
// mid-frame (tiny_fiber::CheckPoint() once per row), so several fibers can be
// inside the same filter at once: the first borrows the cache, the others get
// a buffer of their own instead of overwriting it.
class ScratchLease final {
public:
    ScratchLease(FrameBuffer& cache, bool& cache_in_use, int width, int height) {
        if (!cache_in_use) {
            cache_in_use = true;
            cache_in_use_ = &cache_in_use;
            if (cache.Width() != width || cache.Height() != height) {
                cache = FrameBuffer(width, height);
            }
            buffer_ = &cache;
        } else {
            own_ = FrameBuffer(width, height);
            buffer_ = &own_;
        }
    }

    ~ScratchLease() {
        if (cache_in_use_ != nullptr) {
            *cache_in_use_ = false;
        }
    }

    ScratchLease(const ScratchLease&) = delete;
    ScratchLease& operator=(const ScratchLease&) = delete;
    ScratchLease(ScratchLease&&) = delete;
    ScratchLease& operator=(ScratchLease&&) = delete;

    [[nodiscard]] FrameBuffer& Buffer() noexcept {
        return *buffer_;
    }

private:
    bool* cache_in_use_ {nullptr};
    FrameBuffer own_; // empty unless the cache was taken
    FrameBuffer* buffer_ {nullptr};
};

} // namespace cortex::video_editor
