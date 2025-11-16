{
    function f() -> sum {
        sum := 0
        for { let i := 0 } lt(100, i) { i := add(i, 1) }
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
//         for { } lt(100, i) { i := add(i, 1) }
//         { sum := add(sum, i) }
//     }
// }
