#include "loupe/message_projection.hpp"
#include "loupe/session_parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

template <typename Event>
std::vector<const Event *> events_of(const loupe::SessionIR &session) {
  std::vector<const Event *> events;
  for (const auto &record : session.records) {
    for (const auto &event : record.events) {
      if (const auto *value = std::get_if<Event>(&event.payload)) {
        events.push_back(value);
      }
    }
  }
  return events;
}

bool has_diagnostic(const loupe::SessionParseResult &parsed,
                    loupe::DiagnosticCode code) {
  return std::any_of(parsed.diagnostics.begin(), parsed.diagnostics.end(),
                     [code](const loupe::Diagnostic &diagnostic) {
                       return diagnostic.code == code;
                     });
}

bool
has_diagnostic(const loupe::SessionParseResult &parsed,
               loupe::DiagnosticCode code, loupe::DiagnosticSeverity severity) {
  return std::any_of(parsed.diagnostics.begin(), parsed.diagnostics.end(),
                     [code, severity](const loupe::Diagnostic &diagnostic) {
                       return diagnostic.code == code
                           && diagnostic.severity == severity;
                     });
}

std::string content_text(const std::vector<loupe::ContentBlock> &content) {
  std::string text;
  for (const auto &block : content) {
    if (const auto *value = std::get_if<loupe::TextContent>(&block)) {
      text += value->text;
    } else if (const auto *value = std::get_if<loupe::UnknownContent>(&block)) {
      text += value->json;
    }
  }
  return text;
}

const loupe::ToolCallEvent *
tool_call_named(const loupe::SessionIR &session, std::string_view name) {
  const auto calls = events_of<loupe::ToolCallEvent>(session);
  const auto call =
      std::find_if(calls.begin(), calls.end(), [name](const auto *candidate) {
        return candidate->name == name;
      });
  return call == calls.end() ? nullptr : *call;
}

const loupe::ToolResultEvent *
tool_result_named(const loupe::SessionIR &session, std::string_view name) {
  const auto results = events_of<loupe::ToolResultEvent>(session);
  const auto result = std::find_if(
      results.begin(), results.end(),
      [name](const auto *candidate) { return candidate->name == name; });
  return result == results.end() ? nullptr : *result;
}

const loupe::ExecutionEvent *
execution_event(const loupe::SessionIR &session,
                loupe::ExecutionSubject subject, loupe::ExecutionPhase phase,
                std::string_view native_type = {}) {
  const auto executions = events_of<loupe::ExecutionEvent>(session);
  const auto execution = std::find_if(
      executions.begin(), executions.end(),
      [subject, phase, native_type](const auto *candidate) {
        return candidate->subject == subject
            && candidate->phase == phase
            && (native_type.empty() || candidate->native_type == native_type);
      });
  return execution == executions.end() ? nullptr : *execution;
}

std::size_t message_count(const std::vector<loupe::LogMessage> &messages,
                          std::string_view content) {
  return static_cast<std::size_t>(
      std::count_if(messages.begin(), messages.end(),
                    [content](const loupe::LogMessage &message) {
                      return message.content == content;
                    }));
}

std::size_t annotation_count(const std::vector<loupe::LogMessage> &messages,
                             std::string_view part) {
  std::size_t count = 0;
  for (const auto &message : messages) {
    count += static_cast<std::size_t>(
        std::count_if(message.annotations.begin(), message.annotations.end(),
                      [part](const std::string &annotation) {
                        return annotation.find(part) != std::string::npos;
                      }));
  }
  return count;
}

} // namespace

