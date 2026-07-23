#ifndef PROJECT_LOG_MESSAGE_HPP_
#define PROJECT_LOG_MESSAGE_HPP_

#include <cstddef>
#include <string>
#include <vector>

namespace loupe {
struct LogMessage {
  std::string role{"unknown"};
  std::string content;
  std::vector<std::string> annotations;
  std::string timestamp;
  std::string raw_type;
  std::size_t source_line{0};
};
} // namespace loupe

#endif // PROJECT_LOG_MESSAGE_HPP_
