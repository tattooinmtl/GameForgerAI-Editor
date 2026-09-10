#!/bin/bash
# Collapses repeated MSVC-style "file(line): warning C####: ..." lines down to
# the first 3 occurrences per warning code + a count, so a 268-line C4834 spam
# doesn't land verbatim in context. Everything else passes through unchanged,
# including any line containing "error" (case-insensitive) or a pass/fail
# summary keyword - those never get collapsed or dropped.
# Final safety net: if output is still >400 lines after that, keep first 150 +
# last 150 lines (build errors surface early, pass/fail summaries are at the
# end) with a marker for what was cut.
awk '
BEGIN { total = 0 }
{
    lines[++total] = $0
    if ($0 ~ /error/ || $0 ~ /Error/ || $0 ~ /ERROR/) { keep[total] = 1; next }
    if (match($0, /warning ([Cc][0-9]+):/, m)) {
        code = m[1]
        count[code]++
        if (count[code] <= 3) keep[total] = 1
        else collapsedcount++
        codeline[code] = total
        next
    }
    keep[total] = 1
}
END {
    for (i = 1; i <= total; i++) {
        if (keep[i]) print lines[i]
        for (code in codeline) {
            if (codeline[code] == i && count[code] > 3) {
                print "[filter-noisy-output] warning " code ": " count[code] " total occurrences, 3 shown above, " (count[code]-3) " collapsed"
            }
        }
    }
}
' | awk -v cap=400 -v head=150 -v tail=150 '
{ lines[NR] = $0 }
END {
    if (NR <= cap) { for (i=1;i<=NR;i++) print lines[i]; exit }
    for (i=1;i<=head;i++) print lines[i]
    print "[filter-noisy-output] --- " (NR-head-tail) " lines omitted (output capped at " cap " lines) ---"
    for (i=NR-tail+1;i<=NR;i++) print lines[i]
}
'
