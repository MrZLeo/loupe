Output docs/promo.gif

Set Shell bash
Set FontSize 15
Set Width 1600
Set Height 900
Set Framerate 30
Set TypingSpeed 55ms
Set Theme "Catppuccin Mocha"
Set WindowBar Colorful
Set WindowBarSize 36
Set BorderRadius 8
Set Margin 24
Set MarginFill "#1E1E2E"

Hide
Type "export PATH=/Users/mrzleo/loupe/build/bin/Release:$PATH"
Enter
Type "cd /Users/mrzleo/loupe && clear"
Enter
Show

# --- browser with mixed-format logs ---
Sleep 500ms
Type "loupe docs/demo-logs"
Sleep 700ms
Enter
Sleep 2500ms

Down
Sleep 500ms
Down
Sleep 500ms
Up
Sleep 600ms

# --- filter and open the codex-exec stream ---
Type "/"
Sleep 400ms
Type "exec"
Sleep 900ms
Enter
Sleep 700ms
Enter
Sleep 2500ms

# --- smooth scrolling through messages ---
Set TypingSpeed 85ms
Type "jjjjjj"
Sleep 1s
Type "jjjj"
Sleep 1s
Type "kkkk"
Sleep 1s
Set TypingSpeed 55ms

PageDown
Sleep 1s
PageUp
Sleep 1s
Type "G"
Sleep 1500ms
Type "g"
Sleep 1200ms

# --- search ---
Type "/"
Sleep 400ms
Type "verdict"
Sleep 900ms
Enter
Sleep 1500ms
Type "n"
Sleep 1200ms
Type "n"
Sleep 1200ms
Type "N"
Sleep 1200ms

# --- diagnostics ---
Type "e"
Sleep 1800ms
Set TypingSpeed 120ms
Type "jjjj"
Set TypingSpeed 55ms
Sleep 900ms
Type "e"
Sleep 1200ms

# --- reload, back to browser ---
Type "r"
Sleep 1500ms
Type "b"
Sleep 1500ms

# --- clear the filter, open the Claude Code transcript ---
Type "/"
Sleep 300ms
Ctrl+u
Sleep 400ms
Enter
Sleep 800ms
Enter
Sleep 2500ms

Set TypingSpeed 100ms
Type "jjjj"
Sleep 1s
Type "kk"
Set TypingSpeed 55ms
Sleep 1s

# --- back and quit ---
Type "-"
Sleep 1500ms
Type "q"
Sleep 800ms
Type "echo 'loupe - terminal viewer for coding-agent session logs'"
Sleep 600ms
Enter
Sleep 2500ms
