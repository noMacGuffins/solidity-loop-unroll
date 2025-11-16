{
	let sum := 0
	for { let i := 0 } lt(i, 3) { i := add(i, 1) }
	{
		sum := add(sum, i)
	}
}
// ----
// step: loopUnrolling
//
// {
//     let sum := 0
//     let i := 0
//     sum := add(0, 0)
//     i := add(0, 1)
//     sum := add(sum, 1)
//     i := add(1, 1)
//     sum := add(sum, 2)
//     i := add(2, 1)
// }
