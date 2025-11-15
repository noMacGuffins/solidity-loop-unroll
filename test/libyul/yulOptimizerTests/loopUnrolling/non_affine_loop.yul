// This loop should NOT be unrolled because it's not affine
// (the increment is not constant).
{
    function f() -> sum {
        sum := 0
        for { let i := 1 } lt(i, 100) { i := mul(i, 2) }
        {
            sum := add(sum, i)
        }
    }
}
// ----
// step: loopUnrolling
//
// Expected: No change (loop is not affine)
