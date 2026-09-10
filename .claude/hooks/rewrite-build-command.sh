#!/bin/bash
# PreToolUse hook (Bash matcher). If the command being run is a build/test
# command (cmake --build, ctest, msbuild), rewrite it to pipe through
# filter-noisy-output.sh so repeated warning-code spam doesn't land verbatim
# in context. Leaves every other command untouched. Idempotent: won't
# double-wrap a command that already references the filter.
input=$(cat)
cmd=$(printf '%s' "$input" | grep -o '"command"[[:space:]]*:[[:space:]]*"[^"]*"' | head -1 | sed -E 's/^"command"[[:space:]]*:[[:space:]]*"//; s/"$//')

if [ -z "$cmd" ]; then
    exit 0
fi

case "$input" in
    *filter-noisy-output.sh*)
        exit 0
        ;;
esac

if printf '%s' "$cmd" | grep -qiE 'cmake --build|ctest|msbuild'; then
    filter="C:/GameForgerAI-Editor/.claude/hooks/filter-noisy-output.sh"
    new_cmd="( ${cmd} ) 2>&1 | \"${filter}\""
    escaped=$(printf '%s' "$new_cmd" | sed 's/\\/\\\\/g; s/"/\\"/g' | tr '\n' ' ')
    printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"allow","updatedInput":{"command":"%s"}}}' "$escaped"
fi
exit 0
