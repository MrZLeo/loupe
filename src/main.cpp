#include <algorithm>
#include <argum/argum.h>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "loupe/log_format.hpp"
#include "loupe/log_parser.hpp"
#include "loupe/markdown_text.hpp"
#include "loupe/message_projection.hpp"
#include "loupe/scroll.hpp"
#include "loupe/search.hpp"
#include "loupe/session_parser.hpp"
#include "loupe/structured_text.hpp"
#include "loupe/synchronized_output.hpp"
#include "loupe/version.hpp"
#include "line_frame.hpp"

namespace {
struct CliOptions {
  std::filesystem::path path;
  std::optional<loupe::LogFormat> format;
};

struct CliParseResult {
  CliOptions options;
  std::string format_error;
  bool run{false};
  int exit_code{EXIT_SUCCESS};
};

struct AppState {
  std::filesystem::path path;
  loupe::LogFormat format{loupe::LogFormat::Generic};
  loupe::SessionParseResult session;
  loupe::ParseResult parsed;
  std::size_t selected{0};
  loupe::ScrollState scroll;
  std::string status;
  bool search_active{false};
  std::string search_input;
  std::string search_query;
  std::vector<std::size_t> search_matches;
  std::size_t search_origin_selected{0};
};

struct BrowserEntry {
  std::filesystem::path path;
  std::string display_path;
  std::string modified_label;
};

struct BrowserState {
  std::filesystem::path root;
  std::vector<BrowserEntry> entries;
  std::vector<std::size_t> visible_entries;
  std::size_t selected{0};
  loupe::ScrollState scroll;
  std::string status;
  bool search_active{false};
  std::string search_input;
  std::string search_query;
};

struct ApplicationState {
  loupe::LogFormat format{loupe::LogFormat::Generic};
  BrowserState browser;
  AppState viewer;
  bool browser_available{false};
  bool showing_browser{false};
};

enum class HighlightKind {
  None,
  Match,
  Current,
};

constexpr std::size_t kPageMove = 10;

int wheel_rows(ftxui::Event &event) {
  if (!event.is_mouse()) {
    return 0;
  }
  if (event.mouse().button == ftxui::Mouse::WheelUp) {
    return -1;
  }
  if (event.mouse().button == ftxui::Mouse::WheelDown) {
    return 1;
  }
  return 0;
}

CliParseResult parse_cli_args(int argc, char **argv) {
  const char *program_name =
      argc > 0 && argv[0] != nullptr ? argv[0] : "loupe";

  CliParseResult result;
  Argum::Parser parser;

  parser.add(Argum::Option("--help", "-h")
                 .help("show this help message and exit")
                 .handler([&]() {
                   std::cout << parser.formatHelp(program_name);
                   std::exit(EXIT_SUCCESS);
                 }));
  parser.add(Argum::Option("--version")
                 .help("show program's version number and exit")
                 .handler([]() {
                   std::cout << "loupe " << LOUPE_VERSION << '\n';
                   std::exit(EXIT_SUCCESS);
                 }));
  parser.add(Argum::Option("--format", "-f")
                 .help("log format: pi, codex, claudecode, or generic")
                 .handler([&](const std::string_view &value) {
                   result.options.format = loupe::parse_log_format(value);
                   if (!result.options.format) {
                     result.format_error =
                         "unsupported log format: " + std::string{value};
                   }
                 }));

  parser.add(Argum::Positional("path")
                 .occurs(Argum::zeroOrOneTime)
                 .help("agent log file or directory; defaults to current "
                       "directory")
                 .handler([&](const std::string_view &value) {
                   result.options.path =
                       std::filesystem::path{std::string{value}};
                 }));

  try {
    parser.parse(argc, argv);
  } catch (const Argum::ParsingException &ex) {
    std::cerr << ex.message() << '\n';
    std::cerr << parser.formatUsage(program_name) << '\n';
    result.exit_code = EXIT_FAILURE;
    return result;
  }

  if (!result.format_error.empty()) {
    std::cerr << result.format_error << '\n';
    std::cerr << parser.formatUsage(program_name) << '\n';
    result.exit_code = EXIT_FAILURE;
    return result;
  }
  if (!result.options.format) {
    std::cerr << "error: --format is required "
                 "(pi, codex, claudecode, or generic)\n";
    std::cerr << parser.formatUsage(program_name) << '\n';
    result.exit_code = EXIT_FAILURE;
    return result;
  }

  result.run = true;
  return result;
}

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

std::string lowercase_copy(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char value : text) {
    out.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
  }
  return out;
}