TEST_CASE(
    "parse a complete Codex Exec stream without duplicating item semantics",
    "[codex_exec_parser]") {
  const auto parsed = loupe::parse_session_content(
      R"({"type":"thread.started","thread_id":"thread-1"}
{"type":"turn.started"}
{"type":"item.started","item":{"id":"item_0","type":"command_execution","command":"printf ok","aggregated_output":"","exit_code":null,"status":"in_progress"}}
{"type":"item.completed","item":{"id":"item_0","type":"command_execution","command":"printf ok","aggregated_output":"ok","exit_code":0,"status":"completed"}}
{"type":"item.completed","item":{"id":"item_1","type":"agent_message","text":"done"}}
{"type":"turn.completed","usage":{"input_tokens":100,"cached_input_tokens":80,"cache_write_input_tokens":7,"output_tokens":20,"reasoning_output_tokens":5}})",
      loupe::LogFormat::CodexExec);

  REQUIRE_FALSE(parsed.has_fatal_error());
  REQUIRE(parsed.diagnostics.empty());
  REQUIRE(parsed.session.format == loupe::LogFormat::CodexExec);
  REQUIRE(parsed.session.session_id == "thread-1");
  REQUIRE(parsed.session.records.size() == 6);
  REQUIRE(parsed.session.created_at.empty());
  REQUIRE(parsed.session.source_version.empty());

  for (std::size_t index = 0; index < parsed.session.records.size(); ++index) {
    const auto &record = parsed.session.records[index];
    REQUIRE(record.sequence == index);
    REQUIRE(record.source_line == index + 1);
    REQUIRE(record.native_id.empty());
    REQUIRE(record.timestamp.empty());
    REQUIRE_FALSE(record.raw_json.empty());
  }

  const auto calls = events_of<loupe::ToolCallEvent>(parsed.session);
  const auto results = events_of<loupe::ToolResultEvent>(parsed.session);
  const auto messages = events_of<loupe::MessageEvent>(parsed.session);
  const auto usages = events_of<loupe::UsageEvent>(parsed.session);
  REQUIRE(calls.size() == 1);
  REQUIRE(results.size() == 1);
  REQUIRE(messages.size() == 1);
  REQUIRE(usages.size() == 1);

  REQUIRE(calls.front()->name == "command_execution");
  REQUIRE(calls.front()->name_space == "codex");
  REQUIRE(calls.front()->input.find("printf ok") != std::string::npos);
  REQUIRE_FALSE(calls.front()->input_is_json);
  REQUIRE_FALSE(calls.front()->call_id.empty());

  REQUIRE(results.front()->name == "command_execution");
  REQUIRE(results.front()->call_id == calls.front()->call_id);
  REQUIRE(content_text(results.front()->output) == "ok");
  REQUIRE_FALSE(results.front()->is_error);
  REQUIRE(results.front()->exit_code == 0);

  REQUIRE(messages.front()->role == loupe::Role::Assistant);
  REQUIRE(messages.front()->raw_role == "assistant");
  REQUIRE(content_text(messages.front()->content) == "done");

  REQUIRE(usages.front()->scope == loupe::UsageScope::Unknown);
  REQUIRE(usages.front()->input_tokens == 100);
  REQUIRE(usages.front()->cached_input_tokens == 80);
  REQUIRE(usages.front()->cache_write_tokens == 7);
  REQUIRE(usages.front()->output_tokens == 20);
  REQUIRE(usages.front()->reasoning_tokens == 5);
  REQUIRE_FALSE(usages.front()->total_tokens.has_value());

  const auto displayed = loupe::make_display_messages(parsed.session);
  REQUIRE(message_count(displayed, "ok") == 1);
  REQUIRE(message_count(displayed, "done") == 1);
  REQUIRE(annotation_count(displayed, "call codex::command_execution") == 1);
  REQUIRE(annotation_count(displayed, "result for command_execution") == 1);
  REQUIRE(annotation_count(displayed, "usage input=100") == 1);
}

