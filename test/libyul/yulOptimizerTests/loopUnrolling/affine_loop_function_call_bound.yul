{
    function f() -> sum {
        sum := 0
        for { let i := 0 } lt(i, bound()) { i := add(i, 1) }
        {
            sum := add(sum, i)
        }
    }
    function bound() -> x {
        x := 10
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
//         for { } lt(i, bound()) { i := add(i, 1) }
//         { sum := add(sum, i) }
//     }
//     function bound() -> x
//     { x := 10 }
// }
