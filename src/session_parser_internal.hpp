#ifndef LOUPE_SESSION_PARSER_INTERNAL_HPP_
#define LOUPE_SESSION_PARSER_INTERNAL_HPP_

#include <string_view>

#include "loupe/session_ir.hpp"

namespace loupe::detail {

SessionParseResult parse_pi_session(std::string_view content);
SessionParseResult parse_codex_rollout(std::string_view content);
SessionParseResult parse_claudecode_transcript(std::string_view content);
SessionParseResult parse_generic_session(std::string_view content);

void add_diagnostic(SessionParseResult &result, DiagnosticSeverity severity,
                    DiagnosticCode code, std::string message,
                    std::size_t source_line = 0);

RecordIR make_invalid_record(std::size_t sequence, std::size_t source_line,
                             std::string_view raw);

} // namespace loupe::detail

#endif // LOUPE_SESSION_PARSER_INTERNAL_HPP_
