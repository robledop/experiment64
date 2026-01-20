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

Notes:
- There is no job control; background tasks are detached and not tracked.
