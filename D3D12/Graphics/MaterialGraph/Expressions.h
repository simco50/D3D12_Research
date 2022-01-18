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

		virtual ~Expression() {}

		virtual int Compile(Compiler& compiler, int outputIndex) const = 0;
		virtual std::string GetName() const { return "Unnamed"; }

		void Render()
		{
			ImNodes::BeginNode(ID);

			ImGui::PushItemWidth(100);
			ImNodes::BeginNodeTitleBar();
			ImGui::TextUnformatted(GetName().c_str());
			ImNodes::EndNodeTitleBar();

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
			for (ExpressionInput& input : Inputs)
			{
				ImNodes::BeginInputAttribute(input.ID);
				ImGui::Text(input.Name.c_str());
				if (!input.IsConnected() && input.HasDefaultValue)
				{
					ImGui::SameLine();
					ImGui::InputFloat("", &input.DefaultValue);
				}
				ImNodes::EndInputAttribute();
			}
		}

		virtual void RenderOutputs()
		{
			for (ExpressionOutput& output : Outputs)
			{
				ImNodes::BeginOutputAttribute(output.ID);
				ImGui::Text(output.Name.c_str());
				ImNodes::EndOutputAttribute();
			}
		}

		std::vector<ExpressionOutput> Outputs;
		std::vector<ExpressionInput> Inputs;
		int ID;
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

		virtual std::string GetName() const override { return "Constant"; }

		float Value = 0;
	};

	struct AddExpression : public Expression
	{
		AddExpression()
		{
			Inputs.push_back({ "A", 0 });
			Inputs.push_back({ "B", 0 });
		}

		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.Add(Inputs[0].Compile(compiler), Inputs[1].Compile(compiler));
		}

		virtual std::string GetName() const override { return "Add"; }
	};

	struct PowerExpression : public Expression
	{
		PowerExpression()
		{
			Inputs.push_back({ "A", 0 });
			Inputs.push_back({ "B", 0 });
		}

		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.Power(Inputs[0].Compile(compiler), Inputs[1].Compile(compiler));
		}

		virtual std::string GetName() const override { return "Power"; }
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

		virtual std::string GetName() const override { return "Texture2D"; }

		const char* pTexture = nullptr;
	};

	struct Sample2DExpression : public Expression
	{
		Sample2DExpression()
		{
			Inputs.push_back({ "Texture" });
			Inputs.push_back({ "UV" });

			Outputs.clear();
			Outputs.push_back({ "RGBA" });
			Outputs.push_back({ "R" });
			Outputs.push_back({ "G" });
			Outputs.push_back({ "B" });
			Outputs.push_back({ "A" });
		}

		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			if (!Inputs[0].IsConnected())
			{
				return compiler.Error("Texture not assigned.");
			}
			int result = compiler.Sample2D(Inputs[0].Compile(compiler), Inputs[1].Compile(compiler));
			if (result == INVALID_INDEX)
				return INVALID_INDEX;

			if (outputIndex == 0)
				return result;

			const char* swizzles[] = { "r", "g", "b", "a" };
			return compiler.Swizzle(result, swizzles[outputIndex - 1]);
		}

		virtual std::string GetName() const override { return "Sample2D"; }
	};

	struct SwizzleExpression : public Expression
	{
		SwizzleExpression()
		{
			Inputs.push_back({ "" });
		}

		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.Swizzle(Inputs[0].Compile(compiler), SwizzleString.data());
		}

		virtual void RenderOutputs() override
		{
			ImNodes::BeginOutputAttribute(Outputs[0].ID);
			ImGui::InputText("Swizzle", SwizzleString.data(), SwizzleString.size());
			ImNodes::EndOutputAttribute();
		}

		void SetSwizzle(const char* pSwizzle)
		{
			strcpy_s(SwizzleString.data(), SwizzleString.size(), pSwizzle);
		}

		virtual std::string GetName() const override { return "Swizzle"; }

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

		virtual std::string GetName() const override { return "Vertex Attribute"; }

		std::vector<int> VertexAttributeIndices;
	};

	struct ViewUniformExpression : public Expression
	{
		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			return compiler.ViewUniform(pUniform);
		}

		virtual std::string GetName() const override { return pUniform; }

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
			int result = Inputs[outputIndex].IsConnected() ? Inputs[outputIndex].Compile(compiler) : compiler.Constant(0);
			if (result == INVALID_INDEX)
				return INVALID_INDEX;

			ValueType type = m_InputTypes[outputIndex];
			if (compiler.GetType(result) != type)
			{
				result = compiler.TryCast(result, type);
				if (result == INVALID_INDEX)
					return INVALID_INDEX;
			}
			return result;
		}

		ExpressionInput& AddInput(const char* pName, ValueType type)
		{
			Inputs.push_back(pName);
			return Inputs.back();
		}

		virtual std::string GetName() const override { return "Output"; }

		std::vector<ValueType> m_InputTypes;
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

		virtual std::string GetName() const override { return "System Value"; }

		int m_Index;
	};
}
