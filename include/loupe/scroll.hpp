#ifndef PROJECT_SCROLL_HPP_
#define PROJECT_SCROLL_HPP_

#include <chrono>
#include <cstddef>

namespace loupe {

using ScrollClock = std::chrono::steady_clock;

struct ScrollState {
  double current{0.0};
  double target{0.0};
  bool manual{false};
  bool animating{false};
  ScrollClock::time_point last_frame{};
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
bool advance_scroll_animation(ScrollState &scroll, ScrollClock::time_point now);
bool scroll_animation_active(const ScrollState &scroll);

int scroll_focus_position(const ScrollState &scroll, int viewport_height);
int content_viewport_height(int screen_height);
int content_viewport_width(int screen_width);
double indexed_scroll_offset(std::size_t index, double rows_per_item);

} // namespace loupe

#endif // PROJECT_SCROLL_HPP_
