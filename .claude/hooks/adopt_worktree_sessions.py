# OWNERSHIP=Claude
# SessionStart hook, and PostToolUse hook (Bash) after a `git worktree remove`:
# keep the sessions of removed worktrees in the session history.
#
# A session that runs EnterWorktree has its transcript moved to the project
# folder of the worktree (~/.claude/projects/<main>--claude-worktrees-issue-N),
# and only ExitWorktree moves it back. A session closed while still inside its
# worktree (a closed tab, a VS Code restart) stays there, and the VS Code chat
# history of the main folder never shows it. This moves every such transcript
# whose session is no longer running (with its <id>/ folder of subagents and
# tool results) into the main project folder, and appends the two records
# ExitWorktree writes (relocated back to main, no worktree), so the history
# lists it and a resume opens it in main. A resumed session that needs the
# worktree again runs EnterWorktree again. A transcript already in the main
# folder whose worktree is gone gets the same two records.
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


def running_sessions(config):
    """Session ids of the Claude processes alive right now."""
    alive = set()
    sessions = os.path.join(config, "sessions")
    for entry in os.listdir(sessions) if os.path.isdir(sessions) else []:
        if not entry.endswith(".json"):
            continue
        try:
            with open(os.path.join(sessions, entry), encoding="utf-8") as f:
                record = json.load(f)
            os.kill(int(record["pid"]), 0)
        except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError):
            continue  # gone, or unreadable: not running
        alive.add(record.get("sessionId"))
    return alive


def stamp_exit(transcript, session_id, main):
    """Append what ExitWorktree appends: relocated to main, worktree left."""
    records = [{"type": "relocated", "sessionId": session_id, "relocatedCwd": main},
               {"type": "worktree-state", "worktreeSession": None, "sessionId": session_id}]
    with open(transcript, "rb+") as f:
        f.seek(0, os.SEEK_END)
        if f.tell() > 0:
            f.seek(-1, os.SEEK_END)
            if f.read(1) != b"\n":
                f.write(b"\n")
        for record in records:
            f.write((json.dumps(record, separators=(",", ":")) + "\n").encode())


def sweep(dry_run, own_session=None):
    config = os.environ.get("CLAUDE_CONFIG_DIR") or os.path.expanduser("~/.claude")
    projects = os.path.join(config, "projects")
    main = main_worktree()
    main_key = project_key(main)
    alive = running_sessions(config)
    alive.add(own_session)  # a resume starting now may not be registered yet
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
            session_id = entry[:-len(".jsonl")]
            if session_id in alive:
                continue  # still writing here: ExitWorktree or the next sweep brings it
            dst = os.path.join(main_dir, entry)
            if os.path.exists(dst):
                continue
            side = src[:-len(".jsonl")]
            side_dst = dst[:-len(".jsonl")]
            if dry_run:
                print(f"{src} -> {main_dir}")
                continue
            if session_cwd(src) != main:
                stamp_exit(src, session_id, main)
            os.rename(src, dst)
            if os.path.isdir(side) and not os.path.exists(side_dst):
                os.rename(side, side_dst)
        if not dry_run and not os.listdir(wt_dir):
            os.rmdir(wt_dir)
    # Sessions adopted before the exit records were written, or that ended in a
    # worktree that is gone since: they would resume in a folder that no
    # longer exists (the launch fails), so send them back to main.
    for entry in sorted(os.listdir(main_dir)):
        if not entry.endswith(".jsonl") or entry[:-len(".jsonl")] in alive:
            continue
        path = os.path.join(main_dir, entry)
        cwd = session_cwd(path)
        if cwd is None or cwd == main or os.path.isdir(cwd):
            continue
        if dry_run:
            print(f"{path}: {cwd} is gone -> {main}")
            continue
        stamp_exit(path, entry[:-len(".jsonl")], main)


def main():
    hook_input = {}
    if not sys.stdin.isatty():
        try:
            hook_input = json.load(sys.stdin)
        except (json.JSONDecodeError, ValueError):
            pass
    if not isinstance(hook_input, dict):
        hook_input = {}
    if "--after-bash" in sys.argv:
        command = (hook_input.get("tool_input") or {}).get("command", "")
        if not re.search(r"\bworktree\s+(remove|prune)\b", command):
            return
    try:
        sweep("--dry-run" in sys.argv, hook_input.get("session_id"))
    except (OSError, subprocess.CalledProcessError) as e:
        print(f"adopt_worktree_sessions: {e}", file=sys.stderr)


if __name__ == "__main__":
    main()
