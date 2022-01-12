#pragma once

#include <unordered_set>

struct GraphTexture
{
	const char* pName;
};

namespace ShaderGraph
{
	constexpr static int INVALID_INDEX = -1;
	using Texture = GraphTexture;

	class Compiler
	{
		constexpr static bool CAN_INLINE = true;

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

		struct ShaderChunk
		{
			ValueType Type = Invalid;
			StringHash Hash;
			std::string Code;
			std::string SymbolName;
			bool IsInline;
		};

		struct VertAttribute
		{
			const char* pName;
			ValueType Type;
		};

		std::vector<VertAttribute> VertexAttributes =
		{
			{ "UV", ValueType::Float2 },
			{ "Normal", ValueType::Float3 },
			{ "WorldPosition", ValueType::Float3 },
			{ "VertexID", ValueType::Float1 },
		};

	public:
		int Constant(float value)
		{
			return AddCodeChunkInline(ValueType::Float1, "%ff", value);
		}

		int Constant(const Vector2& value)
		{
			return AddCodeChunkInline(ValueType::Float2, "float2(%ff, %ff)", value.x, value.y);
		}

		int Constant(const Vector3& value)
		{
			return AddCodeChunkInline(ValueType::Float3, "float3(%ff, %ff, %ff)", value.x, value.y, value.z);
		}

		int Constant(const Vector4& value)
		{
			return AddCodeChunkInline(ValueType::Float4, "float4(%ff, %ff, %ff, %ff)", value.x, value.y, value.z, value.w);
		}

		int Texture(Texture* pTexture)
		{
			return AddCodeChunkInline(ValueType::Texture2D, "%s", pTexture->pName);
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
			const VertAttribute* pAttribute = nullptr;
			for (const VertAttribute& attrib : VertexAttributes)
			{
				if (strcmp(pAttributeName, attrib.pName) == 0)
				{
					pAttribute = &attrib;
				}
			}

			if (!pAttribute)
			{
				Error("Attribute '%s' is unknown", pAttributeName);
				return INVALID_INDEX;
			}

			return AddCodeChunkInline(pAttribute->Type, "interpolants.%s", pAttribute->pName);
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

			ValueType valueType = GetChunk(indexA).Type;
			uint32 numComponents = GetNumComponents(valueType);
			uint32 swizzleLength = (uint32)strlen(pSwizzleString);
			if (swizzleLength <= 0 || swizzleLength > 4)
			{
				Error("Invalid swizzle '%s'", pSwizzleString);
				return INVALID_INDEX;
			}

			const char* pCurr = pSwizzleString;
			while (*pCurr)
			{
				if (!IsValidChar(numComponents, *pCurr))
				{
					Error("Invalid swizzle '%s' for type %s", pSwizzleString, ValueTypeToString(valueType));
					return INVALID_INDEX;
				}
				++pCurr;
			}

			return AddCodeChunkInline(NumComponentsToType(swizzleLength), "%s.%s", GetParameterCode(indexA), pSwizzleString);
		}

		int Sample2D(int textureIndex, int uvIndex)
		{
			if (uvIndex == INVALID_INDEX || textureIndex == INVALID_INDEX)
				return INVALID_INDEX;

			if (GetChunk(textureIndex).Type != ValueType::Texture2D)
			{
				Error("Invalid Texture input");
				return INVALID_INDEX;
			}

			int uvIndexCast = TryCast(uvIndex, ValueType::Float2);

			if (uvIndexCast == INVALID_INDEX)
			{
				return INVALID_INDEX;
			}
			return AddCodeChunk(ValueType::Float4, "%s.Sample(sLinearClamp, %s)", GetParameterCode(textureIndex), GetParameterCode(uvIndexCast));
		}

		const char* GetSource() const
		{
			return m_Source.c_str();
		}

		std::string GetError() const
		{
			std::string msg;
			for (const std::string& err : m_Errors)
			{
				msg += err + std::string("\n");
			}
			return msg;
		}

	private:
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
			return m_Chunks[index];
		}

		ShaderChunk& GetChunk(int index)
		{
			return m_Chunks[index];
		}

