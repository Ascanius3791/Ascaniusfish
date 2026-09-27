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
  lockfile=/tmp/ascaniusfish-heavy.lock
  scriptfile=$(mktemp /tmp/ascaniusfish-heavy-cmd.XXXXXX.sh)
  printf '%s\n' "$cmd" > "$scriptfile"
  wrapped="flock -w 3600 $lockfile bash $scriptfile"

  jq -n --arg cmd "$wrapped" '{
    hookSpecificOutput: {
      hookEventName: "PreToolUse",
      permissionDecision: "allow",
      permissionDecisionReason: "Heavy engine command serialized via a system-wide flock so it cannot run concurrently with another session'"'"'s heavy work.",
      updatedInput: {command: $cmd}
    },
    systemMessage: "Heavy command detected - waiting on /tmp/ascaniusfish-heavy.lock (up to 1h) so it does not run alongside another session'"'"'s heavy work."
  }'
else
  exit 0
fi
