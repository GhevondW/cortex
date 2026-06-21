#include <video_editor/filter_chain.hpp>
#include <video_editor/filters/brightness.hpp>
#include <video_editor/frame_source.hpp>
#include <video_editor/pipeline.hpp>
#include <video_editor/progress_listener.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace cortex::video_editor;
using namespace cortex::video_editor::filters;

namespace {

// Test double: a frame source built from caller-supplied buffers.
class FakeFrameSource final : public IFrameSource {
public:
    FakeFrameSource(int w, int h, int count, std::uint8_t base) {
        for (int i = 0; i < count; ++i) {
            FrameBuffer fb(w, h);
            fb.Fill(static_cast<std::uint8_t>(base + i), 0, 0, 255);
            frames_.push_back(std::move(fb));
        }
        width_ = w;
        height_ = h;
    }
    int Width() const noexcept override {
        return width_;
    }
    int Height() const noexcept override {
        return height_;
    }
    int FrameCount() const noexcept override {
        return static_cast<int>(frames_.size());
    }
    const FrameBuffer& At(int idx) const override {
        return frames_[static_cast<std::size_t>(idx)];
    }

private:
    std::vector<FrameBuffer> frames_;
    int width_ {0};
    int height_ {0};
};

} // namespace

TEST(PipelineTest, OutputsMatchSourceWhenChainEmpty) {
    FakeFrameSource src(4, 4, 3, 50);
    FilterChain chain;
    Pipeline pipeline(src, chain);

    for (int i = 0; i < pipeline.FrameCount(); ++i) {
        pipeline.ProcessFrame(i);
    }

    EXPECT_EQ(pipeline.OutputAt(0).Data()[0], 50);
    EXPECT_EQ(pipeline.OutputAt(1).Data()[0], 51);
    EXPECT_EQ(pipeline.OutputAt(2).Data()[0], 52);
}

TEST(PipelineTest, FilterAppliedToEveryFrame) {
    FakeFrameSource src(2, 2, 4, 100);
    FilterChain chain;
    chain.Add(std::make_unique<BrightnessFilter>(0.1f)); // +25
    Pipeline pipeline(src, chain);

    pipeline.ProcessFrame(0);
    pipeline.ProcessFrame(3);

    EXPECT_EQ(pipeline.OutputAt(0).Data()[0], 125);
    EXPECT_EQ(pipeline.OutputAt(3).Data()[0], 128);
}

TEST(PipelineTest, OutOfRangeThrows) {
    FakeFrameSource src(2, 2, 2, 0);
    FilterChain chain;
    Pipeline pipeline(src, chain);
    EXPECT_THROW(pipeline.ProcessFrame(-1), std::out_of_range);
    EXPECT_THROW(pipeline.ProcessFrame(2), std::out_of_range);
    EXPECT_THROW({ (void)pipeline.OutputAt(-1); }, std::out_of_range);
}
