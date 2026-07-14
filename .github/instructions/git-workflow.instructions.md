# Git Workflow

## General Rules

- Prefer Git submodule.
- For renaming and moving, use `git mv` to track the history.
- Any files should not include absolute path.
- Complete and double check all aspects (tests, coding style, document) of the
  current task before commit.
- Only master agent can commit and merge. If the tasks of subagents may have
  code overlaps, use Branches and Git Worktree for parallelism and isolation.
  Don't forget to remove the branch and worktree after the task is completed.
- Use `worktree_create` tools for creating worktree. The worktree directory should be outside the main workspace.

## Lessons Learned

**CRITICAL: Work exclusively in the worktree.**
- The main workspace (`D:\Projects\Neurus`) stays on `master` and must remain clean.
- All file edits, commits, and builds for feature work happen inside the worktree directory.
- Do NOT switch branches in the main workspace - use the worktree for isolation.

**Merge conflicts are expected.**
- When `master` has progressed (new features added after your branch point), merge conflicts will occur.
- Example: `master` had IBL pass additions not present in the feature branch. Resolution: keep IBL code from master, but adapt it to use the refactored patterns from the feature branch.

**`task()` subagents cannot target a worktree directory.**
- Subagents spawned via `task()` always operate in the main workspace root.
- Use the tools and skills `opencode-worktree` for parallelism and isolation of feature work.

**Build and test from the main workspace after merge.**
- After merging, the main workspace contains the combined code.
- Run `cmake --build build/debug` and `make check` from the main workspace.
- The worktree's build directory is separate - do not rely on stale worktree builds.

**Worktree cleanup can be tricky.**
- `git worktree remove` may fail with "Permission denied" if processes (e.g., graphify hook) hold file locks.
- Use `--force` if needed: `git worktree remove ../Neurus_Feature --force`
- If the worktree is already unregistered from git but the directory persists, remove manually.
- Always delete the branch: `git branch -d feature/my-feature`
