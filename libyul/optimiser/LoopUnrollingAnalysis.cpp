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

#include <libyul/optimiser/LoopUnrollingAnalysis.h>

#include <libyul/optimiser/Metrics.h>
#include <libyul/optimiser/DataFlowAnalyzer.h>
#include <libyul/AST.h>
#include <libsolutil/CommonData.h>

#include <iostream>
#include <limits>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::util;

UnrollDecision LoopUnrollingAnalysis::analyzeLoop(
	ForLoop const& _loop,
	std::vector<Statement> const& _blockStatements,
	size_t _loopIndex,
	std::set<YulName> const& _ssaVariables
)
{
	UnrollDecision decision;
	
	// TODO: Use _ssaVariables for SSA-based analysis once implemented
	(void)_ssaVariables;  // Suppress unused warning
	
	// Step 1: Extract induction variable and its initial value
	auto inductionInfo = extractInductionVariable(_loop, _blockStatements, _loopIndex);
	if (!inductionInfo.has_value())
	{
		decision.reason = "No induction variable or initial value found";
		return decision;
	}
	
	auto [inductionVar, varIsFirstArg, initValue] = *inductionInfo;
	
	// Step 2: Try to predict iteration count
	std::optional<size_t> iterCount = predictIterationCount(_loop, inductionVar, varIsFirstArg, initValue);
	if (!iterCount.has_value())
	{
		decision.reason = "Iteration count not predictable";
		return decision;
	}
	
	// Don't unroll loops that are too large
	if (iterCount.value() > MAX_TOTAL_ITERATIONS)
	{
		decision.reason = "Too many iterations: " + std::to_string(iterCount.value());
		return decision;
	}
	
	// Step 3: Check code size constraints
	size_t bodySize = CodeSize::codeSize(_loop.body);
	if (bodySize > CODE_SIZE_THRESHOLD)
	{
		decision.reason = "Loop body too large: " + std::to_string(bodySize);
		return decision;
	}
	
	// Step 4: Check effectiveness - does unrolling provide benefits?
	bool conditionHeavy = isConditionHeavy(_loop);
	bool bodyOptimizable = isBodyOptimizable(_loop);
	
	if (!conditionHeavy && !bodyOptimizable)
	{
		decision.reason = "No significant benefit from unrolling";
		return decision;
	}
	
	// Step 5: Determine the optimal unroll factor
	size_t unrollFactor = determineUnrollFactor(_loop, iterCount.value());
	if (unrollFactor == 0)
	{
		decision.reason = "Cost-benefit analysis suggests no unrolling";
		return decision;
	}
	
	// Decision: Unroll!
	decision.shouldUnroll = true;
	decision.unrollFactor = unrollFactor;
	decision.reason = "Unrolling beneficial (iterations: " + 
		std::to_string(iterCount.value()) + ", factor: " + 
		std::to_string(unrollFactor) + ")";
	
	return decision;
}

