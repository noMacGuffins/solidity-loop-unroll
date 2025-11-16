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

#include <libyul/optimiser/LoopUnrolling.h>

#include <libyul/optimiser/LoopUnrollingAnalysis.h>
#include <libyul/optimiser/SSAValueTracker.h>
#include <libyul/optimiser/ASTCopier.h>
#include <libyul/AST.h>
#include <libsolutil/CommonData.h>

#include <utility>

using namespace solidity;
using namespace solidity::yul;

void LoopUnrolling::run(OptimiserStepContext& _context, Block& _ast)
{
	// Gather SSA variables for analysis
	std::set<YulName> ssaVars = SSAValueTracker::ssaVariables(_ast);
	
	// Create the analyzer with the current context
	LoopUnrollingAnalysis analyzer{_context.dialect};
	
	// Run the transformation
	LoopUnrolling{_context.dialect, ssaVars, std::move(analyzer)}(_ast);
}

void LoopUnrolling::operator()(Block& _block)
{
	util::iterateReplacing(
		_block.statements,
		[&, this](Statement& _s) -> std::optional<std::vector<Statement>>
		{
			visit(_s);
			if (std::holds_alternative<ForLoop>(_s))
			{
				// Find the index of this loop in the block
				size_t loopIndex = 0;
				for (size_t i = 0; i < _block.statements.size(); ++i)
				{
					if (&_block.statements[i] == &_s)
					{
						loopIndex = i;
						break;
					}
				}
				return rewriteLoop(std::get<ForLoop>(_s), _block.statements, loopIndex);
			}
			else
				return {};
		}
	);
}

bool LoopUnrolling::shouldUnroll(
	ForLoop const& _loop,
	std::vector<Statement> const& _blockStatements,
	size_t _loopIndex,
	size_t& _outUnrollFactor
)
{
	// Use the analyzer to make the decision
	UnrollDecision decision = m_analyzer.analyzeLoop(_loop, _blockStatements, _loopIndex, m_ssaVariables);
	
	// Note: m_dialect will be used in rewriteLoop() for AST construction
	(void)m_dialect;  // Suppress unused warning for now
	
	_outUnrollFactor = decision.unrollFactor;
	return decision.shouldUnroll;
}

std::optional<std::vector<Statement>> LoopUnrolling::rewriteLoop(
	ForLoop& _for,
	std::vector<Statement> const& _blockStatements,
	size_t _loopIndex
)
{
	std::cout << "=== rewriteLoop called ===" << std::endl;
	size_t unrollFactor = 0;
	
	// Check if we should unroll this loop
	if (!shouldUnroll(_for, _blockStatements, _loopIndex, unrollFactor))
	{
		std::cout << "shouldUnroll returned false" << std::endl;
		return {};
	}
	
	std::cout << "shouldUnroll returned true with factor: " << unrollFactor << std::endl;
	
	// TODO: Implement the actual unrolling transformation
	// This is where you would:
	// 1. Clone the loop body unrollFactor times
	// 2. Substitute induction variable values
	// 3. Update loop bounds or remove the loop entirely (if fully unrolled)
	// 4. Return the transformed statements
	
	// For now, return empty (no transformation)
	return {};
}
