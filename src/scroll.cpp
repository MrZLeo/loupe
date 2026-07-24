#include "loupe/scroll.hpp"

#include <algorithm>

namespace loupe {

void follow_selection(ScrollState &scroll) { scroll.follow_focus = true; }

void update_scroll_layout(ScrollState &scroll, int focused_top_row,
                          int max_top_row) {
  scroll.max_top_row = std::max(0, max_top_row);
  if (scroll.follow_focus) {
    scroll.top_row = std::clamp(focused_top_row, 0, scroll.max_top_row);
    return;
  }
  scroll.top_row = std::clamp(scroll.top_row, 0, scroll.max_top_row);
}

void scroll_by_rows(ScrollState &scroll, int rows) {
  scroll.follow_focus = false;
  const int clamped_rows =
      std::clamp(rows, -scroll.top_row, scroll.max_top_row - scroll.top_row);
  scroll.top_row += clamped_rows;
}

} // namespace loupe
