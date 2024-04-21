#include "stdafx.h"
#include "MaterialGraph.h"
#include "Expressions.h"

namespace ShaderGraph
{
	int gExpressionID = 0;

	std::unordered_map<const char*, ExpressionFactory> gFactories ={};

	int ExpressionInput::Compile(Compiler& compiler) const
	{
		if (pConnectedExpression)
		{
			return compiler.CompileExpression(ExpressionKey(pConnectedExpression, ConnectedExpressionOutputIndex));
		}
		else if (HasDefaultValue)
		{
			return compiler.Constant(DefaultValue);
		}
		return compiler.Error("Expression input '%s' not connected", Name.c_str());
	}

	const ExpressionOutput* ExpressionInput::GetConnectedOutput() const
	{
		if (!pConnectedExpression)
			return nullptr;
		const std::vector<ExpressionOutput>& outputs = pConnectedExpression->Outputs;
		check(ConnectedExpressionOutputIndex < outputs.size());
		return &outputs[ConnectedExpressionOutputIndex];
	}

	int Compiler::CompileExpression(const ExpressionKey& key)
	{
		auto it = std::find_if(m_ExpressionCache.begin(), m_ExpressionCache.end(), [&key](const std::pair<ExpressionKey, int>& lhs)
			{
				return key == lhs.first;
			});

		if (it != m_ExpressionCache.end())
		{
			return it->second;
		}

		if (std::find(m_ExpressionStack.begin(), m_ExpressionStack.end(), key) != m_ExpressionStack.end())
		{
			return Error("Circular loop found.");
		}

		m_ExpressionStack.push_back(key);
		int result = key.pExpression->Compile(*this, key.OutputIndex);
		m_ExpressionCache.emplace_back(key, result);
		m_ExpressionStack.pop_back();
		return result;
	}

	int Compiler::ErrorInner(const std::string& msg)
	{
		m_Errors.push_back(CompileError(msg.c_str(), m_ExpressionStack.size() ? m_ExpressionStack.back() : ExpressionKey()));
		return INVALID_INDEX;
	}
}
