#include "project/scroll.hpp"

#include <algorithm>

namespace agentlens {
namespace {
double clamp_non_negative(double value) { return std::max(0.0, value); }
} // namespace

void set_scroll_anchor(ScrollState &scroll, double offset) {
  const double anchored_offset = clamp_non_negative(offset);
  scroll.current = anchored_offset;
  scroll.target = anchored_offset;
}

void use_selection_scroll(ScrollState &scroll, double selected_offset) {
  scroll.manual = false;
  set_scroll_anchor(scroll, selected_offset);
}

void scroll_by(ScrollState &scroll, double amount) {
  scroll.manual = true;
  scroll.target = clamp_non_negative(scroll.target + amount);
  scroll.current = scroll.target;
}

void clamp_scroll_target(ScrollState &scroll, double max_offset) {
  if (!scroll.manual) {
    return;
  }

  const double clamped_max = clamp_non_negative(max_offset);
  scroll.target = std::clamp(scroll.target, 0.0, clamped_max);
  scroll.current = std::clamp(scroll.current, 0.0, clamped_max);
}

int scroll_focus_position(const ScrollState &scroll, int viewport_height) {
  return static_cast<int>(scroll.current) + std::max(0, viewport_height / 2);
}

int content_viewport_height(int screen_height) {
  return std::max(1, screen_height - kLayoutChromeRows);
}

int content_viewport_width(int screen_width) {
  return std::max(20, screen_width - 6);
}

double indexed_scroll_offset(std::size_t index, double rows_per_item) {
  return static_cast<double>(index) * rows_per_item;
}

} // namespace agentlens
