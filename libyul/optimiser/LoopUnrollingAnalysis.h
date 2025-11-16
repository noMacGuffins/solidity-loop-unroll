/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0
#pragma once

#include <libyul/AST.h>
#include <libyul/YulName.h>

#include <optional>
#include <set>
#include <string>

namespace solidity::yul
{

class Dialect;

/// Result of loop unrolling analysis
struct UnrollDecision
{
	bool shouldUnroll = false;
	size_t unrollFactor = 0;  // 0 means don't unroll, N means unroll N times
	std::string reason;        // For debugging/logging
};

/**
 * Analyzes loops to determine if they should be unrolled.
 *
 * This class encapsulates all heuristics for making loop unrolling decisions:
 * 
 * Possibility checks:
 * - Is the loop affine (induction variable increments by constant)?
 * - Is the loop iteration count predictable?
 * 
 * Effectiveness checks:
 * - Is the loop condition heavy (expensive to evaluate)?
 * - Is the body order optimizable (memory locality, CSE opportunities)?
 */
class LoopUnrollingAnalysis
{
public:
	explicit LoopUnrollingAnalysis(Dialect const& _dialect):
		m_dialect(_dialect)
	{ }

	/// Analyzes a loop and returns a decision on whether to unroll it.
	/// @param _loop The loop to analyze
	/// @param _blockStatements The statements in the block containing this loop (for finding init values)
	/// @param _loopIndex The index of the loop in _blockStatements
	/// @param _ssaVariables Set of SSA variables in the current scope
	/// @returns UnrollDecision with shouldUnroll flag and unroll factor
	UnrollDecision analyzeLoop(
		ForLoop const& _loop,
		std::vector<Statement> const& _blockStatements,
		size_t _loopIndex,
		std::set<YulName> const& _ssaVariables
	);

	/// Extracts the induction variable and its initial value from the loop and preceding statements.
	/// Returns the variable name, whether it's first arg in condition, and the initial value.
	/// @param _loop The loop to analyze
	/// @param _blockStatements The statements in the block containing this loop
	/// @param _loopIndex The index of the loop in _blockStatements
	/// @returns tuple of (induction variable, is first arg, initial value) or nullopt if not found
	std::optional<std::tuple<YulName, bool, u256>> extractInductionVariable(
		ForLoop const& _loop,
		std::vector<Statement> const& _blockStatements,
		size_t _loopIndex
	);

private:
	// ========== Possibility Checks ==========
	
	/// Attempts to predict the iteration count of the loop.
	/// Works for both for-loops (induction variable updated in post) and
	/// while-loops (induction variable updated in body).
	/// @param _loop The loop to analyze
	/// @param _inductionVar The induction variable name
	/// @param _varIsFirstArg Whether the variable is the first argument in condition
	/// @param _initValue The initial value of the induction variable
	/// @returns iteration count if predictable, nullopt otherwise
	std::optional<size_t> predictIterationCount(
		ForLoop const& _loop,
		YulName const& _inductionVar,
		bool _varIsFirstArg,
		u256 _initValue
	);
	
	// ========== Gas-Based Cost-Benefit Analysis ==========
	
	/// Approximates the gas saved per iteration from unrolling.
	/// Considers:
	/// - Loop condition evaluation cost
	/// - Induction variable update cost (if only used for loop control)
	/// - Memory locality improvements (accessing same memory location)
	/// @param _loop The loop to analyze
	/// @param _inductionVar The induction variable name
	/// @returns estimated gas saved per iteration
	size_t approximateGasSavedPerIteration(
		ForLoop const& _loop,
		YulName const& _inductionVar
	);
	
	/// Approximates the gas increase from code size bloating.
	/// @param _loop The loop to analyze
	/// @param _unrollFactor The proposed unroll factor
	/// @returns estimated gas increase from larger bytecode
	size_t approximateGasIncrease(
		ForLoop const& _loop,
		size_t _unrollFactor
	);
	
	/// Determines if full unrolling is profitable based on gas analysis.
	/// Formula: gasIncrease < gasSavedPerIteration * iterations * estimatedRuns
	/// @param _loop The loop to analyze
	/// @param _inductionVar The induction variable name
	/// @param _iterCount The predicted iteration count
	/// @param _estimatedRuns Estimated number of times this code will run after deployment
	/// @returns true if should fully unroll, false otherwise
	bool shouldFullyUnroll(
		ForLoop const& _loop,
		YulName const& _inductionVar,
		size_t _iterCount,
		size_t _estimatedRuns
	);

	Dialect const& m_dialect;
	
	// Tuning parameters - these control the aggressiveness of unrolling
	static constexpr size_t MAX_CONTRACT_SIZE = 24576;  // Ethereum max contract size in bytes (EIP-170)
	
	// Gas cost constants (approximations for EVM)
	static constexpr size_t GAS_JUMPI = 10;           // Conditional jump for loop condition
	static constexpr size_t GAS_JUMP = 8;             // Unconditional jump back to loop start
	static constexpr size_t GAS_LT = 3;               // Less-than comparison
	static constexpr size_t GAS_GT = 3;               // Greater-than comparison
	static constexpr size_t GAS_ADD = 3;              // Addition
	static constexpr size_t GAS_SUB = 3;              // Subtraction
	static constexpr size_t GAS_MUL = 5;              // Multiplication
	static constexpr size_t GAS_MLOAD = 3;            // Memory load (warm)
	static constexpr size_t GAS_MSTORE = 3;           // Memory store (warm)
	static constexpr size_t GAS_PER_BYTE = 200;       // Gas per byte of bytecode (deployment cost / avg runs)
};

}
