#include <cortex/generator.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <ranges>
#include <stdexcept>
#include <vector>

TEST(GeneratorTest, BasicSequence) {
    auto gen = cortex::Generator<int>::Make([](auto& yield) {
        yield(1);
        yield(2);
        yield(3);
    });

    std::vector<int> values;
    while (gen.Next()) {
        values.push_back(gen.DetachValue());
    }

    std::vector<int> expected = {1, 2, 3};
    EXPECT_EQ(values, expected);
    EXPECT_TRUE(gen.IsDone());
}

TEST(GeneratorTest, DetachValueThrowsWhenEmpty) {
    auto gen = cortex::Generator<int>::Make([](auto& yield) {
        yield(42);
    });

    EXPECT_THROW(gen.DetachValue(), std::logic_error);
    EXPECT_TRUE(gen.Next());
    EXPECT_EQ(gen.DetachValue(), 42);
    EXPECT_THROW(gen.DetachValue(), std::logic_error);
}

TEST(GeneratorTest, ExceptionPropagation) {
    auto gen = cortex::Generator<int>::Make([](auto& yield) {
        yield(7);
        throw std::runtime_error("boom");
    });

    EXPECT_TRUE(gen.Next());
    EXPECT_EQ(gen.DetachValue(), 7);
    EXPECT_THROW(gen.Next(), std::runtime_error);
    EXPECT_TRUE(gen.IsDone());
}

TEST(GeneratorTest, CreateWithBuilder) {
    auto gen = cortex::Generator<int>::Builder().SetStackSizeInBytes(65536).Build([](auto& yield) {
        yield(5);
    });

    EXPECT_TRUE(gen.Next());
    EXPECT_EQ(gen.DetachValue(), 5);
    EXPECT_FALSE(gen.Next());
    EXPECT_TRUE(gen.IsDone());
}

// --- Range support ---------------------------------------------------------

static_assert(std::ranges::input_range<cortex::Generator<int>>);

TEST(GeneratorRangeTest, RangeForVisitsEveryValue) {
    auto gen = cortex::Generator<int>::Make([](auto& yield) {
        for (int i = 0; i < 4; ++i) {
            yield(i);
        }
    });
    std::vector<int> seen;
    for (int value : gen) {
        seen.push_back(value);
    }
    EXPECT_EQ(seen, (std::vector<int> {0, 1, 2, 3}));
    EXPECT_TRUE(gen.IsDone());
}

namespace {

struct TreeNode {
    int value;
    const TreeNode* left;
    const TreeNode* right;
};

// Stackful generators can yield from nested calls, e.g. a recursive walk.
void InOrder(const TreeNode* node, cortex::Generator<int>::YieldContext& yield) {
    if (node == nullptr) {
        return;
    }
    InOrder(node->left, yield);
    yield(node->value);
    InOrder(node->right, yield);
}

} // namespace

TEST(GeneratorRangeTest, YieldsFromNestedRecursiveCalls) {
    const TreeNode one {1, nullptr, nullptr};
    const TreeNode three {3, nullptr, nullptr};
    const TreeNode two {2, &one, &three};
    auto gen = cortex::Generator<int>::Make([&](auto& yield) { InOrder(&two, yield); });
    std::vector<int> seen;
    for (int value : gen) {
        seen.push_back(value);
    }
    EXPECT_EQ(seen, (std::vector<int> {1, 2, 3}));
}

TEST(GeneratorRangeTest, EmptyGeneratorHasNoElements) {
    auto gen = cortex::Generator<int>::Make([](auto&) {});
    EXPECT_EQ(std::ranges::distance(gen.begin(), gen.end()), 0);
}

TEST(GeneratorRangeTest, IteratorGivesAccessToMoveOnlyValues) {
    auto gen = cortex::Generator<std::unique_ptr<int>>::Make([](auto& yield) {
        yield(std::make_unique<int>(5));
        yield(std::make_unique<int>(6));
    });
    std::vector<int> seen;
    for (auto& ptr : gen) {
        seen.push_back(*ptr);
        auto taken = std::move(ptr); // the value may be moved out
    }
    EXPECT_EQ(seen, (std::vector<int> {5, 6}));
}

TEST(GeneratorRangeTest, BodyExceptionPropagatesFromIteration) {
    auto gen = cortex::Generator<int>::Make([](auto& yield) {
        yield(1);
        throw std::runtime_error("broken source");
    });
    auto it = gen.begin();
    EXPECT_EQ(*it, 1);
    EXPECT_THROW(++it, std::runtime_error);
}
