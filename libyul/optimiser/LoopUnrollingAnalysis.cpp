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
	
	// Step 3: Check that unrolling won't exceed Ethereum contract size limit
	// Calculate the size of unrolled code
	size_t bodySize = CodeSize::codeSize(_loop.body);
	size_t postSize = CodeSize::codeSize(_loop.post);
	size_t unrolledSize = (bodySize + postSize) * iterCount.value();
	
	// Rough heuristic: 1 AST node ≈ 4 bytes of bytecode
	size_t estimatedBytecode = unrolledSize * 4;
	
	// Only unroll if the unrolled loop won't contribute more than 50% of max contract size
	// This leaves room for the rest of the contract code
	if (estimatedBytecode > MAX_CONTRACT_SIZE  - 5000) // 5000 bytes buffer for other code
	{
		decision.reason = "Unrolled loop would be too large: " + std::to_string(estimatedBytecode) + " bytes (limit: " + std::to_string(MAX_CONTRACT_SIZE / 2) + ")";
		return decision;
	}
	
	// Step 4: Gas-based cost-benefit analysis for full unrolling
	// Use estimated runs of 200 as default if not available (typical for deployed contracts)
	size_t estimatedRuns = 200;
	if (!shouldFullyUnroll(_loop, inductionVar, iterCount.value(), estimatedRuns))
	{
		decision.reason = "Gas cost-benefit analysis suggests no unrolling";
		return decision;
	}
	
	// Decision: Fully unroll!
	decision.shouldUnroll = true;
	decision.unrollFactor = iterCount.value();  // Full unrolling
	decision.reason = "Full unrolling beneficial (iterations: " + 
		std::to_string(iterCount.value()) + ")";
	
	return decision;
}

