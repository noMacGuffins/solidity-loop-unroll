{
    // Test that loop unrolling correctly handles accumulator patterns
    // Expected result: sum should be 0 + 1 + 2 = 3
    // Note: There is a known issue where the mandatory cleanup passes
    // ("fDnTOcmuO") incorrectly eliminate one of the assignments
    function f() -> sum {
        sum := 0
        for { let i := 0 } lt(i, 3) { i := add(i, 1) }
        {
            sum := add(sum, i)
        }
    }
}
// ----
// step: loopUnrolling
//
// {
//     function f() -> sum
//     {
//         sum := 0
//         let i := 0
//         sum := add(sum, 0)
//         i := add(0, 1)
//         sum := add(sum, 1)
//         i := add(1, 1)
//         sum := add(sum, 2)
//         i := add(2, 1)
//     }
// }
