#include "project/log_parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace {
std::filesystem::path find_none_json_fixture() {
  const auto cwd = std::filesystem::current_path();
  for (const auto &candidate : {
           cwd / "none.json",
           cwd / ".." / "none.json",
           cwd / ".." / ".." / "none.json",
           cwd / ".." / ".." / ".." / "none.json",
       }) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}
} // namespace

TEST_CASE("parse JSONL chat messages", "[log_parser]") {
  const auto parsed = agentlens::parse_log_content(
      R"({"role":"system","content":"You are concise."}
{"role":"user","content":"hello"}
{"role":"assistant","content":"hi there"})");

  REQUIRE(parsed.errors.empty());
  REQUIRE(parsed.messages.size() == 3);
  REQUIRE(parsed.messages[0].role == "system");
  REQUIRE(parsed.messages[1].role == "user");
  REQUIRE(parsed.messages[2].role == "assistant");
  REQUIRE(parsed.messages[2].content == "hi there");
  REQUIRE(parsed.messages[2].source_line == 3);
}

TEST_CASE("parse Claude-style nested content arrays", "[log_parser]") {
  const auto parsed = agentlens::parse_log_content(
      R"({"type":"assistant","message":{"role":"assistant","content":[{"type":"text","text":"first"},{"type":"text","text":"second"}]},"timestamp":"2026-06-16T12:00:00Z"})");

  REQUIRE(parsed.errors.empty());
  REQUIRE(parsed.messages.size() == 1);
  REQUIRE(parsed.messages[0].role == "assistant");
  REQUIRE(parsed.messages[0].content == "first\nsecond");
  REQUIRE(parsed.messages[0].timestamp == "2026-06-16T12:00:00Z");
}

TEST_CASE("parse JSON documents with a messages container", "[log_parser]") {
  const auto parsed = agentlens::parse_log_content(
      R"({"messages":[{"role":"user","content":"question"},{"role":"assistant","content":"answer"}]})");

  REQUIRE(parsed.errors.empty());
  REQUIRE(parsed.messages.size() == 2);
  REQUIRE(parsed.messages[0].role == "user");
  REQUIRE(parsed.messages[1].content == "answer");
}

TEST_CASE("parse agent log tool calls", "[log_parser]") {
  const auto parsed = agentlens::parse_log_content(
      R"({"messages":[{"role":"assistant","content":[{"type":"text","content":"Looking it up."}],"tool_calls":[{"function":"get_all_hotels_in_city","args":{"city":"Boston"},"id":"call_0"}]},{"role":"tool","content":[{"type":"text","content":"Hotel Names:\nRiverside View Hotel"}],"tool_call_id":"call_0","tool_call":{"function":"get_all_hotels_in_city","args":{"city":"Boston"},"id":"call_0"},"error":null}]})");

  REQUIRE(parsed.errors.empty());
  REQUIRE(parsed.messages.size() == 2);
  REQUIRE(parsed.messages[0].role == "assistant");
  REQUIRE(parsed.messages[0].content == "Looking it up.");
  REQUIRE(parsed.messages[0].annotations.size() == 1);
  REQUIRE(parsed.messages[0].annotations[0]
          == R"(call get_all_hotels_in_city {"city":"Boston"} [call_0])");
  REQUIRE(parsed.messages[1].role == "tool");
  REQUIRE(parsed.messages[1].content == "Hotel Names:\nRiverside View Hotel");
  REQUIRE(parsed.messages[1].annotations.size() == 1);
  REQUIRE(parsed.messages[1].annotations[0]
          == R"(result for get_all_hotels_in_city {"city":"Boston"} [call_0])");
}

TEST_CASE("tool call argument objects keep all fields", "[log_parser]") {
  const auto parsed = agentlens::parse_log_content(
      R"({"role":"assistant","content":"","tool_calls":[{"function":"send","args":{"content":"hi","recipient":"a"},"id":"call_1"}]})");

  REQUIRE(parsed.errors.empty());
  REQUIRE(parsed.messages.size() == 1);
  REQUIRE(parsed.messages[0].content.empty());
  REQUIRE(parsed.messages[0].annotations.size() == 1);
  REQUIRE(parsed.messages[0].annotations[0]
          == R"(call send {"content":"hi","recipient":"a"} [call_1])");
}

TEST_CASE("explicit empty tool-call content stays empty", "[log_parser]") {
  const auto parsed = agentlens::parse_log_content(
      R"({"role":"assistant","content":null,"tool_calls":[{"function":"lookup","args":{"query":"x"},"id":"call_2"}]})");

  REQUIRE(parsed.errors.empty());
  REQUIRE(parsed.messages.size() == 1);
  REQUIRE(parsed.messages[0].role == "assistant");
  REQUIRE(parsed.messages[0].content.empty());
  REQUIRE(parsed.messages[0].annotations.size() == 1);
  REQUIRE(parsed.messages[0].annotations[0]
          == R"(call lookup {"query":"x"} [call_2])");
}

TEST_CASE("parse none.json fixture when present", "[log_parser]") {
  const auto fixture = find_none_json_fixture();
  if (fixture.empty()) {
    SKIP("none.json fixture is not present");
  }

  const auto parsed = agentlens::parse_log_file(fixture);

  REQUIRE(parsed.errors.empty());
  REQUIRE(parsed.messages.size() == 26);
  REQUIRE(parsed.messages[0].role == "system");
  REQUIRE(parsed.messages[0].content.find("AI language model")
          != std::string::npos);
  REQUIRE(parsed.messages[2].role == "assistant");
  REQUIRE(parsed.messages[2].annotations.size() == 1);
  REQUIRE(
      parsed.messages[2].annotations[0]
      == R"(call get_user_information {} [call_00_7IPa2nXr0Q6jdH64lzYc9615])");
  REQUIRE(parsed.messages[3].role == "tool");
  REQUIRE(parsed.messages[3].annotations.size() == 1);
  REQUIRE(
      parsed.messages[3].annotations[0]
      == R"(result for get_user_information {} [call_00_7IPa2nXr0Q6jdH64lzYc9615])");
}