TEST_CASE("map every Codex Exec item kind into loss-aware semantic events",
          "[codex_exec_parser]") {
  const auto parsed = loupe::parse_session_content(
      R"({"type":"thread.started","thread_id":"thread-items"}
{"type":"turn.started"}
{"type":"item.completed","item":{"id":"message","type":"agent_message","text":"answer"}}
{"type":"item.completed","item":{"id":"reasoning","type":"reasoning","text":"inspect first"}}
{"type":"item.completed","item":{"id":"command","type":"command_execution","command":"pwd","aggregated_output":"/repo","exit_code":0,"status":"completed"}}
{"type":"item.completed","item":{"id":"patch","type":"file_change","changes":[{"path":"a.txt","kind":"add"},{"path":"b.txt","kind":"update"}],"status":"completed"}}
{"type":"item.completed","item":{"id":"mcp","type":"mcp_tool_call","server":"filesystem","tool":"read","arguments":{"path":"a.txt"},"result":{"content":[{"type":"text","text":"mcp ok"}],"_meta":{"source":"fixture"},"structured_content":{"answer":42}},"error":null,"status":"completed"}}
{"type":"item.completed","item":{"id":"collab","type":"collab_tool_call","tool":"spawn_agent","sender_thread_id":"thread-items","receiver_thread_ids":["child-1"],"prompt":"inspect tests","agents_states":{"child-1":{"status":"completed","message":"done"}},"status":"completed"}}
{"type":"item.completed","item":{"id":"web","type":"web_search","query":"Codex Exec","action":{"type":"open_page","url":"https://example.test"}}}
{"type":"item.updated","item":{"id":"todo","type":"todo_list","items":[{"text":"inspect","completed":true},{"text":"test","completed":false}]}}
{"type":"item.completed","item":{"id":"todo","type":"todo_list","items":[{"text":"inspect","completed":true},{"text":"test","completed":true}]}}
{"type":"item.completed","item":{"id":"warning","type":"error","message":"non-fatal warning"}}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}})",
      loupe::LogFormat::CodexExec);

  REQUIRE_FALSE(parsed.has_fatal_error());

  const auto messages = events_of<loupe::MessageEvent>(parsed.session);
  const auto reasoning = events_of<loupe::ReasoningEvent>(parsed.session);
  REQUIRE(messages.size() == 1);
  REQUIRE(content_text(messages.front()->content) == "answer");
  REQUIRE(reasoning.size() == 1);
  REQUIRE(reasoning.front()->content == "inspect first");

  const auto *command = tool_call_named(parsed.session, "command_execution");
  const auto *command_result =
      tool_result_named(parsed.session, "command_execution");
  REQUIRE(command != nullptr);
  REQUIRE(command_result != nullptr);
  REQUIRE(command_result->call_id == command->call_id);
  REQUIRE(content_text(command_result->output) == "/repo");

  const auto *file_change = tool_call_named(parsed.session, "file_change");
  const auto *file_result = tool_result_named(parsed.session, "file_change");
  REQUIRE(file_change != nullptr);
  REQUIRE(file_result != nullptr);
  REQUIRE(file_change->input.find("a.txt") != std::string::npos);
  REQUIRE(file_change->input.find("b.txt") != std::string::npos);
  REQUIRE_FALSE(file_result->is_error);

  const auto *mcp = tool_call_named(parsed.session, "read");
  const auto *mcp_result = tool_result_named(parsed.session, "read");
  REQUIRE(mcp != nullptr);
  REQUIRE(mcp_result != nullptr);
  REQUIRE(mcp->name_space == "filesystem");
  REQUIRE(mcp->input.find("a.txt") != std::string::npos);
  REQUIRE_FALSE(mcp_result->output.empty());
  REQUIRE(content_text(mcp_result->output).find("mcp ok") != std::string::npos);
  REQUIRE(content_text(mcp_result->output).find("42") != std::string::npos);

  const auto *collab = tool_call_named(parsed.session, "spawn_agent");
  const auto *collab_result =
      tool_result_named(parsed.session, "spawn_agent");
  REQUIRE(collab != nullptr);
  REQUIRE(collab_result != nullptr);
  REQUIRE(collab->name_space == "codex.collab");
  REQUIRE(collab->input.find("inspect tests") != std::string::npos);
  REQUIRE(content_text(collab_result->output).find("child-1")
          != std::string::npos);

  const auto *web = tool_call_named(parsed.session, "web_search");
  REQUIRE(web != nullptr);
  REQUIRE(web->name_space == "codex");
  REQUIRE(web->input.find("Codex Exec") != std::string::npos);
  REQUIRE(web->input.find("https://example.test") != std::string::npos);
  REQUIRE(events_of<loupe::ToolResultEvent>(parsed.session).size() == 4);

  const auto metadata = events_of<loupe::MetadataEvent>(parsed.session);
  REQUIRE(std::any_of(metadata.begin(), metadata.end(), [](const auto *event) {
    return event->name == "codex_exec.todo_list"
        && event->value.find("\"completed\":true") != std::string::npos;
  }));

  const auto *warning =
      execution_event(parsed.session, loupe::ExecutionSubject::Item,
                      loupe::ExecutionPhase::Completed, "error");
  REQUIRE(warning != nullptr);
  REQUIRE(warning->message == "non-fatal warning");
}

TEST_CASE("support self-contained completed Codex Exec tool items",
          "[codex_exec_parser]") {
  const auto parsed = loupe::parse_session_content(
      R"({"type":"thread.started","thread_id":"thread-completed-only"}
{"type":"turn.started"}
{"type":"item.completed","item":{"id":"item_0","type":"command_execution","command":"echo complete","aggregated_output":"complete","exit_code":0,"status":"completed"}}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}})",
      loupe::LogFormat::CodexExec);

  REQUIRE_FALSE(parsed.has_fatal_error());
  REQUIRE_FALSE(
      has_diagnostic(parsed, loupe::DiagnosticCode::InvalidLifecycle));

  const auto calls = events_of<loupe::ToolCallEvent>(parsed.session);
  const auto results = events_of<loupe::ToolResultEvent>(parsed.session);
  REQUIRE(calls.size() == 1);
  REQUIRE(results.size() == 1);
  REQUIRE(calls.front()->call_id == results.front()->call_id);
  REQUIRE(calls.front()->input.find("echo complete") != std::string::npos);
  REQUIRE(content_text(results.front()->output) == "complete");
}

