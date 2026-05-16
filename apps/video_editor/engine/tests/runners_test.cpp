#include <video_editor/blocking_runner.hpp>
#include <video_editor/cooperative_runner.hpp>
#include <video_editor/filter_chain.hpp>
#include <video_editor/filters/brightness.hpp>
#include <video_editor/frame_source.hpp>
#include <video_editor/pipeline.hpp>
#include <video_editor/progress_listener.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using namespace cortex::video_editor;
using namespace cortex::video_editor::filters;

namespace {

class FakeFrameSource final : public IFrameSource {
public:
    FakeFrameSource(int w, int h, int count) {
        for (int i = 0; i < count; ++i) {
            FrameBuffer fb(w, h);
            fb.Fill(static_cast<std::uint8_t>(10 + i), 0, 0, 255);
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

class RecordingListener final : public IProgressListener {
public:
    void OnProgress(float pct) override {
        progresses.push_back(pct);
    }
    void OnComplete() override {
        completed = true;
    }
    void OnCancelled() override {
        cancelled = true;
    }

    std::vector<float> progresses;
    bool completed {false};
    bool cancelled {false};
};

} // namespace

TEST(RunnersTest, BlockingProducesAllOutputs) {
    FakeFrameSource src(4, 4, 6);
    FilterChain chain;
    chain.Add(std::make_unique<BrightnessFilter>(0.1f));
    Pipeline pipeline(src, chain);

    RecordingListener listener;
    BlockingRunner runner;
    runner.Start(pipeline, listener);

    EXPECT_TRUE(listener.completed);
    EXPECT_FALSE(listener.cancelled);
    EXPECT_EQ(listener.progresses.size(), 6u);
    EXPECT_FLOAT_EQ(listener.progresses.back(), 1.0f);
}

TEST(RunnersTest, CooperativeProducesAllOutputs) {
    FakeFrameSource src(4, 4, 8);
    FilterChain chain;
    chain.Add(std::make_unique<BrightnessFilter>(0.1f));
    Pipeline pipeline(src, chain);

    RecordingListener listener;
    CooperativeRunner runner(3);
    runner.Start(pipeline, listener);

    int safety = 0;
    while (runner.Step() && safety++ < 10000) {
    }
    // Step() returning false either means the scheduler is done or there are
    // no more ready fibers; the runner dispatches OnComplete on that final step.

    EXPECT_TRUE(listener.completed);
    EXPECT_FALSE(listener.cancelled);
    EXPECT_FLOAT_EQ(runner.Progress(), 1.0f);
}

TEST(RunnersTest, BothRunnersProduceIdenticalOutputs) {
    FakeFrameSource src(8, 8, 5);

    FilterChain chain_a;
    chain_a.Add(std::make_unique<BrightnessFilter>(0.05f));
    Pipeline pipeline_a(src, chain_a);
    RecordingListener listener_a;
    BlockingRunner blocking;
    blocking.Start(pipeline_a, listener_a);

    FilterChain chain_b;
    chain_b.Add(std::make_unique<BrightnessFilter>(0.05f));
    Pipeline pipeline_b(src, chain_b);
    RecordingListener listener_b;
    CooperativeRunner cooperative(2);
    cooperative.Start(pipeline_b, listener_b);
    int safety = 0;
    while (cooperative.Step() && safety++ < 10000) {
    }

    ASSERT_EQ(pipeline_a.FrameCount(), pipeline_b.FrameCount());
    for (int i = 0; i < pipeline_a.FrameCount(); ++i) {
        const auto& a = pipeline_a.OutputAt(i);
        const auto& b = pipeline_b.OutputAt(i);
        ASSERT_EQ(a.SizeBytes(), b.SizeBytes());
        EXPECT_EQ(std::memcmp(a.Data(), b.Data(), a.SizeBytes()), 0)
            << "Frame " << i << " differs between blocking and cooperative runners";
    }
}

TEST(RunnersTest, CooperativeCancelDispatchesOnCancelled) {
    FakeFrameSource src(4, 4, 40);
    FilterChain chain;
    chain.Add(std::make_unique<BrightnessFilter>(0.1f));
    Pipeline pipeline(src, chain);

    RecordingListener listener;
    CooperativeRunner runner(2);
    runner.Start(pipeline, listener);

    // Run a couple of steps then cancel.
    runner.Step();
    runner.Step();
    runner.Cancel();

    int safety = 0;
    while (runner.Step() && safety++ < 10000) {
    }

    EXPECT_TRUE(listener.cancelled);
    EXPECT_FALSE(listener.completed);
}
