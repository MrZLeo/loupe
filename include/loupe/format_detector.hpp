#ifndef LOUPE_FORMAT_DETECTOR_HPP_
#define LOUPE_FORMAT_DETECTOR_HPP_

#include <optional>
#include <string_view>

#include "loupe/log_format.hpp"

namespace loupe {

// Sniffs the first lines of a JSONL document and votes on which native
// session format it follows. The four native formats use disjoint
// top-level `type` vocabularies, so voting is decisive; structural hints
// (Claude Code's sessionId/uuid pair, Codex's timestamp+payload envelope)
// cover records whose `type` is missing or unrecognized.
//
// Never returns Generic: the lossy compatibility path stays explicitly
// selected, so unknown content yields std::nullopt instead of a guess.
std::optional<LogFormat> detect_log_format(std::string_view content);

} // namespace loupe

#endif // LOUPE_FORMAT_DETECTOR_HPP_
