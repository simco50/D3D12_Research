#pragma once

class CBT
{
public:
	using StorageType = uint32;
	constexpr static uint32 NumBitsPerElement = sizeof(StorageType) * 8;

	void Init(uint32 maxDepth, uint32 initialDepth)
	{
		assert(initialDepth <= maxDepth);

		uint32 NumBits = Math::Exp2(maxDepth + 2);
		assert(NumBits < NumBitsPerElement || NumBits % NumBitsPerElement == 0);

		Bits.clear();
		Bits.resize(NumBits / NumBitsPerElement);
		Bits[0] |= 1 << maxDepth;

		uint32 minRange = Math::Exp2(initialDepth);
		uint32 maxRange = Math::Exp2(initialDepth + 1);
		uint32 interval = Math::Exp2(maxDepth - initialDepth);
		for (uint32 heapIndex = minRange; heapIndex < maxRange; ++heapIndex)
		{
			SetData(heapIndex * interval, 1);
		}
		SumReduction();
	}

	// Get a value from the bag of bits. We must read from 2 elements in case the value crosses the boundary
	uint32 BinaryHeapGet(uint32 bitOffset, uint32 bitCount) const
	{
		auto BitfieldGet_Single = [](StorageType buffer, uint32 bitOffset, uint32 bitCount) -> uint32
		{
			assert(bitOffset + bitCount <= NumBitsPerElement);
			uint32 bitMask = ~(~0u << bitCount);
			return (buffer >> bitOffset) & bitMask;
		};

		uint32 elementIndex = bitOffset / NumBitsPerElement;
		uint32 elementOffsetLSB = bitOffset % NumBitsPerElement;
		uint32 bitCountLSB = Math::Min(bitCount, NumBitsPerElement - elementOffsetLSB);
		uint32 bitCountMSB = bitCount - bitCountLSB;
		uint32 valueLSB = BitfieldGet_Single(Bits[elementIndex], elementOffsetLSB, bitCountLSB);
		uint32 valueMSB = BitfieldGet_Single(Bits[Math::Min(elementIndex + 1, (uint32)Bits.size() - 1)], 0, bitCountMSB);
		uint32 val = valueLSB | (valueMSB << bitCountLSB);
		return val;
	}

	// Set a value in the bag of bits. We must write to 2 elements in case the value crosses the boundary
	void BinaryHeapSet(uint32 bitOffset, uint32 bitCount, uint32 value)
	{
		auto BitfieldSet_Single = [](StorageType& buffer, uint32 bitOffset, uint32 bitCount, uint32 value)
		{
			assert(bitOffset + bitCount <= NumBitsPerElement);
			uint32 bitMask = ~(~(~0u << bitCount) << bitOffset);
			buffer &= bitMask;
			buffer |= value << bitOffset;
		};

		uint32 elementIndex = bitOffset / NumBitsPerElement;
		uint32 elementOffsetLSB = bitOffset % NumBitsPerElement;
		uint32 bitCountLSB = Math::Min(bitCount, NumBitsPerElement - elementOffsetLSB);
		uint32 bitCountMSB = bitCount - bitCountLSB;
		BitfieldSet_Single(Bits[elementIndex], elementOffsetLSB, bitCountLSB, value);
		BitfieldSet_Single(Bits[Math::Min(elementIndex + 1, (uint32)Bits.size() - 1)], 0, bitCountMSB, value >> bitCountLSB);
	}

	// Sum reduction bottom to top. This can be parallelized per layer
	void SumReduction()
	{
		int32 depth = GetMaxDepth() - 1;
		while (depth >= 0)
		{
			uint32 minRange = Math::Exp2(depth);
			uint32 maxRange = Math::Exp2(depth + 1);
			for (uint32 k = minRange; k < maxRange; ++k)
			{
				SetData(k, GetData(LeftChildID(k)) + GetData(RightChildID(k)));
			}
			--depth;
		}
	}

