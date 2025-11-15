# Loop Unrolling Pass - Implementation Guide

## Overview

This document provides a comprehensive guide for implementing the loop unrolling optimization pass in the Solidity Yul optimizer. The skeleton has been created and integrated into the build system.

## Files Created

1. **`LoopUnrolling.h/cpp`** - Main optimization pass
2. **`LoopUnrollingAnalysis.h/cpp`** - Heuristics and decision-making logic
3. **Test cases** in `test/libyul/yulOptimizerTests/loopUnrolling/`

## Architecture

### Decision Flow

```
LoopUnrolling::run()
  ├─> Gather SSA variables (SSAValueTracker)
  ├─> Create LoopUnrollingAnalysis
  └─> Apply transformation to AST
       └─> For each ForLoop:
            ├─> shouldUnroll()
            │    └─> LoopUnrollingAnalysis::analyzeLoop()
            │         ├─> isAffineLoop() ✓ Possibility
            │         ├─> predictIterationCount() ✓ Possibility
            │         ├─> isConditionHeavy() ✓ Effectiveness
            │         ├─> isBodyOptimizable() ✓ Effectiveness
            │         │    ├─> hasMemoryLocality()
            │         │    └─> hasCSEOpportunities()
            │         └─> determineUnrollFactor()
            │              └─> estimateUnrollBenefit()
            └─> rewriteLoop() (perform transformation)
```

### Separation of Concerns

**LoopUnrollingAnalysis** (Decision Making):
- All heuristics for determining IF and HOW MUCH to unroll
- No AST modification
- Pure analysis and decision logic

**LoopUnrolling** (Transformation):
- AST traversal and modification
- Actual code generation for unrolled loops
- Uses decisions from LoopUnrollingAnalysis

## Implementation Roadmap

### Phase 1: Basic Affine Loop Detection

**Goal**: Detect simple affine loops with constant bounds

**Files to modify**: `LoopUnrollingAnalysis.cpp`

**Function**: `isAffineLoop()`

**Steps**:
1. Check that `pre` block contains exactly one variable declaration
2. Verify the declaration initializes with a literal (constant)
3. Check that `condition` is a function call (e.g., `lt(i, bound)`)
4. Verify `post` block updates the induction variable by constant amount
5. Ensure the induction variable is used consistently

**Example affine loop**:
```yul
for { let i := 0 } lt(i, 10) { i := add(i, 1) }
{
    // body
}
```

**Non-affine examples**:
```yul
// Non-constant increment
for { let i := 1 } lt(i, 100) { i := mul(i, 2) } { }

// Multiple variables
for { let i := 0 let j := 0 } lt(i, 10) { i := add(i, 1) j := add(j, 2) } { }
```

**Useful utilities**:
- `std::holds_alternative<VariableDeclaration>()` - Check AST node types
- `std::holds_alternative<Literal>()` - Check for constants
- `std::holds_alternative<FunctionCall>()` - Check for operations

### Phase 2: Iteration Count Prediction

**Function**: `predictIterationCount()`

**Steps**:
1. Extract initial value from `pre` block
2. Extract bound from `condition` 
3. Extract step size from `post` block
4. Compute: `(bound - initial) / step`
5. Handle different comparison operators (lt, gt, lte, gte)

**Challenges**:
- Need to evaluate constant expressions
- Handle both increasing (i++) and decreasing (i--) loops
- Validate that step evenly divides range

**Example**:
```yul
for { let i := 0 } lt(i, 12) { i := add(i, 3) }
// initial = 0, bound = 12, step = 3
// iterations = (12 - 0) / 3 = 4
```

### Phase 3: Effectiveness Heuristics

#### 3a. Condition Complexity (`isConditionHeavy()`)

Count operations in the condition expression. Use `CodeSize::codeSize()` or implement a custom counter.

**Heavy conditions** (worth unrolling):
- `lt(mul(i, 2), bound)` - 2 operations
- `lt(add(i, offset), bound)` - 2 operations

**Light conditions** (less benefit):
- `lt(i, bound)` - 1 operation
- `iszero(i)` - 1 operation

#### 3b. Memory Locality (`hasMemoryLocality()`)

Look for patterns like:
```yul
mstore(add(baseAddr, mul(i, 32)), value)  // Sequential access
mload(add(ptr, i))                         // Strided access
```