		ValueType GetCombinedType(int indexA, int indexB)
		{
			ValueType a = GetChunk(indexA).Type;
			ValueType b = GetChunk(indexB).Type;
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
			std::string name = Sprintf("%s_%d", pSymbolNameHint, m_Index);
			m_Index++;
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

	private:

		template<typename ...Args>
		int AddCodeChunkInline(ValueType type, const char* pCode, Args... args)
		{
			std::string code = Sprintf(pCode, std::forward<Args>(args)...);
			return AddCodeChunkPrivate(type, code.c_str(), CAN_INLINE);
		}

		int AddCodeChunkPrivate(ValueType type, const char* pCode, bool isInline)
		{
			StringHash hash(pCode);

			if (isInline)
			{
				ShaderChunk chunk;
				chunk.Type = type;
				chunk.Hash = hash;
				chunk.Code = pCode;
				chunk.IsInline = true;
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

				ShaderChunk chunk;
				chunk.Type = type;
				chunk.Hash = hash;
				chunk.Code = pCode;
				chunk.IsInline = false;
				std::string symbolName = GetSymbolName("_local").c_str();
				m_Source += Sprintf("%s %s = %s;\n", ValueTypeToString(type), symbolName.c_str(), pCode);
				chunk.SymbolName = symbolName;
				m_Chunks.push_back(chunk);
			}

			return (int)m_Chunks.size() - 1;
		}

		template<typename ...Args>
		void Error(const char* pFormat, Args... args)
		{
			m_Errors.insert(Sprintf(pFormat, std::forward<Args>(args)...));
		}

		int m_Index = 0;
		std::string m_Source;
		std::unordered_set<std::string> m_Errors;
		std::vector<ShaderChunk> m_Chunks;
	};

	struct NodeOutput
	{
		std::string Name;
	};

	struct Node
	{
		Node()
		{
			m_Outputs.push_back({ });
		}

		virtual int Compile(Compiler& compiler, int outputIndex) const = 0;
		virtual std::vector<struct NodeInput*> GetInputs() { return {}; }
		virtual const std::vector<NodeOutput>& GetOutputs() { return m_Outputs; }
		virtual const char* GetName() const = 0;

		std::vector<NodeOutput> m_Outputs;
	};

	struct NodeInput
	{
		NodeInput(const char* pName = "")
			: InputName(pName)
		{}

		int32 Compile(Compiler& compiler) const
		{
			if (pNode)
			{
				return pNode->Compile(compiler, OutputIndex);
			}
			return INVALID_INDEX;
		}

		void Connect(Node* pConnectNode, int outputIndex = 0)
		{
			pNode = pConnectNode;
			OutputIndex = outputIndex;
		}

		bool IsConnected() const { return pNode != nullptr; }

		Node* pNode = nullptr;
		int OutputIndex = 0;
		std::string InputName = "";
	};

