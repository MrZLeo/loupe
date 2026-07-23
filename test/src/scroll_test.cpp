#include "loupe/scroll.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("scroll follows selection anchors", "[scroll]") {
  loupe::ScrollState scroll;

  loupe::use_selection_scroll(scroll, 12.0);

  REQUIRE_FALSE(scroll.manual);
  REQUIRE(scroll.current == 12.0);
  REQUIRE(scroll.target == 12.0);
}

TEST_CASE("wheel scrolling animates toward a target offset", "[scroll]") {
  loupe::ScrollState scroll;

  loupe::scroll_by(scroll, loupe::kMouseWheelCells);
  REQUIRE(scroll.manual);
  REQUIRE(scroll.current == 0.0);
  REQUIRE(scroll.target == 3.0);
  REQUIRE(loupe::scroll_animation_active(scroll));

  const auto start = loupe::ScrollClock::now();
  REQUIRE(loupe::advance_scroll_animation(scroll, start));
  REQUIRE(scroll.current > 0.0);
  REQUIRE(scroll.current < scroll.target);

  loupe::advance_scroll_animation(scroll, start + std::chrono::seconds(1));
  REQUIRE(scroll.current == scroll.target);
  REQUIRE_FALSE(loupe::scroll_animation_active(scroll));
}

TEST_CASE("scroll target clamps to content bounds", "[scroll]") {
  loupe::ScrollState scroll;

  loupe::scroll_by(scroll, 30.0);
  loupe::clamp_scroll_target(scroll, 10.0);
  REQUIRE(scroll.current == 0.0);
  REQUIRE(scroll.target == 10.0);

  loupe::scroll_by(scroll, -30.0);
  loupe::clamp_scroll_target(scroll, 10.0);
  REQUIRE(scroll.current == 0.0);
  REQUIRE(scroll.target == 0.0);
}

TEST_CASE("scroll helpers calculate viewport positions", "[scroll]") {
  loupe::ScrollState scroll;
  loupe::set_scroll_anchor(scroll, 8.0);

  REQUIRE(loupe::scroll_focus_position(scroll, 20) == 17);
  loupe::set_scroll_anchor(scroll, 0.0);
  REQUIRE(loupe::scroll_focus_position(scroll, 20) == 9);
  REQUIRE(loupe::content_viewport_height(24) == 20);
  REQUIRE(loupe::content_viewport_height(2) == 1);
  REQUIRE(loupe::content_viewport_width(80) == 74);
  REQUIRE(loupe::content_viewport_width(12) == 20);
  REQUIRE(loupe::indexed_scroll_offset(5, loupe::kBrowserEntryRows)
          == 15.0);
}