TEST_CASE("accept null prompts for Codex Exec collab tools",
          "[codex_exec_parser]") {
  const auto parsed = loupe::parse_session_content(
      R"({"type":"thread.started","thread_id":"thread-optional-prompts"}
{"type":"turn.started"}
{"type":"item.completed","item":{"id":"wait","type":"collab_tool_call","tool":"wait","sender_thread_id":"thread-optional-prompts","receiver_thread_ids":["child-1"],"prompt":null,"agents_states":{"child-1":{"status":"completed"}},"status":"completed"}}
{"type":"item.completed","item":{"id":"close","type":"collab_tool_call","tool":"close_agent","sender_thread_id":"thread-optional-prompts","receiver_thread_ids":["child-1"],"prompt":null,"agents_states":{"child-1":{"status":"shutdown"}},"status":"completed"}}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}})",
      loupe::LogFormat::CodexExec);

  REQUIRE_FALSE(parsed.has_fatal_error());
  REQUIRE(parsed.diagnostics.empty());

  const auto calls = events_of<loupe::ToolCallEvent>(parsed.session);
  REQUIRE(calls.size() == 2);
  REQUIRE(calls[0]->name == "wait");
  REQUIRE(calls[0]->input.find("\"prompt\"") == std::string::npos);
  REQUIRE(calls[1]->name == "close_agent");
  REQUIRE(calls[1]->input.find("\"prompt\"") == std::string::npos);
}

TEST_CASE("preserve every JSON shape accepted by MCP arguments",
          "[codex_exec_parser]") {
  const auto parsed = loupe::parse_session_content(
      R"({"type":"thread.started","thread_id":"thread-mcp-arguments"}
{"type":"turn.started"}
{"type":"item.completed","item":{"id":"object","type":"mcp_tool_call","server":"test","tool":"object","arguments":{"value":1},"result":null,"error":null,"status":"completed"}}
{"type":"item.completed","item":{"id":"scalar","type":"mcp_tool_call","server":"test","tool":"scalar","arguments":7,"result":null,"error":null,"status":"completed"}}
{"type":"item.completed","item":{"id":"null","type":"mcp_tool_call","server":"test","tool":"null","arguments":null,"result":null,"error":null,"status":"completed"}}
{"type":"turn.completed","usage":{"input_tokens":1,"output_tokens":1}})",
      loupe::LogFormat::CodexExec);

  REQUIRE_FALSE(parsed.has_fatal_error());
  const auto calls = events_of<loupe::ToolCallEvent>(parsed.session);
  REQUIRE(calls.size() == 3);
  REQUIRE(calls[0]->input.find("\"value\":1") != std::string::npos);
  REQUIRE(calls[1]->input == "7");
  REQUIRE(calls[2]->input == "null");
  REQUIRE(std::all_of(calls.begin(), calls.end(),
                      [](const auto *call) { return call->input_is_json; }));
}

TEST_CASE("merge Codex Exec item snapshots without duplicating tool semantics",
          "[codex_exec_parser]") {
  SECTION("latest fields win and absent completed fields are inherited") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-snapshots"}
{"type":"turn.started"}
{"type":"item.started","item":{"id":"item_0","type":"command_execution","command":"echo inherited","aggregated_output":"","exit_code":null,"status":"in_progress"}}
{"type":"item.updated","item":{"id":"item_0","type":"command_execution","aggregated_output":"partial","status":"in_progress"}}
{"type":"item.completed","item":{"id":"item_0","type":"command_execution","aggregated_output":"final","exit_code":0,"status":"completed"}}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    const auto calls = events_of<loupe::ToolCallEvent>(parsed.session);
    const auto results = events_of<loupe::ToolResultEvent>(parsed.session);
    REQUIRE(calls.size() == 1);
    REQUIRE(results.size() == 1);
    REQUIRE(calls.front()->input.find("echo inherited") != std::string::npos);
    REQUIRE(content_text(results.front()->output) == "final");
    REQUIRE(results.front()->call_id == calls.front()->call_id);
  }

  SECTION("duplicate completion is diagnosed and remains idempotent") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-duplicate"}
{"type":"turn.started"}
{"type":"item.completed","item":{"id":"item_0","type":"command_execution","command":"echo once","aggregated_output":"once","exit_code":0,"status":"completed"}}
{"type":"item.completed","item":{"id":"item_0","type":"command_execution","command":"echo once","aggregated_output":"once","exit_code":0,"status":"completed"}}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    REQUIRE(has_diagnostic(parsed, loupe::DiagnosticCode::InvalidLifecycle,
                           loupe::DiagnosticSeverity::Warning));
    REQUIRE(events_of<loupe::ToolCallEvent>(parsed.session).size() == 1);
    REQUIRE(events_of<loupe::ToolResultEvent>(parsed.session).size() == 1);
  }

  SECTION("an explicit null clears a previously observed optional field") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-null-clear"}
{"type":"turn.started"}
{"type":"item.started","item":{"id":"item_0","type":"command_execution","command":"echo partial","aggregated_output":"partial","exit_code":null,"status":"in_progress"}}
{"type":"item.updated","item":{"id":"item_0","type":"command_execution","aggregated_output":null}}
{"type":"item.completed","item":{"id":"item_0","type":"command_execution","exit_code":0,"status":"completed"}}
{"type":"turn.completed","usage":{"input_tokens":1,"output_tokens":1}})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    const auto results = events_of<loupe::ToolResultEvent>(parsed.session);
    REQUIRE(results.size() == 1);
    REQUIRE(content_text(results.front()->output).empty());
  }

  SECTION("later snapshots refresh a previously emitted tool call") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-call-refresh"}
{"type":"turn.started"}
{"type":"item.started","item":{"id":"item_0","type":"command_execution","command":"echo old","aggregated_output":"","exit_code":null,"status":"in_progress"}}
{"type":"item.updated","item":{"id":"item_0","type":"command_execution","command":"echo new"}}
{"type":"item.completed","item":{"id":"item_0","type":"command_execution","aggregated_output":"new","exit_code":0,"status":"completed"}}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    const auto calls = events_of<loupe::ToolCallEvent>(parsed.session);
    REQUIRE(calls.size() == 1);
    REQUIRE(calls.front()->input == "echo new");
  }

  SECTION("an item type change is diagnosed without duplicating the old type") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-type-change"}
{"type":"turn.started"}
{"type":"item.started","item":{"id":"item_0","type":"command_execution","command":"echo command","aggregated_output":"","exit_code":null,"status":"in_progress"}}
{"type":"item.completed","item":{"id":"item_0","type":"mcp_tool_call","server":"filesystem","tool":"read","arguments":{"path":"a.txt"},"result":{"content":[{"type":"text","text":"ok"}],"structured_content":null},"error":null,"status":"completed"}}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    REQUIRE(has_diagnostic(parsed, loupe::DiagnosticCode::InvalidLifecycle));
    REQUIRE(has_diagnostic(parsed, loupe::DiagnosticCode::IncompleteStream));
    const auto calls = events_of<loupe::ToolCallEvent>(parsed.session);
    const auto results = events_of<loupe::ToolResultEvent>(parsed.session);
    REQUIRE(static_cast<std::size_t>(std::count_if(calls.begin(), calls.end(),
                                                   [](const auto *call) {
                                                     return call->name
                                                         == "command_execution";
                                                   }))
            == 1);
    REQUIRE(results.empty());
  }
}

