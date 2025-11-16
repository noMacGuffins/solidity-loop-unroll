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

#include <libyul/optimiser/ASTWalker.h>
#include <libyul/optimiser/OptimiserStep.h>
#include <libyul/optimiser/LoopUnrollingAnalysis.h>

namespace solidity::yul
{

/**
 * Loop unrolling optimization.
 *
 * This optimization unrolls loops that have predictable iteration counts and
 * exhibit optimization opportunities from unrolling (e.g., CSE, memory locality).
 *
 * Only loops that meet the following criteria are considered:
 * - Affine loop (induction variable increments by constant)
 * - Predictable iteration count
 * - Cost-benefit analysis suggests unrolling is beneficial
 *
 * Requirements:
 * - The Disambiguator, ForLoopInitRewriter and FunctionHoister must be run upfront.
 * - Expression splitter and SSA transform should be run upfront to obtain better results.
 */

class LoopUnrolling: public ASTModifier
{
public:
	static constexpr char const* name{"LoopUnrolling"};
	static void run(OptimiserStepContext& _context, Block& _ast);

	void operator()(Block& _block) override;

private:
	explicit LoopUnrolling(
		Dialect const& _dialect,
		std::set<YulName> const& _ssaVariables,
		LoopUnrollingAnalysis _analyzer
	):
		m_dialect(_dialect),
		m_ssaVariables(_ssaVariables),
		m_analyzer(std::move(_analyzer))
	{ }

	/// @returns true if the given loop should be unrolled based on heuristics.
	bool shouldUnroll(
		ForLoop const& _loop,
		std::vector<Statement> const& _blockStatements,
		size_t _loopIndex,
		size_t& _outUnrollFactor
	);
	
	/// Performs the actual loop unrolling transformation.
	/// @returns the unrolled statements if successful, nullopt otherwise.
	std::optional<std::vector<Statement>> rewriteLoop(
		ForLoop& _for,
		std::vector<Statement> const& _blockStatements,
		size_t _loopIndex
	);

	Dialect const& m_dialect;
	std::set<YulName> const& m_ssaVariables;
	LoopUnrollingAnalysis m_analyzer;
};

}
