#!/usr/bin/env bash
# Runs the ArkTS local unit tests (entry/src/test) through hvigor and fails on any failing case.
# Needs the DevEco Studio tools (hvigorw, node, ...) on PATH.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-host
hvigorw test --mode module -p module=entry@default -p product=default --no-daemon > build-host/arkts-tests.log 2>&1 \
  || { tail -30 build-host/arkts-tests.log; exit 1; }
result=entry/.test/default/intermediates/test/coverage_data/test_result.txt
grep -a -E '^(class|test|result)=' "$result" | paste -sd' ' | sed 's/ class=/\nclass=/g' | tail -40
summary=$(grep -a '^Tests run:' "$result" | tail -1)
echo "$summary"
echo "$summary" | grep -q 'Failure: 0, Error: 0'
