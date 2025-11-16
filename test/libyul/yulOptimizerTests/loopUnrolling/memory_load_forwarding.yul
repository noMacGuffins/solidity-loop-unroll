{
	// This test verifies that load forwarding actually works after loop unrolling
	// The loop loads from the same memory location multiple times without modification
	// After unrolling + LoadResolver, redundant loads should be eliminated
	
	let x := 0
	for { let i := 0 } lt(i, 4) { i := add(i, 1) }
	{
		// Load from constant address 0x40 (not modified in loop)
		let val := mload(0x40)
		x := add(x, val)
	}
}
// step: loopUnrolling
// ----
// step: loopUnrolling
//
// {
//     let x := 0
//     let i := x
//     for { } lt(i, 4) { i := add(i, 1) }
//     {
//         let val := mload(0x40)
//         x := add(x, val)
//     }
// }
