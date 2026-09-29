#!/usr/bin/env bash
# OWNERSHIP=Claude
#
# One git worktree per GitHub issue (see docs/WORKFLOW.md, "Issue worktrees").
#
#   tools/issue_worktree.sh N      print the absolute path of issue N's worktree,
#                                  creating it first if it does not exist yet
#   tools/issue_worktree.sh list   every issue worktree: branch, ahead/behind main, dirty
#
# Issue N lives in <repo>/.claude/worktrees/issue-N on branch issue/N, made from the
# local main (not origin/main, which lags whenever main is not pushed). Only the path
# goes to stdout, so `cd "$(tools/issue_worktree.sh 42)"` works; everything else is
# stderr. Works from the main folder or from inside any worktree.
set -euo pipefail

usage() { echo "usage: $0 <issue-number> | list" >&2; exit 2; }
[[ $# -eq 1 ]] || usage

# The main folder, even when called from inside a worktree.
git_dir=$(git rev-parse --path-format=absolute --git-common-dir)
main_dir=$(dirname "$git_dir")
wt_root="$main_dir/.claude/worktrees"

# Path of the worktree that has refs/heads/$1 checked out, or nothing.
worktree_of_branch() {
    git -C "$main_dir" worktree list --porcelain | awk -v ref="refs/heads/$1" '
        /^worktree /{ path = substr($0, 10) }
        $0 == "branch " ref { print path; exit }'
}

# "ahead A, behind B" of branch $1 relative to main.
ahead_behind() {
    local counts
    counts=$(git -C "$main_dir" rev-list --left-right --count "main...$1")
    echo "ahead ${counts##*[[:space:]]}, behind ${counts%%[[:space:]]*}"
}

if [[ $1 == list ]]; then
    git -C "$main_dir" for-each-ref --format='%(refname:short)' 'refs/heads/issue/*' |
    while read -r branch; do
        path=$(worktree_of_branch "$branch")
        if [[ -z $path ]]; then
            echo "$branch  (no worktree)  $(ahead_behind "$branch")"
            continue
        fi
        dirty=$([[ -n $(git -C "$path" status --porcelain) ]] && echo ", uncommitted changes" || true)
        echo "$branch  $path  $(ahead_behind "$branch")$dirty"
    done
    exit 0
fi

[[ $1 =~ ^[0-9]+$ ]] || usage
n=$1
branch="issue/$n"

# Already there: whichever folder has the branch checked out is the issue's worktree.
path=$(worktree_of_branch "$branch")
if [[ -n $path ]]; then
    echo "issue #$n: existing worktree, $(ahead_behind "$branch")" >&2
    echo "$path"
    exit 0
fi

path="$wt_root/issue-$n"
if [[ -e $path ]]; then
    echo "error: $path exists but is not a worktree of $branch; look at it before reusing it" >&2
    exit 1
fi

mkdir -p "$wt_root"
if git -C "$main_dir" show-ref --verify --quiet "refs/heads/$branch"; then
    # The branch outlived its folder (e.g. `git worktree remove` without deleting it).
    git -C "$main_dir" worktree add --quiet "$path" "$branch" >&2
    echo "issue #$n: re-created worktree for existing branch, $(ahead_behind "$branch")" >&2
else
    git -C "$main_dir" worktree add --quiet -b "$branch" "$path" main >&2
    echo "issue #$n: new worktree, branch $branch from main" >&2
fi

# Git-ignored files a session needs that a fresh checkout does not have.
if [[ -f $main_dir/.claude/settings.local.json && ! -e $path/.claude/settings.local.json ]]; then
    cp "$main_dir/.claude/settings.local.json" "$path/.claude/settings.local.json"
fi

echo "$path"
