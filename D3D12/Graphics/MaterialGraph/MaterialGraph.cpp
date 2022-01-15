#include "stdafx.h"
#include "MaterialGraph.h"

namespace ShaderGraph
{
	int gExpressionID = 0;

	int ExpressionInput::Compile(Compiler& compiler) const
	{
		if (pConnectedExpression)
		{
			return pConnectedExpression->Compile(compiler, ConnectedExpressionOutputIndex);
		}
		return compiler.Error("Expression input '%s' not connected", Name.c_str());
	}

	const ExpressionOutput* ExpressionInput::GetConnectedOutput() const
	{
		if (!pConnectedExpression)
			return nullptr;
		const std::vector<ExpressionOutput> outputs = pConnectedExpression->GetOutputs();
		check(ConnectedExpressionOutputIndex < outputs.size());
		return &outputs[ConnectedExpressionOutputIndex];
	}

}
