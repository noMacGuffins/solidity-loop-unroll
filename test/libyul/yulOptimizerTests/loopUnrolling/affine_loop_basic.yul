{
    function f() -> sum {
        sum := 0
        for { let i := 0 } lt(i, 10) { i := add(i, 1) }
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
//         for { } lt(i, 10) { i := add(i, 1) }
//         { sum := add(sum, i) }
//     }
// }
