#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/log_parser.hpp"
#include "project/structured_text.hpp"

namespace {
struct AppState {
  std::filesystem::path path;
  agentlens::ParseResult parsed;
  std::size_t selected{0};
  std::string status;
};

ftxui::Color role_color(std::string_view role) {
  if (role == "user") {
    return ftxui::Color::MagentaLight;
  }
  if (role == "assistant") {
    return ftxui::Color::GreenLight;
  }
  if (role == "system") {
    return ftxui::Color::CyanLight;
  }
  if (role == "tool") {
    return ftxui::Color::YellowLight;
  }
  return ftxui::Color::GrayLight;
}

std::string short_path(const std::filesystem::path &path) {
  auto filename = path.filename().string();
  if (!filename.empty()) {
    return filename;
  }
  return path.string();
}

std::string clipped_content(const std::string &content) {
  constexpr std::size_t kMaxContentLength = 12000;
  if (content.size() <= kMaxContentLength) {
    return content;
  }
  return content.substr(0, kMaxContentLength) + "\n[content clipped]";
}

void clamp_selection(AppState &state) {
  if (state.parsed.messages.empty()) {
    state.selected = 0;
    return;
  }
  state.selected = std::min(state.selected, state.parsed.messages.size() - 1);
}

void move_up(AppState &state, std::size_t amount) {
  if (amount > state.selected) {
    state.selected = 0;
    return;
  }
  state.selected -= amount;
}

void move_down(AppState &state, std::size_t amount) {
  if (state.parsed.messages.empty()) {
    state.selected = 0;
    return;
  }
  const std::size_t last = state.parsed.messages.size() - 1;
  state.selected = std::min(last, state.selected + amount);
}

void reload(AppState &state) {
  state.parsed = agentlens::parse_log_file(state.path);
  clamp_selection(state);
  state.status =
      "reloaded " + std::to_string(state.parsed.messages.size()) + " messages";
}

ftxui::Element render_message(const agentlens::LogMessage &message,
                              bool selected) {
  using namespace ftxui;

  const Color accent = role_color(message.role);
  Elements metadata{
      text(message.role) | bold | color(accent),
  };

  if (!message.timestamp.empty()) {
    metadata.push_back(text("  " + message.timestamp) | dim);
  }
  if (message.source_line > 0) {
    metadata.push_back(text("  line " + std::to_string(message.source_line))
                       | dim);
  }
  if (!message.raw_type.empty() && message.raw_type != message.role) {
    metadata.push_back(text("  " + message.raw_type) | dim);
  }

  Element body = message.content.empty()
                     ? text("(empty)") | dim
                     : paragraph(clipped_content(agentlens::format_structured_text(
                           message.content)))
                           | color(Color::GrayLight);

  Elements body_rows{body};
  for (const auto &annotation : message.annotations) {
    body_rows.push_back(paragraph(annotation) | color(Color::YellowLight) | dim);
  }

  Element block = vbox({
                      hbox(std::move(metadata)),
                      vbox(std::move(body_rows)),
                  })
                | xflex;

  if (selected) {
    return hbox({
               text("|") | color(accent),
               text(" "),
               block,
           })
         | focus;
  }

  return hbox({
      text("  "),
      block,
  });
}

ftxui::Element render_errors(const std::vector<std::string> &errors) {
  using namespace ftxui;

  Elements lines;
  lines.push_back(text("No messages parsed") | bold | color(Color::RedLight));
  lines.push_back(separatorEmpty());
  for (const auto &error : errors) {
    lines.push_back(paragraph(error) | color(Color::YellowLight));
  }
  return vbox(std::move(lines)) | flex;
}

ftxui::Element render(AppState &state) {
  using namespace ftxui;

  Elements rows;
  if (state.parsed.messages.empty()) {
    rows.push_back(render_errors(state.parsed.errors));
  } else {
    std::size_t index = 0;
    for (const auto &message : state.parsed.messages) {
      rows.push_back(render_message(message, index == state.selected));
      rows.push_back(separatorEmpty());
      ++index;
    }
  }

  const std::string count =
      std::to_string(state.parsed.messages.size()) + " messages";
  const std::string cursor =
      state.parsed.messages.empty()
          ? "0/0"
          : std::to_string(state.selected + 1) + "/"
                + std::to_string(state.parsed.messages.size());

  Elements status_items{
      text(" AgentLens ") | bold | color(Color::Black)
          | bgcolor(Color::MagentaLight),
      text("  " + count) | color(Color::GrayLight),
      text("  " + cursor) | color(Color::GrayLight),
      text("  " + short_path(state.path)) | color(Color::GrayLight),
  };

  if (!state.status.empty()) {
    status_items.push_back(text("  " + state.status)
                           | color(Color::YellowLight));
  }

  return vbox({
             hbox(std::move(status_items)),
             separatorEmpty(),
             vbox(std::move(rows)) | yframe | vscroll_indicator | flex,
             separatorEmpty(),
             text("j/k arrows page g/G scroll  r reload  q quit")
                 | color(Color::GrayDark),
         })
       | flex;
}

bool handle_event(AppState &state, ftxui::Event event,
                  const ftxui::Closure &quit) {
  if (event == ftxui::Event::q || event == ftxui::Event::Escape) {
    quit();
    return true;
  }
  if (event == ftxui::Event::j || event == ftxui::Event::ArrowDown) {
    move_down(state, 1);
    return true;
  }
  if (event == ftxui::Event::k || event == ftxui::Event::ArrowUp) {
    move_up(state, 1);
    return true;
  }
  if (event == ftxui::Event::PageDown) {
    move_down(state, 10);
    return true;
  }
  if (event == ftxui::Event::PageUp) {
    move_up(state, 10);
    return true;
  }
  if (event == ftxui::Event::g) {
    state.selected = 0;
    return true;
  }
  if (event == ftxui::Event::G) {
    if (!state.parsed.messages.empty()) {
      state.selected = state.parsed.messages.size() - 1;
    }
    return true;
  }
  if (event == ftxui::Event::r) {
    reload(state);
    return true;
  }
  return false;
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2 || std::string_view{argv[1]} == "-h"
      || std::string_view{argv[1]} == "--help") {
    std::cerr << "usage: agentlens <agent-log.json|agent-log.jsonl>\n";
    return argc == 2 ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  AppState state{
      .path = std::filesystem::path{argv[1]},
      .parsed = agentlens::parse_log_file(std::filesystem::path{argv[1]}),
  };

  auto screen = ftxui::App::Fullscreen();
  const ftxui::Closure quit = screen.ExitLoopClosure();

  auto component = ftxui::Renderer([&] { return render(state); });
  component |= ftxui::CatchEvent([&](ftxui::Event event) {
    return handle_event(state, std::move(event), quit);
  });

  screen.Loop(component);
  return EXIT_SUCCESS;
}
