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
	/// @param _ssaVariables Set of SSA variables in the current scope
	/// @returns UnrollDecision with shouldUnroll flag and unroll factor
	UnrollDecision analyzeLoop(
		ForLoop const& _loop,
		std::set<YulName> const& _ssaVariables
	);

private:
	// ========== Possibility Checks ==========
	
	/// Checks if the loop is affine (induction variable changes by constant).
	/// An affine loop has the form: for { let i := start } lt(i, bound) { i := add(i, step) }
	/// @returns true if the loop is affine, false otherwise
	bool isAffineLoop(ForLoop const& _loop);
	
	/// Attempts to predict the iteration count of the loop.
	/// This only works for loops with constant bounds and steps.
	/// @returns iteration count if predictable, nullopt otherwise
	std::optional<size_t> predictIterationCount(ForLoop const& _loop);
	
	// ========== Effectiveness Checks ==========
	
	/// Checks if the loop condition is computationally heavy.
	/// Heavy conditions benefit more from unrolling to reduce re-evaluation.
	/// @returns true if the condition is considered heavy
	bool isConditionHeavy(ForLoop const& _loop);
	
	/// Checks if the loop body has optimization opportunities from unrolling.
	/// This includes memory locality and common subexpression elimination.
	/// @returns true if the body would benefit from unrolling
	bool isBodyOptimizable(ForLoop const& _loop);
	
	/// Checks if the loop body accesses memory locations that exhibit locality.
	/// Sequential or stride-based access patterns benefit from unrolling.
	/// @returns true if memory accesses show locality
	bool hasMemoryLocality(ForLoop const& _loop);
	
	/// Checks if the loop body has common subexpressions that could be eliminated.
	/// @returns true if CSE opportunities exist
	bool hasCSEOpportunities(ForLoop const& _loop);
	
	// ========== Cost-Benefit Analysis ==========
	
	/// Estimates the benefit of unrolling the loop by a given factor.
	/// Considers code size increase vs. performance gains.
	/// @param _loop The loop to analyze
	/// @param _iterCount The predicted iteration count
	/// @param _factor The proposed unroll factor
	/// @returns estimated benefit score (higher is better)
	size_t estimateUnrollBenefit(
		ForLoop const& _loop,
		size_t _iterCount,
		size_t _factor
	);
	
	/// Determines the optimal unroll factor for a loop.
	/// @param _loop The loop to analyze
	/// @param _iterCount The predicted iteration count
	/// @returns suggested unroll factor (0 means don't unroll)
	size_t determineUnrollFactor(ForLoop const& _loop, size_t _iterCount);

	Dialect const& m_dialect;
	
	// Tuning parameters - these control the aggressiveness of unrolling
	static constexpr size_t MAX_UNROLL_FACTOR = 8;
	static constexpr size_t MAX_TOTAL_ITERATIONS = 32;  // Don't unroll loops with more iterations
	static constexpr size_t MIN_CONDITION_COMPLEXITY = 3;  // Minimum ops in condition to be "heavy"
	static constexpr size_t CODE_SIZE_THRESHOLD = 100;  // Max body size to consider unrolling
};

}