	void GetDataRange(uint32 heapIndex, uint32* pOffset, uint32* pSize) const
	{
		uint32 depth = (uint32)floor(log2(heapIndex));
		*pSize = GetMaxDepth() - depth + 1;
		*pOffset = Math::Exp2(depth + 1) + heapIndex * *pSize;
		assert(*pSize < NumBitsPerElement);
	}

	uint32 GetData(uint32 index) const
	{
		uint32 offset, size;
		GetDataRange(index, &offset, &size);
		return BinaryHeapGet(offset, size);
	}

	void SetData(uint32 index, uint32 value)
	{
		uint32 offset, size;
		GetDataRange(index, &offset, &size);
		BinaryHeapSet(offset, size, value);
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
		uint32 heapIndex = 1;
		while (GetData(heapIndex) > 1)
		{
			uint32 leftChildValue = GetData(LeftChildID(heapIndex));
			if (leafIndex < leftChildValue)
			{
				heapIndex = LeftChildID(heapIndex);
			}
			else
			{
				leafIndex -= leftChildValue;
				heapIndex = RightChildID(heapIndex);
			}
		}
		return heapIndex;
	}

	uint32 BitfieldHeapID(uint32 heapIndex) const
	{
		uint32 msb = 0;
		assert(BitOperations::MostSignificantBit(heapIndex, &msb));
		return heapIndex * Math::Exp2(GetMaxDepth() - msb);
	}

	void SplitNode(uint32 heapIndex)
	{
		uint32 rightChild = RightChildID(heapIndex);
		uint32 bit = BitfieldHeapID(rightChild);
		SetData(bit, 1);
	}

	void MergeNode(uint32 heapIndex)
	{
		uint32 rightSibling = heapIndex | 1;
		uint32 bit = BitfieldHeapID(rightSibling);
		SetData(bit, 0);
	}

	// Returns true if the node is at the bottom of the tree and can't be split further
	bool IsCeilNode(uint32 heapIndex)
	{
		uint32 msb;
		assert(BitOperations::MostSignificantBit(heapIndex, &msb));
		return msb == GetMaxDepth();
	}

	// Contains the final sum reduction value == number of leaf nodes
	uint32 NumNodes() const
	{
		return GetData(1);
	}

	uint32 GetMaxDepth() const
	{
		uint32 maxDepth;
		assert(BitOperations::LeastSignificantBit(Bits[0], &maxDepth));
		return maxDepth;
	}

	uint32 NumBitfieldBits() const
	{
		return Math::Exp2(GetMaxDepth());
	}

	void GetElementRange(uint32 heapIndex, uint32& begin, uint32& size) const
	{
		uint32 depth = GetDepth(heapIndex);
		size = GetMaxDepth() - depth + 1;
		begin = Math::Exp2(depth + 1) + heapIndex * size;
	}

	uint32 GetMemoryUse() const
	{
		return (uint32)Bits.size() * sizeof(StorageType);
	}

	// Utility functions

	static uint32 LeftChildID(uint32 heapIndex)
	{
		return heapIndex * 2;
	}

	static uint32 RightChildID(uint32 heapIndex)
	{
		return heapIndex * 2 + 1;
	}

	static uint32 ParentID(uint32 heapIndex)
	{
		return heapIndex >> 1;
	}

	static uint32 SiblingID(uint32 heapIndex)
	{
		return heapIndex ^ 1;
	}

