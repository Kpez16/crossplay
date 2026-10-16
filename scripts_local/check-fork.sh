#!/bin/bash
# Run the gate (scripts_local/check.sh) and judge it against the failures this
# fork has decided to live with. Same arguments as check.sh:
#
#   scripts_local/check-fork.sh --tests --committed
#
# Exit 0 when the gate passed, or when every failing stage is covered by a rule
# in scripts_local/check-known-failures.txt. Exit 1 on anything else: a stage
# that is not listed, or a listed stage showing a failure its rules do not
# cover. The gate's own output is printed unchanged, then a verdict is appended.
#
# Deliberately conservative: if the gate fails but no failing stage can be read
# from its output, that is treated as an unknown failure and exit is non-zero.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RULES="${CHECK_FORK_RULES:-$HERE/check-known-failures.txt}"
GATE="${CHECK_FORK_GATE:-$HERE/check.sh}"
LOG="$(mktemp "${TMPDIR:-/tmp}/check-fork.XXXXXX")"
trap 'rm -f "$LOG"' EXIT

"$GATE" "$@" 2>&1 | tee "$LOG"
gate_status=${PIPESTATUS[0]}

if [ "$gate_status" -eq 0 ]; then
  echo
  echo "CHECKFORK-VERDICT: gate passed"
  exit 0
fi

python3 -I - "$LOG" "$RULES" "$gate_status" <<'PY'
import re
import sys

log_path, rules_path, gate_status = sys.argv[1], sys.argv[2], sys.argv[3]

rules = {}
try:
    with open(rules_path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            stage, _, pattern = line.partition("\t")
            if not pattern:
                print(f"CHECKFORK: bad rule (need TAB between stage and pattern): {line!r}")
                sys.exit(1)
            rules.setdefault(stage.strip(), []).append(re.compile(pattern))
except FileNotFoundError:
    print(f"CHECKFORK: rules file not found: {rules_path}")
    sys.exit(1)

stage_re = re.compile(r"^ {2}(\S+)\s+FAILED\b")
detail_re = re.compile(r"^\s+FAIL\s")

stages = []  # [name, stage_line, [detail lines]]
with open(log_path, encoding="utf-8", errors="replace") as fh:
    for raw in fh:
        line = raw.rstrip("\n")
        m = stage_re.match(line)
        if m:
            stages.append([m.group(1), line, []])
        elif stages and detail_re.match(line):
            stages[-1][2].append(line)

if not stages:
    print(f"CHECKFORK-VERDICT: failed (gate exit {gate_status}, no failing stage could be read from the output)")
    sys.exit(1)

unexpected = []
tolerated = []
for name, stage_line, details in stages:
    patterns = rules.get(name, [])
    # With detail lines, every one must be covered. Without any, the stage
    # line itself has to match a rule.
    candidates = details if details else [stage_line]
    bad = [c for c in candidates if not any(p.search(c) for p in patterns)]
    if bad:
        unexpected.append((name, bad))
    else:
        tolerated.append(name)

print()
if tolerated:
    print("CHECKFORK: known failures (tolerated): " + ", ".join(tolerated))
if unexpected:
    print("CHECKFORK: NOT covered by check-known-failures.txt:")
    for name, bad in unexpected:
        print(f"  {name}")
        for c in bad[:10]:
            print(f"    {c.strip()}")
        if len(bad) > 10:
            print(f"    ... and {len(bad) - 10} more")
    print(f"CHECKFORK-VERDICT: failed (gate exit {gate_status})")
    sys.exit(1)

print(f"CHECKFORK-VERDICT: only known failures (gate exit {gate_status})")
sys.exit(0)
PY