bool
contains_case_insensitive(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }

  const std::string lower_haystack = lowercase_copy(haystack);
  const std::string lower_needle = lowercase_copy(needle);
  return lower_haystack.find(lower_needle) != std::string::npos;
}

bool is_log_candidate(const std::filesystem::path &path) {
  const std::string extension = lowercase_copy(path.extension().string());
  return extension == ".json"
      || extension == ".jsonl"
      || extension == ".ndjson"
      || extension == ".log";
}

bool is_ignored_browser_directory(const std::filesystem::path &path) {
  const std::string name = path.filename().string();
  if (name == ".git"
      || name == ".cpm-cache"
      || name == "build"
      || name == "CMakeFiles"
      || name == "_deps"
      || name == "node_modules"
      || name == "dist"
      || name == "out"
      || name == "target"
      || name == ".venv"
      || name == "venv") {
    return true;
  }
  return name.rfind("cmake-build-", 0) == 0;
}

std::string pluralize(std::size_t count, std::string_view singular,
                      std::string_view plural) {
  return std::to_string(count)
       + " "
       + std::string{count == 1 ? singular : plural};
}

std::string age_label(std::filesystem::file_time_type modified_time) {
  using namespace std::chrono;

  const auto now = std::filesystem::file_time_type::clock::now();
  if (modified_time >= now) {
    return "just now";
  }

  const auto age = now - modified_time;
  const auto minutes = duration_cast<std::chrono::minutes>(age).count();
  if (minutes < 1) {
    return "just now";
  }
  if (minutes < 60) {
    return pluralize(static_cast<std::size_t>(minutes), "minute ago",
                     "minutes ago");
  }

  const auto hours = duration_cast<std::chrono::hours>(age).count();
  if (hours < 24) {
    return pluralize(static_cast<std::size_t>(hours), "hour ago", "hours ago");
  }

  const auto days = duration_cast<std::chrono::hours>(age).count() / 24;
  if (days < 30) {
    return pluralize(static_cast<std::size_t>(days), "day ago", "days ago");
  }
  if (days < 365) {
    return pluralize(static_cast<std::size_t>(days / 30), "month ago",
                     "months ago");
  }
  return pluralize(static_cast<std::size_t>(days / 365), "year ago",
                   "years ago");
}

std::string relative_display_path(const std::filesystem::path &path,
                                  const std::filesystem::path &root) {
  std::error_code error;
  const auto relative = std::filesystem::relative(path, root, error);
  if (!error && !relative.empty()) {
    return relative.generic_string();
  }
  return path.generic_string();
}

std::string browser_query(const BrowserState &state) {
  return state.search_active ? state.search_input : state.search_query;
}

void apply_browser_filter(BrowserState &state) {
  loupe::follow_selection(state.scroll);
  state.visible_entries.clear();
  const std::string query = browser_query(state);

  for (std::size_t index = 0; index < state.entries.size(); ++index) {
    if (contains_case_insensitive(state.entries[index].display_path, query)) {
      state.visible_entries.push_back(index);
    }
  }

  if (state.visible_entries.empty()) {
    state.selected = 0;
    return;
  }
  state.selected = std::min(state.selected, state.visible_entries.size() - 1);
}

