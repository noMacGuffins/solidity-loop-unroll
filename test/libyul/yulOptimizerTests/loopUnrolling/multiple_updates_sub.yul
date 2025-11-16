// Test: Loop with multiple subtractions (both in POST and BODY)
{
    let i := 100
    for { } gt(i, 0) { i := sub(i, 2) }
    {
        mstore(i, i)
        i := sub(i, 3)
    }
}
// ----
// step: loopUnrolling
//
// {
//     let i := 100
//     for { } gt(i, 0) { i := sub(i, 2) }
//     {
//         mstore(i, i)
//         i := sub(i, 3)
//     }
// }