	template<typename T>
	struct ConstantTFloatNode : public Node
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.Constant(Value);
		}

		virtual const char* GetName() const override { return "Constant"; }

		T Value{};
	};

	using ConstantFloatNode = ConstantTFloatNode<float>;
	using ConstantFloat2Node = ConstantTFloatNode<Vector2>;
	using ConstantFloat3Node = ConstantTFloatNode<Vector3>;
	using ConstantFloat4Node = ConstantTFloatNode<Vector4>;

	struct AddNode : public Node
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.Add(
				InputA.IsConnected() ? InputA.Compile(compiler) : compiler.Constant(ConstantA),
				InputB.IsConnected() ? InputB.Compile(compiler) : compiler.Constant(ConstantB)
			);
		}

		virtual std::vector<struct NodeInput*> GetInputs() { return { &InputA, &InputB }; }
		virtual const char* GetName() const override { return "Add"; }

		NodeInput InputA;
		float ConstantA = 0;
		NodeInput InputB;
		float ConstantB = 0;
	};

	struct PowerNode : public Node
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.Power(
				InputA.IsConnected() ? InputA.Compile(compiler) : compiler.Constant(ConstantA),
				InputB.IsConnected() ? InputB.Compile(compiler) : compiler.Constant(ConstantB)
			);
		}

		virtual std::vector<struct NodeInput*> GetInputs() { return { &InputA, &InputB }; }
		virtual const char* GetName() const override { return "Power"; }

		NodeInput InputA;
		float ConstantA = 0;
		NodeInput InputB;
		float ConstantB = 0;
	};

	struct TextureNode : public Node
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.Texture(pTexture);
		}

		virtual const char* GetName() const override { return "Texture2D"; }

		Texture* pTexture = nullptr;
	};

	struct Sample2DNode : public Node
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.Sample2D(TextureInput.IsConnected() ? TextureInput.Compile(compiler) : compiler.Texture(pTexture), UVInput.Compile(compiler));
		}

		virtual std::vector<struct NodeInput*> GetInputs() { return { &TextureInput, &UVInput }; }
		virtual const char* GetName() const override { return "Sample2D"; }

		NodeInput TextureInput;
		Texture* pTexture = nullptr;
		NodeInput UVInput;
	};

	struct SwizzleNode : public Node
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.Swizzle(Input.Compile(compiler), SwizzleString.c_str());
		}

		virtual std::vector<struct NodeInput*> GetInputs() { return { &Input }; }
		virtual const char* GetName() const override { return "Swizzle"; }

		NodeInput Input;
		std::string SwizzleString;
	};

	struct VertexAttributeNode : public Node
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.VertexAttribute(pAttribute);
		}

		virtual const char* GetName() const override { return pAttribute; }

		const char* pAttribute;
	};

#if 0
	struct DataDrivenNode : public Node
	{
		DataDrivenNode(const char* pName)
		{
			ReturnType = Compiler::ValueType::Float;
			Definition = "%A:Float% + %B:Float%";

			std::stringstream token;
			const char* pCurr = Definition.c_str();

			enum Stage
			{
				Code,
				InputName,
				InputType,
			};

			Stage stage = Code;

			NodeInput currentInput;
			while (*pCurr)
			{
				if (*pCurr == '%')
				{
					if (stage == Stage::Code)
					{
						stage = Stage::InputName;
					}
					else if (stage == Stage::InputType)
					{
						token = std::stringstream();
						stage = Stage::Code;
						Inputs.push_back(currentInput);
					}
					++pCurr;
					continue;
				}
				else if (*pCurr == ':')
				{
					if (stage == Stage::InputName)
					{
						stage = Stage::InputType;
						currentInput.InputName = token.str();
						token = std::stringstream();
						++pCurr;
						continue;
					}
				}
				if(stage != Stage::Code)
					token << *pCurr;
				++pCurr;
			}
		}

		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			std::stringstream code;
			std::stringstream token;
			const char* pCurr = Definition.c_str();

			enum Stage
			{
				Code,
				InputName,
				InputType,
			};

			Stage stage = Code;

			const NodeInput* pCurrentInput = nullptr;
			while (*pCurr)
			{
				if (*pCurr == '%')
				{
					if (stage == Stage::Code)
					{
						stage = Stage::InputName;
					}
					else if (stage == Stage::InputType)
					{
						code << compiler.GetParameterCode(pCurrentInput->Compile(compiler));
						token = std::stringstream();
						stage = Stage::Code;
					}
					++pCurr;
					continue;
				}
				else if (*pCurr == ':')
				{
					if (stage == Stage::InputName)
					{
						stage = Stage::InputType;
						pCurrentInput = &*std::find_if(Inputs.begin(), Inputs.end(), [&token](const NodeInput& input) { return input.InputName == token.str(); });
						token = std::stringstream();
						++pCurr;
						continue;
					}
				}
				if (stage != Stage::Code)
					token << *pCurr;
				else
					code << *pCurr;
				++pCurr;
			}

			return compiler.AddCodeChunk(ReturnType, code.str().c_str());
		}

		const char* pAttribute;
		std::vector<NodeInput> Inputs;

	protected:
		Compiler::ValueType ReturnType;
		std::string Definition;

		virtual const char* GetName() const override { return pAttribute; }
	};
#endif
}