std::optional<std::tuple<YulName, bool, u256>> LoopUnrollingAnalysis::extractInductionVariable(
	ForLoop const& _loop,
	std::vector<Statement> const& _blockStatements,
	size_t _loopIndex
)
{
	// Step 1: Find the induction variable from the loop condition
	// Must have a condition
	if (!_loop.condition)
		return std::nullopt;
	
	// Condition must be a function call (comparison)
	auto const* condCall = std::get_if<FunctionCall>(_loop.condition.get());
	if (!condCall || condCall->arguments.size() != 2)
		return std::nullopt;
	
	// Extract function name
	std::string condOp;
	if (auto const* builtinName = std::get_if<BuiltinName>(&condCall->functionName))
		condOp = m_dialect.builtin(builtinName->handle).name;
	else if (auto const* identName = std::get_if<Identifier>(&condCall->functionName))
		condOp = identName->name.str();
	else
		return std::nullopt;
	
	// Must be a comparison operator
	if (condOp != "lt" && condOp != "gt" && condOp != "eq" && condOp != "iszero")
		return std::nullopt;
	
	// Find which argument is the induction variable (identifier)
	// The other must be a literal (the bound)
	YulName inductionVar;
	bool varIsFirstArg = false;
	
	// Check if first arg is variable, second is literal
	if (auto const* ident = std::get_if<Identifier>(&condCall->arguments[0]))
	{
		if (std::holds_alternative<Literal>(condCall->arguments[1]))
		{
			inductionVar = ident->name;
			varIsFirstArg = true;
		}
	}
	// Or first arg is literal, second is variable
	else if (std::holds_alternative<Literal>(condCall->arguments[0]))
	{
		if (auto const* ident = std::get_if<Identifier>(&condCall->arguments[1]))
		{
			inductionVar = ident->name;
			varIsFirstArg = false;
		}
	}
	
	if (inductionVar.empty())
		return std::nullopt;
	
	// Step 2: Search for the initial value of the induction variable
	// Look backwards through statements before the loop
	std::optional<u256> initValue;
	
	for (size_t i = _loopIndex; i > 0; --i)
	{
		size_t idx = i - 1;
		
		// Check for variable declaration: let i := <literal>
		if (auto const* varDecl = std::get_if<VariableDeclaration>(&_blockStatements[idx]))
		{
			for (size_t j = 0; j < varDecl->variables.size(); ++j)
			{
				if (varDecl->variables[j].name == inductionVar && varDecl->value)
				{
					if (auto const* lit = std::get_if<Literal>(varDecl->value.get()))
					{
						initValue = lit->value.value();
						break;
					}
				}
			}
			if (initValue.has_value())
				break;
		}
		
		// Check for assignment: i := <literal>
		if (auto const* assignment = std::get_if<Assignment>(&_blockStatements[idx]))
		{
			for (size_t j = 0; j < assignment->variableNames.size(); ++j)
			{
				if (assignment->variableNames[j].name == inductionVar)
				{
					if (auto const* lit = std::get_if<Literal>(assignment->value.get()))
					{
						initValue = lit->value.value();
						break;
					}
				}
			}
			if (initValue.has_value())
				break;
		}
	}
	
	// If no initial value found, cannot proceed
	if (!initValue.has_value())
		return std::nullopt;
	
	return std::make_tuple(inductionVar, varIsFirstArg, *initValue);
}

