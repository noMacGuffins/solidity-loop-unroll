#!/bin/bash

# Script to run isoltest and capture baseline results
# This allows you to compare test results before and after implementing loop unrolling

set -e

# Get the repository root (parent of test_scripts)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && cd .. && pwd)"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_DIR="$SCRIPT_DIR/test_results"
BASELINE_FILE="$RESULTS_DIR/isoltest_baseline_${TIMESTAMP}.txt"
SUMMARY_FILE="$RESULTS_DIR/isoltest_summary_${TIMESTAMP}.txt"
LATEST_BASELINE="$RESULTS_DIR/isoltest_baseline_latest.txt"
LATEST_SUMMARY="$RESULTS_DIR/isoltest_summary_latest.txt"

# Create results directory if it doesn't exist
mkdir -p "$RESULTS_DIR"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║   isoltest Baseline Runner                                ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "This script runs isoltest and saves results for comparison."
echo "Failing tests will be automatically skipped."
echo ""

# Check if isoltest exists
if [[ ! -f "$SCRIPT_DIR/build/test/tools/isoltest" ]]; then
    echo "❌ Error: isoltest not found at $SCRIPT_DIR/build/test/tools/isoltest"
    echo "Please build the project first: cd build && make"
    exit 1
fi

echo "Running isoltest..."
echo "Results will be saved to:"
echo "  - $BASELINE_FILE"
echo "  - $SUMMARY_FILE"
echo ""

# Run isoltest with no-color for clean output
# The --no-smt flag skips SMT tests which can be slow
# The --no-ipc flag disables IPC tests
# Capture both stdout and stderr
echo "Starting test run at $(date)"
echo "This will skip all failing tests automatically..."
echo "----------------------------------------"

# Run isoltest and capture output
# We pipe 's' (skip) to automatically skip all failing tests
# The --no-color flag removes color codes for clean parsing
yes 's' | "$SCRIPT_DIR/build/test/tools/isoltest" --no-color 2>&1 | tee "$BASELINE_FILE"

# Extract summary from the output
echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║   Test Summary                                             ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo "" | tee "$SUMMARY_FILE"

# Extract the summary lines (last few lines typically contain summary)
tail -20 "$BASELINE_FILE" | grep -E "(Summary:|successful|tests skipped|FAILED|OK)" | tee -a "$SUMMARY_FILE"

echo "" | tee -a "$SUMMARY_FILE"
echo "Test run completed at $(date)" | tee -a "$SUMMARY_FILE"
echo "" | tee -a "$SUMMARY_FILE"

# Create symlinks to latest results
ln -sf "$(basename "$BASELINE_FILE")" "$LATEST_BASELINE"
ln -sf "$(basename "$SUMMARY_FILE")" "$LATEST_SUMMARY"

echo "Results saved to:"
echo "  Full output: $BASELINE_FILE"
echo "  Summary:     $SUMMARY_FILE"
echo ""
echo "Symlinks created:"
echo "  Latest full: $LATEST_BASELINE"
echo "  Latest sum:  $LATEST_SUMMARY"
echo ""

# Count passing and failing tests
TOTAL_TESTS=$(grep -c "^yulOptimizerTests/\|^semanticTests/\|^syntaxTests/" "$BASELINE_FILE" || echo "0")
PASSING_TESTS=$(grep -c ": OK$" "$BASELINE_FILE" || echo "0")
FAILING_TESTS=$(grep -c ": FAILED$" "$BASELINE_FILE" || echo "0")

echo "Quick Stats:"
echo "  Total tests found: $TOTAL_TESTS"
echo "  Passing tests:     $PASSING_TESTS"
echo "  Failing tests:     $FAILING_TESTS"
echo ""

# Extract list of failing tests for easy reference
FAILING_LIST="$RESULTS_DIR/failing_tests_${TIMESTAMP}.txt"
grep ": FAILED$" "$BASELINE_FILE" | sed 's/: FAILED$//' > "$FAILING_LIST" 2>/dev/null || touch "$FAILING_LIST"

if [[ -s "$FAILING_LIST" ]]; then
    FAIL_COUNT=$(wc -l < "$FAILING_LIST" | tr -d ' ')
    echo "List of $FAIL_COUNT failing tests saved to: $FAILING_LIST"
    echo ""
    echo "First 10 failing tests:"
    head -10 "$FAILING_LIST" | sed 's/^/  - /'
    if [[ $FAIL_COUNT -gt 10 ]]; then
        echo "  ... and $((FAIL_COUNT - 10)) more"
    fi
else
    echo "✓ All tests passing!"
fi

echo ""
echo "════════════════════════════════════════════════════════════"
echo "To compare results later, run:"
echo "  diff $LATEST_BASELINE test_results/isoltest_baseline_<new_timestamp>.txt"
echo ""
echo "Or use the comparison script:"
echo "  ./compare_isoltest_results.sh $BASELINE_FILE <new_results_file>"
echo "════════════════════════════════════════════════════════════"
