#pragma once
# include "imnodes.h"

struct GraphTexture
{
	const char* pName;
};

namespace ShaderGraph
{
	class Compiler;
	struct Expression;

	extern int gExpressionID;
	constexpr static int INVALID_INDEX = -1;
	using Texture = GraphTexture;

	enum ValueType
	{
		Invalid = 0,

		Float1 = 1 << 0,
		Float2 = 1 << 1,
		Float3 = 1 << 2,
		Float4 = 1 << 3,
		Texture2D = 1 << 4,

		Float = Float1 | Float2 | Float3 | Float4,
	};

	struct Uniform
	{
		const char* pName;
		ValueType Type;
	};

	constexpr Uniform VertexAttributes[] =
	{
		{ "UV", ValueType::Float2 },
		{ "Normal", ValueType::Float3 },
		{ "WorldPosition", ValueType::Float3 },
		{ "VertexID", ValueType::Float1 },
	};

	constexpr Uniform ViewUniforms[] =
	{
		{ "Time", ValueType::Float },
	};

	struct ExpressionOutput
	{
		ExpressionOutput(const char* pName = "")
			: Name(pName), ID(gExpressionID++)
		{}
		std::string Name;
		int ID;
	};

	// Represents an input expression that can be connected to an output expression
	struct ExpressionInput
	{
		ExpressionInput(const char* pName = "In")
			: Name(pName), ID(gExpressionID++)
		{}

		int Compile(Compiler& compiler) const;

		void Connect(Expression* pConnectExpression, int outputIndex = 0)
		{
			pConnectedExpression = pConnectExpression;
			ConnectedExpressionOutputIndex = outputIndex;
		}

		bool IsConnected() const { return GetConnectedOutput() != nullptr; }

		const ExpressionOutput* GetConnectedOutput() const;

		Expression* pConnectedExpression = nullptr;
		int ConnectedExpressionOutputIndex = 0;
		std::string Name = "";
		int ID;
	};

	// Used to identify an expression that can be compiled
	struct ExpressionKey
	{
		ExpressionKey(Expression* pConnectExpression = nullptr, int outputIndex = 0)
			: pExpression(pConnectExpression), OutputIndex(outputIndex)
		{}
		bool operator==(const ExpressionKey& other) const
		{
			return pExpression == other.pExpression &&
				OutputIndex == other.OutputIndex;
		}
		Expression* pExpression;
		int OutputIndex;
	};

	class Compiler
	{
		constexpr static bool CAN_INLINE = true;

		struct ShaderChunk
		{
			ValueType Type = Invalid;
			StringHash Hash;
			std::string Code;
			std::string SymbolName;
			bool IsInline;
		};

	public:
		struct CompileError
		{
			CompileError(const char* pMessage, ExpressionKey key = ExpressionKey())
				: Message(pMessage), Expression(key)
			{
			}
			std::string Message;
			ExpressionKey Expression;
		};

		int Constant(float value)
		{
			return AddCodeChunkInline(ValueType::Float1, "%ff", value);
		}

		int Texture(Texture* pTexture)
		{
			return pTexture ? AddCodeChunkInline(ValueType::Texture2D, "%s", pTexture->pName) : INVALID_INDEX;
		}

		int Add(int indexA, int indexB)
		{
			if (indexA == INVALID_INDEX || indexB == INVALID_INDEX)
				return INVALID_INDEX;

			ValueType resultType = GetCombinedType(indexA, indexB);
			if (resultType == ValueType::Invalid)
				return INVALID_INDEX;

			return AddCodeChunk(resultType, "%s + %s", GetParameterCode(indexA), GetParameterCode(indexB));
		}

		int Power(int indexA, int indexB)
		{
			if (indexA == INVALID_INDEX || indexB == INVALID_INDEX)
				return INVALID_INDEX;

			ValueType resultType = GetCombinedType(indexA, indexB);
			if (resultType == ValueType::Invalid)
				return INVALID_INDEX;

			return AddCodeChunk(resultType, "pow(%s, %s)", GetParameterCode(indexA), GetParameterCode(indexB));
		}

		int VertexAttribute(const char* pAttributeName)
		{
			const Uniform* pAttribute = nullptr;
			for (const Uniform& attrib : VertexAttributes)
			{
				if (strcmp(pAttributeName, attrib.pName) == 0)
				{
					pAttribute = &attrib;
				}
			}
			if (!pAttribute)
			{
				return Error("Attribute '%s' is unknown", pAttributeName);
			}
			return AddCodeChunkInline(pAttribute->Type, "interpolants.%s", pAttribute->pName);
		}