TEST_CASE("map failed and declined Codex Exec commands to error results",
          "[codex_exec_parser]") {
  const auto parsed = loupe::parse_session_content(
      R"({"type":"thread.started","thread_id":"thread-command-failures"}
{"type":"turn.started"}
{"type":"item.completed","item":{"id":"failed","type":"command_execution","command":"exit 7","aggregated_output":"failed output","exit_code":-7,"status":"failed"}}
{"type":"item.completed","item":{"id":"declined","type":"command_execution","command":"dangerous command","aggregated_output":"","exit_code":null,"status":"declined"}}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}})",
      loupe::LogFormat::CodexExec);

  REQUIRE_FALSE(parsed.has_fatal_error());
  const auto results = events_of<loupe::ToolResultEvent>(parsed.session);
  REQUIRE(results.size() == 2);
  REQUIRE(results[0]->is_error);
  REQUIRE(results[0]->exit_code == -7);
  REQUIRE(content_text(results[0]->output) == "failed output");
  REQUIRE(results[1]->is_error);
  REQUIRE_FALSE(results[1]->exit_code.has_value());

  const auto displayed = loupe::make_display_messages(parsed.session);
  REQUIRE(message_count(displayed, "failed output") == 1);
  REQUIRE(static_cast<std::size_t>(
              std::count_if(displayed.begin(), displayed.end(),
                            [](const loupe::LogMessage &message) {
                              return message.role == "tool";
                            }))
          == 2);
  REQUIRE(std::none_of(displayed.begin(), displayed.end(),
                       [](const loupe::LogMessage &message) {
                         return message.role == "system";
                       }));
}

TEST_CASE("quarantine duplicate Codex Exec terminal events",
          "[codex_exec_parser]") {
  const auto parsed = loupe::parse_session_content(
      R"({"type":"thread.started","thread_id":"thread-duplicates"}
{"type":"turn.started"}
{"type":"item.completed","item":{"id":"warning","type":"error","message":"warn once"}}
{"type":"item.completed","item":{"id":"warning","type":"error","message":"warn twice"}}
{"type":"item.completed","item":{"id":"answer","type":"agent_message","text":"done"}}
{"type":"turn.completed","usage":{"input_tokens":2,"output_tokens":1}}
{"type":"turn.failed","error":{"message":"late failure"}})",
      loupe::LogFormat::CodexExec);

  REQUIRE_FALSE(parsed.has_fatal_error());
  REQUIRE(has_diagnostic(parsed, loupe::DiagnosticCode::InvalidLifecycle,
                         loupe::DiagnosticSeverity::Warning));
  REQUIRE(events_of<loupe::UsageEvent>(parsed.session).size() == 1);
  REQUIRE(execution_event(parsed.session, loupe::ExecutionSubject::Turn,
                          loupe::ExecutionPhase::Failed)
          == nullptr);

  const auto displayed = loupe::make_display_messages(parsed.session);
  REQUIRE(message_count(displayed, "warn once") == 1);
  REQUIRE(message_count(displayed, "warn twice") == 0);
  REQUIRE(message_count(displayed, "late failure") == 0);
  REQUIRE(message_count(displayed, "done") == 1);
}