	static uint32 GetDepth(uint32 heapIndex)
	{
		return (uint32)floor(log2(heapIndex));
	}

private:
	std::vector<uint32> Bits;
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
				c, 0.0f, b,
				0.0f, 1.0f, 0.0f,
				b, 0.0f, c
			);
		}

		inline Matrix GetSquareMatrix(uint32 quadBit)
		{
			float b = float(quadBit);
			float c = 1.0f - b;

			return DirectX::XMFLOAT3X3(
				c, 0.0f, b,
				b, c, b,
				b, 0.0f, c
			);
		}
	}

	inline Matrix GetMatrix(uint32 heapIndex)
	{
		uint32 d;
		BitOperations::MostSignificantBit(heapIndex, &d);

		int32 bitID = Math::Max(0, (int32)d - 1);
		Matrix m = Private::GetSquareMatrix(Private::GetBitValue(heapIndex, bitID));

		for (bitID = d - 2; bitID >= 0; --bitID)
		{
			m = Private::GetSplitMatrix(Private::GetBitValue(heapIndex, bitID)) * m;
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

	struct NeighborIDs
	{
		uint32 Left;
		uint32 Right;
		uint32 Edge;
		uint32 Current;
	};

	inline NeighborIDs GetNeighbors(uint32 heapIndex)
	{
		uint32 depth;
		BitOperations::MostSignificantBit(heapIndex, &depth);

		int32 bitID = depth > 0 ? (int32)depth - 1 : 0;
		uint32 b = Private::GetBitValue(heapIndex, bitID);
		NeighborIDs neighbors{ 0, 0, 3u - b, 2u + b };

		for (bitID = depth - 2; bitID >= 0; --bitID)
		{
			uint32 n1 = neighbors.Left;
			uint32 n2 = neighbors.Right;
			uint32 n3 = neighbors.Edge;
			uint32 n4 = neighbors.Current;

			uint32 b2 = n2 == 0 ? 0 : 1;
			uint32 b3 = n3 == 0 ? 0 : 1;

			uint32 bit = Private::GetBitValue(heapIndex, bitID);
			if (bit == 0)
			{
				neighbors = NeighborIDs{ (n4 << 1) | 1, (n3 << 1) | b3, (n2 << 1) | b2, (n4 << 1) };
			}
			else
			{
				neighbors = NeighborIDs{ (n3 << 1), (n4 << 1), (n1 << 1), (n4 << 1) | 1 };
			}
		}
		return neighbors;
	}

	struct DiamondIDs
	{
		uint32 Base;
		uint32 Top;
	};

	inline DiamondIDs GetDiamond(uint32 heapIndex)
	{
		uint32 parent = CBT::ParentID(heapIndex);
		uint32 edge = GetNeighbors(parent).Edge;
		edge = edge > 0 ? edge : parent;
		return DiamondIDs{ parent, edge };
	}

	inline void CBTSplitConformed(CBT& cbt, uint32 heapIndex)
	{
		if (!cbt.IsCeilNode(heapIndex))
		{
			const uint32 minNodeID = 1u;

			cbt.SplitNode(heapIndex);
			uint32 edgeNeighbor = GetNeighbors(heapIndex).Edge;
			while (edgeNeighbor > minNodeID)
			{
				cbt.SplitNode(edgeNeighbor);
				edgeNeighbor >>= 1;
				if (edgeNeighbor > minNodeID)
				{
					cbt.SplitNode(edgeNeighbor);
					edgeNeighbor = GetNeighbors(edgeNeighbor).Edge;
				}
			}
		}
	}

	inline void CBTMergeConformed(CBT& cbt, uint32 heapIndex)
	{
		if (cbt.GetDepth(heapIndex) > 1)
		{
			DiamondIDs diamond = GetDiamond(heapIndex);
			if(cbt.GetData(diamond.Base) <= 2 && cbt.GetData(diamond.Top) <= 2)
			{
				cbt.MergeNode(heapIndex);
			}
		}
	}

	inline bool PointInTriangle(const Vector2& pt, uint32 heapIndex, float scale)
	{
		float d1, d2, d3;
		bool has_neg, has_pos;

		Vector3 a, b, c;
		GetTriangleVertices(heapIndex, a, b, c);
		Vector2 v1(a), v2(b), v3(c);
		v1 *= scale;
		v2 *= scale;
		v3 *= scale;

		auto sign = [](const Vector2& p1, const Vector2& p2, const Vector2& p3) -> float
		{
			return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
		};

		d1 = sign(pt, v1, v2);
		d2 = sign(pt, v2, v3);
		d3 = sign(pt, v3, v1);

		has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
		has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

		return !(has_neg && has_pos);
	}
}

