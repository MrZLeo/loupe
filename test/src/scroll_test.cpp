#include "loupe/scroll.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("row scrolling updates the viewport immediately", "[scroll]") {
  loupe::ScrollState scroll;
  loupe::update_scroll_layout(scroll, 5, 20);

  loupe::scroll_by_rows(scroll, 1);

  REQUIRE_FALSE(scroll.follow_focus);
  REQUIRE(scroll.top_row == 6);

  loupe::scroll_by_rows(scroll, -2);
  REQUIRE(scroll.top_row == 4);
}

TEST_CASE("row scrolling clamps to content bounds", "[scroll]") {
  loupe::ScrollState scroll;
  loupe::update_scroll_layout(scroll, 0, 3);

  loupe::scroll_by_rows(scroll, -1);
  REQUIRE(scroll.top_row == 0);

  loupe::scroll_by_rows(scroll, 10);
  REQUIRE(scroll.top_row == 3);

  loupe::update_scroll_layout(scroll, 0, 1);
  REQUIRE(scroll.top_row == 1);
}

TEST_CASE("overscrolling does not delay the first reverse step", "[scroll]") {
  loupe::ScrollState scroll;
  loupe::update_scroll_layout(scroll, 0, 3);

  loupe::scroll_by_rows(scroll, 3);
  for (int event = 0; event < 1000; ++event) {
    loupe::scroll_by_rows(scroll, 1);
  }
  REQUIRE(scroll.top_row == scroll.max_top_row);

  loupe::scroll_by_rows(scroll, -1);
  REQUIRE(scroll.top_row == scroll.max_top_row - 1);
}

TEST_CASE("selection navigation resumes focus following", "[scroll]") {
  loupe::ScrollState scroll;
  loupe::update_scroll_layout(scroll, 2, 20);
  loupe::scroll_by_rows(scroll, 5);
  REQUIRE(scroll.top_row == 7);

  loupe::follow_selection(scroll);
  loupe::update_scroll_layout(scroll, 12, 20);

  REQUIRE(scroll.follow_focus);
  REQUIRE(scroll.top_row == 12);
}

TEST_CASE("scroll progress follows the visible viewport", "[scroll]") {
  loupe::ScrollState scroll;

  REQUIRE(loupe::scroll_progress_percent(scroll) == 100);

  loupe::update_scroll_layout(scroll, 0, 8);
  REQUIRE(loupe::scroll_progress_percent(scroll) == 0);

  loupe::scroll_by_rows(scroll, 3);
  REQUIRE(loupe::scroll_progress_percent(scroll) == 37);

  loupe::scroll_by_rows(scroll, 20);
  REQUIRE(loupe::scroll_progress_percent(scroll) == 100);
}
