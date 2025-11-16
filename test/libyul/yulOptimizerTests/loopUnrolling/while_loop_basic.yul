// Test: Basic while loop should be detected (ForLoop with empty pre and post)
{
    let i := 0
    for { } lt(i, 10) { }
    {
        i := add(i, 1)
        mstore(i, i)
    }
}
// ----
// step: loopUnrolling
//
// {
//     let i := 0
//     for { } lt(i, 10) { }
//     {
//         i := add(i, 1)
//         mstore(i, i)
//     }
// }
