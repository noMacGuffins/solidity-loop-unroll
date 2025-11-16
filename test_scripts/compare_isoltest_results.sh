#!/bin/bash

# Script to compare two isoltest result files
# Usage: ./compare_isoltest_results.sh <baseline_file> <new_results_file>

set -e

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <baseline_file> <new_results_file>"
    echo ""
    echo "Example:"
    echo "  $0 test_results/isoltest_baseline_latest.txt test_results/isoltest_baseline_20231116_143022.txt"
    exit 1
fi

BASELINE="$1"
NEWRESULTS="$2"

if [[ ! -f "$BASELINE" ]]; then
    echo "❌ Error: Baseline file not found: $BASELINE"
    exit 1
fi

if [[ ! -f "$NEWRESULTS" ]]; then
    echo "❌ Error: New results file not found: $NEWRESULTS"
    exit 1
fi

# Get the repository root (parent of test_scripts)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && cd .. && pwd)"
RESULTS_DIR="$SCRIPT_DIR/test_results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
DIFF_FILE="$RESULTS_DIR/diff_${TIMESTAMP}.txt"

mkdir -p "$RESULTS_DIR"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║   isoltest Results Comparison                             ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "Comparing:"
echo "  Baseline: $BASELINE"
echo "  New:      $NEWRESULTS"
echo ""

# Extract test results from both files
BASELINE_PASSING=$(grep ": OK$" "$BASELINE" | sort || echo "")
BASELINE_FAILING=$(grep ": FAILED$" "$BASELINE" | sort || echo "")
NEW_PASSING=$(grep ": OK$" "$NEWRESULTS" | sort || echo "")
NEW_FAILING=$(grep ": FAILED$" "$NEWRESULTS" | sort || echo "")

# Count tests
BASELINE_PASS_COUNT=$(echo "$BASELINE_PASSING" | grep -c ":" || echo "0")
BASELINE_FAIL_COUNT=$(echo "$BASELINE_FAILING" | grep -c ":" || echo "0")
NEW_PASS_COUNT=$(echo "$NEW_PASSING" | grep -c ":" || echo "0")
NEW_FAIL_COUNT=$(echo "$NEW_FAILING" | grep -c ":" || echo "0")

echo "Test Counts:" | tee "$DIFF_FILE"
echo "════════════════════════════════════════════════════════════" | tee -a "$DIFF_FILE"
echo "  Baseline: $BASELINE_PASS_COUNT passing, $BASELINE_FAIL_COUNT failing" | tee -a "$DIFF_FILE"
echo "  New:      $NEW_PASS_COUNT passing, $NEW_FAIL_COUNT failing" | tee -a "$DIFF_FILE"
echo "" | tee -a "$DIFF_FILE"

# Find newly passing tests (failed in baseline, passing now)
NEWLY_PASSING=$(comm -13 <(echo "$BASELINE_PASSING" | sed 's/: OK$//' | sort) <(echo "$NEW_PASSING" | sed 's/: OK$//' | sort) | grep -v "^$" || echo "")
NEWLY_PASSING_COUNT=$(echo "$NEWLY_PASSING" | grep -c "^" || echo "0")

if [[ "$NEWLY_PASSING_COUNT" -gt 0 ]]; then
    echo "✅ Newly Passing Tests ($NEWLY_PASSING_COUNT):" | tee -a "$DIFF_FILE"
    echo "────────────────────────────────────────────────────────────" | tee -a "$DIFF_FILE"
    echo "$NEWLY_PASSING" | sed 's/^/  /' | tee -a "$DIFF_FILE"
    echo "" | tee -a "$DIFF_FILE"
else
    echo "No newly passing tests." | tee -a "$DIFF_FILE"
    echo "" | tee -a "$DIFF_FILE"
fi

# Find newly failing tests (passed in baseline, failing now)
NEWLY_FAILING=$(comm -13 <(echo "$BASELINE_FAILING" | sed 's/: FAILED$//' | sort) <(echo "$NEW_FAILING" | sed 's/: FAILED$//' | sort) | grep -v "^$" || echo "")
NEWLY_FAILING_COUNT=$(echo "$NEWLY_FAILING" | grep -c "^" || echo "0")

if [[ "$NEWLY_FAILING_COUNT" -gt 0 ]]; then
    echo "❌ Newly Failing Tests ($NEWLY_FAILING_COUNT):" | tee -a "$DIFF_FILE"
    echo "────────────────────────────────────────────────────────────" | tee -a "$DIFF_FILE"
    echo "$NEWLY_FAILING" | sed 's/^/  /' | tee -a "$DIFF_FILE"
    echo "" | tee -a "$DIFF_FILE"
else
    echo "✓ No newly failing tests." | tee -a "$DIFF_FILE"
    echo "" | tee -a "$DIFF_FILE"
fi

# Find tests that are still failing
STILL_FAILING=$(comm -12 <(echo "$BASELINE_FAILING" | sed 's/: FAILED$//' | sort) <(echo "$NEW_FAILING" | sed 's/: FAILED$//' | sort) | grep -v "^$" || echo "")
STILL_FAILING_COUNT=$(echo "$STILL_FAILING" | grep -c "^" || echo "0")

if [[ "$STILL_FAILING_COUNT" -gt 0 ]]; then
    echo "⚠️  Still Failing Tests ($STILL_FAILING_COUNT):" | tee -a "$DIFF_FILE"
    echo "────────────────────────────────────────────────────────────" | tee -a "$DIFF_FILE"
    echo "$STILL_FAILING" | head -20 | sed 's/^/  /' | tee -a "$DIFF_FILE"
    if [[ "$STILL_FAILING_COUNT" -gt 20 ]]; then
        echo "  ... and $((STILL_FAILING_COUNT - 20)) more" | tee -a "$DIFF_FILE"
    fi
    echo "" | tee -a "$DIFF_FILE"
fi

# Summary
echo "════════════════════════════════════════════════════════════" | tee -a "$DIFF_FILE"
echo "Summary:" | tee -a "$DIFF_FILE"
echo "════════════════════════════════════════════════════════════" | tee -a "$DIFF_FILE"

NET_CHANGE=$((NEW_PASS_COUNT - BASELINE_PASS_COUNT))
if [[ $NET_CHANGE -gt 0 ]]; then
    echo "✅ Net improvement: +$NET_CHANGE passing tests" | tee -a "$DIFF_FILE"
elif [[ $NET_CHANGE -lt 0 ]]; then
    echo "❌ Net regression: $NET_CHANGE passing tests" | tee -a "$DIFF_FILE"
else
    echo "➡️  No net change in passing test count" | tee -a "$DIFF_FILE"
fi

if [[ "$NEWLY_PASSING_COUNT" -gt 0 || "$NEWLY_FAILING_COUNT" -gt 0 ]]; then
    echo "  - $NEWLY_PASSING_COUNT tests now passing" | tee -a "$DIFF_FILE"
    echo "  - $NEWLY_FAILING_COUNT tests now failing" | tee -a "$DIFF_FILE"
fi

echo "" | tee -a "$DIFF_FILE"
echo "Full diff saved to: $DIFF_FILE"
echo ""

# Optionally show detailed diff
read -p "Show detailed line-by-line diff? (y/n): " -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "════════════════════════════════════════════════════════════"
    echo "Detailed Diff:"
    echo "════════════════════════════════════════════════════════════"
    diff -u "$BASELINE" "$NEWRESULTS" || true
fi
