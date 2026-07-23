#include "loupe/log_format.hpp"
#include "loupe/message_projection.hpp"
#include "loupe/session_parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <variant>
#include <vector>

TEST_CASE("parse explicit log format names", "[session_ir]") {
  REQUIRE(loupe::parse_log_format("pi") == loupe::LogFormat::Pi);
  REQUIRE(loupe::parse_log_format("codex") == loupe::LogFormat::Codex);
  REQUIRE(loupe::parse_log_format("claudecode")
          == loupe::LogFormat::ClaudeCode);
  REQUIRE(loupe::parse_log_format("generic") == loupe::LogFormat::Generic);
  REQUIRE_FALSE(loupe::parse_log_format("auto").has_value());
}

TEST_CASE("select active branch from parent-linked records", "[session_ir]") {
  loupe::SessionIR session{
      .format = loupe::LogFormat::Pi,
      .active_leaf_id = "right",
      .records =
          {
              loupe::RecordIR{.sequence = 0,
                              .native_id = "root",
                              .native_parent_id = std::nullopt},
              loupe::RecordIR{.sequence = 1,
                              .native_id = "left",
                              .native_parent_id = "root"},
              loupe::RecordIR{.sequence = 2,
                              .native_id = "right",
                              .native_parent_id = "root"},
          },
  };

  const auto selected = loupe::select_conversation_records(session);

  REQUIRE(selected == std::vector<std::size_t>{0, 2});
  REQUIRE(loupe::select_conversation_records(session, "missing").empty());

  session.active_leaf_id = "missing";
  REQUIRE(loupe::select_conversation_records(session).empty());
  const auto diagnostics = loupe::validate_session(session);
  REQUIRE(std::any_of(diagnostics.begin(), diagnostics.end(),
                      [](const loupe::Diagnostic &diagnostic) {
                        return diagnostic.code
                                == loupe::DiagnosticCode::MissingParent
                            && diagnostic.message.find("active conversation")
                                   != std::string::npos;
                      }));
}

TEST_CASE("navigation parent can explicitly override a native edge with root",
          "[session_ir]") {
  const loupe::SessionIR session{
      .format = loupe::LogFormat::Pi,
      .active_leaf_id = "child",
      .records =
          {
              loupe::RecordIR{
                  .sequence = 0,
                  .native_id = "root",
              },
              loupe::RecordIR{
                  .sequence = 1,
                  .native_id = "child",
                  .native_parent_id = "root",
                  .navigation_parent_id = "",
              },
          },
  };

  REQUIRE(loupe::select_conversation_records(session)
          == std::vector<std::size_t>{1});
}

TEST_CASE("validate duplicate ids, missing parents, and empty call ids",
          "[session_ir]") {
  loupe::RecordIR first{
      .sequence = 0,
      .source_line = 1,
      .native_id = "same",
  };
  loupe::append_event(first, loupe::ToolCallEvent{});

  const loupe::SessionIR session{
      .format = loupe::LogFormat::Pi,
      .records =
          {
              std::move(first),
              loupe::RecordIR{
                  .sequence = 1,
                  .source_line = 2,
                  .native_id = "same",
                  .native_parent_id = "missing",
              },
          },
  };

  const auto diagnostics = loupe::validate_session(session);

  REQUIRE(diagnostics.size() == 3);
}

TEST_CASE("project structured events into display messages", "[session_ir]") {
  loupe::RecordIR record{
      .sequence = 0,
      .source_line = 7,
      .native_type = "message",
      .timestamp = "2026-07-23T00:00:00Z",
  };
  loupe::append_event(record,
                      loupe::MessageEvent{
                          .role = loupe::Role::Assistant,
                          .raw_role = "assistant",
                          .content = {loupe::TextContent{.text = "Checking."}},
                      });
  loupe::append_event(record, loupe::ToolCallEvent{
                                  .call_id = "call-1",
                                  .name = "bash",
                                  .input = R"({"cmd":"pwd"})",
                                  .input_is_json = true,
                              });

  const loupe::SessionIR session{
      .format = loupe::LogFormat::Codex,
      .records = {std::move(record)},
  };

  const auto messages = loupe::make_display_messages(session);

  REQUIRE(messages.size() == 1);
  REQUIRE(messages.front().role == "assistant");
  REQUIRE(messages.front().content == "Checking.");
  REQUIRE(messages.front().source_line == 7);
  REQUIRE(messages.front().annotations.size() == 1);
  REQUIRE(messages.front().annotations.front().find("call bash")
          != std::string::npos);
}

