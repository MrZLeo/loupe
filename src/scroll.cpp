#include "project/scroll.hpp"

#include <algorithm>
#include <cmath>

namespace agentlens {
namespace {
double clamp_non_negative(double value) { return std::max(0.0, value); }

constexpr double kScrollFollowRate = 24.0;
constexpr double kScrollSnapDistance = 0.05;
constexpr double kInitialFrameSeconds = 1.0 / 60.0;
constexpr double kMaxFrameSeconds = 0.25;

double frame_seconds(ScrollState &scroll, ScrollClock::time_point now) {
  if (scroll.last_frame == ScrollClock::time_point{}) {
    scroll.last_frame = now;
    return kInitialFrameSeconds;
  }

  const auto elapsed = now - scroll.last_frame;
  scroll.last_frame = now;
  const auto seconds = std::chrono::duration<double>(elapsed).count();
  return std::clamp(seconds, kInitialFrameSeconds, kMaxFrameSeconds);
}
} // namespace

void set_scroll_anchor(ScrollState &scroll, double offset) {
  const double anchored_offset = clamp_non_negative(offset);
  scroll.current = anchored_offset;
  scroll.target = anchored_offset;
  scroll.animating = false;
  scroll.last_frame = {};
}

void use_selection_scroll(ScrollState &scroll, double selected_offset) {
  scroll.manual = false;
  set_scroll_anchor(scroll, selected_offset);
}

void scroll_by(ScrollState &scroll, double amount) {
  scroll.manual = true;
  scroll.target = clamp_non_negative(scroll.target + amount);
  scroll.animating =
      std::abs(scroll.target - scroll.current) > kScrollSnapDistance;
}

void clamp_scroll_target(ScrollState &scroll, double max_offset) {
  if (!scroll.manual) {
    return;
  }

  const double clamped_max = clamp_non_negative(max_offset);
  scroll.target = std::clamp(scroll.target, 0.0, clamped_max);
  scroll.current = std::clamp(scroll.current, 0.0, clamped_max);
  if (std::abs(scroll.target - scroll.current) <= kScrollSnapDistance) {
    scroll.current = scroll.target;
    scroll.animating = false;
    scroll.last_frame = {};
  }
}

bool
advance_scroll_animation(ScrollState &scroll, ScrollClock::time_point now) {
  if (!scroll.manual || !scroll.animating) {
    return false;
  }

  const double distance = scroll.target - scroll.current;
  if (std::abs(distance) <= kScrollSnapDistance) {
    scroll.current = scroll.target;
    scroll.animating = false;
    scroll.last_frame = {};
    return false;
  }

  const double elapsed = frame_seconds(scroll, now);
  const double progress = 1.0 - std::exp(-kScrollFollowRate * elapsed);
  scroll.current += distance * progress;

  if (std::abs(scroll.target - scroll.current) <= kScrollSnapDistance) {
    scroll.current = scroll.target;
    scroll.animating = false;
    scroll.last_frame = {};
    return false;
  }

  return true;
}

bool scroll_animation_active(const ScrollState &scroll) {
  return scroll.manual && scroll.animating;
}

int scroll_focus_position(const ScrollState &scroll, int viewport_height) {
  const int frame_center = std::max(0, (viewport_height - 1) / 2);
  return static_cast<int>(std::lround(scroll.current)) + frame_center;
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
