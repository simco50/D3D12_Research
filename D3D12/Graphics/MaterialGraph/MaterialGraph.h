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

			ValueType valueType = GetChunk(indexA).Type;
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

			if (GetChunk(textureIndex).Type != ValueType::Texture2D)
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
			m_Errors.push_back(Sprintf(pFormat, std::forward<Args>(args)...));
			return INVALID_INDEX;
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

		int m_Index = 0;
		std::string m_Source;
		std::vector<std::string> m_Errors;
		std::vector<ShaderChunk> m_Chunks;
	};

	struct Expression
	{
		Expression()
			: ID(gExpressionID++)
		{
			Outputs.push_back(ExpressionOutput(""));
		}

		void Render()
		{
			ImNodes::BeginNode(ID);

			ImNodes::BeginNodeTitleBar();
			ImGui::TextUnformatted(GetName());
			ImNodes::EndNodeTitleBar();
			ImGui::PushItemWidth(80);

			ImGui::BeginGroup();
			RenderInputs();
			ImGui::EndGroup();

			ImGui::SameLine();

			ImGui::BeginGroup();
			RenderOutputs();
			ImGui::EndGroup();

			ImGui::PopItemWidth();
			ImNodes::EndNode();
		}

		virtual void RenderInputs()
		{
			for (const ExpressionInput* input : GetInputs())
			{
				ImNodes::BeginInputAttribute(input->ID);
				ImGui::Text(input->Name.c_str());
				ImNodes::EndInputAttribute();
			}
		}

		virtual void RenderOutputs()
		{
			for (const ExpressionOutput& output : GetOutputs())
			{
				ImNodes::BeginOutputAttribute(output.ID);
				ImGui::Text(output.Name.c_str());
				ImNodes::EndOutputAttribute();
			}
		}

		virtual ~Expression() {}

		std::vector<ExpressionOutput> Outputs;
		int ID;

		virtual int Compile(Compiler& compiler, int outputIndex) const = 0;
		virtual std::vector<struct ExpressionInput*> GetInputs() { return {}; }
		virtual const std::vector<ExpressionOutput>& GetOutputs() { return Outputs; }
		virtual const char* GetName() const = 0;
	};

	template<typename T>
	struct ConstantTFloatExpression : public Expression
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.Constant(Value);
		}

		virtual void RenderInputs() override
		{
			ImNodes::BeginOutputAttribute(Outputs[0].ID);
			ImGui::InputFloat("", &Value);
			ImNodes::EndOutputAttribute();
		}

		virtual const char* GetName() const override { return "Constant"; }

		T Value{};
	};

	using ConstantFloatExpression = ConstantTFloatExpression<float>;
	using ConstantFloat2Expression = ConstantTFloatExpression<Vector2>;
	using ConstantFloat3Expression = ConstantTFloatExpression<Vector3>;
	using ConstantFloat4Expression = ConstantTFloatExpression<Vector4>;

	struct AddExpression : public Expression
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.Add(
				InputA.IsConnected() ? InputA.Compile(compiler) : compiler.Constant(ConstantA),
				InputB.IsConnected() ? InputB.Compile(compiler) : compiler.Constant(ConstantB)
			);
		}

		virtual void RenderInputs() override
		{
			ImNodes::BeginInputAttribute(InputA.ID);
			ImGui::Text(InputA.Name.c_str());
			if (!InputA.IsConnected())
			{
				ImGui::SameLine();
				ImGui::InputFloat("", &ConstantA);
			}
			ImNodes::EndInputAttribute();

			ImNodes::BeginInputAttribute(InputB.ID);
			ImGui::Text(InputB.Name.c_str());
			if (!InputB.IsConnected())
			{
				ImGui::SameLine();
				ImGui::InputFloat("", &ConstantB);
			}
			ImNodes::EndInputAttribute();
		}

		virtual std::vector<struct ExpressionInput*> GetInputs() { return { &InputA, &InputB }; }
		virtual const char* GetName() const override { return "Add"; }

		ExpressionInput InputA = "A";
		float ConstantA = 0;
		ExpressionInput InputB = "B";
		float ConstantB = 0;
	};

	struct PowerExpression : public Expression
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.Power(
				InputA.IsConnected() ? InputA.Compile(compiler) : compiler.Constant(ConstantA),
				InputB.IsConnected() ? InputB.Compile(compiler) : compiler.Constant(ConstantB)
			);
		}

		virtual void RenderInputs() override
		{
			ImNodes::BeginInputAttribute(InputA.ID);
			ImGui::Text(InputA.Name.c_str());
			if (!InputA.IsConnected())
			{
				ImGui::SameLine();
				ImGui::InputFloat("", &ConstantA);
			}
			ImNodes::EndInputAttribute();

			ImNodes::BeginInputAttribute(InputB.ID);
			ImGui::Text(InputB.Name.c_str());
			if (!InputB.IsConnected())
			{
				ImGui::SameLine();
				ImGui::InputFloat("", &ConstantB);
			}
			ImNodes::EndInputAttribute();
		}

		virtual std::vector<struct ExpressionInput*> GetInputs() { return { &InputA, &InputB }; }
		virtual const char* GetName() const override { return "Power"; }

		ExpressionInput InputA = "A";
		float ConstantA = 0;
		ExpressionInput InputB = "B";
		float ConstantB = 0;
	};

	struct TextureExpression : public Expression
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			if (!pTexture)
			{
				return compiler.Error("Texture not assigned.");
			}
			return compiler.Texture(pTexture);
		}

		virtual const char* GetName() const override { return "Texture2D"; }

		Texture* pTexture = nullptr;
	};

	struct Sample2DExpression : public Expression
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			if (!TextureInput.IsConnected() && !pTexture)
			{
				return compiler.Error("Texture not assigned.");
			}
			return compiler.Sample2D(TextureInput.IsConnected() ? TextureInput.Compile(compiler) : compiler.Texture(pTexture), UVInput.Compile(compiler));
		}

		virtual std::vector<struct ExpressionInput*> GetInputs() { return { &TextureInput, &UVInput }; }
		virtual const char* GetName() const override { return "Sample2D"; }

		ExpressionInput TextureInput = "Texture";
		Texture* pTexture = nullptr;
		ExpressionInput UVInput = "UV";
	};

	struct SwizzleExpression : public Expression
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.Swizzle(Input.Compile(compiler), SwizzleString.data());
		}

		virtual void RenderInputs() override
		{
			ImNodes::BeginInputAttribute(Input.ID);
			ImGui::InputText("Swizzle", SwizzleString.data(), SwizzleString.size());
			ImNodes::EndInputAttribute();
		}

		void SetSwizzle(const char* pSwizzle)
		{
			strcpy_s(SwizzleString.data(), SwizzleString.size(), pSwizzle);
		}

		virtual std::vector<struct ExpressionInput*> GetInputs() { return { &Input }; }
		virtual const char* GetName() const override { return "Swizzle"; }

		ExpressionInput Input;
		std::array<char, 5> SwizzleString{};
	};

	struct VertexAttributeExpression : public Expression
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			const Uniform& uniform = VertexAttributes[VertexAttributeIndices[outputIndex]];
			return compiler.VertexAttribute(uniform.pName);
		}

		virtual void RenderOutputs() override
		{
			for (size_t i = 0; i < VertexAttributeIndices.size(); ++i)
			{
				ImNodes::BeginOutputAttribute(Outputs[i].ID);
				int* index = &VertexAttributeIndices[i];
				ImGui::Combo("", index, [](void* pData, int index, const char** pOut)
					{
						Uniform* pAttr = (Uniform*)pData;
						*pOut = pAttr[index].pName;
						return true;
					}, (void*)VertexAttributes, ARRAYSIZE(VertexAttributes));
				ImGui::SameLine();

				ImNodes::EndOutputAttribute();
			}

			if (ImGui::Button("+"))
			{
				AddVertexAttribute("UV");
			}
		}

		void AddVertexAttribute(const char* pVertexAttribute)
		{
			for (int i = 0; i < ARRAYSIZE(VertexAttributes); ++i)
			{
				if (strcmp(VertexAttributes[i].pName, pVertexAttribute) == 0)
				{
					VertexAttributeIndices.push_back(i);
					Outputs.resize(VertexAttributeIndices.size());
					Outputs[VertexAttributeIndices.size() - 1].Name = pVertexAttribute;
					return;
				}
			}
		}

		virtual const char* GetName() const override { return "Vertex Attribute"; }

		std::vector<int> VertexAttributeIndices;
	};

	struct ViewUniformExpression : public Expression
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.ViewUniform(pUniform);
		}

		virtual const char* GetName() const override { return pUniform; }

		const char* pUniform;
	};
}
