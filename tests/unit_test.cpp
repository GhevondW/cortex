#include "cortex/core.hpp"
#include <gtest/gtest.h>

TEST(CortexBasicTest, Add) {
    auto result = cortex::add(2, 3);
    EXPECT_EQ(result, 5);
}
