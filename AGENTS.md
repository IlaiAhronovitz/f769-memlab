# f769-memlab: agent rules (Codex and other agents)

Read `CLAUDE.md`: its rules apply to every agent in this repository, not only Claude. If `CLAUDE.local.md` exists,
read it as well (notes for one machine, git-ignored).

Codex-specific notes:
- `tools/quality.sh` needs bash and the Linux tools (gcc, clang 21, clang-tidy 21, llvm 21, cppcheck 2.16.0). If
  your sandbox cannot run them, run `python build.py test` and say in your report that the gate was not run; never
  report it as passed.
- The measured layout is frozen (`CLAUDE.md`): any `src/` edit needs `tools/disasm_check.py` against the measured
  build, and a board comparison if anything moved.
- Do not commit, push or flash unless the task says so. Keep your notes out of tracked files.