void refresh_browser(BrowserState &state) {
  state.entries.clear();
  std::error_code error;

  if (!std::filesystem::exists(state.root, error) || error) {
    state.status = "directory does not exist: " + state.root.string();
    apply_browser_filter(state);
    return;
  }
  if (!std::filesystem::is_directory(state.root, error) || error) {
    state.status = "not a directory: " + state.root.string();
    apply_browser_filter(state);
    return;
  }

  const auto options =
      std::filesystem::directory_options::skip_permission_denied;
  std::filesystem::recursive_directory_iterator iterator{state.root, options,
                                                         error};
  if (error) {
    state.status = "could not read directory: " + error.message();
    apply_browser_filter(state);
    return;
  }

  const std::filesystem::recursive_directory_iterator end;
  bool skipped_entries = false;

  while (!error && iterator != end) {
    const auto entry = *iterator;
    std::error_code entry_error;
    if (entry.is_directory(entry_error)) {
      if (is_ignored_browser_directory(entry.path())) {
        iterator.disable_recursion_pending();
      }
    } else if (!entry_error
               && entry.is_regular_file(entry_error)
               && !entry_error
               && is_log_candidate(entry.path())) {
      std::string modified = "unknown";
      std::error_code time_error;
      const auto write_time =
          std::filesystem::last_write_time(entry.path(), time_error);
      if (!time_error) {
        modified = age_label(write_time);
      }
      state.entries.push_back(BrowserEntry{
          .path = entry.path(),
          .display_path = relative_display_path(entry.path(), state.root),
          .modified_label = modified,
      });
    }

    iterator.increment(error);
    if (error) {
      skipped_entries = true;
      error.clear();
    }
  }

  std::sort(state.entries.begin(), state.entries.end(),
            [](const BrowserEntry &left, const BrowserEntry &right) {
              return lowercase_copy(left.display_path)
                   < lowercase_copy(right.display_path);
            });

  apply_browser_filter(state);
  state.status = skipped_entries ? "some entries were skipped" : "";
}

void begin_browser_search(BrowserState &state) {
  state.search_active = true;
  state.search_input.clear();
  state.status.clear();
  apply_browser_filter(state);
}

void cancel_browser_search(BrowserState &state) {
  state.search_active = false;
  state.search_input.clear();
  state.status = "search canceled";
  apply_browser_filter(state);
}

void commit_browser_search(BrowserState &state) {
  state.search_active = false;
  state.search_query = state.search_input;
  apply_browser_filter(state);
  if (state.search_query.empty()) {
    state.status = "empty search";
    return;
  }
  state.status = state.visible_entries.empty() ? "no matches" : "";
}

void update_browser_search_preview(BrowserState &state) {
  state.status.clear();
  apply_browser_filter(state);
}

void move_browser_up(BrowserState &state, std::size_t amount) {
  loupe::follow_selection(state.scroll);
  state.selected -= std::min(state.selected, amount);
}

void move_browser_down(BrowserState &state, std::size_t amount) {
  loupe::follow_selection(state.scroll);
  if (state.visible_entries.empty()) {
    state.selected = 0;
    return;
  }
  const std::size_t last = state.visible_entries.size() - 1;
  state.selected += std::min(last - state.selected, amount);
}

const BrowserEntry *selected_browser_entry(const BrowserState &state) {
  if (state.visible_entries.empty()) {
    return nullptr;
  }
  return &state.entries[state.visible_entries[state.selected]];
}

AppState
load_viewer_state(const std::filesystem::path &path, loupe::LogFormat format) {
  auto session = loupe::parse_session_file(path, format);
  loupe::ParseResult parsed;
  if (!session.has_fatal_error()) {
    parsed.messages = loupe::make_display_messages(session.session);
  }
  for (const auto &diagnostic : session.diagnostics) {
    parsed.errors.push_back(loupe::format_diagnostic(diagnostic));
  }
  AppState loaded;
  loaded.path = path;
  loaded.format = format;
  loaded.session = std::move(session);
  loaded.parsed = std::move(parsed);
  return loaded;
}

bool is_space(char value) {
  return std::isspace(static_cast<unsigned char>(value)) != 0;
}

bool is_plain_text_span(const loupe::MarkdownSpan &span) {
  return !span.bold && !span.italic && !span.code && span.link_url.empty();
}

