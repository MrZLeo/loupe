#include "line_frame.hpp"

#include <catch2/catch_test_macros.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <string>

#include "loupe/scroll.hpp"

TEST_CASE("manual scrolling survives wrapped layout passes", "[scroll]") {
  loupe::ScrollState scroll;
  auto frame = loupe::line_frame(
      ftxui::paragraph(
          "zero one two three four five six seven eight nine ten eleven twelve "
          "thirteen fourteen fifteen sixteen seventeen eighteen omega"),
      scroll);
  auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(12),
                                      ftxui::Dimension::Fixed(4));

  ftxui::Render(screen, frame);
  REQUIRE(scroll.max_top_row > 0);
  REQUIRE(screen.ToString().find("omega") == std::string::npos);

  loupe::scroll_by_rows(scroll, scroll.max_top_row);
  const int bottom_row = scroll.top_row;

  ftxui::Render(screen, frame);

  REQUIRE(scroll.top_row == bottom_row);
  REQUIRE(scroll.top_row == scroll.max_top_row);
  REQUIRE(screen.ToString().find("omega") != std::string::npos);
}
