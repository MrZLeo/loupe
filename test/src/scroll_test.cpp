#include "project/scroll.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("scroll follows selection anchors", "[scroll]") {
  agentlens::ScrollState scroll;

  agentlens::use_selection_scroll(scroll, 12.0);

  REQUIRE_FALSE(scroll.manual);
  REQUIRE(scroll.current == 12.0);
  REQUIRE(scroll.target == 12.0);
}

TEST_CASE("wheel scrolling updates offset directly", "[scroll]") {
  agentlens::ScrollState scroll;

  agentlens::scroll_by(scroll, agentlens::kMouseWheelCells);
  REQUIRE(scroll.manual);
  REQUIRE(scroll.current == 3.0);
  REQUIRE(scroll.target == 3.0);

  agentlens::scroll_by(scroll, -agentlens::kMouseWheelCells);
  REQUIRE(scroll.current == 0.0);
  REQUIRE(scroll.target == 0.0);
}

TEST_CASE("scroll target clamps to content bounds", "[scroll]") {
  agentlens::ScrollState scroll;

  agentlens::scroll_by(scroll, 30.0);
  agentlens::clamp_scroll_target(scroll, 10.0);
  REQUIRE(scroll.current == 10.0);
  REQUIRE(scroll.target == 10.0);

  agentlens::scroll_by(scroll, -30.0);
  REQUIRE(scroll.current == 0.0);
  REQUIRE(scroll.target == 0.0);
}

TEST_CASE("scroll helpers calculate viewport positions", "[scroll]") {
  agentlens::ScrollState scroll;
  agentlens::scroll_by(scroll, 8.0);

  REQUIRE(agentlens::scroll_focus_position(scroll, 20) == 18);
  REQUIRE(agentlens::content_viewport_height(24) == 20);
  REQUIRE(agentlens::content_viewport_height(2) == 1);
  REQUIRE(agentlens::content_viewport_width(80) == 74);
  REQUIRE(agentlens::content_viewport_width(12) == 20);
  REQUIRE(agentlens::indexed_scroll_offset(5, agentlens::kBrowserEntryRows)
          == 15.0);
}
