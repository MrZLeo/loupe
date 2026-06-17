#ifndef PROJECT_LOG_PARSER_HPP_
#define PROJECT_LOG_PARSER_HPP_

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "project/log_message.hpp"

namespace agentlens {
struct ParseResult {
  std::vector<LogMessage> messages;
  std::vector<std::string> errors;
};

ParseResult parse_log_content(std::string_view content);
ParseResult parse_log_file(const std::filesystem::path &path);
} // namespace agentlens

#endif // PROJECT_LOG_PARSER_HPP_
