#include "stdafx.h"
#include "MaterialGraph.h"
#include "Expressions.h"

namespace ShaderGraph
{
	int gExpressionID = 0;

	std::map<const char*, ExpressionFactory> gFactories ={};

	int ExpressionInput::Compile(Compiler& compiler) const
	{
		if (pConnectedExpression)
		{
			return compiler.CompileExpression(ExpressionKey(pConnectedExpression, ConnectedExpressionOutputIndex));
		}
		return compiler.Error("Expression input '%s' not connected", Name.c_str());
	}

	const ExpressionOutput* ExpressionInput::GetConnectedOutput() const
	{
		if (!pConnectedExpression)
			return nullptr;
		const std::vector<ExpressionOutput>& outputs = pConnectedExpression->GetOutputs();
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
		m_ExpressionStack.push_back(key);
		int result = key.pExpression->Compile(*this, key.OutputIndex);
		m_ExpressionCache.emplace_back(key, result);
		m_ExpressionStack.pop_back();
		return result;
	}

	int Compiler::ErrorInner(const std::string& msg)
	{
		if (m_ExpressionStack.size() > 0)
		{
			const ExpressionKey& key = m_ExpressionStack.back();
			m_Errors.push_back(CompileError(Sprintf("Expression %s - %s", key.pExpression->GetName(), msg.c_str()).c_str(), key));
		}
		else
		{
			m_Errors.push_back(CompileError(msg.c_str()));
		}
		return INVALID_INDEX;
	}
}
