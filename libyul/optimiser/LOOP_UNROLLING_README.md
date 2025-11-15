# Loop Unrolling Pass - Quick Reference

## What Was Created

### Core Files
- `libyul/optimiser/LoopUnrolling.h` - Main pass header
- `libyul/optimiser/LoopUnrolling.cpp` - Main pass implementation
- `libyul/optimiser/LoopUnrollingAnalysis.h` - Heuristics header
- `libyul/optimiser/LoopUnrollingAnalysis.cpp` - Heuristics implementation

### Test Files
- `test/libyul/yulOptimizerTests/loopUnrolling/simple_constant_loop.yul`
- `test/libyul/yulOptimizerTests/loopUnrolling/non_affine_loop.yul`
- `test/libyul/yulOptimizerTests/loopUnrolling/README.md`

### Documentation
- `libyul/optimiser/LOOP_UNROLLING_GUIDE.md` - Comprehensive implementation guide

### Integration
- Updated `libyul/CMakeLists.txt` - Added new files to build
- Updated `libyul/optimiser/Suite.cpp` - Registered pass with abbreviation 'R'

## Build Status

✅ **Successfully compiles** - All files build without errors

## Where Decision-Making Happens

**File**: `LoopUnrollingAnalysis.cpp`

All heuristics are implemented here:

### Possibility Checks
- `isAffineLoop()` - Check if loop is affine (constant increment)
- `predictIterationCount()` - Determine iteration count from bounds

### Effectiveness Checks  
- `isConditionHeavy()` - Check if condition is expensive to evaluate
- `isBodyOptimizable()` - Check for optimization opportunities
  - `hasMemoryLocality()` - Sequential/strided memory access patterns
  - `hasCSEOpportunities()` - Common subexpressions to eliminate

### Cost-Benefit
- `estimateUnrollBenefit()` - Calculate benefit vs cost
- `determineUnrollFactor()` - Choose optimal unroll factor (2, 4, 8, full, etc.)

## Where Transformation Happens

**File**: `LoopUnrolling.cpp`

The actual AST modification:

- `operator()(Block&)` - Traverse AST and find loops
- `shouldUnroll()` - Query the analyzer for decision
- `rewriteLoop()` - Clone loop body and modify AST

## Key Design Decisions

1. **Separation of Concerns**
   - Analysis (decision) separate from transformation
   - Follows pattern of LICM and other passes

2. **Conservative Defaults**
   - All TODOs return false/0/nullopt (no unrolling)
   - Safe to build and run immediately
   - Implement incrementally

3. **Tuning Parameters** (in LoopUnrollingAnalysis.h)
   ```cpp
   MAX_UNROLL_FACTOR = 8
   MAX_TOTAL_ITERATIONS = 32
   MIN_CONDITION_COMPLEXITY = 3
   CODE_SIZE_THRESHOLD = 100
   ```

## How to Use

### In Optimizer Sequence

Use abbreviation **'R'** in optimization sequence:

```bash
solc --optimize --yul-optimizations "dhfo[xarrscL]R..."
                                                   └─ Loop unrolling
```

### Running Tests

```bash
# Run all loop unrolling tests
./build/test/tools/isoltest -t yulOptimizerTests/loopUnrolling

# Update test expectations
./build/test/tools/isoltest -t yulOptimizerTests/loopUnrolling --update
```

## Implementation Order

1. ✅ **Setup** - Files created and integrated
2. ⏳ **Affine Detection** - Implement `isAffineLoop()`
3. ⏳ **Iteration Count** - Implement `predictIterationCount()`
4. ⏳ **Heuristics** - Implement effectiveness checks
5. ⏳ **Transformation** - Implement `rewriteLoop()`
6. ⏳ **Testing** - Add comprehensive test cases
7. ⏳ **Tuning** - Optimize parameters based on gas measurements

## Quick Start

1. Start with `libyul/optimiser/LoopUnrollingAnalysis.cpp`
2. Implement `isAffineLoop()` first
3. Add test case and verify it works
4. Continue with `predictIterationCount()`
5. See `LOOP_UNROLLING_GUIDE.md` for detailed steps

## Architecture Diagram

```
User Code (Yul)
      ↓
LoopUnrolling::run()
      ↓
  ┌───────────────────────────────────┐
  │  For each ForLoop in AST:         │
  │  ┌─────────────────────────────┐  │
  │  │ LoopUnrollingAnalysis       │  │
  │  │  - isAffineLoop()           │  │
  │  │  - predictIterationCount()  │  │
  │  │  - isConditionHeavy()       │  │
  │  │  - isBodyOptimizable()      │  │
  │  │  - determineUnrollFactor()  │  │
  │  └─────────────────────────────┘  │
  │           ↓                        │
  │    UnrollDecision                  │
  │           ↓                        │
  │  ┌─────────────────────────────┐  │
  │  │ LoopUnrolling               │  │
  │  │  - rewriteLoop()            │  │
  │  │    - Clone loop body        │  │
  │  │    - Update induction var   │  │
  │  │    - Flatten/modify loop    │  │
  │  └─────────────────────────────┘  │
  └───────────────────────────────────┘
      ↓
Optimized Code
```

## Related Passes

Study these for patterns and utilities:

- **LoopInvariantCodeMotion** - Loop analysis, similar structure
- **ForLoopInitRewriter** - Loop structure manipulation
- **ControlFlowSimplifier** - Block flattening
- **ASTCopier** - Cloning AST nodes
- **SSATransform** - Variable handling

## Current Status

🟢 **Compiles Successfully**
🟡 **No Functionality** (all TODOs return conservative defaults)
🔵 **Ready for Implementation**

All the infrastructure is in place. Now implement the analysis and transformation logic step by step!
