{
    function f() -> sum {
        sum := 0
        for { let i := 0 } lt(i, 100) { i := add(i, 5) }
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
//         for { } lt(i, 100) { i := add(i, 5) }
//         { sum := add(sum, i) }
//     }
// }