std::optional<size_t> LoopUnrollingAnalysis::predictIterationCount(
	ForLoop const& _loop,
	YulName const& _inductionVar,
	bool _varIsFirstArg,
	u256 _initValue
)
{
	// Step 1: Extract bound and comparison operator from condition
	auto const* condCall = std::get_if<FunctionCall>(_loop.condition.get());
	if (!condCall)
		return std::nullopt;
	
	std::string condOp;
	if (auto const* builtinName = std::get_if<BuiltinName>(&condCall->functionName))
		condOp = m_dialect.builtin(builtinName->handle).name;
	else if (auto const* identName = std::get_if<Identifier>(&condCall->functionName))
		condOp = identName->name.str();
	else
		return std::nullopt;
	
	// Extract bound literal (opposite side from induction variable)
	Literal const* boundLiteral = nullptr;
	if (_varIsFirstArg)
		boundLiteral = std::get_if<Literal>(&condCall->arguments[1]);
	else
		boundLiteral = std::get_if<Literal>(&condCall->arguments[0]);
	
	if (!boundLiteral)
		return std::nullopt;
	
	// Step 2: Find all updates to the induction variable (in both POST and BODY)
	// This handles for-loops, while-loops, and loops with multiple updates
	struct Update {
		std::string operation;  // "add", "sub", "mul"
		u256 value;
	};
	std::vector<Update> updates;
	
	// Helper lambda to extract an update from an assignment
	auto extractUpdate = [&](Assignment const* assignment) -> std::optional<Update> {
		if (!assignment || assignment->variableNames.size() != 1)
			return std::nullopt;
		
		if (assignment->variableNames[0].name != _inductionVar)
			return std::nullopt;
		
		auto const* updateCall = std::get_if<FunctionCall>(assignment->value.get());
		if (!updateCall || updateCall->arguments.size() != 2)
			return std::nullopt;
		
		// Extract operation name
		std::string updateOp;
		if (auto const* builtinName = std::get_if<BuiltinName>(&updateCall->functionName))
			updateOp = m_dialect.builtin(builtinName->handle).name;
		else if (auto const* identName = std::get_if<Identifier>(&updateCall->functionName))
			updateOp = identName->name.str();
		else
			return std::nullopt;
		
		// Only support add, sub, mul
		if (updateOp != "add" && updateOp != "sub" && updateOp != "mul")
			return std::nullopt;
		
		// Find the literal argument (the other must be the induction variable)
		Literal const* stepLiteral = nullptr;
		if (auto const* ident = std::get_if<Identifier>(&updateCall->arguments[0]))
		{
			if (ident->name == _inductionVar)
				stepLiteral = std::get_if<Literal>(&updateCall->arguments[1]);
		}
		else if (auto const* lit = std::get_if<Literal>(&updateCall->arguments[0]))
		{
			if (auto const* ident = std::get_if<Identifier>(&updateCall->arguments[1]))
			{
				if (ident->name == _inductionVar)
					stepLiteral = lit;
			}
		}
		
		if (!stepLiteral)
			return std::nullopt;
		
		return Update{updateOp, stepLiteral->value.value()};
	};
	
	// Check POST block
	for (auto const& stmt : _loop.post.statements)
	{
		if (auto const* assignment = std::get_if<Assignment>(&stmt))
		{
			if (auto update = extractUpdate(assignment))
				updates.push_back(*update);
		}
	}
	
	// Check BODY
	for (auto const& stmt : _loop.body.statements)
	{
		if (auto const* assignment = std::get_if<Assignment>(&stmt))
		{
			if (auto update = extractUpdate(assignment))
				updates.push_back(*update);
		}
	}
	
	// Must have at least one update
	if (updates.empty())
		return std::nullopt;
	
	// Step 3: Calculate the net effect per iteration
	// For simplicity, we only handle:
	// - All updates are "add" (net positive increment)
	// - All updates are "sub" (net negative decrement)  
	// - Single "mul" update (geometric progression)
	
	bool allAdd = true;
	bool allSub = true;
	bool singleMul = (updates.size() == 1 && updates[0].operation == "mul");
	
	for (auto const& update : updates)
	{
		if (update.operation != "add") allAdd = false;
		if (update.operation != "sub") allSub = false;
	}
	
	std::string effectiveOp;
	u256 effectiveStep;
	
	if (allAdd)
	{
		// Sum all the additions
		effectiveOp = "add";
		effectiveStep = 0;
		for (auto const& update : updates)
			effectiveStep += update.value;
	}
	else if (allSub)
	{
		// Sum all the subtractions
		effectiveOp = "sub";
		effectiveStep = 0;
		for (auto const& update : updates)
			effectiveStep += update.value;
	}
	else if (singleMul)
	{
		effectiveOp = "mul";
		effectiveStep = updates[0].value;
	}
	else
	{
		// Mixed operations or unsupported pattern
		return std::nullopt;
	}
	
	// Step 4: Use the provided initial value
	u256 init = _initValue;
	
	// Step 5: Calculate iteration count
	// Parse the numeric values
	try
	{
		u256 bound = boundLiteral->value.value();
		u256 step = effectiveStep;
		
		if (step == 0)
			return std::nullopt;  // Infinite loop or no progress
		
		u256 iterCount;
		
		// Calculate based on operation and comparison
		if (effectiveOp == "add")
		{
			// Incrementing loop
			if (condOp == "lt" && _varIsFirstArg)  // i < bound
			{
				if (init >= bound)
					return 0;  // Never executes
				iterCount = (bound - init + step - 1) / step;  // Ceiling division
			}
			else if (condOp == "gt" && !_varIsFirstArg)  // bound > i (same as i < bound)
			{
				if (init >= bound)
					return 0;
				iterCount = (bound - init + step - 1) / step;
			}
			else
				return std::nullopt;  // Other patterns not yet supported
		}
		else if (effectiveOp == "sub")
		{
			// Decrementing loop
			if (condOp == "gt" && _varIsFirstArg)  // i > bound
			{
				if (init <= bound)
					return 0;  // Never executes
				iterCount = (init - bound + step - 1) / step;  // Ceiling division
			}
			else if (condOp == "lt" && !_varIsFirstArg)  // bound < i (same as i > bound)
			{
				if (init <= bound)
					return 0;
				iterCount = (init - bound + step - 1) / step;
			}
			else
				return std::nullopt;  // Other patterns not yet supported
		}
		else if (effectiveOp == "mul")
		{
			// Geometric progression - calculate logarithmically
			if (step <= 1)
				return std::nullopt;  // Infinite or no progress
			
			u256 current = init;
			size_t count = 0;
			constexpr size_t MAX_ITER = 1000;  // Safety limit
			
			if (condOp == "lt" && _varIsFirstArg)  // i < bound
			{
				while (current < bound && count < MAX_ITER)
				{
					current *= step;
					count++;
				}
			}
			else if (condOp == "gt" && _varIsFirstArg)  // i > bound
			{
				while (current > bound && count < MAX_ITER)
				{
					current *= step;
					count++;
				}
			}
			else
				return std::nullopt;
			
			if (count >= MAX_ITER)
				return std::nullopt;  // Too many iterations
			
			iterCount = u256(count);
		}
		else
		{
			return std::nullopt;  // Unsupported operation
		}
		
		// Convert to size_t, checking for overflow
		if (iterCount > u256(std::numeric_limits<size_t>::max()))
			return std::nullopt;
		
		return static_cast<size_t>(iterCount);
	}
	catch (...)
	{
		return std::nullopt;  // Parsing or arithmetic error
	}
}

