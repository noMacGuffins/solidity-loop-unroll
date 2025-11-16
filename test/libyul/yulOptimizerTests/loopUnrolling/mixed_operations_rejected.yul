// Test: Mixed add and sub operations should be rejected
{
    let i := 0
    for { } lt(i, 100) { }
    {
        mstore(i, i)
        i := add(i, 5)
        i := sub(i, 2)
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
//         i := add(i, 5)
//         i := sub(i, 2)
//     }
// }