		int ViewUniform(const char* pUniformName)
		{
			const Uniform* pUniform = nullptr;
			for (const Uniform& uniform : ViewUniforms)
			{
				if (strcmp(pUniformName, uniform.pName) == 0)
				{
					pUniform = &uniform;
				}
			}
			if (!pUniform)
			{
				return Error("Attribute '%s' is unknown", pUniformName);
			}
			return AddCodeChunkInline(pUniform->Type, "cView.%s", pUniform->pName);
		}

		int Swizzle(int indexA, const char* pSwizzleString)
		{
			if (indexA == INVALID_INDEX || !pSwizzleString)
				return INVALID_INDEX;

			auto IsValidChar = [](uint32 numComponents, char swizzleChar) {
				const char pValidChars[] = { 'x', 'r', 'y', 'g', 'z', 'b', 'w', 'a' };
				for (uint32 i = 0; i < numComponents * 2; ++i)
				{
					if (swizzleChar == pValidChars[i])
					{
						return true;
					}
				}
				return false;
			};

			ValueType valueType = GetType(indexA);
			uint32 numComponents = GetNumComponents(valueType);
			uint32 swizzleLength = (uint32)strlen(pSwizzleString);
			if (swizzleLength <= 0 || swizzleLength > 4)
			{
				return Error("Invalid swizzle '%s'", pSwizzleString);
			}

			const char* pCurr = pSwizzleString;
			while (*pCurr)
			{
				if (!IsValidChar(numComponents, *pCurr))
				{
					return Error("Invalid swizzle '%s' for type %s", pSwizzleString, ValueTypeToString(valueType));
				}
				++pCurr;
			}

			return AddCodeChunkInline(NumComponentsToType(swizzleLength), "%s.%s", GetParameterCode(indexA), pSwizzleString);
		}

		int Sample2D(int textureIndex, int uvIndex)
		{
			if (uvIndex == INVALID_INDEX || textureIndex == INVALID_INDEX)
				return INVALID_INDEX;

			if (GetType(textureIndex)!= ValueType::Texture2D)
			{
				return Error("Invalid Texture input");
			}

			int uvIndexCast = TryCast(uvIndex, ValueType::Float2);

			if (uvIndexCast == INVALID_INDEX)
			{
				return INVALID_INDEX;
			}
			return AddCodeChunk(ValueType::Float4, "%s.Sample(sLinearClamp, %s)", GetParameterCode(textureIndex), GetParameterCode(uvIndexCast));
		}

		template<typename ...Args>
		int Error(const char* pFormat, Args... args)
		{
			return ErrorInner(Sprintf(pFormat, std::forward<Args>(args)...));
		}

		int CompileExpression(const ExpressionKey& input);

		const char* GetSource() const
		{
			return m_Source.c_str();
		}

		const std::vector<CompileError>& GetErrors() const
		{
			return m_Errors;
		}

	private:
		int ErrorInner(const std::string& msg);

		int TryCast(int index, ValueType destinationType)
		{
			const ShaderChunk& chunk = m_Chunks[index];
			if (chunk.Type == destinationType)
			{
				return index;
			}
			if ((chunk.Type & ValueType::Float) && (destinationType & ValueType::Float))
			{
				int numSourceComponents = GetNumComponents(chunk.Type);
				int numDestComponents = GetNumComponents(destinationType);
				if (numSourceComponents == 1 && numDestComponents > 1)
				{
					switch (numDestComponents)
					{
					case 2: return AddCodeChunkInline(destinationType, "%s.xx", chunk.Code.c_str());
					case 3: return AddCodeChunkInline(destinationType, "%s.xxx", chunk.Code.c_str());
					case 4: return AddCodeChunkInline(destinationType, "%s.xxxx", chunk.Code.c_str());
					}
					
				}
				else if (numSourceComponents > numDestComponents)
				{
					switch (numDestComponents)
					{
					case 1: return AddCodeChunkInline(destinationType, "%s.x", chunk.Code.c_str());
					case 2: return AddCodeChunkInline(destinationType, "%s.xy", chunk.Code.c_str());
					case 3: return AddCodeChunkInline(destinationType, "%s.xyz", chunk.Code.c_str());
					}
				}
				else if (numSourceComponents < numDestComponents)
				{
					if (numSourceComponents == 1)
					{
						uint32 pad = numDestComponents - numSourceComponents;
						std::string commaSeperatedStr = Sprintf(",%s", chunk.Code.c_str());
						return AddCodeChunkInline(
							destinationType,
							"%s(%s%s%s%s)",
							ValueTypeToString(destinationType),
							chunk.Code.c_str(),
							pad >= 1 ? commaSeperatedStr.c_str() : "",
							pad >= 2 ? commaSeperatedStr.c_str() : "",
							pad >= 2 ? commaSeperatedStr.c_str() : "");
					}
				}
				else
				{
					return index;
				}
			}
			Error("Failed to cast '%s' to '%s' (%s)", ValueTypeToString(chunk.Type), ValueTypeToString(destinationType), GetParameterCode(index));
			return INVALID_INDEX;
		}

