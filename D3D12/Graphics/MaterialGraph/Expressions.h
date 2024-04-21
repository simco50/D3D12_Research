#pragma once

#include "MaterialGraph.h"
#include "External/imnodes/imnodes.h"

namespace ShaderGraph
{
	struct ExpressionFactory
	{
		using CreateFn = struct Expression* (*)();

		CreateFn Callback;
		const char* pName;
	};
	extern std::unordered_map<const char*, ExpressionFactory> gFactories;

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
			ImGui::PushItemWidth(100);
			ImNodes::BeginNode(ID);

			ImNodes::BeginNodeTitleBar();
			ImGui::TextUnformatted(GetName().c_str());
			ImNodes::EndNodeTitleBar();

			ImGui::BeginGroup();
			for (ExpressionInput& input : Inputs)
			{
				ImNodes::BeginInputAttribute(input.ID);
				ImGui::Text("%s", input.Name.c_str());
				if (!input.IsConnected() && input.HasDefaultValue)
				{
					ImGui::SameLine();
					ImGui::InputFloat("", &input.DefaultValue);
				}
				ImNodes::EndInputAttribute();
			}
			ImGui::EndGroup();

			ImGui::SameLine();

			ImGui::BeginGroup();
			RenderBody();
			ImGui::EndGroup();

			ImGui::SameLine();

			ImGui::BeginGroup();
			for (ExpressionOutput& output : Outputs)
			{
				ImNodes::BeginOutputAttribute(output.ID);
				ImGui::Text("%s", output.Name.c_str());
				ImNodes::EndOutputAttribute();
			}
			ImGui::EndGroup();

			ImNodes::EndNode();
			ImGui::PopItemWidth();
		}

		virtual void RenderBody() {}

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

		virtual void RenderBody() override
		{
			ImGui::InputFloat("", &Value);
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

		const char* pTexture = nullptr;

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

		virtual void RenderBody() override
		{
			ImGui::InputText("Swizzle", SwizzleString.data(), SwizzleString.size());
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
		VertexAttributeExpression()
		{
			Outputs.clear();
		}

		virtual int Compile(Compiler& compiler, int outputIndex) const override
		{
			const Uniform& uniform = VertexAttributes[VertexAttributeIndices[outputIndex]];
			return compiler.VertexAttribute(uniform.pName);
		}

		virtual void RenderBody() override
		{
			for (size_t i = 0; i < VertexAttributeIndices.size(); ++i)
			{
				int* index = &VertexAttributeIndices[i];
				ImGui::PushID(ID + (int)i);
				ImGui::Combo("", index, [](void* pData, int index)
					{
						Uniform* pAttr = (Uniform*)pData;
						return pAttr[index].pName;
					}, (void*)VertexAttributes, ARRAYSIZE(VertexAttributes));
				Outputs[i].Name = VertexAttributes[*index].pName;
				ImGui::PopID();
			}

			if (ImGui::Button("+"))
			{
				AddVertexAttribute();
			}
		}

		void AddVertexAttribute()
		{
			Outputs.push_back(ExpressionOutput(""));
			VertexAttributeIndices.push_back(0);
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

		virtual void RenderBody() override
		{
			ImGui::Combo("", &m_Index, [](void* pData, int index)
				{
					SystemValueData* pSystemValue = (SystemValueData*)pData;
					return pSystemValue[index].pSymbolName;
				}, (void*)SystemValues, ARRAYSIZE(SystemValues));
		}

		virtual std::string GetName() const override { return "System Value"; }

		int m_Index;
	};

	namespace Separate
	{
		struct Slot
		{
			int Value;
		};

		enum class Type
		{
			Add,
			Constant,
			Output,
		};

		struct Node
		{
			int ID;
			std::vector<Slot> Inputs;
			std::vector<Slot> Outputs;
			Type Type;
		};

		struct Graph
		{
			int ID = 0;
			std::vector<std::unique_ptr<Node>> Nodes;
			Node* pMasterNode;
			std::unordered_map<int, Node*> NodeMap;

			struct LinkData
			{
				int SourceNode;
				int TargetNode;
				int SourcePin;
				int TargetPin;
			};

			ImVector<LinkData> Links;

			Node& AddNode(Type t)
			{
				Nodes.push_back(std::make_unique<Node>());
				Node* pExp = Nodes.back().get();
				pExp->ID = ID++;
				pExp->Type = t;
				NodeMap[pExp->ID] = pExp;
				return *pExp;
			}

			int Link(int sourceNode, int sourcePin, int targetNode, int targetPin)
			{
				LinkData link{ sourceNode, targetNode, sourcePin, targetPin };
				for (LinkData& l : Links)
				{
					if (l.TargetNode == targetNode && l.TargetPin == targetPin)
					{
						Links.erase_unsorted(&l);
						break;
					}
				}
				Links.push_back(link);
				return Links.size() - 1;
			}

			bool Unlink(int sourceNode, int sourcePin, int targetNode, int targetPin)
			{
				LinkData link{ sourceNode, targetNode, sourcePin, targetPin };
				for (LinkData& l : Links)
				{
					if (l.TargetNode == targetNode && l.TargetPin == targetPin && l.SourceNode == sourceNode && l.SourcePin == sourcePin)
					{
						Links.erase_unsorted(&l);
						return true;
					}
				}
				return false;
			}

			const LinkData* FindLink(int expression, int pinIndex)
			{
				for (const LinkData& link : Links)
				{
					if (link.TargetNode == expression && link.TargetPin == pinIndex)
					{
						return &link;
					}
				}
				return nullptr;
			}

			void Resolve()
			{
				struct ResolveTarget
				{
					Node* pNode;
					int InputSlot;
				};

				std::vector<ResolveTarget> data;

				std::vector<ResolveTarget> stack;
				stack.push_back({ pMasterNode, 0 });
				while (!stack.empty())
				{
					ResolveTarget target = stack.back();
					stack.pop_back();

					const LinkData* pLink = FindLink(target.pNode->ID, target.InputSlot);
					if (pLink)
					{
						Node* pNode = NodeMap[pLink->SourceNode];
						data.push_back({ pNode, pLink->SourcePin });
						for (int i = 0; i < pNode->Inputs.size(); ++i)
						{
							stack.push_back({ pNode, i });
						}
					}
				}

				std::reverse(data.begin(), data.end());

				std::string code;
				int id = 0;

				std::vector<std::string> codestack;
				for (const ResolveTarget& target : data)
				{
					switch (target.pNode->Type)
					{
					case Type::Add:
					{
						std::string a = codestack.back();
						codestack.pop_back();
						std::string b = codestack.back();
						codestack.pop_back();
						code = Sprintf("%s\nfloat l_%d = %s + %s;", code.c_str(), id++, a.c_str(), b.c_str());
						codestack.push_back(Sprintf("l_%d", id));
						break;
					}
					case Type::Constant:
					{
						code = Sprintf("%s\nfloat l_%d = %d;", code.c_str(), id++, target.pNode->Outputs[0].Value);
						codestack.push_back(Sprintf("l_%d", id));
						break;
					}
					case Type::Output:
					{
						std::string a = codestack.back();
						codestack.pop_back();
						code = Sprintf("%s\nfloat l_%d = %s;", code.c_str(), id++, a.c_str());
						break;
					}
					}
				}
			}
		};
	}
}
