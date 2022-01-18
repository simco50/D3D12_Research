#pragma once

namespace ShaderGraph
{
	class Compiler;
	struct Expression;

	extern int gExpressionID;
	constexpr static int INVALID_INDEX = -1;

	enum class ValueType
	{
		Invalid = 0,

		Float1 = 1 << 0,
		Float2 = 1 << 1,
		Float3 = 1 << 2,
		Float4 = 1 << 3,
		Texture2D = 1 << 4,

		UInt1 = 1 << 5,
		UInt2 = 1 << 6,
		UInt3 = 1 << 7,
		UInt4 = 1 << 8,

		UInt = UInt1 | UInt2 | UInt3 | UInt4,
		Float = Float1 | Float2 | Float3 | Float4,
		Numeric = UInt | Float,
		Texture = Texture2D,
		All = Numeric | Texture,
	};

	DECLARE_BITMASK_TYPE(ValueType)

	enum class ShaderType
	{
		Invalid = 0,
		Vertex = 1 << 0,
		Pixel = 1 << 1,
		Compute = 1 << 2,
	};

	DECLARE_BITMASK_TYPE(ShaderType)

	enum class SystemValue
	{
		ThreadID,
	};

	struct SystemValueData
	{
		ValueType ValueType;
		const char* pSymbolName;
		const char* pSemantic;
		ShaderType ShaderType;
	};

	constexpr SystemValueData SystemValues[] =
	{
		{ ValueType::UInt3, "ThreadID", "SV_ThreadID", ShaderType::Compute },
		{ ValueType::UInt1, "PrimitiveID", "SV_PrimitiveID", ShaderType::Pixel }
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
		ExpressionInput(const char* pName, float defaultValue)
			: Name(pName), ID(gExpressionID++), DefaultValue(defaultValue), HasDefaultValue(true)
		{}

		ExpressionInput(const char* pName = "In")
			: Name(pName), ID(gExpressionID++), DefaultValue(0), HasDefaultValue(false)
		{}

		int Compile(Compiler& compiler) const;

		void Connect(Expression* pConnectExpression, int outputIndex = 0)
		{
			pConnectedExpression = pConnectExpression;
			ConnectedExpressionOutputIndex = outputIndex;
		}

		bool IsConnected() const { return GetConnectedOutput() != nullptr; }

		const ExpressionOutput* GetConnectedOutput() const;

		bool HasDefaultValue;
		float DefaultValue;
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
	public:
		Compiler(ShaderType context)
			: m_ShaderContext(context)
		{}

	private:

		constexpr static bool CAN_INLINE = true;

		struct ShaderChunk
		{
			ValueType Type = ValueType::Invalid;
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

		int Texture(const char* pTextureName)
		{
			return pTextureName ? AddCodeChunkInline(ValueType::Texture2D, "%s", pTextureName) : INVALID_INDEX;
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
					break;
				}
			}
			if (!pAttribute)
				return Error("Attribute '%s' is unknown", pAttributeName);

			return AddCodeChunkInline(pAttribute->Type, "interpolants.%s", pAttribute->pName);
		}

