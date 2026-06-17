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
#include "project/search.hpp"
#include "project/structured_text.hpp"

namespace {
struct AppState {
  std::filesystem::path path;
  agentlens::ParseResult parsed;
  std::size_t selected{0};
  std::string status;
  bool search_active{false};
  std::string search_input;
  std::string search_query;
  std::vector<std::size_t> search_matches;
  std::size_t search_origin_selected{0};
};

enum class HighlightKind {
  None,
  Match,
  Current,
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

std::string clipped_label(std::string_view text) {
  constexpr std::size_t kMaxLabelLength = 60;
  if (text.size() <= kMaxLabelLength) {
    return std::string{text};
  }
  return std::string{text.substr(0, kMaxLabelLength - 3)} + "...";
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

ftxui::Element
styled_text(std::string_view value, const agentlens::MarkdownSpan &span,
            ftxui::Color base_color, HighlightKind highlight) {
  using namespace ftxui;

  Element element = text(value);

  if (highlight == HighlightKind::Current) {
    element = element | color(Color::Black) | bgcolor(Color::MagentaLight);
  } else if (highlight == HighlightKind::Match) {
    element = element | color(Color::Black) | bgcolor(Color::YellowLight);
  } else if (span.code) {
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
                           ftxui::Color base_color, HighlightKind highlight) {
  if (value.empty()) {
    return;
  }

  if (span.code || highlight != HighlightKind::None) {
    out.push_back(styled_text(value, span, base_color, highlight));
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
                              base_color, highlight));
  }
}

void
append_styled_tokens(ftxui::Elements &out, const agentlens::MarkdownSpan &span,
                     ftxui::Color base_color, std::string_view search_query,
                     bool current_search_match) {
  if (span.text.empty()) {
    return;
  }

  const auto ranges = agentlens::find_text_matches(span.text, search_query);
  if (ranges.empty()) {
    append_tokenized_text(out, span.text, span, base_color,
                          HighlightKind::None);
    return;
  }

  std::size_t cursor = 0;
  for (const auto &range : ranges) {
    append_tokenized_text(
        out, std::string_view{span.text}.substr(cursor, range.offset - cursor),
        span, base_color, HighlightKind::None);

    append_tokenized_text(
        out, std::string_view{span.text}.substr(range.offset, range.length),
        span, base_color,
        current_search_match ? HighlightKind::Current : HighlightKind::Match);

    cursor = range.offset + range.length;
  }

  append_tokenized_text(out, std::string_view{span.text}.substr(cursor), span,
                        base_color, HighlightKind::None);
}

ftxui::Element
render_inline_spans(const std::vector<agentlens::MarkdownSpan> &spans,
                    ftxui::Color base_color, std::string_view search_query,
                    bool current_search_match) {
  using namespace ftxui;

  if (spans.empty()) {
    return text("");
  }

  if (search_query.empty() && is_plain_text(spans)) {
    return paragraph(spans.front().text) | color(base_color);
  }

  Elements tokens;
  for (const auto &span : spans) {
    append_styled_tokens(tokens, span, base_color, search_query,
                         current_search_match);
  }

  if (tokens.empty()) {
    return text("");
  }
  return hflow(std::move(tokens)) | xflex;
}

ftxui::Element
render_searchable_text(std::string_view value, ftxui::Color base_color,
                       std::string_view search_query,
                       bool current_search_match) {
  return render_inline_spans(
      {agentlens::MarkdownSpan{.text = std::string{value}}}, base_color,
      search_query, current_search_match);
}

ftxui::Element
render_code_block(std::string_view code, std::string_view search_query,
                  bool current_search_match) {
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
        render_searchable_text(line, Color::CyanLight, search_query,
                               current_search_match),
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

ftxui::Element render_markdown_block(const agentlens::MarkdownBlock &block,
                                     std::string_view search_query,
                                     bool current_search_match) {
  using namespace ftxui;

  switch (block.kind) {
  case agentlens::MarkdownBlockKind::Blank:
    return text("");

  case agentlens::MarkdownBlockKind::CodeBlock:
    return render_code_block(block.code, search_query, current_search_match);

  case agentlens::MarkdownBlockKind::Heading: {
    const Color heading_color =
        block.level <= 2 ? Color::White : Color::GrayLight;
    return render_inline_spans(block.spans, heading_color, search_query,
                               current_search_match)
         | bold;
  }

  case agentlens::MarkdownBlockKind::ListItem: {
    const int indent = std::min(block.level, 6) * 2;
    std::string marker(static_cast<std::size_t>(indent), ' ');
    marker += block.marker.empty() ? "-" : block.marker;
    marker += " ";
    return hbox({
        text(marker) | color(Color::GrayDark),
        render_inline_spans(block.spans, Color::GrayLight, search_query,
                            current_search_match)
            | xflex,
    });
  }

  case agentlens::MarkdownBlockKind::Quote:
    return hbox({
        text("> ") | color(Color::GrayDark),
        render_inline_spans(block.spans, Color::GrayLight, search_query,
                            current_search_match)
            | dim
            | xflex,
    });

  case agentlens::MarkdownBlockKind::Paragraph:
    return render_inline_spans(block.spans, Color::GrayLight, search_query,
                               current_search_match);
  }

  return text("");
}

ftxui::Element
render_markdown_text(std::string_view content, std::string_view search_query,
                     bool current_search_match) {
  using namespace ftxui;

  Elements rows;
  for (const auto &block : agentlens::parse_markdown_text(content)) {
    rows.push_back(
        render_markdown_block(block, search_query, current_search_match));
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

void refresh_search_matches(AppState &state) {
  state.search_matches = agentlens::find_message_matches(state.parsed.messages,
                                                         state.search_query);
}

void restore_search_origin(AppState &state) {
  if (state.parsed.messages.empty()) {
    state.selected = 0;
    return;
  }
  state.selected =
      std::min(state.search_origin_selected, state.parsed.messages.size() - 1);
}

std::string search_progress(const AppState &state) {
  const std::string_view query = state.search_active
                                   ? std::string_view{state.search_input}
                                   : std::string_view{state.search_query};
  if (query.empty()) {
    return "";
  }

  const std::string label = "\"" + clipped_label(query) + "\"";
  if (state.search_matches.empty()) {
    return "0/0 " + label;
  }

  const auto ordinal =
      agentlens::match_ordinal(state.search_matches, state.selected);
  if (!ordinal.has_value()) {
    return std::to_string(state.search_matches.size()) + " matches " + label;
  }

  return std::to_string(*ordinal + 1)
       + "/"
       + std::to_string(state.search_matches.size())
       + " "
       + label;
}

void begin_search(AppState &state) {
  state.search_active = true;
  state.search_origin_selected = state.selected;
  state.search_input.clear();
  state.search_matches.clear();
  state.status.clear();
}

void update_search_preview(AppState &state) {
  clamp_selection(state);
  restore_search_origin(state);
  state.status.clear();

  if (state.search_input.empty()) {
    state.search_matches.clear();
    return;
  }

  state.search_matches = agentlens::find_message_matches(state.parsed.messages,
                                                         state.search_input);
  const auto match = agentlens::find_next_match(
      state.search_matches, state.search_origin_selected,
      agentlens::SearchDirection::Forward, true);
  if (match.has_value()) {
    state.selected = *match;
  } else {
    restore_search_origin(state);
  }
}

void cancel_search(AppState &state) {
  state.search_active = false;
  clamp_selection(state);
  restore_search_origin(state);
  refresh_search_matches(state);
  state.status = "search canceled";
}

void commit_search(AppState &state) {
  state.search_active = false;
  if (state.search_input.empty()) {
    refresh_search_matches(state);
    restore_search_origin(state);
    state.status = "empty search";
    return;
  }

  state.search_query = state.search_input;
  refresh_search_matches(state);

  const auto match = agentlens::find_next_match(
      state.search_matches, state.search_origin_selected,
      agentlens::SearchDirection::Forward, true);
  if (!match.has_value()) {
    restore_search_origin(state);
    state.status = "no matches";
    return;
  }

  state.selected = *match;
  state.status.clear();
}

void jump_to_search_match(AppState &state, agentlens::SearchDirection direction,
                          bool include_selected) {
  if (state.search_query.empty()) {
    state.status = "no active search";
    return;
  }

  refresh_search_matches(state);
  const auto match = agentlens::find_next_match(
      state.search_matches, state.selected, direction, include_selected);
  if (!match.has_value()) {
    state.status = "no matches";
    return;
  }

  state.selected = *match;
  state.status.clear();
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
  if (!state.search_query.empty()) {
    jump_to_search_match(state, agentlens::SearchDirection::Forward, true);
  }
  state.status =
      "reloaded " + std::to_string(state.parsed.messages.size()) + " messages";
}

ftxui::Element
render_message(const agentlens::LogMessage &message, bool selected,
               std::string_view search_query, bool current_search_match) {
  using namespace ftxui;

  const Color accent = role_color(message.role);
  Elements metadata{
      render_searchable_text(message.role, accent, search_query,
                             current_search_match)
          | bold,
  };

  if (!message.timestamp.empty()) {
    metadata.push_back(render_searchable_text("  " + message.timestamp,
                                              Color::GrayLight, search_query,
                                              current_search_match)
                       | dim);
  }
  if (message.source_line > 0) {
    metadata.push_back(text("  line " + std::to_string(message.source_line))
                       | dim);
  }
  if (!message.raw_type.empty() && message.raw_type != message.role) {
    metadata.push_back(render_searchable_text("  " + message.raw_type,
                                              Color::GrayLight, search_query,
                                              current_search_match)
                       | dim);
  }

  Element body = message.content.empty()
                   ? text("(empty)") | dim
                   : render_markdown_text(
                         clipped_content(agentlens::format_structured_text(
                             message.content)),
                         search_query, current_search_match);

  Elements body_rows{body};
  for (const auto &annotation : message.annotations) {
    body_rows.push_back(render_searchable_text(annotation, Color::YellowLight,
                                               search_query,
                                               current_search_match)
                        | dim);
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

  const std::string_view visible_search_query =
      state.search_active ? std::string_view{state.search_input}
                          : std::string_view{state.search_query};

  Elements rows;
  if (state.parsed.messages.empty()) {
    rows.push_back(render_errors(state.parsed.errors));
  } else {
    std::size_t index = 0;
    for (const auto &message : state.parsed.messages) {
      const bool selected_message = index == state.selected;
      const bool current_search_match =
          selected_message
          && std::binary_search(state.search_matches.begin(),
                                state.search_matches.end(), index);
      rows.push_back(render_message(message, selected_message,
                                    visible_search_query,
                                    current_search_match));
      rows.push_back(separatorEmpty());
      ++index;
    }
  }

  const std::string count =
      std::to_string(state.parsed.messages.size()) + " messages";
  const std::string cursor =
      state.parsed.messages.empty()
          ? "0/0"
          : std::to_string(state.selected + 1)
                + "/"
                + std::to_string(state.parsed.messages.size());

  Elements status_items{
      text(" AgentLens ")
          | bold
          | color(Color::Black)
          | bgcolor(Color::MagentaLight),
      text("  " + count) | color(Color::GrayLight),
      text("  " + cursor) | color(Color::GrayLight),
      text("  " + short_path(state.path)) | color(Color::GrayLight),
  };

  if (!state.status.empty()) {
    status_items.push_back(text("  " + state.status)
                           | color(Color::YellowLight));
  }
  const bool has_visible_search = state.search_active
                                    ? !state.search_input.empty()
                                    : !state.search_query.empty();
  if (has_visible_search) {
    const std::string prefix = state.search_active ? "  preview " : "  search ";
    status_items.push_back(text(prefix + search_progress(state))
                           | color(Color::CyanLight));
  }

  Element help = state.search_active
                   ? hbox({
                         text("/") | bold | color(Color::CyanLight),
                         text(state.search_input) | color(Color::White),
                         text("  enter search  esc cancel  backspace edit")
                             | color(Color::GrayDark),
                     })
                   : text("j/k arrows page g/G scroll  / search  n/N next/prev "
                          " r reload  q quit")
                         | color(Color::GrayDark);

  return vbox({
             hbox(std::move(status_items)),
             separatorEmpty(),
             vbox(std::move(rows)) | yframe | vscroll_indicator | flex,
             separatorEmpty(),
             help,
         })
       | flex;
}

bool handle_search_event(AppState &state, const ftxui::Event &event) {
  if (event == ftxui::Event::Escape) {
    cancel_search(state);
    return true;
  }
  if (event == ftxui::Event::Return) {
    commit_search(state);
    return true;
  }
  if (event == ftxui::Event::Backspace) {
    if (!state.search_input.empty()) {
      state.search_input.pop_back();
    }
    update_search_preview(state);
    return true;
  }
  if (event == ftxui::Event::CtrlU) {
    state.search_input.clear();
    update_search_preview(state);
    return true;
  }
  if (event.is_character()) {
    state.search_input += event.character();
    update_search_preview(state);
    return true;
  }

  return true;
}

bool
handle_event(AppState &state, ftxui::Event event, const ftxui::Closure &quit) {
  if (state.search_active) {
    return handle_search_event(state, event);
  }
  if (event == ftxui::Event::Character('/')) {
    begin_search(state);
    return true;
  }
  if (event == ftxui::Event::n) {
    jump_to_search_match(state, agentlens::SearchDirection::Forward, false);
    return true;
  }
  if (event == ftxui::Event::N) {
    jump_to_search_match(state, agentlens::SearchDirection::Backward, false);
    return true;
  }
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
  if (argc != 2
      || std::string_view{argv[1]} == "-h"
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
