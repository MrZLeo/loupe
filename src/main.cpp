#include <algorithm>
#include <cctype>
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
#include "project/markdown_text.hpp"
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

bool is_space(char value) {
  return std::isspace(static_cast<unsigned char>(value)) != 0;
}

bool is_plain_text_span(const agentlens::MarkdownSpan &span) {
  return !span.bold && !span.italic && !span.code && span.link_url.empty();
}

bool is_plain_text(const std::vector<agentlens::MarkdownSpan> &spans) {
  return spans.size() == 1 && is_plain_text_span(spans.front());
}

ftxui::Element styled_text(std::string_view value,
                           const agentlens::MarkdownSpan &span,
                           ftxui::Color base_color) {
  using namespace ftxui;

  Element element = text(value);

  if (span.code) {
    element = element | color(Color::Black) | bgcolor(Color::CyanLight);
  } else if (!span.link_url.empty()) {
    element = element | color(Color::CyanLight) | underlined;
  } else {
    element = element | color(base_color);
  }

  if (span.bold) {
    element = element | bold;
  }
  if (span.italic) {
    element = element | italic;
  }

  return element;
}

void append_tokenized_text(ftxui::Elements &out, std::string_view value,
                           const agentlens::MarkdownSpan &span,
                           ftxui::Color base_color) {
  if (value.empty()) {
    return;
  }

  if (span.code) {
    out.push_back(styled_text(value, span, base_color));
    return;
  }

  std::size_t index = 0;
  while (index < value.size()) {
    const std::size_t start = index;
    if (is_space(value[index])) {
      while (index < value.size() && is_space(value[index])) {
        ++index;
      }
    } else {
      while (index < value.size() && !is_space(value[index])) {
        ++index;
      }
      while (index < value.size() && is_space(value[index])) {
        ++index;
      }
    }

    out.push_back(styled_text(value.substr(start, index - start), span,
                              base_color));
  }
}

ftxui::Element render_inline_spans(
    const std::vector<agentlens::MarkdownSpan> &spans,
    ftxui::Color base_color) {
  using namespace ftxui;

  if (spans.empty()) {
    return text("");
  }

  if (is_plain_text(spans)) {
    return paragraph(spans.front().text) | color(base_color);
  }

  Elements tokens;
  for (const auto &span : spans) {
    append_tokenized_text(tokens, span.text, span, base_color);
  }

  if (tokens.empty()) {
    return text("");
  }
  return hflow(std::move(tokens)) | xflex;
}

ftxui::Element render_code_block(std::string_view code) {
  using namespace ftxui;

  Elements rows;
  std::size_t start = 0;
  while (start <= code.size()) {
    const std::size_t end = code.find('\n', start);
    const std::string_view line = end == std::string_view::npos
                                      ? code.substr(start)
                                      : code.substr(start, end - start);
    rows.push_back(hbox({
        text("  ") | color(Color::CyanLight),
        paragraph(std::string{line}) | color(Color::CyanLight),
    }));
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }

  if (rows.empty()) {
    rows.push_back(text("  ") | color(Color::CyanLight));
  }

  return vbox(std::move(rows));
}

ftxui::Element render_markdown_block(const agentlens::MarkdownBlock &block) {
  using namespace ftxui;

  switch (block.kind) {
  case agentlens::MarkdownBlockKind::Blank:
    return text("");

  case agentlens::MarkdownBlockKind::CodeBlock:
    return render_code_block(block.code);

  case agentlens::MarkdownBlockKind::Heading: {
    const Color heading_color =
        block.level <= 2 ? Color::White : Color::GrayLight;
    return render_inline_spans(block.spans, heading_color) | bold;
  }

  case agentlens::MarkdownBlockKind::ListItem: {
    const int indent = std::min(block.level, 6) * 2;
    std::string marker(static_cast<std::size_t>(indent), ' ');
    marker += block.marker.empty() ? "-" : block.marker;
    marker += " ";
    return hbox({
        text(marker) | color(Color::GrayDark),
        render_inline_spans(block.spans, Color::GrayLight) | xflex,
    });
  }

  case agentlens::MarkdownBlockKind::Quote:
    return hbox({
        text("> ") | color(Color::GrayDark),
        render_inline_spans(block.spans, Color::GrayLight) | dim | xflex,
    });

  case agentlens::MarkdownBlockKind::Paragraph:
    return render_inline_spans(block.spans, Color::GrayLight);
  }

  return text("");
}

ftxui::Element render_markdown_text(std::string_view content) {
  using namespace ftxui;

  Elements rows;
  for (const auto &block : agentlens::parse_markdown_text(content)) {
    rows.push_back(render_markdown_block(block));
  }

  if (rows.empty()) {
    return text("");
  }
  return vbox(std::move(rows)) | xflex;
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
                     : render_markdown_text(clipped_content(
                           agentlens::format_structured_text(message.content)));

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