		int ViewUniform(const char* pUniformName)
		{
			const Uniform* pUniform = nullptr;
			for (const Uniform& uniform : ViewUniforms)
			{
				if (strcmp(pUniformName, uniform.pName) == 0)
					pUniform = &uniform;
			}
			if (!pUniform)
				return Error("Attribute '%s' is unknown", pUniformName);

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
						return true;
				}
				return false;
			};

			ValueType valueType = GetType(indexA);
			uint32 numComponents = GetNumComponents(valueType);
			uint32 swizzleLength = (uint32)strlen(pSwizzleString);
			if (swizzleLength <= 0 || swizzleLength > 4)
				return Error("Invalid swizzle '%s'", pSwizzleString);

			const char* pCurr = pSwizzleString;
			while (*pCurr)
			{
				if (!IsValidChar(numComponents, *pCurr))
					return Error("Invalid swizzle '%s' for type %s", pSwizzleString, ValueTypeToString(valueType));
				++pCurr;
			}

			return AddCodeChunkInline(NumComponentsToType(swizzleLength, EnumHasAnyFlags(valueType, ValueType::Float)), "%s.%s", GetParameterCode(indexA), pSwizzleString);
		}

		int Sample2D(int textureIndex, int uvIndex)
		{
			if (uvIndex == INVALID_INDEX || textureIndex == INVALID_INDEX)
				return INVALID_INDEX;

			if (GetType(textureIndex)!= ValueType::Texture2D)
				return Error("Invalid Texture input");

			int uvIndexCast = TryCast(uvIndex, ValueType::Float2);

			if (uvIndexCast == INVALID_INDEX)
				return INVALID_INDEX;

			return AddCodeChunk(ValueType::Float4, "%s.Sample(sLinearClamp, %s)", GetParameterCode(textureIndex), GetParameterCode(uvIndexCast));
		}

		int SystemValue(SystemValue systemValue)
		{
			const SystemValueData& data = SystemValues[(int)systemValue];

			if (!EnumHasAnyFlags(data.ShaderType, m_ShaderContext))
				return Error("%s is invalid to use in current shader context", data.pSemantic);

			return AddCodeChunkInline(data.ValueType, "%s", data.pSymbolName);
		}

		int TryCast(int index, ValueType destinationType)
		{
			const ShaderChunk& chunk = m_Chunks[index];

			if (chunk.Type == destinationType)
				return index;

			if (EnumHasAnyFlags(chunk.Type, ValueType::Float) && EnumHasAnyFlags(destinationType, ValueType::Float))
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
#if 0

				else if (numSourceComponents > numDestComponents)
				{
					switch (numDestComponents)
					{
					case 1: return AddCodeChunkInline(destinationType, "%s.x", chunk.Code.c_str());
					case 2: return AddCodeChunkInline(destinationType, "%s.xy", chunk.Code.c_str());
					case 3: return AddCodeChunkInline(destinationType, "%s.xyz", chunk.Code.c_str());
					}
				}
#else
				else if (numSourceComponents > numDestComponents)
				{
					return Error("Failed to cast '%s' to '%s'", ValueTypeToString(chunk.Type), ValueTypeToString(destinationType));
				}
#endif
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
			else if (EnumHasAnyFlags(chunk.Type, ValueType::UInt) && EnumHasAnyFlags(destinationType, ValueType::Float))
			{
				return index;
			}
			return Error("Failed to cast '%s' to '%s", ValueTypeToString(chunk.Type), ValueTypeToString(destinationType));
		}

		ValueType GetType(int index)
		{
			return index != INVALID_INDEX ? GetChunk(index).Type : ValueType::Invalid;
		}

		int GetNumComponents(ValueType type) const
		{
			switch (type)
			{
			case ValueType::Float1: return 1;
			case ValueType::Float2: return 2;
			case ValueType::Float3: return 3;
			case ValueType::Float4: return 4;
			case ValueType::UInt1: return 1;
			case ValueType::UInt2: return 2;
			case ValueType::UInt3: return 3;
			case ValueType::UInt4: return 4;
			default:				return INVALID_INDEX;
			}
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

		ShaderType GetContext() const
		{
			return m_ShaderContext;
		}

	private:
		int ErrorInner(const std::string& msg);

		ValueType NumComponentsToType(int components, bool isFloat) const
		{
			switch (components)
			{
			case 1:		return isFloat ? ValueType::Float1 : ValueType::UInt1;
			case 2:		return isFloat ? ValueType::Float2 : ValueType::UInt2;
			case 3:		return isFloat ? ValueType::Float3 : ValueType::UInt3;
			case 4:		return isFloat ? ValueType::Float4 : ValueType::UInt4;
			}
			return ValueType::Invalid;
		}

		const ShaderChunk& GetChunk(int index) const
		{
			check(index != INVALID_INDEX && index < m_Chunks.size());
			return m_Chunks[index];
		}

		ValueType GetCombinedType(int indexA, int indexB)
		{
			ValueType a = GetType(indexA);
			ValueType b = GetType(indexB);
			if (a == b)
			{
				return a;
			}

			bool anyFloat = EnumHasAnyFlags(a, ValueType::Float) || EnumHasAnyFlags(b, ValueType::Float);
			int numComponentsA = GetNumComponents(a);
			int numComponentsB = GetNumComponents(b);
			if (numComponentsA == numComponentsB)
			{
				return NumComponentsToType(numComponentsA, anyFloat);
			}

			if (numComponentsA == 1 || numComponentsB == 1)
			{
				return NumComponentsToType(numComponentsA == 1 ? numComponentsB : numComponentsB, anyFloat);
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
			case ValueType::UInt1: return "uint1";
			case ValueType::UInt2: return "uint2";
			case ValueType::UInt3: return "uint3";
			case ValueType::UInt4: return "uint4";
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
		ShaderType m_ShaderContext;
	};
}
