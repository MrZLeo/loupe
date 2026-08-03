#include "loupe/format_detector.hpp"

#include "json_helpers.hpp"
#include "jsonl_reader.hpp"
#include "loupe/log_format.hpp"

#include <cstddef>
#include <optional>

#include <array>
#include <simdjson.h>
#include <string>
#include <string_view>

namespace loupe {
namespace {

// Scanning more of the file rarely changes the outcome: native formats
// open with a signature record (session/session_meta/thread.started) or
// repeat their type vocabulary within a few lines.
constexpr std::size_t kMaxLines = 64;
constexpr std::size_t kMaxBytes = std::size_t{64} * 1024;

// A first-record hit is the format's signature header, so it outweighs
// votes from later lines.
constexpr int kFirstLineBonus = 2;
constexpr int kStrongVote = 3;
constexpr int kMediumVote = 2;
constexpr int kWeakVote = 1;

// Decisive scores: one strong hit, or several hints agreeing.
constexpr int kWinThreshold = 3;

bool is_codex_exec_type(std::string_view type) {
  static constexpr std::array<std::string_view, 8> kTypes{
      "thread.started", "turn.started", "turn.completed", "turn.failed",
      "item.started",   "item.updated", "item.completed", "error",
  };
  for (const auto known : kTypes) {
    if (type == known) {
      return true;
    }
  }
  // Future item.*/turn.* events stay attributable to the event stream.
  return type.starts_with("item.")
      || type.starts_with("turn.")
      || type.starts_with("thread.");
}

bool is_codex_rollout_type(std::string_view type) {
  static constexpr std::array<std::string_view, 6> kTypes{
      "session_meta", "response_item", "event_msg",
      "compacted",    "turn_context",  "world_state",
  };
  for (const auto known : kTypes) {
    if (type == known) {
      return true;
    }
  }
  return false;
}

bool is_pi_entry_type(std::string_view type) {
  static constexpr std::array<std::string_view, 12> kTypes{
      "message",
      "compaction",
      "branch_summary",
      "custom_message",
      "custom",
      "model_change",
      "thinking_level_change",
      "active_tools_change",
      "label",
      "session_info",
      "leaf",
  };
  for (const auto known : kTypes) {
    if (type == known) {
      return true;
    }
  }
  return false;
}

bool is_claudecode_type(std::string_view type) {
  static constexpr std::array<std::string_view, 16> kTypes{
      "user",
      "assistant",
      "system",
      "summary",
      "ai-title",
      "attachment",
      "file-history-delta",
      "file-history-snapshot",
      "last-prompt",
      "mode",
      "permission-mode",
      "progress",
      "queue-operation",
      "agent-name",
      "rate-limit-event",
  };
  for (const auto known : kTypes) {
    if (type == known) {
      return true;
    }
  }
  return false;
}

struct FormatScores {
  int pi{0};
  int codex{0};
  int codex_exec{0};
  int claudecode{0};

  int best() const {
    int best_score = pi;
    best_score = best_score < codex ? codex : best_score;
    best_score = best_score < codex_exec ? codex_exec : best_score;
    return best_score < claudecode ? claudecode : best_score;
  }
};

} // namespace

std::optional<LogFormat> detect_log_format(std::string_view content) {
  detail::JsonlReader reader{content};
  simdjson::dom::parser parser;
  FormatScores scores;
  bool first_record = true;

  detail::JsonlLine line;
  std::size_t scanned_bytes = 0;
  std::size_t scanned_lines = 0;
  while (scanned_lines < kMaxLines
         && scanned_bytes < kMaxBytes
         && reader.next(line)) {
    scanned_bytes += line.raw.size();
    ++scanned_lines;

    simdjson::dom::element document;
    if (parser.parse(line.json).get(document)
        || document.type() != simdjson::dom::element_type::OBJECT) {
      // Malformed or non-object lines carry no signal; the formats are
      // distinguishable from any handful of intact records.
      continue;
    }

    const int bonus = first_record ? kFirstLineBonus : 0;
    first_record = false;
    const std::string type = detail::string_at(document, "/type").value_or("");

    if (is_codex_exec_type(type)) {
      scores.codex_exec += kStrongVote + bonus;
    }
    if (is_codex_rollout_type(type)) {
      scores.codex += kStrongVote + bonus;
    }
    if (type == "session") {
      scores.pi += kStrongVote + bonus;
    } else if (is_pi_entry_type(type)) {
      scores.pi += kMediumVote;
    }
    if (is_claudecode_type(type)) {
      scores.claudecode += kMediumVote;
    }

    // Structural hints for records with a missing or foreign `type`.
    simdjson::dom::element field;
    if (detail::element_at(document, "/sessionId", field)
        && detail::element_at(document, "/uuid", field)) {
      scores.claudecode += kMediumVote;
    }
    simdjson::dom::element payload;
    if (detail::element_at(document, "/timestamp", field)
        && detail::element_at(document, "/payload", payload)
        && payload.type() == simdjson::dom::element_type::OBJECT) {
      scores.codex += kWeakVote;
    }
    if (type == "session"
        && detail::element_at(document, "/id", field)
        && detail::element_at(document, "/cwd", field)) {
      scores.pi += kWeakVote;
    }
  }

  const int best = scores.best();
  if (best < kWinThreshold) {
    return std::nullopt;
  }
  // A tie means the content genuinely mixes signatures; refuse to guess.
  int winners = 0;
  std::optional<LogFormat> winner;
  const auto consider = [&](int score, LogFormat format) {
    if (score == best) {
      ++winners;
      winner = format;
    }
  };
  consider(scores.pi, LogFormat::Pi);
  consider(scores.codex, LogFormat::Codex);
  consider(scores.codex_exec, LogFormat::CodexExec);
  consider(scores.claudecode, LogFormat::ClaudeCode);
  if (winners != 1) {
    return std::nullopt;
  }
  return winner;
}

} // namespace loupe
