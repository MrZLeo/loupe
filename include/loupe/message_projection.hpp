#ifndef LOUPE_MESSAGE_PROJECTION_HPP_
#define LOUPE_MESSAGE_PROJECTION_HPP_

#include <optional>
#include <string>
#include <vector>

#include "loupe/log_message.hpp"
#include "loupe/session_ir.hpp"

namespace loupe {

struct DisplayOptions {
  std::optional<std::string> leaf_id;
  bool show_reasoning{false};
  bool show_metadata{false};
  bool show_unknown{false};
  bool show_compaction{true};
};

std::vector<LogMessage>
make_display_messages(const SessionIR &session,
                      const DisplayOptions &options = {});

std::string format_diagnostic(const Diagnostic &diagnostic);

std::string severity_name(DiagnosticSeverity severity);

} // namespace loupe

#endif // LOUPE_MESSAGE_PROJECTION_HPP_
