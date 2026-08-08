# Git Workflow

## Issue & Label Conventions

- **Title**: clean, no prefix. Category expressed via label instead (e.g. title `Transform Editor panel` + label `UI & UX`, not `feat(ui): Transform Editor panel`).
- **Labels**: `Feature`, `Bug`, `Documentation`, `Core`, `UI & UX`, `Render`, `Good First Issue`, `Duplicated`.

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
- The main workspace stays on `master` and must remain clean.
- All file edits, commits, and builds for feature work happen inside the worktree directory.
- Do NOT switch branches in the main workspace - use the worktree for isolation.
- Do NOT create worktree in `~/.config/opencode/*`, create the worktree inside `.omo/worktrees/*` or `.slim/worktrees/*`. So that user can view the changes in the main directory and the worktree directory at the same time.
- ALWAYS use `make update` to update the git submodules and pre-compiled libs.

**Merge conflicts are expected.**
- When `master` has progressed (new features added after your branch point), merge conflicts will occur.
- Example: `master` had IBL pass additions not present in the feature branch. Resolution: keep IBL code from master, but adapt it to use the refactored patterns from the feature branch.

**`task()` subagents cannot target a worktree directory.**
- Subagents spawned via `task()` always operate in the main workspace root.
- Use the tools and skills `opencode-worktree` for parallelism and isolation of feature work.

**Build and test from the main workspace after merge.**
- After merging, the main workspace contains the combined code.
- Run `cmake --build build --config Debug` and `make check` (which uses `build/`) from the main workspace.
- The worktree's build directory is separate - do not rely on stale worktree builds.

**Worktree cleanup can be tricky.**
- `git worktree remove` may fail with "Permission denied" if processes (e.g., graphify hook) hold file locks.
- Use `--force` if needed: `git worktree remove ../Neurus_Feature --force`
- If the worktree is already unregistered from git but the directory persists, remove manually.
- Always delete the branch: `git branch -d feature/my-feature`