TEST_CASE("scope repeated Codex Exec item ids to their invocation",
          "[codex_exec_parser]") {
  SECTION("resuming the same thread does not merge reused item ids") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-resume"}
{"type":"turn.started"}
{"type":"item.completed","item":{"id":"item_0","type":"command_execution","command":"echo first","aggregated_output":"first","exit_code":0,"status":"completed"}}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}}
{"type":"thread.started","thread_id":"thread-resume"}
{"type":"turn.started"}
{"type":"item.completed","item":{"id":"item_0","type":"command_execution","command":"echo second","aggregated_output":"second","exit_code":0,"status":"completed"}}
{"type":"turn.completed","usage":{"input_tokens":2,"cached_input_tokens":0,"output_tokens":2,"reasoning_output_tokens":0}})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    REQUIRE_FALSE(
        has_diagnostic(parsed, loupe::DiagnosticCode::DuplicateNativeId));

    const auto calls = events_of<loupe::ToolCallEvent>(parsed.session);
    const auto results = events_of<loupe::ToolResultEvent>(parsed.session);
    REQUIRE(calls.size() == 2);
    REQUIRE(results.size() == 2);
    REQUIRE(calls[0]->input.find("first") != std::string::npos);
    REQUIRE(calls[1]->input.find("second") != std::string::npos);
    REQUIRE(calls[0]->call_id != calls[1]->call_id);
    REQUIRE(content_text(results[0]->output) == "first");
    REQUIRE(content_text(results[1]->output) == "second");

    const auto executions = events_of<loupe::ExecutionEvent>(parsed.session);
    std::set<std::string> runs;
    for (const auto *execution : executions) {
      if (execution->subject == loupe::ExecutionSubject::Thread
          && execution->phase == loupe::ExecutionPhase::Started) {
        runs.insert(execution->correlation_id);
      }
    }
    REQUIRE(runs.size() == 2);
  }

  SECTION("a concatenated different thread starts a new run") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-a"}
{"type":"turn.started"}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}}
{"type":"thread.started","thread_id":"thread-b"}
{"type":"turn.started"}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    REQUIRE(parsed.session.session_id == "thread-a");
    REQUIRE(
        has_diagnostic(parsed, loupe::DiagnosticCode::InconsistentSessionId));

    const auto executions = events_of<loupe::ExecutionEvent>(parsed.session);
    std::set<std::string> runs;
    for (const auto *execution : executions) {
      if (execution->subject == loupe::ExecutionSubject::Thread
          && execution->phase == loupe::ExecutionPhase::Started) {
        runs.insert(execution->correlation_id);
      }
    }
    REQUIRE(runs.size() == 2);
  }

  SECTION("a leading recovered turn does not collide with the native run") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"turn.started"}
{"type":"turn.completed","usage":{"input_tokens":1,"output_tokens":1}}
{"type":"thread.started","thread_id":"thread-after-fragment"}
{"type":"turn.started"}
{"type":"turn.completed","usage":{"input_tokens":2,"output_tokens":1}})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    const auto executions = events_of<loupe::ExecutionEvent>(parsed.session);
    std::set<std::string> turns;
    for (const auto *execution : executions) {
      if (execution->subject == loupe::ExecutionSubject::Turn
          && execution->phase == loupe::ExecutionPhase::Started) {
        turns.insert(execution->correlation_id);
      }
    }
    REQUIRE(turns.size() == 2);
  }
}