bool LoopUnrollingAnalysis::isConditionHeavy(ForLoop const& _loop)
{
	// TODO: Implement condition complexity analysis
	// Count the number of operations in the condition expression
	// Consider it "heavy" if it exceeds MIN_CONDITION_COMPLEXITY
	
	if (!_loop.condition)
		return false;
	
	// For now, use a simple metric: any non-trivial condition is considered heavy
	// A trivial condition is just an identifier or literal
	if (std::holds_alternative<Identifier>(*_loop.condition) ||
		std::holds_alternative<Literal>(*_loop.condition))
		return false;
	
	return true;  // Conservative: assume other conditions are heavy
}

bool LoopUnrollingAnalysis::isBodyOptimizable(ForLoop const& _loop)
{
	// Body is optimizable if it has memory locality or CSE opportunities
	return hasMemoryLocality(_loop) || hasCSEOpportunities(_loop);
}

bool LoopUnrollingAnalysis::hasMemoryLocality(ForLoop const& _loop)
{
	// TODO: Implement memory locality analysis
	// This requires:
	// 1. Identifying memory access patterns (mload/mstore)
	// 2. Checking if accesses are to sequential or strided locations
	// 3. Determining if the induction variable is used in address calculations
	
	(void)_loop;  // Suppress unused warning
	// For now, return false (conservative)
	return false;
}

bool LoopUnrollingAnalysis::hasCSEOpportunities(ForLoop const& _loop)
{
	// TODO: Implement CSE opportunity detection
	// This requires:
	// 1. Finding repeated subexpressions in the loop body
	// 2. Checking if they would become loop-invariant after unrolling
	
	(void)_loop;  // Suppress unused warning
	// For now, return false (conservative)
	return false;
}

size_t LoopUnrollingAnalysis::estimateUnrollBenefit(
	ForLoop const& _loop,
	size_t _iterCount,
	size_t _factor
)
{
	// TODO: Implement cost-benefit estimation
	// Benefits:
	// - Eliminated loop overhead (condition checks, jumps)
	// - Better optimization opportunities (CSE, constant propagation)
	// - Improved instruction scheduling
	//
	// Costs:
	// - Increased code size
	// - Potential instruction cache pressure
	
	(void)_loop;       // Suppress unused warning
	(void)_iterCount;  // Suppress unused warning
	(void)_factor;     // Suppress unused warning
	// For now, return 0 (no benefit)
	return 0;
}

size_t LoopUnrollingAnalysis::determineUnrollFactor(ForLoop const& _loop, size_t _iterCount)
{
	// Simple strategy:
	// 1. If iterations <= 4, fully unroll
	// 2. If iterations <= MAX_TOTAL_ITERATIONS/2, partially unroll by factor of 2-4
	// 3. Otherwise, don't unroll (return 0)
	
	(void)_loop;  // For now, don't consider loop body size
	
	if (_iterCount == 0)
		return 0;  // No iterations, no unrolling
	
	if (_iterCount <= 4)
		return _iterCount;  // Fully unroll small loops
	
	if (_iterCount <= 16)
		return 4;  // Unroll by 4x for medium loops
	
	if (_iterCount <= MAX_TOTAL_ITERATIONS)
		return 2;  // Unroll by 2x for larger loops
	
	return 0;  // Too many iterations, don't unroll
}
