#ifndef PROJECT_SCROLL_HPP_
#define PROJECT_SCROLL_HPP_

#include <cstddef>

namespace agentlens {

struct ScrollState {
  double current{0.0};
  double target{0.0};
  bool manual{false};
};

inline constexpr double kMouseWheelCells = 3.0;
inline constexpr double kMouseWheelHorizontalCells = 6.0;
inline constexpr double kBrowserEntryRows = 3.0;
inline constexpr double kEstimatedMessageRows = 4.0;
inline constexpr int kLayoutChromeRows = 4;

void set_scroll_anchor(ScrollState &scroll, double offset);
void use_selection_scroll(ScrollState &scroll, double selected_offset);
void scroll_by(ScrollState &scroll, double amount);
void clamp_scroll_target(ScrollState &scroll, double max_offset);

int scroll_focus_position(const ScrollState &scroll, int viewport_height);
int content_viewport_height(int screen_height);
int content_viewport_width(int screen_width);
double indexed_scroll_offset(std::size_t index, double rows_per_item);

} // namespace agentlens

#endif // PROJECT_SCROLL_HPP_
