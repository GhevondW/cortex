#include "cortex/coroutine.hpp"
#include "cortex/errors/resume_on_completed_coroutine_error.hpp"
#include "cortex/memory_resource.hpp"
#include <gtest/gtest.h>
#include <vector>

TEST(CortexCoroutineTest, BasicExecution) {
    bool executed = false;
    auto cr = cortex::Coroutine::Make([&executed](cortex::CoroutineSuspendContext&) {
        executed = true;
    });

    EXPECT_FALSE(cr.IsDone());
    cr.Resume();
    EXPECT_TRUE(executed);
    EXPECT_TRUE(cr.IsDone());
}

TEST(CortexCoroutineTest, Suspension) {
    std::vector<int> values;
    auto cr = cortex::Coroutine::Make([&values](cortex::CoroutineSuspendContext& ctx) {
        values.push_back(1);
        ctx.Suspend();
        values.push_back(3);
    });

    EXPECT_FALSE(cr.IsDone());
    cr.Resume();
    values.push_back(2);
    EXPECT_FALSE(cr.IsDone());
    cr.Resume();
    EXPECT_TRUE(cr.IsDone());

    std::vector<int> expected = {1, 2, 3};
    EXPECT_EQ(values, expected);
}

TEST(CortexCoroutineTest, MultipleSuspensions) {
    int counter = 0;
    auto cr = cortex::Coroutine::Make([&counter](cortex::CoroutineSuspendContext& ctx) {
        for (int i = 0; i < 3; ++i) {
            counter++;
            ctx.Suspend();
        }
    });

    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(cr.IsDone());
        cr.Resume();
        EXPECT_EQ(counter, i + 1);
    }
    EXPECT_FALSE(cr.IsDone());
    cr.Resume();
    EXPECT_TRUE(cr.IsDone());
    EXPECT_EQ(counter, 3);
}

TEST(CortexCoroutineTest, ExceptionHandling) {
    auto cr = cortex::Coroutine::Make([](cortex::CoroutineSuspendContext&) {
        throw std::runtime_error("Test exception");
    });

    EXPECT_FALSE(cr.IsDone());
    EXPECT_THROW(cr.Resume(), std::runtime_error);
    EXPECT_TRUE(cr.IsDone());
    EXPECT_TRUE(cr.HasException());
}

TEST(CortexCoroutineTest, ResumeOnDone) {
    auto cr = cortex::Coroutine::Make([](cortex::CoroutineSuspendContext&) {
    });
    cr.Resume();
    EXPECT_TRUE(cr.IsDone());
    EXPECT_THROW(cr.Resume(), cortex::ResumeOnDoneCoroutineError);
}

struct ResourceTracker {
    bool& destroyed;
    ResourceTracker(bool& d)
        : destroyed(d) {}
    ~ResourceTracker() {
        destroyed = true;
    }
};

TEST(CortexCoroutineTest, ForcedUnwindOnDestruction) {
    bool resource_destroyed = false;
    {
        auto cr = cortex::Coroutine::Make([&resource_destroyed](cortex::CoroutineSuspendContext& ctx) {
            ResourceTracker tracker(resource_destroyed);
            ctx.Suspend();
        });

        cr.Resume();
        EXPECT_FALSE(resource_destroyed);
        EXPECT_FALSE(cr.IsDone());
        // cr goes out of scope here
    }
    EXPECT_TRUE(resource_destroyed);
}

class CoroutineTrackingResource : public cortex::MemoryResource {
public:
    size_t allocations = 0;
    size_t deallocations = 0;

protected:
    void* DoAllocate(size_t bytes, size_t alignment) override {
        allocations++;
        return cortex::GetDefaultMemoryResource()->Allocate(bytes, alignment);
    }

    void DoDeallocate(void* p, size_t bytes, size_t alignment) override {
        deallocations++;
        cortex::GetDefaultMemoryResource()->Deallocate(p, bytes, alignment);
    }
};

TEST(CortexCoroutineTest, MemoryResourceSupport) {
    auto tracker = std::make_shared<CoroutineTrackingResource>();
    {
        auto cr = cortex::Coroutine::Make(
            [](cortex::CoroutineSuspendContext& ctx) {
                ctx.Suspend();
            },
            4096,
            tracker);

        EXPECT_GT(tracker->allocations, 0);
        cr.Resume();
        EXPECT_FALSE(cr.IsDone());
        cr.Resume();
        EXPECT_TRUE(cr.IsDone());
    }
    EXPECT_GT(tracker->deallocations, 0);
    EXPECT_EQ(tracker->allocations, tracker->deallocations);
}
