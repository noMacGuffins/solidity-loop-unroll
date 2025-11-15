# Loop Unrolling Test Cases

This directory contains test cases for the loop unrolling optimization pass.

## Test Case Organization

- `simple_constant_loop.yul` - Basic affine loop with constant bounds (should unroll)
- `non_affine_loop.yul` - Non-affine loop (should NOT unroll)

## Running Tests

To run these tests:
```bash
./build/test/tools/isoltest -t yulOptimizerTests/loopUnrolling
```

To update expectations after implementing the pass:
```bash
./build/test/tools/isoltest -t yulOptimizerTests/loopUnrolling --update
```

## Implementation Checklist

1. ☐ Implement `isAffineLoop()` - detect affine loops
2. ☐ Implement `predictIterationCount()` - extract loop bounds
3. ☐ Implement `isConditionHeavy()` - analyze condition complexity
4. ☐ Implement `hasMemoryLocality()` - detect memory access patterns
5. ☐ Implement `hasCSEOpportunities()` - find common subexpressions
6. ☐ Implement `determineUnrollFactor()` - choose optimal unroll factor
7. ☐ Implement `rewriteLoop()` - perform actual loop unrolling
8. ☐ Handle partial unrolling (when iterations % factor != 0)
9. ☐ Handle full unrolling (when factor == iteration count)
10. ☐ Add comprehensive test cases