**Detection strategy**:
1. Find all `mload`/`mstore` calls in loop body
2. Check if address expressions contain the induction variable
3. Determine if access pattern is sequential or strided

#### 3c. CSE Opportunities (`hasCSEOpportunities()`)

Look for repeated subexpressions that would benefit from unrolling:
```yul
for { let i := 0 } lt(i, 4) { i := add(i, 1) }
{
    let x := mul(someConst, 5)  // Loop-invariant, appears in every iteration
    sum := add(sum, x)
}
```

After unrolling, `mul(someConst, 5)` can be computed once.

**Detection strategy**:
1. Build expression frequency map
2. Identify expressions that don't depend on induction variable
3. Estimate CSE benefit

### Phase 4: Cost-Benefit Analysis

**Function**: `estimateUnrollBenefit()`

**Benefit calculation**:
```cpp
size_t benefit = 0;

// Eliminated loop overhead
benefit += iterCount * LOOP_OVERHEAD_COST;

// Condition evaluation savings
if (isConditionHeavy())
    benefit += iterCount * conditionComplexity * EVAL_COST;

// Optimization opportunities
if (hasMemoryLocality())
    benefit += MEMORY_LOCALITY_BONUS;
if (hasCSEOpportunities())
    benefit += cseOpportunityCount * CSE_BENEFIT;

return benefit;
```

**Cost calculation**:
```cpp
size_t cost = bodySize * (unrollFactor - 1);  // Code size increase
```

**Function**: `determineUnrollFactor()`

**Strategy**:
1. Full unrolling for small loops (≤ 4 iterations)
2. Partial unrolling for larger loops
3. Find largest factor that:
   - Divides `iterCount` evenly (or handle remainder)
   - ≤ `MAX_UNROLL_FACTOR` (currently 8)
   - Keeps `benefit > cost`

### Phase 5: Loop Transformation

**Function**: `rewriteLoop()`

This is the most complex part - actually modifying the AST.

#### 5a. Full Unrolling

When `unrollFactor == iterationCount`:

**Input**:
```yul
for { let i := 0 } lt(i, 3) { i := add(i, 1) }
{
    sum := add(sum, i)
}
```

**Output**:
```yul
{
    let i := 0
    sum := add(sum, i)
    i := add(i, 1)
    sum := add(sum, i)
    i := add(i, 1)
    sum := add(sum, i)
}
```

**Steps**:
1. Extract `pre` block statements
2. For each iteration:
   - Copy loop body using `ASTCopier`
   - Substitute induction variable with current value (if constant)
   - Copy `post` block statements (except for last iteration)
3. Return flattened statements

#### 5b. Partial Unrolling

When `unrollFactor < iterationCount`:

**Input** (6 iterations, unroll by 2):
```yul
for { let i := 0 } lt(i, 6) { i := add(i, 1) }
{
    sum := add(sum, i)
}
```

**Output**:
```yul
for { let i := 0 } lt(i, 6) { i := add(i, 2) }
{
    sum := add(sum, i)
    i := add(i, 1)
    sum := add(sum, i)
}
```

**Steps**:
1. Clone loop body `unrollFactor` times
2. Update step in `post` block: `step *= unrollFactor`
3. Insert intermediate updates between cloned bodies

**Useful classes**:
- `ASTCopier` - Clone AST nodes
- `Substitution` - Replace variables with values
- `NameDispenser` - Generate unique variable names

### Phase 6: Testing

Create test cases in `test/libyul/yulOptimizerTests/loopUnrolling/`:

1. **simple_constant_loop.yul** - Basic affine loop ✓ (created)
2. **non_affine_loop.yul** - Should not unroll ✓ (created)
3. **partial_unroll.yul** - Partial unrolling case
4. **memory_locality.yul** - Memory access pattern
5. **nested_loops.yul** - Nested loop handling
6. **large_body.yul** - Should not unroll (body too large)
7. **many_iterations.yul** - Should not unroll (too many iterations)

Run tests:
```bash
./build/test/tools/isoltest -t yulOptimizerTests/loopUnrolling
```

Update expectations:
```bash
./build/test/tools/isoltest -t yulOptimizerTests/loopUnrolling --update
```

## Key Classes and Utilities

### From Existing Codebase

