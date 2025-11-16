# Test Scripts

This directory contains scripts for running and verifying Solidity compiler tests.

## Scripts

### 1. `run_isoltest_baseline.sh`
Runs all isoltest tests and saves baseline results for later comparison.

**Usage:**
```bash
./test_scripts/run_isoltest_baseline.sh
```

**Features:**
- Automatically skips failing tests (no manual intervention needed)
- Saves full output with timestamp
- Creates summary of passing/failing tests
- Creates symlinks to latest results for easy access
- Lists all failing tests in a separate file

**Output:**
- `test_results/isoltest_baseline_<timestamp>.txt` - Full test output
- `test_results/isoltest_summary_<timestamp>.txt` - Test summary
- `test_results/failing_tests_<timestamp>.txt` - List of failing tests
- `test_results/isoltest_baseline_latest.txt` - Symlink to latest baseline
- `test_results/isoltest_summary_latest.txt` - Symlink to latest summary

### 2. `compare_isoltest_results.sh`
Compares two isoltest result files to identify regressions or improvements.

**Usage:**
```bash
./test_scripts/compare_isoltest_results.sh <baseline_file> <new_results_file>
```

**Example:**
```bash
# Compare before and after implementing loop unrolling
./test_scripts/compare_isoltest_results.sh \
  test_results/isoltest_baseline_20251116_074718.txt \
  test_results/isoltest_baseline_20251116_101234.txt
```

**Features:**
- Shows newly passing tests (fixes/improvements)
- Shows newly failing tests (regressions)
- Shows tests that are still failing
- Calculates net change in passing tests
- Saves comparison report with timestamp
- Optional detailed line-by-line diff

**Output:**
- `test_results/diff_<timestamp>.txt` - Comparison report

### 3. `verify_correctness.sh`
Comprehensive guide for running Solidity tests and verification tools.

**Usage:**
```bash
./test_scripts/verify_correctness.sh
```

**Features:**
- Interactive guide to Solidity test infrastructure
- Examples of common test commands
- Information about fuzzing
- SMT checker documentation
- Current build status check

## Workflow for Loop Unrolling Implementation

### Step 1: Capture Baseline (Before Implementation)
```bash
# Run tests before implementing loop unrolling logic
./test_scripts/run_isoltest_baseline.sh
```

This creates a baseline at:
- `test_results/isoltest_baseline_<timestamp>.txt`

### Step 2: Implement Loop Unrolling
Make your changes to:
- `libyul/optimiser/LoopUnrolling.cpp`
- `libyul/optimiser/LoopUnrollingAnalysis.cpp`

### Step 3: Rebuild
```bash
cd build
make -j$(nproc)
```

### Step 4: Run Tests Again
```bash
./test_scripts/run_isoltest_baseline.sh
```

### Step 5: Compare Results
```bash
./test_scripts/compare_isoltest_results.sh \
  test_results/isoltest_baseline_<old_timestamp>.txt \
  test_results/isoltest_baseline_<new_timestamp>.txt
```

### Step 6: Investigate Regressions
If any tests are newly failing, investigate and fix them before proceeding.

## Directory Structure

```
test_scripts/           # This directory
  ├── README.md                        # This file
  ├── run_isoltest_baseline.sh         # Capture baseline
  ├── compare_isoltest_results.sh      # Compare results
  └── verify_correctness.sh            # Test guide

test_results/           # Created automatically
  ├── isoltest_baseline_<timestamp>.txt
  ├── isoltest_summary_<timestamp>.txt
  ├── failing_tests_<timestamp>.txt
  ├── diff_<timestamp>.txt
  ├── isoltest_baseline_latest.txt -> isoltest_baseline_<timestamp>.txt
  └── isoltest_summary_latest.txt -> isoltest_summary_<timestamp>.txt
```

## Tips

1. **Run baseline before making changes**: Always capture a baseline before implementing new features.

2. **Use timestamps wisely**: The scripts automatically timestamp all outputs, making it easy to track changes over time.

3. **Check symlinks**: Use `test_results/isoltest_baseline_latest.txt` for quick access to the most recent results.

4. **Focus on regressions**: Pay special attention to newly failing tests - these indicate your changes broke something.

5. **Celebrate improvements**: Newly passing tests mean your optimization is working correctly!

6. **Keep results**: Don't delete old result files - they provide a history of your development process.

## Common Issues

### "isoltest not found"
Make sure you've built the project:
```bash
cd build
cmake .. && make -j$(nproc)
```

### Tests taking too long
The full test suite takes 5-10 minutes. You can run specific tests:
```bash
./build/test/tools/isoltest -t 'yulOptimizerTests/loopUnrolling/*'
```

### Permission denied
Make sure scripts are executable:
```bash
chmod +x test_scripts/*.sh
```
