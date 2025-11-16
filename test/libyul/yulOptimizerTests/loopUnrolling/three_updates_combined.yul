// Test: Three additions in different locations (POST + 2x BODY)
{
    let i := 5
    for { } lt(i, 50) { i := add(i, 1) }
    {
        i := add(i, 2)
        mstore(i, i)
        i := add(i, 2)
    }
}
// ----
// step: loopUnrolling
//
// {
//     let i := 5
//     for { } lt(i, 50) { i := add(i, 1) }
//     {
//         i := add(i, 2)
//         mstore(i, i)
//         i := add(i, 2)
//     }
// }