- **`SSAValueTracker`**: Track SSA variables
- **`ASTCopier`**: Clone AST nodes
- **`DataFlowAnalyzer`**: Analyze data flow
- **`CodeSize`**: Measure code size
- **`Substitution`**: Substitute variables
- **`NameDispenser`**: Generate unique names
- **`SideEffectsCollector`**: Check for side effects

### AST Navigation

```cpp
// Check node type
if (std::holds_alternative<ForLoop>(statement))
    ForLoop& loop = std::get<ForLoop>(statement);

// Check expression type
if (std::holds_alternative<FunctionCall>(*expression))
    FunctionCall const& call = std::get<FunctionCall>(*expression);

// Check for literal
if (std::holds_alternative<Literal>(*expression))
    Literal const& lit = std::get<Literal>(*expression);
```

## Integration Points

### Optimizer Sequence

The pass is registered with abbreviation **'R'** in `Suite.cpp`.

**Suggested placement** in optimization sequence:
```
"dhfo[xarrscL]R..."
     └─────┘ └─ LoopUnrolling (after SSA, CSE, LoadResolver)
```

**Reasoning**:
- Run after SSA transform (`a`) for better analysis
- Run after expression splitting (`x`) for normalized AST
- Run after CSE (`c`) to avoid duplicating work
- Run before final simplification passes

### Prerequisites

As documented in `LoopUnrolling.h`:
- Disambiguator (variable uniqueness)
- ForLoopInitRewriter (normalized loop structure)
- FunctionHoister (no nested functions)

Optional for better results:
- ExpressionSplitter (normalized expressions)
- SSATransform (SSA form)

## Debugging Tips

1. **Print AST**: Use `AsmPrinter` to debug transformations
   ```cpp
   #include <libyul/AsmPrinter.h>
   std::cout << AsmPrinter{m_dialect}(_loop) << std::endl;
   ```

2. **Decision logging**: Add verbose output in `analyzeLoop()`
   ```cpp
   std::cout << "Analyzing loop: " << decision.reason << std::endl;
   ```

3. **Use isoltest**: Iterate quickly with test-driven development
   ```bash
   ./build/test/tools/isoltest -t loopUnrolling --no-color
   ```

4. **Check other passes**: Study similar transformations:
   - `ForLoopInitRewriter` - Loop structure manipulation
   - `LoopInvariantCodeMotion` - Loop analysis
   - `ControlFlowSimplifier` - Block manipulation

## Common Pitfalls

1. **Forgetting to copy debugData** - Always preserve source locations
2. **Not handling edge cases** - Empty body, zero iterations, etc.
3. **Breaking SSA form** - Don't duplicate variable declarations
4. **Code size explosion** - Always check size constraints
5. **Incorrect substitution** - Verify induction variable updates

## Performance Considerations

- Unrolling increases code size (I-cache pressure)
- Balance between eliminated overhead and increased size
- Consider target platform (EVM has no I-cache, but bytecode size matters)
- Full unrolling is often better than partial for small loops

## Next Steps

1. Implement `isAffineLoop()` with basic pattern matching
2. Implement `predictIterationCount()` for constant bounds
3. Add simple test cases and verify detection works
4. Implement `rewriteLoop()` for full unrolling only
5. Test with real-world Solidity contracts
6. Refine heuristics based on gas measurements
7. Add partial unrolling support
8. Optimize for common patterns

## Resources

- **Yul Documentation**: docs/yul.rst
- **Optimizer README**: libyul/optimiser/README.md
- **Similar passes**: ForLoopInitRewriter, LoopInvariantCodeMotion
- **AST definition**: libyul/AST.h
- **Test framework**: test/libyul/yulOptimizerTests/README.md

## Summary

You now have a fully integrated, compiling loop unrolling pass skeleton with:

✅ Header and implementation files
✅ Analysis class for heuristics
✅ Integration with CMake build system
✅ Registration in optimizer suite (abbreviation 'R')
✅ Test directory structure
✅ Comprehensive TODOs marking implementation points

The architecture cleanly separates **decision-making** (LoopUnrollingAnalysis) from **transformation** (LoopUnrolling), following the pattern established by other Yul optimizer passes.

Start with Phase 1 (affine loop detection) and work incrementally through the phases. Good luck!
