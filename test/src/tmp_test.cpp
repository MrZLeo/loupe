#include "project/tmp.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("tmp::add sums two integers", "[tmp]")
{
  REQUIRE(tmp::add(1, 2) == 3);
  REQUIRE(tmp::add(-2, 2) == 0);
}
