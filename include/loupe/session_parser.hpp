#ifndef LOUPE_SESSION_PARSER_HPP_
#define LOUPE_SESSION_PARSER_HPP_

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "loupe/session_ir.hpp"

namespace loupe {

SessionParseResult
parse_session_content(std::string_view content, LogFormat format);
SessionParseResult
parse_session_file(const std::filesystem::path &path, LogFormat format);

std::vector<Diagnostic> validate_session(const SessionIR &session);

std::vector<std::size_t> select_conversation_records(
    const SessionIR &session,
    std::optional<std::string_view> leaf_id = std::nullopt);

} // namespace loupe

#endif // LOUPE_SESSION_PARSER_HPP_
