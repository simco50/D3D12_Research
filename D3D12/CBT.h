#pragma once

class CBT
{
public:
	void Init(uint32 maxDepth, uint32 depth)
	{
		assert(depth <= maxDepth);

		MaxDepth = maxDepth;
		Size = Math::Exp2(maxDepth + 1);
		if (Bits)
		{
			delete[] Bits;
		}
		Bits = new uint32[Size];
		memset(Bits, 0, Size * sizeof(uint32));
		SetData(0, MaxDepth);
		uint32 minRange = Math::Exp2(depth);
		uint32 maxRange = Math::Exp2(depth + 1);
		uint32 interval = Math::Exp2(MaxDepth - depth);
		for (uint32 heapID = minRange; heapID < maxRange; ++heapID)
		{
			SetData(heapID * interval, 1);
		}
		SumReduction();
	}

	void SumReduction()
	{
		int32 d = MaxDepth - 1;
		while (d >= 0)
		{
			uint32 minRange = Math::Exp2(d);
			uint32 maxRange = Math::Exp2(d + 1);
			for (uint32 k = minRange; k < maxRange; ++k)
			{
				SetData(k, GetData(LeftChildID(k)) + GetData(RightChildID(k)));
			}
			--d;
		}
	}

	uint32 GetData(uint32 index) const
	{
		return Bits[index];
	}

	void SetData(uint32 index, uint32 value)
	{
		Bits[index] = value;
	}

	template<typename HeapFn>
	void Update(HeapFn&& fn)
	{
		IterateLeaves(fn);
		SumReduction();
	}

	template<typename HeapFn>
	void IterateLeaves(HeapFn&& fn)
	{
		uint32 numNodes = NumNodes();
		for (uint32 leafIndex = 0; leafIndex < numNodes; ++leafIndex)
		{
			uint32 heapIndex = LeafIndexToHeapIndex(leafIndex);
			fn(heapIndex);
		}
	}

	uint32 LeafIndexToHeapIndex(uint32 leafIndex) const
	{
		uint32 heapID = 1;
		while (GetData(heapID) > 1)
		{
			uint32 leftChildValue = GetData(LeftChildID(heapID));
			if (leafIndex < leftChildValue)
			{
				heapID = LeftChildID(heapID);
			}
			else
			{
				leafIndex -= leftChildValue;
				heapID = RightChildID(heapID);
			}
		}
		return heapID;
	}

	uint32 EncodeNode(uint32 heapID) const
	{
		uint32 leafIndex = 0;
		while (heapID > 1)
		{
			if (leafIndex % 2 == 0)
			{
				leafIndex = leafIndex + GetData(heapID ^ 1);
			}
			heapID = heapID / 2;
		}
		return leafIndex;
	}

	uint32 BitfieldHeapID(uint32 heapID) const
	{
		uint32 msb = 0;
		assert(BitOperations::MostSignificantBit(heapID, &msb));
		return heapID * Math::Exp2(MaxDepth - msb);
	}

	void SplitNode(uint32 heapID)
	{
		uint32 rightChild = RightChildID(heapID);
		uint32 bit = BitfieldHeapID(rightChild);
		SetData(bit, 1);
	}

	void MergeNode(uint32 heapID)
	{
		uint32 rightSibling = heapID | 1;
		uint32 bit = BitfieldHeapID(rightSibling);
		SetData(bit, 0);
	}

	static uint32 LeftChildID(uint32 heapID)
	{
		return heapID * 2;
	}

	static uint32 RightChildID(uint32 heapID)
	{
		return heapID * 2 + 1;
	}

	static uint32 ParentID(uint32 heapID)
	{
		return heapID >> 1;
	}

	static uint32 SiblingID(uint32 heapID)
	{
		return heapID ^ 1;
	}

	static uint32 GetDepth(uint32 heapID)
	{
		return (uint32)floor(log2(heapID));
	}

	uint32 GetMaxDepth() const
	{
		return MaxDepth;
	}

	bool IsLeafNode(uint32 heapID)
	{
		return GetData(heapID) == 1;
	}

	uint32 NumNodes() const
	{
		return GetData(1);
	}

	uint32 NumBitfieldBits() const
	{
		return Math::Exp2(MaxDepth);
	}

	void GetElementRange(uint32 heapID, uint32& begin, uint32& size) const
	{
		uint32 depth = GetDepth(heapID);
		size = MaxDepth - depth + 1;
		begin = Math::Exp2(depth + 1) + heapID * size;
	}

	~CBT()
	{
		delete[] Bits;
	}

private:
	uint32 MaxDepth;
	uint32 Size;
	uint32* Bits = nullptr;
};

namespace LEB
{
	namespace Private
	{
		inline bool GetBitValue(uint32 value, uint32 bit)
		{
			return (value >> bit) & 1u;
		}

		inline Matrix GetSplitMatrix(bool bitSet)
		{
			float b = (float)bitSet;
			float c = 1.0f - b;
			return DirectX::XMFLOAT3X3(
				c, b, 0.0f,
				0.5f, 0.0f, 0.5f,
				0.0f, c, b
			);
		}

		inline Matrix GetWindingMatrix(uint32 bit)
		{
			float b = (float)bit;
			float c = 1.0f - b;
			return DirectX::XMFLOAT3X3(
				c,		0.0f,	b,
				0.0f,	1.0f,	0.0f,
				b,		0.0f,	c
			);
		}

		inline Matrix GetSquareMatrix(uint32 quadBit)
		{
			float b = float(quadBit);
			float c = 1.0f - b;

			return DirectX::XMFLOAT3X3(
				c,	0.0f,	b,
				b,	c,		b,
				b,	0.0f,	c
			);
		}
	}

	inline Matrix GetMatrix(uint32 heapID)
	{
		uint32 d;
		BitOperations::MostSignificantBit(heapID, &d);

		int32 bitID = Math::Max(0, (int32)d - 1);
		Matrix m = Private::GetSquareMatrix(Private::GetBitValue(heapID, bitID));

		for (bitID = d - 2; bitID >= 0; --bitID)
		{
			m = Private::GetSplitMatrix(Private::GetBitValue(heapID, bitID)) * m;
		}
		return Private::GetWindingMatrix((d ^ 1) & 1) * m;
	}

	inline void GetTriangleVertices(uint32 heapIndex, Vector3& a, Vector3& b, Vector3& c)
	{
		static const Matrix baseTriangle = DirectX::XMFLOAT3X3{
			0, 1, 0,
			0, 0, 0,
			1, 0, 0,
		};
		Matrix t = GetMatrix(heapIndex) * baseTriangle;
		a = Vector3(t._11, t._12, t._13);
		b = Vector3(t._21, t._22, t._23);
		c = Vector3(t._31, t._32, t._33);
	}

	float sign(const Vector2& p1, const Vector2& p2, const Vector2& p3)
	{
		return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
	}

	bool PointInTriangle(const Vector2& pt, const Vector2& v1, const Vector2& v2, const Vector2& v3)
	{
		float d1, d2, d3;
		bool has_neg, has_pos;

		d1 = sign(pt, v1, v2);
		d2 = sign(pt, v2, v3);
		d3 = sign(pt, v3, v1);

		has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
		has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

		return !(has_neg && has_pos);
	}
}

