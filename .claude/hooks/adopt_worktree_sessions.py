# OWNERSHIP=Claude
# SessionStart hook, and PostToolUse hook (Bash) after a `git worktree remove`:
# keep the sessions of removed worktrees in the session history.
#
# A session that runs EnterWorktree has its transcript moved to the project
# folder of the worktree (~/.claude/projects/<main>--claude-worktrees-issue-N).
# The history lists those folders only while the worktree is still in
# `git worktree list`, so once an issue lands and its worktree is removed, its
# sessions disappear. This moves every transcript whose worktree no longer
# exists (with its <id>/ folder of subagents and tool results) into the main
# project folder, where the history lists it and a resume finds it.
#
#   adopt_worktree_sessions.py               sweep (SessionStart)
#   adopt_worktree_sessions.py --after-bash  sweep only if the Bash command on
#                                            stdin removed a worktree
#   adopt_worktree_sessions.py --dry-run     print what would move
import json
import os
import re
import subprocess
import sys


def main_worktree():
    start = os.environ.get("CLAUDE_PROJECT_DIR") or os.getcwd()
    git_dir = subprocess.run(
        ["git", "-C", start, "rev-parse", "--path-format=absolute", "--git-common-dir"],
        capture_output=True, text=True, check=True).stdout.strip()
    return os.path.dirname(git_dir)


def project_key(path):
    return re.sub(r"[^a-zA-Z0-9]", "-", path)


def session_cwd(transcript):
    """Where the session ended up: its last relocation, else where it started."""
    first_cwd = relocated = None
    with open(transcript, encoding="utf-8", errors="replace") as f:
        for line in f:
            if '"relocatedCwd"' not in line and (first_cwd is not None or '"cwd"' not in line):
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue
            if record.get("type") == "relocated" and record.get("relocatedCwd"):
                relocated = record["relocatedCwd"]
            elif first_cwd is None and record.get("cwd"):
                first_cwd = record["cwd"]
    return relocated or first_cwd


def sweep(dry_run):
    config = os.environ.get("CLAUDE_CONFIG_DIR") or os.path.expanduser("~/.claude")
    projects = os.path.join(config, "projects")
    main_key = project_key(main_worktree())
    main_dir = os.path.join(projects, main_key)
    if not os.path.isdir(main_dir):
        return
    for name in sorted(os.listdir(projects)):
        if not name.startswith(main_key + "--claude-worktrees-"):
            continue
        wt_dir = os.path.join(projects, name)
        for entry in sorted(os.listdir(wt_dir)):
            if not entry.endswith(".jsonl"):
                continue
            src = os.path.join(wt_dir, entry)
            cwd = session_cwd(src)
            if cwd is None or os.path.isdir(cwd):
                continue  # worktree still there: the history lists it already
            dst = os.path.join(main_dir, entry)
            if os.path.exists(dst):
                continue
            side = src[:-len(".jsonl")]
            side_dst = dst[:-len(".jsonl")]
            if dry_run:
                print(f"{src} -> {main_dir}")
                continue
            os.rename(src, dst)
            if os.path.isdir(side) and not os.path.exists(side_dst):
                os.rename(side, side_dst)
        if not dry_run and not os.listdir(wt_dir):
            os.rmdir(wt_dir)


def main():
    if "--after-bash" in sys.argv:
        try:
            command = json.load(sys.stdin).get("tool_input", {}).get("command", "")
        except (json.JSONDecodeError, AttributeError):
            return
        if not re.search(r"\bworktree\s+(remove|prune)\b", command):
            return
    try:
        sweep("--dry-run" in sys.argv)
    except (OSError, subprocess.CalledProcessError) as e:
        print(f"adopt_worktree_sessions: {e}", file=sys.stderr)


if __name__ == "__main__":
    main()
