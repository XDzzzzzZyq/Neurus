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

## Git Worktree Parallel Workflow

For isolated feature work, use git worktrees on separate branches:

```
# 1. CREATE worktree + branch (from clean master)
git worktree add ../Neurus_Feature -b feature/my-feature

# 2. WORK EXCLUSIVELY in the worktree directory
#    NEVER touch files in the main workspace (D:\Projects\Neurus)
#    All edits, commits happen in D:\Projects\Neurus_Feature
#    The main workspace stays on master, clean.

# 3. COMMIT in worktree
cd ../Neurus_Feature
git add <files>
git commit -m "feat(scope): description"

# 4. MERGE back to master (from main workspace)
cd ../Neurus                  # back to main workspace (on master)
git merge feature/my-feature

# 5. RESOLVE merge conflicts if any
#    - Read all conflicted files with conflict markers
#    - Combine both sides intelligently (keep new features from master,
#      keep your refactoring from the feature branch)
#    - Verify no <<<<<< / ====== / >>>>>> markers remain
#    git add <resolved-files>
#    git commit -m "merge: feature/my-feature into master"

# 6. RUN FULL TEST SUITE (never skip)
cmake --preset default
cmake --build build/debug
cd build/debug && ctest --output-on-failure

# 7. CLEAN UP
git branch -d feature/my-feature
git worktree remove ../Neurus_Feature
# If worktree directory is locked (Permission denied), remove with --force
# If already unregistered from git, manually delete: Remove-Item -Recurse -Force
```

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
- For worktree work, use direct `edit`/`read`/`write` tools with absolute paths to the worktree.
- Plan simple refactoring tasks to be completable with direct edits rather than delegation.

**Build and test from the main workspace after merge.**
- After merging, the main workspace contains the combined code.
- Run `cmake --build build/debug` and `ctest` from the main workspace.
- The worktree's build directory is separate - do not rely on stale worktree builds.

**Worktree cleanup can be tricky.**
- `git worktree remove` may fail with "Permission denied" if processes (e.g., graphify hook) hold file locks.
- Use `--force` if needed: `git worktree remove ../Neurus_Feature --force`
- If the worktree is already unregistered from git but the directory persists, remove manually.
- Always delete the branch: `git branch -d feature/my-feature`
