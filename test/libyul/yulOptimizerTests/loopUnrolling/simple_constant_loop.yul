// This is a simple example test case for loop unrolling.
// Once the pass is fully implemented, this loop should be unrolled
// because it has a predictable iteration count (4 iterations).
{
    function f() -> sum {
        sum := 0
        for { let i := 0 } lt(i, 4) { i := add(i, 1) }
        {
            sum := add(sum, i)
        }
    }
}
// ----
// step: loopUnrolling
//
// Expected output (once implemented):
// {
//     function f() -> sum {
//         sum := 0
//         let i := 0
//         sum := add(sum, i)
//         i := add(i, 1)
//         sum := add(sum, i)
//         i := add(i, 1)
//         sum := add(sum, i)
//         i := add(i, 1)
//         sum := add(sum, i)
//     }
// }
