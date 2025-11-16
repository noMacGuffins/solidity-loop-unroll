{
    function f() -> sum {
        sum := 0
        for { } 1 { }
        {
            sum := add(sum, 1)
            if gt(sum, 10) { break }
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
//         for { } 1 { }
//         {
//             sum := add(sum, 1)
//             if gt(sum, 10) { break }
//         }
//     }
// }
