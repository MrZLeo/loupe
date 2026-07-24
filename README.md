# Loupe

Loupe is a terminal viewer for coding-agent session logs. It parses each
provider's native JSONL into a common, loss-aware intermediate representation
and projects that representation into the existing message UI.

## Supported formats

The input format is always explicit. Loupe does not guess from file contents or
paths.

| `--format` | Input |
| --- | --- |
| `pi` | Pi session JSONL, including parent-linked branches |
| `codex` | Codex rollout JSONL |
| `claudecode` | Claude Code transcript JSONL |
| `generic` | Loupe's legacy generic JSON/JSONL parser |

```sh
loupe --format pi ~/.pi/agent/sessions/.../session.jsonl
loupe --format codex ~/.codex/sessions/2026/07/23/rollout-....jsonl
loupe --format claudecode ~/.claude/projects/.../session.jsonl
```

A directory can be opened instead of a file. The selected format is then
applied to every file opened from the browser:

```sh
loupe --format codex ~/.codex/sessions
```

## Architecture

```text
native JSONL
    |
    +-- Pi parser
    +-- Codex parser       -> SessionIR -> validation/branch selection
    +-- Claude Code parser                    |
    +-- Generic parser                        v
                                      display projection -> TUI
```

`SessionIR` keeps provider-independent events for messages, reasoning, tool
calls and results, compaction, token usage, metadata, and unknown extensions.
Every native record also retains its source line, native type and IDs, event
order, and raw JSON. Unknown records are preserved instead of being silently
discarded.

The IR has three levels:

- `SessionIR` contains format/session metadata and the ordered native records.
- `RecordIR` is the lossless source envelope. `native_parent_id` always keeps
  the provider's raw edge; `navigation_parent_id` is an optional logical edge
  used for cases such as Claude Code compaction boundaries. An engaged empty
  navigation parent explicitly marks a logical root.
- `EventIR` contains one typed semantic payload. A single native record may
  expand into several ordered events, such as assistant text, reasoning, a
  tool call, and usage.

`parent_session_ref` is deliberately opaque because providers use different
reference kinds (for example, an ID or a path). Consumers must not assume it
can always be joined as a session ID.

The public parsing API is:

```cpp
auto parsed =
    loupe::parse_session_file(path, loupe::LogFormat::Codex);
auto messages = loupe::make_display_messages(parsed.session);
```

Format adapters only translate native data into the IR. Shared validation,
branch selection, and display behavior live outside the adapters, so adding a
backend does not require changing the UI.

Parsing is recovering: malformed lines and unknown native types remain as
records and produce diagnostics. Fatal diagnostics prevent the TUI from
showing a partially trusted transcript; non-fatal diagnostics remain visible
in the status line. The `generic` adapter is an explicitly selected, lossy
compatibility path for the old parser, so the raw-record guarantee applies to
the `pi`, `codex`, and `claudecode` adapters.

## Build and test

Loupe requires CMake 3.25+ and a C++20 compiler. Dependencies are fetched with
CPM during configuration.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The executable is written below `build/bin/<configuration>/loupe`.
Terminal frames use DEC synchronized output so supporting emulators can display
each redraw atomically and avoid visible tearing.

## Keyboard shortcuts

- `j` / `k`, Up / Down, Page Up / Page Down: navigate file and message lists
- Mouse wheel / touchpad scroll: move the viewport by line without changing
  the selected item
- `/`: filter files or search the current log
- `n` / `N`: move between matches in the current log
- `r`: reload the current log with the same explicit format
- `b`: return to the file browser
- `q`: quit

## License

Loupe is distributed under the terms in [LICENSE](LICENSE).
