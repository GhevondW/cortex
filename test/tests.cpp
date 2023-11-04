#include <catch2/catch_test_macros.hpp>

#include <cortex/cortex.hpp>

TEST_CASE("Factorials are computed", "[factorial]")
{
  REQUIRE(1 * 1 == 1);
  cortex::foo();
}