TEST_CASE("hide unknown content blocks unless explicitly requested",
          "[session_ir]") {
  loupe::RecordIR record{
      .sequence = 0,
      .source_line = 3,
      .native_type = "response_item",
  };
  loupe::append_event(
      record,
      loupe::MessageEvent{
          .role = loupe::Role::Agent,
          .raw_role = "agent",
          .content =
              {
                  loupe::TextContent{.text = "readable"},
                  loupe::UnknownContent{
                      .native_type = "encrypted_content",
                      .json = R"({"type":"encrypted_content","data":"cipher"})",
                  },
              },
      });

  const loupe::SessionIR session{
      .format = loupe::LogFormat::Codex,
      .records = {std::move(record)},
  };

  const auto default_messages = loupe::make_display_messages(session);
  REQUIRE(default_messages.size() == 1);
  REQUIRE(default_messages.front().content == "readable");

  const auto messages_with_unknown = loupe::make_display_messages(
      session, loupe::DisplayOptions{.show_unknown = true});
  REQUIRE(messages_with_unknown.size() == 1);
  REQUIRE(messages_with_unknown.front().content.find("cipher")
          != std::string::npos);
}

TEST_CASE("content parsing includes shared semantic validation",
          "[session_ir]") {
  const auto parsed = loupe::parse_session_content(
      R"({"type":"session","version":3,"id":"session","timestamp":"2026-07-23T00:00:00Z","cwd":"/work"}
{"type":"message","id":"a1","parentId":null,"timestamp":"2026-07-23T00:00:01Z","message":{"role":"assistant","content":[{"type":"toolCall","id":"","name":"read","arguments":{}}]}})",
      loupe::LogFormat::Pi);

  REQUIRE(std::any_of(parsed.diagnostics.begin(), parsed.diagnostics.end(),
                      [](const loupe::Diagnostic &diagnostic) {
                        return diagnostic.code
                            == loupe::DiagnosticCode::EmptyCallId;
                      }));
}

TEST_CASE("usage after a hidden event stays attached to the visible turn",
          "[session_ir]") {
  loupe::RecordIR message_record{
      .sequence = 0,
      .source_line = 1,
      .native_type = "response_item",
  };
  loupe::append_event(
      message_record,
      loupe::MessageEvent{
          .role = loupe::Role::Assistant,
          .raw_role = "assistant",
          .content = {loupe::TextContent{.text = "visible answer"}},
      });

  loupe::RecordIR usage_record{
      .sequence = 1,
      .source_line = 2,
      .native_type = "event_msg",
  };
  loupe::append_event(usage_record, loupe::ReasoningEvent{
                                        .summary = "hidden plan",
                                        .content = "hidden details",
                                    });
  loupe::append_event(usage_record, loupe::UsageEvent{
                                        .scope = loupe::UsageScope::Turn,
                                        .input_tokens = 12,
                                        .output_tokens = 4,
                                    });

  const loupe::SessionIR session{
      .format = loupe::LogFormat::Codex,
      .records = {std::move(message_record), std::move(usage_record)},
  };

  const auto messages = loupe::make_display_messages(session);

  REQUIRE(messages.size() == 1);
  REQUIRE(messages.front().content == "visible answer");
  REQUIRE(messages.front().annotations.size() == 1);
  REQUIRE(messages.front().annotations.front().find("input=12")
          != std::string::npos);
}
