# Shell

The user-space shell is a small, xv6-style parser for running programs.

Features:
- Runs commands by path or by searching `/bin`.
- Redirection with `<`, `>`, and `>>`.
- Pipelines with `|`.
- Command lists with `;`.
- Background execution by appending `&` at the end of a command or pipeline.

Built-ins:
- `cd <path>`
- `exit`
- `cls`
- `reboot`
- `shutdown`

Common userland commands include `ls`, `cat`, `grep`, `mv`, `rm`, and `wc`.

Notes:
- There is no job control; background tasks are detached and not tracked.
- The shell can run with stdio attached to a PTY slave fd from `openpty`.
- `term` uses this PTY mode to host `/bin/sh` in a WM client window, with ANSI colors/control sequences and resize-driven PTY winsize updates.
- Test helper binaries used by the kernel test suite are installed under `/tests`.
