#ifndef PROJECT_SCROLL_HPP_
#define PROJECT_SCROLL_HPP_

namespace loupe {

struct ScrollState {
  int top_row{0};
  int max_top_row{0};
  bool follow_focus{true};
};

void follow_selection(ScrollState &scroll);
void
update_scroll_layout(ScrollState &scroll, int focused_top_row, int max_top_row);
void scroll_by_rows(ScrollState &scroll, int rows);

} // namespace loupe

#endif // PROJECT_SCROLL_HPP_