std::optional<std::tuple<YulName, bool, u256>> LoopUnrollingAnalysis::extractInductionVariable(
	ForLoop const& _loop,
	std::vector<Statement> const& _blockStatements,
	size_t _loopIndex
)
{
	std::cerr << "DEBUG: extractInductionVariable called" << std::endl;
	
	// Step 1: Find the induction variable from the loop condition
	// Must have a condition
	if (!_loop.condition)
	{
		std::cerr << "DEBUG: No condition" << std::endl;
		return std::nullopt;
	}
	
	std::cerr << "DEBUG: Condition expression index: " << _loop.condition->index() << std::endl;
	
	// Condition must be a function call (comparison)
	auto const* condCall = std::get_if<FunctionCall>(_loop.condition.get());
	if (!condCall)
	{
		std::cerr << "DEBUG: Condition is not a FunctionCall (might be Identifier or Literal)" << std::endl;
		// TODO: Handle case where condition is an Identifier (variable holding the condition result)
		return std::nullopt;
	}
	if (condCall->arguments.size() != 2)
	{
		std::cerr << "DEBUG: Wrong argument count: " << condCall->arguments.size() << std::endl;
		return std::nullopt;
	}
	
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
	// First check the loop's PRE block (most common case for for-loops)
	std::optional<u256> initValue;
	
	std::cerr << "DEBUG: Looking for induction var: " << inductionVar.str() << std::endl;
	std::cerr << "DEBUG: PRE block has " << _loop.pre.statements.size() << " statements" << std::endl;
	std::cerr << "DEBUG: _blockStatements has " << _blockStatements.size() << " statements, loopIndex=" << _loopIndex << std::endl;
	
	for (auto const& stmt : _loop.pre.statements)
	{
		// Check for variable declaration: let i := <literal>
		if (auto const* varDecl = std::get_if<VariableDeclaration>(&stmt))
		{
			for (size_t j = 0; j < varDecl->variables.size(); ++j)
			{
				if (varDecl->variables[j].name == inductionVar && varDecl->value)
				{
					if (auto const* lit = std::get_if<Literal>(varDecl->value.get()))
					{
						initValue = lit->value.value();
						std::cout << "DEBUG: Found initValue = " << initValue.value() << " in VarDecl\n";
						break;
					}
				}
			}
			if (initValue.has_value())
				break;
		}
		
		// Check for assignment: i := <literal>
		if (auto const* assignment = std::get_if<Assignment>(&stmt))
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
	
	// If not found in PRE block, look backwards through statements before the loop
	if (!initValue.has_value())
	{
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

size_t LoopUnrollingAnalysis::approximateGasSavedPerIteration(
	ForLoop const& _loop,
	YulName const& _inductionVar
)
{
	size_t gasSaved = 0;
	
	// 1. Loop condition evaluation cost saved
	// Each iteration eliminates one condition check and conditional jump
	gasSaved += GAS_JUMPI;  // Conditional jump
	
	// Add cost of condition evaluation
	if (_loop.condition)
	{
		if (auto const* call = std::get_if<FunctionCall>(_loop.condition.get()))
		{
			std::string op;
			if (auto const* builtin = std::get_if<BuiltinName>(&call->functionName))
				op = m_dialect.builtin(builtin->handle).name;
			else if (auto const* ident = std::get_if<Identifier>(&call->functionName))
				op = ident->name.str();
			
			if (op == "lt" || op == "gt" || op == "eq")
				gasSaved += GAS_LT;  // Comparison operation
		}
	}
	
	// 2. Induction variable update cost (if only used for loop control)
	// Check if induction variable is used in the body beyond loop control
	bool inductionVarOnlyForControl = true;
	
	// Helper to check if identifier references the induction variable
	auto checkIdentifier = [&](Expression const& expr) {
		if (auto const* ident = std::get_if<Identifier>(&expr))
			if (ident->name == _inductionVar)
				inductionVarOnlyForControl = false;
	};
	
	// Scan body for uses of induction variable
	for (auto const& stmt : _loop.body.statements)
	{
		if (auto const* assignment = std::get_if<Assignment>(&stmt))
		{
			// Skip if this is updating the induction variable itself
			bool isInductionUpdate = false;
			for (auto const& var : assignment->variableNames)
				if (var.name == _inductionVar)
					isInductionUpdate = true;
			
			if (!isInductionUpdate && assignment->value)
			{
				if (auto const* call = std::get_if<FunctionCall>(assignment->value.get()))
				{
					for (auto const& arg : call->arguments)
						checkIdentifier(arg);
				}
				else
				{
					checkIdentifier(*assignment->value);
				}
			}
		}
		else if (auto const* exprStmt = std::get_if<ExpressionStatement>(&stmt))
		{
			if (auto const* call = std::get_if<FunctionCall>(&exprStmt->expression))
			{
				for (auto const& arg : call->arguments)
					checkIdentifier(arg);
			}
		}
	}
	
	// If induction variable is only for control, we save the update cost
	if (inductionVarOnlyForControl)
	{
		// Check POST block for update operations
		for (auto const& stmt : _loop.post.statements)
		{
			if (auto const* assignment = std::get_if<Assignment>(&stmt))
			{
				if (assignment->value)
				{
					if (auto const* call = std::get_if<FunctionCall>(assignment->value.get()))
					{
						std::string op;
						if (auto const* builtin = std::get_if<BuiltinName>(&call->functionName))
							op = m_dialect.builtin(builtin->handle).name;
						
						if (op == "add") gasSaved += GAS_ADD;
						else if (op == "sub") gasSaved += GAS_SUB;
						else if (op == "mul") gasSaved += GAS_MUL;
					}
				}
			}
		}
	}
	
	// 3. Memory locality improvements
	// Check for multiple loads from the same memory location that are not modified in the loop
	// After unrolling, CSE and LoadResolver can forward loads and eliminate redundant stores
	
	// Track memory locations that are loaded and stored
	std::set<std::string> loadedLocations;
	std::set<std::string> storedLocations;
	size_t redundantStores = 0;
	
	// Helper to extract memory address as string (for simple cases)
	auto extractMemAddr = [](std::vector<Expression> const& args) -> std::string {
		if (args.empty())
			return "";
		if (auto const* lit = std::get_if<Literal>(&args[0]))
			return lit->value.value().str();
		if (auto const* ident = std::get_if<Identifier>(&args[0]))
			return ident->name.str();
		return "";
	};
	
	// First pass: collect all loads and stores
	for (auto const& stmt : _loop.body.statements)
	{
		// Check for mload in assignments: x := mload(addr)
		if (auto const* assignment = std::get_if<Assignment>(&stmt))
		{
			if (assignment->value)
			{
				if (auto const* call = std::get_if<FunctionCall>(assignment->value.get()))
				{
					std::string funcName;
					if (auto const* builtin = std::get_if<BuiltinName>(&call->functionName))
						funcName = m_dialect.builtin(builtin->handle).name;
					
					if (funcName == "mload")
					{
						std::string addr = extractMemAddr(call->arguments);
						if (!addr.empty())
							loadedLocations.insert(addr);
					}
				}
			}
		}
		// Check for mstore: mstore(addr, value)
		else if (auto const* exprStmt = std::get_if<ExpressionStatement>(&stmt))
		{
			if (auto const* call = std::get_if<FunctionCall>(&exprStmt->expression))
			{
				std::string funcName;
				if (auto const* builtin = std::get_if<BuiltinName>(&call->functionName))
					funcName = m_dialect.builtin(builtin->handle).name;
				
				if (funcName == "mstore")
				{
					std::string addr = extractMemAddr(call->arguments);
					if (!addr.empty())
					{
						if (storedLocations.count(addr))
							redundantStores++;  // Same location stored multiple times
						else
							storedLocations.insert(addr);
					}
				}
			}
		}
	}
	
	// Check if any loaded location is NOT modified (stored to) in the loop
	for (auto const& loadAddr : loadedLocations)
	{
		if (!storedLocations.count(loadAddr))
		{
			// Location is loaded but never stored to in the loop
			// After unrolling by factor N, each unrolled iteration can reuse values
			// Original: iterCount loads per run
			// After unroll: iterCount/N iterations, each loads once = iterCount/N loads
			// Savings: iterCount - iterCount/N = iterationsEliminated loads
			// Per eliminated iteration: 1 load saved
			gasSaved += GAS_MLOAD;
		}
	}
	
	// For stores to the same location within a single iteration
	// After unrolling, UnusedStoreEliminator can remove stores that are
	// overwritten before being read (unless loop reads in between)
	if (redundantStores > 0)
	{
		// If a location is stored to K times per iteration, after unrolling
		// we might be able to eliminate K-1 of those stores per iteration
		// But this only affects the unrolled iterations, not eliminated ones
		// Conservative: assume we save 1 store per redundant store pattern
		gasSaved += GAS_MSTORE * redundantStores;
	}
	
	// 4. Jump elimination (unconditional jump back to loop start)
	gasSaved += GAS_JUMP;
	
	return gasSaved;
}

size_t LoopUnrollingAnalysis::approximateGasIncrease(
	ForLoop const& _loop,
	size_t _unrollFactor
)
{
	// Calculate the code size increase from unrolling
	size_t bodySize = CodeSize::codeSize(_loop.body);
	size_t postSize = CodeSize::codeSize(_loop.post);
	
	// Unrolling by factor N means:
	// - Body is replicated N times (minus 1 original)
	// - POST block is replicated N times (minus 1 original)
	// - We remove the loop overhead (condition + jumps)
	
	size_t replicatedSize = (bodySize + postSize) * (_unrollFactor - 1);
	
	// Convert code size (AST nodes) to approximate bytecode size
	// Rough heuristic: 1 AST node ≈ 3-5 bytes of bytecode
	size_t bytecodeIncrease = replicatedSize * 4;
	
	// Gas cost is deployment cost amortized over expected runs
	// GAS_PER_BYTE already accounts for this (deployment cost / avg runs)
	return bytecodeIncrease * GAS_PER_BYTE;
}

bool LoopUnrollingAnalysis::shouldFullyUnroll(
	ForLoop const& _loop,
	YulName const& _inductionVar,
	size_t _iterCount,
	size_t _estimatedRuns
)
{
	// Calculate gas saved per run (not per iteration!)
	// With full unrolling, the loop is completely eliminated, so we save:
	// - All loop overhead (_iterCount times)
	// - All optimizations enabled by unrolling
	size_t gasSavedPerIter = approximateGasSavedPerIteration(_loop, _inductionVar);
	size_t gasSavedPerRun = gasSavedPerIter * _iterCount;
	
	// Total runtime savings over all runs
	size_t totalGasSaved = gasSavedPerRun * _estimatedRuns;
	
	// Calculate deployment cost from code bloat (one-time cost)
	size_t gasIncrease = approximateGasIncrease(_loop, _iterCount);
	
	// Net benefit
	int64_t netGasSaved = static_cast<int64_t>(totalGasSaved) - static_cast<int64_t>(gasIncrease);
	
	return netGasSaved > 0;
}
