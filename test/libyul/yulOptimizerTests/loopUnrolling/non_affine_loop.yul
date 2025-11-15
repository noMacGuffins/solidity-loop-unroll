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
// {
//     function f() -> sum
//     {
//         sum := 0
//         let i := 1
//         for { } lt(i, 100) { i := mul(i, 2) }
//         { sum := add(sum, i) }
//     }
// }