TEST_CASE("preserve Codex Exec runtime errors without hiding the transcript",
          "[codex_exec_parser]") {
  SECTION("stream error and failed turn are non-fatal execution events") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-failed"}
{"type":"turn.started"}
{"type":"error","message":"temporary stream error"}
{"type":"turn.failed","error":{"message":"turn failed"}})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    REQUIRE_FALSE(
        has_diagnostic(parsed, loupe::DiagnosticCode::IncompleteStream));

    const auto *stream_error =
        execution_event(parsed.session, loupe::ExecutionSubject::Stream,
                        loupe::ExecutionPhase::Error);
    const auto *turn_failure =
        execution_event(parsed.session, loupe::ExecutionSubject::Turn,
                        loupe::ExecutionPhase::Failed);
    REQUIRE(stream_error != nullptr);
    REQUIRE(stream_error->message == "temporary stream error");
    REQUIRE_FALSE(stream_error->terminal);
    REQUIRE(turn_failure != nullptr);
    REQUIRE(turn_failure->message == "turn failed");
    REQUIRE(turn_failure->terminal);

    const auto displayed = loupe::make_display_messages(parsed.session);
    REQUIRE(message_count(displayed, "temporary stream error") == 1);
    REQUIRE(message_count(displayed, "turn failed") == 1);
  }

  SECTION("failed turn accepts the legacy string error shape") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-failed-string"}
{"type":"turn.started"}
{"type":"turn.failed","error":"string failure"})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    const auto *turn_failure =
        execution_event(parsed.session, loupe::ExecutionSubject::Turn,
                        loupe::ExecutionPhase::Failed);
    REQUIRE(turn_failure != nullptr);
    REQUIRE(turn_failure->message == "string failure");
    REQUIRE(turn_failure->terminal);
  }

  SECTION("completion-only errors are hidden until their terminal wrapper") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-error-lifecycle"}
{"type":"turn.started"}
{"type":"item.started","item":{"id":"warning","type":"error","message":"too early"}}
{"type":"item.completed","item":{"id":"warning","type":"error","message":"final warning"}}
{"type":"turn.completed","usage":{"input_tokens":1,"output_tokens":1}})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    REQUIRE(has_diagnostic(parsed, loupe::DiagnosticCode::InvalidLifecycle,
                           loupe::DiagnosticSeverity::Warning));
    const auto displayed = loupe::make_display_messages(parsed.session);
    REQUIRE(message_count(displayed, "too early") == 0);
    REQUIRE(message_count(displayed, "final warning") == 1);
  }

  SECTION("an item warning before turn start is retained exactly once") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-warning"}
{"type":"item.completed","item":{"id":"warning","type":"error","message":"configuration warning"}}
{"type":"turn.started"}
{"type":"item.completed","item":{"id":"answer","type":"agent_message","text":"continued"}}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    REQUIRE_FALSE(
        has_diagnostic(parsed, loupe::DiagnosticCode::IncompleteStream));
    const auto *warning =
        execution_event(parsed.session, loupe::ExecutionSubject::Item,
                        loupe::ExecutionPhase::Completed, "error");
    REQUIRE(warning != nullptr);
    REQUIRE(warning->message == "configuration warning");

    const auto displayed = loupe::make_display_messages(parsed.session);
    REQUIRE(message_count(displayed, "configuration warning") == 1);
    REQUIRE(message_count(displayed, "continued") == 1);
  }
}

TEST_CASE("preserve unknown Codex Exec extensions and reject wrong formats",
          "[codex_exec_parser]") {
  SECTION("unknown top-level and item types survive beside known events") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-future","future_thread_field":{"enabled":true}}
{"type":"turn.started","future_turn_field":7}
{"type":"future.event","payload":{"value":1}}
{"type":"item.completed","item":{"id":"future-item","type":"future_item","items":{"future":"shape"},"status":7,"future_field":["kept"]}}
{"type":"item.completed","item":{"id":"answer","type":"agent_message","text":"known","future_item_field":true}}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"cache_write_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0,"future_tokens":99},"future_terminal_field":null})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    REQUIRE_FALSE(
        has_diagnostic(parsed, loupe::DiagnosticCode::FormatMismatch));
    REQUIRE_FALSE(
        has_diagnostic(parsed, loupe::DiagnosticCode::InvalidFieldType));
    REQUIRE(events_of<loupe::UnknownEvent>(parsed.session).size() == 2);
    REQUIRE(events_of<loupe::MessageEvent>(parsed.session).size() == 1);
    REQUIRE(events_of<loupe::UsageEvent>(parsed.session).size() == 1);
    REQUIRE(parsed.session.records[2].raw_json.find("future.event")
            != std::string::npos);
    REQUIRE(parsed.session.records[3].raw_json.find("future_field")
            != std::string::npos);
  }

  SECTION("an unknown-only stream is a format mismatch") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"future.event","payload":{"value":1}})",
        loupe::LogFormat::CodexExec);

    REQUIRE(parsed.has_fatal_error());
    REQUIRE(has_diagnostic(parsed, loupe::DiagnosticCode::FormatMismatch,
                           loupe::DiagnosticSeverity::Fatal));
    REQUIRE(events_of<loupe::UnknownEvent>(parsed.session).size() == 1);
  }

  SECTION("Codex session and Exec adapters do not autodetect each other") {
    const auto rollout = loupe::parse_session_content(
        R"({"timestamp":"2026-07-24T00:00:00Z","type":"session_meta","payload":{"id":"thread-1"}})",
        loupe::LogFormat::CodexExec);
    const auto exec = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-1"})",
        loupe::LogFormat::Codex);

    REQUIRE(rollout.has_fatal_error());
    REQUIRE(exec.has_fatal_error());
    REQUIRE(has_diagnostic(rollout, loupe::DiagnosticCode::FormatMismatch));
    REQUIRE(has_diagnostic(exec, loupe::DiagnosticCode::FormatMismatch));
  }
}

