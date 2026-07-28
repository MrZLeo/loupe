# Loupe

Loupe is a terminal viewer for coding-agent session logs. It parses each
provider's native JSONL into a common, loss-aware intermediate representation
and projects that representation into the existing message UI.

<p align="center">
  <img src="docs/promo.gif" alt="loupe browsing and inspecting mixed-format session logs" width="900">
</p>

## Supported formats

Loupe auto-detects the format of each opened log: the native formats use
disjoint record-type vocabularies, so scanning the first records is enough
to classify them. `--format` overrides detection, and detection never
selects the lossy `generic` path — unclassifiable content fails with a
fatal diagnostic asking for an explicit `--format`.

| `--format` | Input |
| --- | --- |
| `auto` (default) | Detect `pi`, `codex`, `codex-exec`, or `claudecode` from the first records |
| `pi` | Pi session JSONL, including parent-linked branches |
| `codex` | Codex session rollout JSONL |
| `codex-exec` | `codex exec --json` event-stream JSONL |
| `claudecode` | Claude Code transcript JSONL |
| `generic` | Loupe's legacy generic JSON/JSONL parser |

```sh
loupe ~/.pi/agent/sessions/.../session.jsonl
loupe ~/.codex/sessions/2026/07/23/rollout-....jsonl
codex exec --json "summarize this repository" > codex-exec.jsonl
loupe codex-exec.jsonl
loupe ~/.claude/projects/.../session.jsonl
```

`codex` and `codex-exec` are separate formats. The former reads the persisted
session rollout, while the latter reads the machine-readable stdout event
stream produced by `codex exec --json`.

A directory can be opened instead of a file. With the default auto-detection,
each file opened from the browser is classified independently; an explicit
`--format` applies to all of them:

```sh
loupe ~/.codex/sessions
loupe --format codex ~/.codex/sessions
```

## Architecture

```text
native JSONL
    |
    +-- Pi parser
    +-- Codex session parser
    +-- Codex Exec parser
    +-- Claude Code parser
    +-- Generic parser
            |
            v
        SessionIR -> validation/branch selection
                              |
                              v
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

auto exec =
    loupe::parse_session_file(exec_path, loupe::LogFormat::CodexExec);
```

Format adapters only translate native data into the IR. Shared validation,
branch selection, and display behavior live outside the adapters, so adding a
backend does not require changing the UI.

Parsing is recovering: malformed lines and unknown native types remain as
records and produce diagnostics. Fatal diagnostics prevent the TUI from
showing a partially trusted transcript; non-fatal diagnostics remain visible
in the status line. The `generic` adapter is an explicitly selected, lossy
compatibility path for the old parser, so the raw-record guarantee applies to
the `pi`, `codex`, `codex-exec`, and `claudecode` adapters.

## Build and test

Loupe requires CMake 3.25+ and a C++23 compiler. Dependencies are fetched with
CPM during configuration.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The executable is written below `build/bin/<configuration>/loupe`.
Terminal frames use DEC synchronized output so supporting emulators can display
each redraw atomically and avoid visible tearing.

## Releases

Pushing a tag such as `v0.3.0` creates a GitHub release with these archives and
a `SHA256SUMS` file:

| Platform | Archive | Linking |
| --- | --- | --- |
| Linux x86_64 | `loupe-v0.3.0-linux-x86_64.tar.gz` | Fully static glibc executable |
| Windows x86_64 | `loupe-v0.3.0-windows-x86_64.zip` | Static MSVC runtime |
| macOS 13+ | `loupe-v0.3.0-macos-universal.tar.gz` | Universal arm64/x86_64 executable; system libraries only |

Each archive contains one executable named `loupe` or `loupe.exe` and requires
no separately installed project or C++ runtime libraries. macOS system
libraries remain dynamically linked because macOS does not provide a supported
fully static system runtime.

```sh
tar -xzf loupe-v0.3.0-linux-x86_64.tar.gz
./loupe --version
```

## Keyboard shortcuts

- `j` / `k`, Up / Down, Page Up / Page Down: navigate file and message lists
- `g` / `G`, Home / End: jump to the first / last file or message
- Mouse wheel / touchpad scroll: move the viewport by line; the topmost
  visible message becomes the current one, and its role header stays pinned
  to the top of the screen while its body scrolls
- Move the mouse over the role-colored overview bars on the right to inspect
  the transcript; click a bar to jump directly to that message
- `/`: filter files or search the current log
- `n` / `N`: move between matches in the current log
- `r`: reload the current log with the same explicit format
- `e`: toggle the diagnostics view when the log produced diagnostics
- `b` / `-`: return to the file browser
- `q`: quit

## License

Loupe is distributed under the terms in [LICENSE](LICENSE). The bundled
third-party libraries remain under their own licenses, reproduced in
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
