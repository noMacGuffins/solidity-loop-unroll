// Test: Loop with multiple additions to induction variable
{
    let i := 0
    for { } lt(i, 100) { }
    {
        mstore(i, i)
        i := add(i, 3)
        mstore(add(i, 32), i)
        i := add(i, 7)
    }
}
// ----
// step: loopUnrolling
//
// {
//     let i := 0
//     for { } lt(i, 100) { }
//     {
//         mstore(i, i)
//         i := add(i, 3)
//         mstore(add(i, 32), i)
//         i := add(i, 7)
//     }
// }
