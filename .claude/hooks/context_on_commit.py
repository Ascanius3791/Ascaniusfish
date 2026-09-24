# OWNERSHIP=Claude
# PostToolUse hook (Bash): after a `git commit`, report the current context size
# back to Claude so it can decide whether to keep going, suggest /compact, or hand
# off to a fresh session. Context size = input + cache tokens of the last
# main-thread assistant message in the transcript.
# Manual check any time: python3 .claude/hooks/context_on_commit.py --now
import glob
import json
import os
import re
import sys

WARN_TOKENS = 100_000  # above this: finish the current issue, then wrap up
STOP_TOKENS = 160_000  # above this: stop gracefully now

COMMIT_RE = re.compile(r"(^|[;&|]\s*|\s)git\s+(-C\s+\S+\s+)?commit\b")


def last_context_tokens(transcript_path):
    tokens = None
    with open(transcript_path) as f:
        for line in f:
            if '"assistant"' not in line:
                continue
            try:
                entry = json.loads(line)
            except json.JSONDecodeError:
                continue
            if entry.get("type") != "assistant" or entry.get("isSidechain"):
                continue
            usage = (entry.get("message") or {}).get("usage")
            if usage:
                tokens = (usage.get("input_tokens", 0)
                          + usage.get("cache_creation_input_tokens", 0)
                          + usage.get("cache_read_input_tokens", 0))
    return tokens


def newest_transcript():
    slug = re.sub(r"[^A-Za-z0-9]", "-", os.getcwd())
    files = glob.glob(os.path.expanduser(f"~/.claude/projects/{slug}/*.jsonl"))
    return max(files, key=os.path.getmtime) if files else None


def main():
    if "--now" in sys.argv:
        transcript = newest_transcript()
        if transcript is None:
            return
    else:
        data = json.load(sys.stdin)
        command = (data.get("tool_input") or {}).get("command", "")
        if not COMMIT_RE.search(command):
            return
        transcript = data["transcript_path"]
    tokens = last_context_tokens(transcript)
    if tokens is None:
        return

    if tokens >= STOP_TOKENS:
        level, advice = "STOP", (
            "Stop gracefully now: leave the tree clean, then either recommend /compact "
            "(if this session's knowledge is still needed) or write a startup prompt for a "
            "new session plus a model/effort recommendation. See docs/WORKFLOW.md.")
    elif tokens >= WARN_TOKENS:
        level, advice = "WARN", (
            "Finish the current issue, then wrap up as described in docs/WORKFLOW.md "
            "instead of starting new work.")
    else:
        level, advice = "OK", "Continue."

    summary = f"Context: {tokens:,} tokens [{level}] (warn {WARN_TOKENS:,}, stop {STOP_TOKENS:,})."
    print(json.dumps({
        "systemMessage": summary,
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": f"{summary} {advice}",
        },
    }))


if __name__ == "__main__":
    main()
