// Test: While loop with subtraction (counting down)
{
    let i := 100
    for { } gt(i, 0) { }
    {
        mstore(i, i)
        i := sub(i, 5)
    }
}
// ----
// step: loopUnrolling
//
// {
//     let i := 100
//     for { } gt(i, 0) { }
//     {
//         mstore(i, i)
//         i := sub(i, 5)
//     }
// }
