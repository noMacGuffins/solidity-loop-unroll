{
    function f() -> sum {
        sum := 0
        for { let i := 10 } gt(i, 0) { i := sub(i, 1) }
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
//         let i := 10
//         for { } gt(i, 0) { i := sub(i, 1) }
//         { sum := add(sum, i) }
//     }
// }