bool is_plain_text(const std::vector<loupe::MarkdownSpan> &spans) {
  return spans.size() == 1 && is_plain_text_span(spans.front());
}

ftxui::Element
styled_text(std::string_view value, const loupe::MarkdownSpan &span,
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
                           const loupe::MarkdownSpan &span,
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
append_styled_tokens(ftxui::Elements &out, const loupe::MarkdownSpan &span,
                     ftxui::Color base_color, std::string_view search_query,
                     bool current_search_match) {
  if (span.text.empty()) {
    return;
  }

  const auto ranges = loupe::find_text_matches(span.text, search_query);
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
render_inline_spans(const std::vector<loupe::MarkdownSpan> &spans,
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
      {loupe::MarkdownSpan{.text = std::string{value}, .link_url = {}}},
      base_color, search_query, current_search_match);
}

ftxui::Element
render_searchable_lines(std::string_view value, ftxui::Color base_color,
                        std::string_view search_query,
                        bool current_search_match) {
  using namespace ftxui;

  Elements rows;
  std::size_t start = 0;
  while (start <= value.size()) {
    const std::size_t end = value.find('\n', start);
    const std::string_view line = end == std::string_view::npos
                                    ? value.substr(start)
                                    : value.substr(start, end - start);
    rows.push_back(render_searchable_text(line, base_color, search_query,
                                          current_search_match));
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }

  if (rows.empty()) {
    return text("");
  }
  return vbox(std::move(rows)) | xflex;
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

ftxui::Element render_markdown_block(const loupe::MarkdownBlock &block,
                                     std::string_view search_query,
                                     bool current_search_match) {
  using namespace ftxui;

  switch (block.kind) {
  case loupe::MarkdownBlockKind::Blank:
    return text("");

  case loupe::MarkdownBlockKind::CodeBlock:
    return render_code_block(block.code, search_query, current_search_match);

  case loupe::MarkdownBlockKind::Heading: {
    const Color heading_color =
        block.level <= 2 ? Color::White : Color::GrayLight;
    return render_inline_spans(block.spans, heading_color, search_query,
                               current_search_match)
         | bold;
  }

  case loupe::MarkdownBlockKind::ListItem: {
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

  case loupe::MarkdownBlockKind::Quote:
    return hbox({
        text("> ") | color(Color::GrayDark),
        render_inline_spans(block.spans, Color::GrayLight, search_query,
                            current_search_match)
            | dim
            | xflex,
    });

  case loupe::MarkdownBlockKind::Paragraph:
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
  for (const auto &block : loupe::parse_markdown_text(content)) {
    rows.push_back(
        render_markdown_block(block, search_query, current_search_match));
  }

  if (rows.empty()) {
    return text("");
  }
  return vbox(std::move(rows)) | xflex;
}

void clamp_selection(AppState &state) {
  loupe::follow_selection(state.scroll);
  if (state.parsed.messages.empty()) {
    state.selected = 0;
    return;
  }
  state.selected = std::min(state.selected, state.parsed.messages.size() - 1);
}

void refresh_search_matches(AppState &state) {
  state.search_matches = loupe::find_message_matches(state.parsed.messages,
                                                         state.search_query);
}

void restore_search_origin(AppState &state) {
  loupe::follow_selection(state.scroll);
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
      loupe::match_ordinal(state.search_matches, state.selected);
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
  loupe::follow_selection(state.scroll);
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

  state.search_matches = loupe::find_message_matches(state.parsed.messages,
                                                         state.search_input);
  const auto match = loupe::find_next_match(
      state.search_matches, state.search_origin_selected,
      loupe::SearchDirection::Forward, true);
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

  const auto match = loupe::find_next_match(
      state.search_matches, state.search_origin_selected,
      loupe::SearchDirection::Forward, true);
  if (!match.has_value()) {
    restore_search_origin(state);
    state.status = "no matches";
    return;
  }

  state.selected = *match;
  loupe::follow_selection(state.scroll);
  state.status.clear();
}

void jump_to_search_match(AppState &state, loupe::SearchDirection direction,
                          bool include_selected) {
  if (state.search_query.empty()) {
    state.status = "no active search";
    return;
  }

  refresh_search_matches(state);
  const auto match = loupe::find_next_match(
      state.search_matches, state.selected, direction, include_selected);
  if (!match.has_value()) {
    state.status = "no matches";
    return;
  }

  state.selected = *match;
  loupe::follow_selection(state.scroll);
  state.status.clear();
}

void move_up(AppState &state, std::size_t amount) {
  loupe::follow_selection(state.scroll);
  state.selected -= std::min(state.selected, amount);
}

void move_down(AppState &state, std::size_t amount) {
  loupe::follow_selection(state.scroll);
  if (state.parsed.messages.empty()) {
    state.selected = 0;
    return;
  }
  const std::size_t last = state.parsed.messages.size() - 1;
  state.selected += std::min(last - state.selected, amount);
}

void reload(AppState &state) {
  AppState loaded = load_viewer_state(state.path, state.format);
  state.session = std::move(loaded.session);
  state.parsed = std::move(loaded.parsed);
  clamp_selection(state);
  if (!state.search_query.empty()) {
    jump_to_search_match(state, loupe::SearchDirection::Forward, true);
  }
  state.status =
      "reloaded " + std::to_string(state.parsed.messages.size()) + " messages";
}

ftxui::Element
render_message(const loupe::LogMessage &message, bool selected,
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
                         clipped_content(loupe::format_structured_text(
                             message.content)),
                         search_query, current_search_match);

  Elements body_rows{body};
  for (const auto &annotation : message.annotations) {
    body_rows.push_back(render_searchable_lines(annotation, Color::YellowLight,
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

ftxui::Element render_browser_entry(const BrowserEntry &entry, bool selected) {
  using namespace ftxui;

  const Color path_color = selected ? Color::MagentaLight : Color::GrayLight;
  Element block = vbox({
      text(entry.display_path) | color(path_color),
      text(entry.modified_label) | color(Color::GrayDark),
  });

  if (selected) {
    return hbox({
               text("|") | color(Color::MagentaLight),
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

ftxui::Element render_browser_empty(const BrowserState &state) {
  using namespace ftxui;

  Elements lines;
  if (state.entries.empty()) {
    lines.push_back(text("No log files found") | bold | color(Color::RedLight));
    lines.push_back(separatorEmpty());
    lines.push_back(
        paragraph("Looking for .json, .jsonl, .ndjson, and .log files under "
                  + state.root.string())
        | color(Color::YellowLight));
  } else {
    lines.push_back(text("No matches") | bold | color(Color::YellowLight));
    lines.push_back(separatorEmpty());
    lines.push_back(paragraph("No file path matches \""
                              + clipped_label(browser_query(state))
                              + "\"")
                    | color(Color::GrayLight));
  }
  return vbox(std::move(lines)) | flex;
}

ftxui::Element render_browser(BrowserState &state) {
  using namespace ftxui;

  Elements rows;
  if (state.visible_entries.empty()) {
    rows.push_back(render_browser_empty(state));
  } else {
    for (std::size_t visible_index = 0;
         visible_index < state.visible_entries.size(); ++visible_index) {
      const BrowserEntry &entry =
          state.entries[state.visible_entries[visible_index]];
      rows.push_back(
          render_browser_entry(entry, visible_index == state.selected));
      rows.push_back(separatorEmpty());
    }
  }

  const std::string count = pluralize(state.entries.size(), "file", "files");
  const std::string cursor =
      state.visible_entries.empty()
          ? "0/0"
          : std::to_string(state.selected + 1)
                + "/"
                + std::to_string(state.visible_entries.size());

  Elements status_items{
      text(" Loupe ")
          | bold
          | color(Color::Black)
          | bgcolor(Color::MagentaLight),
      text("  " + count) | color(Color::GrayLight),
      text("  " + cursor) | color(Color::GrayLight),
      text("  " + state.root.string()) | color(Color::GrayLight),
  };

  const std::string query = browser_query(state);
  if (!state.status.empty()) {
    status_items.push_back(text("  " + state.status)
                           | color(Color::YellowLight));
  }
  if (!query.empty()) {
    status_items.push_back(text("  filter \"" + clipped_label(query) + "\"")
                           | color(Color::CyanLight));
  }

  Element help = state.search_active
                   ? hbox({
                         text("/") | bold | color(Color::CyanLight),
                         text(state.search_input) | color(Color::White),
                         text("  enter filter  esc cancel  backspace edit")
                             | color(Color::GrayDark),
                     })
                   : text("wheel lines  j/k up/down page files  / find  "
                          "enter open  r refresh  q quit")
                         | color(Color::GrayDark);

  return vbox({
             hbox(std::move(status_items)),
             separatorEmpty(),
             loupe::line_frame(
                 vbox(std::move(rows)) | vscroll_indicator, state.scroll)
                 | flex,
             separatorEmpty(),
             help,
         })
       | flex;
}

ftxui::Element render(AppState &state, bool can_return_to_browser) {
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
      text(" Loupe ")
          | bold
          | color(Color::Black)
          | bgcolor(Color::MagentaLight),
      text("  " + count) | color(Color::GrayLight),
      text("  " + cursor) | color(Color::GrayLight),
      text("  " + std::string{loupe::log_format_name(state.format)})
          | color(Color::GrayLight),
      text("  " + short_path(state.path)) | color(Color::GrayLight),
  };

  if (!state.parsed.errors.empty()) {
    status_items.push_back(
        text("  " + std::to_string(state.parsed.errors.size()) + " diagnostics")
        | color(Color::YellowLight));
    status_items.push_back(
        text("  " + clipped_label(state.parsed.errors.front()))
        | color(Color::YellowLight));
  }
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

  std::string help_text = "wheel lines  j/k up/down page messages  / search  "
                          "n/N next/prev  r reload";
  if (can_return_to_browser) {
    help_text += "  b files";
  }
  help_text += "  q quit";

  Element help = state.search_active
                   ? hbox({
                         text("/") | bold | color(Color::CyanLight),
                         text(state.search_input) | color(Color::White),
                         text("  enter search  esc cancel  backspace edit")
                             | color(Color::GrayDark),
                     })
                   : text(help_text) | color(Color::GrayDark);
  Element footer = hbox({
      help | flex,
      text(" "),
      loupe::scroll_progress_indicator(state.scroll) | color(Color::GrayLight),
  });

  return vbox({
             hbox(std::move(status_items)),
             separatorEmpty(),
             loupe::line_frame(
                 vbox(std::move(rows)) | vscroll_indicator, state.scroll)
                 | flex,
             separatorEmpty(),
             footer,
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
  if (const int rows = wheel_rows(event); rows != 0) {
    loupe::scroll_by_rows(state.scroll, rows);
    return true;
  }
  if (state.search_active) {
    return handle_search_event(state, event);
  }
  if (event == ftxui::Event::Character('/')) {
    begin_search(state);
    return true;
  }
  if (event == ftxui::Event::n) {
    jump_to_search_match(state, loupe::SearchDirection::Forward, false);
    return true;
  }
  if (event == ftxui::Event::N) {
    jump_to_search_match(state, loupe::SearchDirection::Backward, false);
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
    move_down(state, kPageMove);
    return true;
  }
  if (event == ftxui::Event::PageUp) {
    move_up(state, kPageMove);
    return true;
  }
  if (event == ftxui::Event::r) {
    reload(state);
    return true;
  }
  return false;
}

bool
handle_browser_search_event(BrowserState &state, const ftxui::Event &event) {
  if (event == ftxui::Event::Escape) {
    cancel_browser_search(state);
    return true;
  }
  if (event == ftxui::Event::Return) {
    commit_browser_search(state);
    return true;
  }
  if (event == ftxui::Event::Backspace) {
    if (!state.search_input.empty()) {
      state.search_input.pop_back();
    }
    update_browser_search_preview(state);
    return true;
  }
  if (event == ftxui::Event::CtrlU) {
    state.search_input.clear();
    update_browser_search_preview(state);
    return true;
  }
  if (event.is_character()) {
    state.search_input += event.character();
    update_browser_search_preview(state);
    return true;
  }
  return true;
}

void open_selected_file(ApplicationState &state) {
  const BrowserEntry *entry = selected_browser_entry(state.browser);
  if (entry == nullptr) {
    state.browser.status = "no file selected";
    return;
  }

  state.viewer = load_viewer_state(entry->path, state.format);
  state.showing_browser = false;
}

bool handle_browser_event(ApplicationState &state, ftxui::Event event,
                          const ftxui::Closure &quit) {
  BrowserState &browser = state.browser;
  if (const int rows = wheel_rows(event); rows != 0) {
    loupe::scroll_by_rows(browser.scroll, rows);
    return true;
  }
  if (browser.search_active) {
    return handle_browser_search_event(browser, event);
  }
  if (event == ftxui::Event::Character('/')) {
    begin_browser_search(browser);
    return true;
  }
  if (event == ftxui::Event::Return) {
    open_selected_file(state);
    return true;
  }
  if (event == ftxui::Event::q || event == ftxui::Event::Escape) {
    quit();
    return true;
  }
  if (event == ftxui::Event::j || event == ftxui::Event::ArrowDown) {
    move_browser_down(browser, 1);
    return true;
  }
  if (event == ftxui::Event::k || event == ftxui::Event::ArrowUp) {
    move_browser_up(browser, 1);
    return true;
  }
  if (event == ftxui::Event::PageDown) {
    move_browser_down(browser, kPageMove);
    return true;
  }
  if (event == ftxui::Event::PageUp) {
    move_browser_up(browser, kPageMove);
    return true;
  }
  if (event == ftxui::Event::r) {
    refresh_browser(browser);
    return true;
  }
  return false;
}

ftxui::Element render_app(ApplicationState &state) {
  if (state.showing_browser) {
    return render_browser(state.browser);
  }
  return render(state.viewer, state.browser_available);
}

bool handle_app_event(ApplicationState &state, ftxui::Event event,
                      const ftxui::Closure &quit) {
  if (state.showing_browser) {
    return handle_browser_event(state, std::move(event), quit);
  }
  if (state.browser_available
      && !state.viewer.search_active
      && event == ftxui::Event::b) {
    state.browser.status = "returned from " + short_path(state.viewer.path);
    state.showing_browser = true;
    return true;
  }
  return handle_event(state.viewer, std::move(event), quit);
}

std::filesystem::path current_directory_path() {
  std::error_code error;
  const auto current = std::filesystem::current_path(error);
  if (!error) {
    return current;
  }
  return ".";
}

bool is_directory_path(const std::filesystem::path &path) {
  std::error_code error;
  const bool directory = std::filesystem::is_directory(path, error);
  return !error && directory;
}
} // namespace

int main(int argc, char **argv) {
  const CliParseResult cli = parse_cli_args(argc, argv);
  if (!cli.run) {
    return cli.exit_code;
  }

  const bool path_was_omitted = cli.options.path.empty();
  const std::filesystem::path start_path =
      path_was_omitted ? current_directory_path() : cli.options.path;

  ApplicationState state;
  state.format = *cli.options.format;
  if (path_was_omitted || is_directory_path(start_path)) {
    state.browser.root = start_path;
    state.browser_available = true;
    state.showing_browser = true;
    refresh_browser(state.browser);
  } else {
    state.viewer = load_viewer_state(start_path, state.format);
  }

  auto screen = ftxui::App::Fullscreen();
  screen.TrackMouse(true);
  const ftxui::Closure quit = screen.ExitLoopClosure();

  auto component = ftxui::Renderer([&] { return render_app(state); });
  component |= ftxui::CatchEvent([&](ftxui::Event event) {
    return handle_app_event(state, std::move(event), quit);
  });

  {
    loupe::ScopedSynchronizedOutput synchronized_output(std::cout);
    screen.Loop(component);
  }
  return EXIT_SUCCESS;
}
