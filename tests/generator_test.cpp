#include <cortex/generator.hpp>

#include <gtest/gtest.h>

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
