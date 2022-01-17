#pragma once

#include "MaterialGraph.h"
#include "imnodes.h"

namespace ShaderGraph
{
	struct ExpressionFactory
	{
		using CreateFn = struct Expression*(*)();

		CreateFn Callback;
		const char* pName;
	};
	extern std::map<const char*, ExpressionFactory> gFactories;

	template<typename T>
	void RegisterExpression(const char* pName)
	{
		ExpressionFactory factory;
		factory.Callback = []() -> Expression* { return (new T()); };
		factory.pName = pName;
		gFactories[pName] = factory;
	}

	inline Expression* CreateExpression(const char* pName)
	{
		return gFactories[pName].Callback();
	}

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

			ImGui::PushItemWidth(100);
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

	struct ConstantFloatExpression : public Expression
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

		float Value = 0;
	};

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

		const char* pTexture = nullptr;
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
		const char* pTexture = nullptr;
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

	struct OutputExpression : public Expression
	{
		OutputExpression()
		{
			Outputs.clear();
		}

		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			const Input& input = m_Inputs[outputIndex];
			int result = input.Expression.IsConnected() ? input.Expression.Compile(compiler) : compiler.Constant(0);
			if (result == INVALID_INDEX)
				return INVALID_INDEX;
			if (compiler.GetType(result) != input.Type)
			{
				result = compiler.TryCast(result, input.Type);
				if (result == INVALID_INDEX)
					return INVALID_INDEX;
			}
			return result;
		}

		ExpressionInput& AddInput(const char* pName, ValueType type)
		{
			m_Inputs.push_back(Input(pName, type));
			return m_Inputs.back().Expression;
		}

		virtual std::vector<ExpressionInput*> GetInputs() override
		{
			std::vector<ExpressionInput*> inputs;
			for (Input& input : m_Inputs)
			{
				inputs.push_back(&input.Expression);
			}
			return inputs;
		}

		virtual const char* GetName() const override { return "Output"; }

		struct Input
		{
			Input(const char* pName, ValueType type)
				: Expression(pName), Type(type)
			{}
			ExpressionInput Expression;
			ValueType Type;
		};
		std::vector<Input> m_Inputs;
	};

	struct SystemValueExpression : public Expression
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.SystemValue((SystemValue)m_Index);
		}

		virtual void RenderOutputs() override
		{
			ImNodes::BeginOutputAttribute(Outputs[0].ID);
			ImGui::Combo("", &m_Index, [](void* pData, int index, const char** pOut)
				{
					SystemValueData* pSystemValue = (SystemValueData*)pData;
					*pOut = pSystemValue[index].pSymbolName;
					return true;
				}, (void*)SystemValues, ARRAYSIZE(SystemValues));
			ImNodes::EndOutputAttribute();
		}

		virtual const char* GetName() const override { return "System Value"; }

		int m_Index;
	};
}
