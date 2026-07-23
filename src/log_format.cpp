#include "loupe/log_format.hpp"

namespace loupe {

std::optional<LogFormat> parse_log_format(std::string_view value) {
  if (value == "pi") {
    return LogFormat::Pi;
  }
  if (value == "codex") {
    return LogFormat::Codex;
  }
  if (value == "claudecode" || value == "claude-code") {
    return LogFormat::ClaudeCode;
  }
  if (value == "generic") {
    return LogFormat::Generic;
  }
  return std::nullopt;
}

std::string_view log_format_name(LogFormat format) {
  switch (format) {
  case LogFormat::Pi:
    return "pi";
  case LogFormat::Codex:
    return "codex";
  case LogFormat::ClaudeCode:
    return "claudecode";
  case LogFormat::Generic:
    return "generic";
  }
  return "unknown";
}

} // namespace loupe
