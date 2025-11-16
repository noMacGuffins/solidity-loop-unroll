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
#include <libyul/optimiser/Substitution.h>
#include <libyul/Dialect.h>
#include <libyul/AST.h>
#include <libsolutil/CommonData.h>

#include <utility>
#include <map>

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
	size_t unrollFactor = 0;
	
	// Check if we should unroll this loop
	if (!shouldUnroll(_for, _blockStatements, _loopIndex, unrollFactor))
		return {};
	
	// Extract induction variable information
	auto inductionInfo = m_analyzer.extractInductionVariable(_for, _blockStatements, _loopIndex);
	if (!inductionInfo.has_value())
		return {};  // Should not happen if shouldUnroll returned true
	
	auto [inductionVar, varIsFirstArg, initValue] = *inductionInfo;
	
	// Determine the step value (how much the induction variable changes per iteration)
	// For simplicity, we only handle the common case: add(i, 1) or sub(i, 1)
	u256 stepValue = 1;
	bool isIncrement = true;
	
	// Check if it's updated in POST or BODY
	for (auto const& stmt : _for.post.statements)
	{
		if (auto const* assignment = std::get_if<Assignment>(&stmt))
		{
			if (assignment->variableNames.size() == 1 && 
			    assignment->variableNames[0].name == inductionVar)
			{
				if (auto const* call = std::get_if<FunctionCall>(assignment->value.get()))
				{
					std::string op;
					if (auto const* builtin = std::get_if<BuiltinName>(&call->functionName))
						op = m_dialect.builtin(builtin->handle).name;
					
					if (op == "add" || op == "sub")
					{
						isIncrement = (op == "add");
						// Try to extract the step literal
						if (call->arguments.size() == 2)
						{
							if (auto const* lit = std::get_if<Literal>(&call->arguments[1]))
								stepValue = lit->value.value();
							else if (auto const* lit = std::get_if<Literal>(&call->arguments[0]))
								stepValue = lit->value.value();
						}
					}
				}
			}
		}
	}
	
	// Generate the unrolled statements
	std::vector<Statement> unrolledStatements;
	
	// Store literals so they persist for the substitution references
	std::vector<Expression> literals;
	
	// First, add the PRE block statements (initialization)
	// These set up variables like "let i := 0" but may also contain other initialization
	ASTCopier copier;
	for (auto const& stmt : _for.pre.statements)
	{
		unrolledStatements.emplace_back(copier.translate(stmt));
	}
	
	// Current value of the induction variable
	u256 currentValue = initValue;
	
	for (size_t iteration = 0; iteration < unrollFactor; ++iteration)
	{
		// Create a literal expression for the current induction variable value and store it
		literals.emplace_back(Literal{
			_for.debugData,
			LiteralKind::Number,
			LiteralValue{currentValue}
		});
		Expression const* valueLiteral = &literals.back();
		
		// Create substitution map: replace induction variable with its current value
		std::map<YulName, Expression const*> substitutions;
		substitutions[inductionVar] = valueLiteral;
		
		// Copy the loop body statements with substitutions
		Substitution substituter(substitutions);
		
		// Add all statements from the body to our result
		for (auto const& stmt : _for.body.statements)
		{
			unrolledStatements.emplace_back(static_cast<ASTCopier&>(substituter).translate(stmt));
		}
		
		// Add POST block statements for all iterations (including the last one)
		// This preserves any side effects beyond just updating the induction variable
		// (e.g., if POST contains memory operations or updates to other variables)
		// The induction variable update itself (like i := add(i, 1)) becomes a dead
		// assignment after substitution and will be cleaned up by later optimizer passes
		for (auto const& stmt : _for.post.statements)
		{
			unrolledStatements.emplace_back(static_cast<ASTCopier&>(substituter).translate(stmt));
		}
		
		// Update the induction variable value for next iteration
		if (isIncrement)
			currentValue += stepValue;
		else
			currentValue -= stepValue;
	}
	
	return unrolledStatements;
}