		int GetNumComponents(ValueType type) const
		{
			switch (type)
			{
			case ValueType::Float1: return 1;
			case ValueType::Float2: return 2;
			case ValueType::Float3: return 3;
			case ValueType::Float4: return 4;
			default:				return INVALID_INDEX;
			}
		}

		ValueType NumComponentsToType(int components) const
		{
			switch (components)
			{
			case 1:		return ValueType::Float1;
			case 2:		return ValueType::Float2;
			case 3:		return ValueType::Float3;
			case 4:		return ValueType::Float4;
			}
			return ValueType::Invalid;
		}

		const ShaderChunk& GetChunk(int index) const
		{
			check(index != INVALID_INDEX && index < m_Chunks.size());
			return m_Chunks[index];
		}

		ValueType GetType(int index)
		{
			return index != INVALID_INDEX ? GetChunk(index).Type : ValueType::Invalid;
		}

		ValueType GetCombinedType(int indexA, int indexB)
		{
			ValueType a = GetType(indexA);
			ValueType b = GetType(indexB);
			if (a == b)
			{
				return a;
			}
			if (a == ValueType::Float1)
			{
				return b;
			}
			if (b == ValueType::Float1)
			{
				return a;
			}
			Error("Failed to combine types '%s' and '%s' (%s & %s)", ValueTypeToString(a), ValueTypeToString(b), GetParameterCode(indexA), GetParameterCode(indexB));
			return ValueType::Invalid;
		}

		const char* ValueTypeToString(ValueType v)
		{
			switch (v)
			{
			case ValueType::Float1: return "float";
			case ValueType::Float2: return "float2";
			case ValueType::Float3: return "float3";
			case ValueType::Float4: return "float4";
			case ValueType::Texture2D: return "Texture2D";
			}
			return "float";
		}

		std::string GetSymbolName(const char* pSymbolNameHint)
		{
			std::string name = Sprintf("%s_%d", pSymbolNameHint, m_SymbolIndex);
			m_SymbolIndex++;
			return name;
		}

		const char* GetParameterCode(int index)
		{
			const ShaderChunk& chunk = m_Chunks[index];
			if (chunk.IsInline)
			{
				return chunk.Code.c_str();
			}
			return chunk.SymbolName.c_str();
		}


		template<typename ...Args>
		int AddCodeChunk(ValueType type, const char* pCode, Args... args)
		{
			std::string code = Sprintf(pCode, std::forward<Args>(args)...);
			return AddCodeChunkPrivate(type, code.c_str(), false);
		}

		template<typename ...Args>
		int AddCodeChunkInline(ValueType type, const char* pCode, Args... args)
		{
			std::string code = Sprintf(pCode, std::forward<Args>(args)...);
			return AddCodeChunkPrivate(type, code.c_str(), CAN_INLINE);
		}

		int AddCodeChunkPrivate(ValueType type, const char* pCode, bool isInline)
		{
			StringHash hash(pCode);

			ShaderChunk chunk;
			chunk.Type = type;
			chunk.Hash = hash;
			chunk.Code = pCode;
			chunk.IsInline = isInline;

			if (isInline)
			{
				m_Chunks.push_back(chunk);
			}
			else
			{
				for (size_t i = 0; i < m_Chunks.size(); ++i)
				{
					if (m_Chunks[i].Hash == hash)
					{
						return (int)i;
					}
				}

				chunk.SymbolName = GetSymbolName("_local").c_str();
				m_Chunks.push_back(chunk);
				m_Source += Sprintf("%s %s = %s;\n", ValueTypeToString(type), chunk.SymbolName.c_str(), pCode);
			}

			return (int)m_Chunks.size() - 1;
		}

		int m_SymbolIndex = 0;
		std::string m_Source;
		std::vector<CompileError> m_Errors;
		std::vector<ShaderChunk> m_Chunks;
		std::vector<std::pair<ExpressionKey, int>> m_ExpressionCache;
		std::vector<ExpressionKey> m_ExpressionStack;
	};
}