TEST_CASE("diagnose malformed Codex Exec fields while preserving raw lines",
          "[codex_exec_parser]") {
  const auto parsed = loupe::parse_session_content(
      R"({"type":"thread.started","thread_id":"thread-malformed"}

[]
{"type":"turn.started"}
{"type":"item.completed","item":{"id":7,"type":"agent_message","text":false}}
{"type":"item.completed","item":"not-an-object"}
{"type":"turn.completed","usage":{"input_tokens":"bad","cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}})",
      loupe::LogFormat::CodexExec);

  REQUIRE_FALSE(parsed.has_fatal_error());
  REQUIRE(parsed.session.records.size() == 6);
  REQUIRE(parsed.session.records[0].source_line == 1);
  REQUIRE(parsed.session.records[1].source_line == 3);
  REQUIRE(parsed.session.records[2].source_line == 4);
  REQUIRE(parsed.session.records[3].source_line == 5);
  REQUIRE(parsed.session.records[4].source_line == 6);
  REQUIRE(parsed.session.records[5].source_line == 7);
  for (const auto &record : parsed.session.records) {
    REQUIRE_FALSE(record.raw_json.empty());
  }

  REQUIRE(has_diagnostic(parsed, loupe::DiagnosticCode::ExpectedObject,
                         loupe::DiagnosticSeverity::Error));
  REQUIRE(has_diagnostic(parsed, loupe::DiagnosticCode::InvalidFieldType,
                         loupe::DiagnosticSeverity::Warning));
}

TEST_CASE("recover incomplete Codex Exec streams at EOF",
          "[codex_exec_parser]") {
  SECTION("a valid pending item without a terminal turn remains usable") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-incomplete"}
{"type":"turn.started"}
{"type":"item.started","item":{"id":"item_0","type":"command_execution","command":"sleep 10","aggregated_output":"","exit_code":null,"status":"in_progress"}})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    REQUIRE(has_diagnostic(parsed, loupe::DiagnosticCode::IncompleteStream,
                           loupe::DiagnosticSeverity::Warning));
    REQUIRE(parsed.session.records.size() == 3);
    REQUIRE(parsed.session.records.back().source_line == 3);
    REQUIRE_FALSE(parsed.session.records.back().raw_json.empty());
    REQUIRE(events_of<loupe::ToolCallEvent>(parsed.session).size() == 1);
  }

  SECTION("a lexically truncated final line keeps preceding records") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-truncated"}
{"type":"turn.started"}
{"type":"item.started")",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    REQUIRE(parsed.session.records.size() == 3);
    REQUIRE(parsed.session.records.back().native_type == "invalid_json");
    REQUIRE(parsed.session.records.back().source_line == 3);
    REQUIRE_FALSE(parsed.session.records.back().raw_json.empty());
    REQUIRE(has_diagnostic(parsed, loupe::DiagnosticCode::InvalidJson));
    REQUIRE(has_diagnostic(parsed, loupe::DiagnosticCode::IncompleteStream));
  }

  SECTION("a complete final line needs no trailing newline") {
    const auto parsed = loupe::parse_session_content(
        R"({"type":"thread.started","thread_id":"thread-complete"}
{"type":"turn.started"}
{"type":"turn.completed","usage":{"input_tokens":1,"cached_input_tokens":0,"output_tokens":1,"reasoning_output_tokens":0}})",
        loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    REQUIRE_FALSE(
        has_diagnostic(parsed, loupe::DiagnosticCode::IncompleteStream));
  }

  SECTION("BOM, CRLF, and blank physical lines preserve logical line numbers") {
    const std::string content =
        "\xEF\xBB\xBF{\"type\":\"thread.started\",\"thread_id\":\"thread-lines\"}\r\n"
        "\r\n"
        "{\"type\":\"turn.started\"}\r\n"
        "{\"type\":\"turn.completed\",\"usage\":{\"input_tokens\":0}}\r\n";
    const auto parsed =
        loupe::parse_session_content(content, loupe::LogFormat::CodexExec);

    REQUIRE_FALSE(parsed.has_fatal_error());
    REQUIRE(parsed.session.records.size() == 3);
    REQUIRE(parsed.session.records[0].source_line == 1);
    REQUIRE(parsed.session.records[1].source_line == 3);
    REQUIRE(parsed.session.records[2].source_line == 4);
    REQUIRE(parsed.session.records[0].raw_json.starts_with("\xEF\xBB\xBF"));
    REQUIRE_FALSE(
        has_diagnostic(parsed, loupe::DiagnosticCode::IncompleteStream));
  }
}
