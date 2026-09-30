#!/usr/bin/env bash
# OWNERSHIP=Claude
#
# PreToolUse hook (Bash matcher). Serializes heavy/parallel-unsafe engine
# commands (benchmarks, tt-stats, perft, self-play tournaments) across every
# concurrently-running Claude Code session on this machine, via a single
# system-wide flock. Two sessions racing these at once is what causes
# out-of-memory/CPU-starvation crashes; this makes the second one wait
# instead of crashing.
set -euo pipefail

input=$(cat)
cmd=$(printf '%s' "$input" | jq -r '.tool_input.command // empty')

[[ -z "$cmd" ]] && exit 0

# Already wrapped (e.g. re-entrant call) - don't double-wrap.
[[ "$cmd" == flock* ]] && exit 0

pattern='make[[:space:]]+(bench|speed-compare|tt-stats|perft|profile-startpos)\b'
pattern+='|benchmarks/profile_startpos'
pattern+='|self[_-]?play'
pattern+='|tournament'

if [[ "$cmd" =~ $pattern ]]; then
  jq -n '{
    hookSpecificOutput: {
      hookEventName: "PreToolUse",
      permissionDecision: "deny",
      permissionDecisionReason: "Heavy engine command: rerun it under the system-wide lock so it waits for other sessions'"'"' heavy work instead of running alongside it. A single command: `flock -w 3600 /tmp/ascaniusfish-heavy.lock make bench 2>&1 | tail -3`. Several commands joined with && or ;: `flock -w 3600 /tmp/ascaniusfish-heavy.lock bash -c '"'"'...'"'"'`."
    }
  }'
else
  exit 0
fi
