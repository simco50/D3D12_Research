#pragma once
#include "Core/BitField.h"

class CBT
{
public:
	using StorageType = uint32;
	constexpr static uint32 NumBitsPerElement = sizeof(StorageType) * 8;

	static uint32 ComputeSize(uint32 maxDepth)
	{
		uint32 numBits = 1u << (maxDepth + 2);
		return sizeof(uint32) * (numBits / NumBitsPerElement);
	}

	void InitBare(uint32 maxDepth, uint32 initialDepth)
	{
		assert(initialDepth <= maxDepth);

		Storage.clear();
		Storage.resize(ComputeSize(maxDepth) / sizeof(uint32));
		Storage[0] |= 1 << maxDepth;

		uint32 minRange = 1u << initialDepth;
		uint32 maxRange = 1u << (initialDepth + 1);
		uint32 interval = 1u << (maxDepth - initialDepth);
		for (uint32 heapIndex = minRange; heapIndex < maxRange; ++heapIndex)
		{
			SetData(heapIndex * interval, 1);
		}
	}

	void Init(uint32 maxDepth, uint32 initialDepth)
	{
		InitBare(maxDepth, initialDepth);
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
		uint32 valueLSB = BitfieldGet_Single(Storage[elementIndex], elementOffsetLSB, bitCountLSB);
		uint32 valueMSB = BitfieldGet_Single(Storage[Math::Min(elementIndex + 1, (uint32)Storage.size() - 1)], 0, bitCountMSB);
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
		BitfieldSet_Single(Storage[elementIndex], elementOffsetLSB, bitCountLSB, value);
		BitfieldSet_Single(Storage[Math::Min(elementIndex + 1, (uint32)Storage.size() - 1)], 0, bitCountMSB, value >> bitCountLSB);
	}

	uint32 GetNodeBitSize(uint32 heapIndex) const
	{
		uint32 depth = GetDepth(heapIndex);
		return GetMaxDepth() - depth + 1;
	}

	// Sum reduction bottom to top. This can be parallelized per layer
	void SumReduction()
	{
		int32 depth = GetMaxDepth();
		uint32 count = 1 << depth;

		// Prepass
		for (uint32 bitIndex = 0; bitIndex < count; bitIndex += (1 << 5))
		{
			uint32 nodeIndex = bitIndex + count;
			uint32 bitOffset = NodeBitIndex(nodeIndex);
			uint32 elementIndex = bitOffset >> 5u;

			uint32 bitField = Storage[elementIndex];
			bitField = (bitField & 0x55555555u) + ((bitField >> 1u) & 0x55555555u);
			uint32 data = bitField;
			Storage[(bitOffset - count) >> 5] = data;

			bitField = (bitField & 0x33333333u) + ((bitField >> 2u) & 0x33333333u);
			data = ((bitField >> 0u) & (7u << 0u)) |
				((bitField >> 1u) & (7u << 3u)) |
				((bitField >> 2u) & (7u << 6u)) |
				((bitField >> 3u) & (7u << 9u)) |
				((bitField >> 4u) & (7u << 12u)) |
				((bitField >> 5u) & (7u << 15u)) |
				((bitField >> 6u) & (7u << 18u)) |
				((bitField >> 7u) & (7u << 21u));

			BinaryHeapSet(NodeBitIndex(nodeIndex >> 2), 24, data);

			bitField = (bitField & 0x0F0F0F0Fu) + ((bitField >> 4u) & 0x0F0F0F0Fu);
			data = ((bitField >> 0u) & (15u << 0u)) |
				((bitField >> 4u) & (15u << 4u)) |
				((bitField >> 8u) & (15u << 8u)) |
				((bitField >> 12u) & (15u << 12u));

			BinaryHeapSet(NodeBitIndex(nodeIndex >> 3), 16, data);

			bitField = (bitField & 0x00FF00FFu) + ((bitField >> 8u) & 0x00FF00FFu);
			data = ((bitField >> 0u) & (31u << 0u)) |
				((bitField >> 11u) & (31u << 5u));

			BinaryHeapSet(NodeBitIndex(nodeIndex >> 4), 10, data);

			bitField = (bitField & 0x0000FFFFu) + ((bitField >> 16u) & 0x0000FFFFu);
			data = bitField;
			BinaryHeapSet(NodeBitIndex(nodeIndex >> 5), 6, data);
		}

		depth -= 5;
		while (--depth >= 0)
		{
			count = 1u << depth;
			for (uint32 k = count; k < count << 1u; ++k)
			{
				SetData(k, GetData(LeftChildID(k)) + GetData(RightChildID(k)));
			}
		}
	}

	uint32 GetData(uint32 heapIndex) const
	{
		uint32 offset = NodeBitIndex(heapIndex);
		uint32 size = GetNodeBitSize(heapIndex);
		return BinaryHeapGet(offset, size);
	}

	void SetData(uint32 heapIndex, uint32 value)
	{
		uint32 offset = NodeBitIndex(heapIndex);
		uint32 size = GetNodeBitSize(heapIndex);
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

	uint32 CeilNode(uint32 heapIndex) const
	{
		uint32 depth = GetDepth(heapIndex);
		return heapIndex << (GetMaxDepth() - depth);
	}

	uint32 NodeBitIndex(uint32 heapIndex) const
	{
		uint32 depth = GetDepth(heapIndex);
		uint32 t1 = 2u << depth;
		uint32 t2 = 1u + GetMaxDepth() - depth;
		return t1 + heapIndex * t2;
	};

	uint32 BitfieldHeapIndex(uint32 heapIndex) const
	{
		return heapIndex * (1u << (GetMaxDepth() - GetDepth(heapIndex)));
	}

	uint32 LeafIndexToHeapIndex(uint32 leafIndex) const
	{
		uint32 heapIndex = 1u;
		while (GetData(heapIndex) > 1u)
		{
			uint32 leftChild = LeftChildID(heapIndex);
			uint32 leftChildValue = GetData(leftChild);
			uint32 bit = leafIndex < leftChildValue ? 0 : 1;

			heapIndex = leftChild | bit;
			leafIndex -= bit * leftChildValue;
		}
		return heapIndex;
	}

	void BitfieldSet(uint32 bitOffset, uint32 value)
	{
		uint32 elementIndex = bitOffset / NumBitsPerElement;
		uint32 bitIndex = bitOffset % NumBitsPerElement;
		uint32 bitMask = ~(1u << bitIndex);

		Storage[elementIndex] &= bitMask;
		Storage[elementIndex] |= value << bitIndex;
	};

	void SplitNode(uint32 heapIndex)
	{
		if (!IsCeilNode(heapIndex))
		{
			uint32 rightChild = RightChildID(heapIndex);
			uint32 bitfieldIndex = BitfieldHeapIndex(rightChild);
			uint32 bit = NodeBitIndex(bitfieldIndex);
			BitfieldSet(bit, 1);
		}
	}

	void MergeNode(uint32 heapIndex)
	{
		if (!IsRootNode(heapIndex))
		{
			uint32 rightSibling = heapIndex | 1;
			uint32 bitfieldIndex = BitfieldHeapIndex(rightSibling);
			uint32 bit = NodeBitIndex(bitfieldIndex);
			BitfieldSet(bit, 0);
		}
	}

	// Returns true if the node is at the bottom of the tree and can't be split further
	bool IsCeilNode(uint32 heapIndex)
	{
		uint32 msb;
		assert(BitOperations::MostSignificantBit(heapIndex, &msb));
		return msb == GetMaxDepth();
	}

	static bool IsRootNode(uint32 heapIndex)
	{
		return heapIndex == 1u;
	}

	// Contains the final sum reduction value == number of leaf nodes
	uint32 NumNodes() const
	{
		return GetData(1);
	}

	uint32 GetMaxDepth() const
	{
		uint32 maxDepth;
		assert(BitOperations::LeastSignificantBit(Storage[0], &maxDepth));
		return maxDepth;
	}

	uint32 NumBitfieldBits() const
	{
		return (1u << GetMaxDepth());
	}

	// Utility functions

	static uint32 LeftChildID(uint32 heapIndex)
	{
		return heapIndex << 1;
	}

	static uint32 RightChildID(uint32 heapIndex)
	{
		return (heapIndex << 1) | 1;
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
		uint32 msb;
		BitOperations::MostSignificantBit(heapIndex, &msb);
		return msb;
	}

	const void* GetData() const
	{
		return Storage.data();
	}

	void* GetData()
	{
		return Storage.data();
	}

private:
	std::vector<uint32> Storage;
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

	inline NeighborIDs GetNeighbors(const NeighborIDs& neighbors, uint32 bit)
	{
		uint32 n1 = neighbors.Left;
		uint32 n2 = neighbors.Right;
		uint32 n3 = neighbors.Edge;
		uint32 n4 = neighbors.Current;

		uint32 b2 = n2 == 0u ? 0u : 1u;
		uint32 b3 = n3 == 0u ? 0u : 1u;

		if (bit == 0u)
		{
			return NeighborIDs{ (n4 << 1u) | 1u, (n3 << 1u) | b3, (n2 << 1u) | b2, (n4 << 1u) };
		}
		else
		{
			return NeighborIDs{ (n3 << 1u), (n4 << 1u), (n1 << 1u), (n4 << 1u) | 1u };
		}
	}

	inline NeighborIDs GetNeighbors(uint32 heapIndex)
	{
		uint32 depth = (int32)CBT::GetDepth(heapIndex);
		int32 bitID = 0;
		if (depth > 0u)
		{
			bitID = depth - 1u;
		}
		uint32 b = Private::GetBitValue(heapIndex, bitID);
		NeighborIDs neighbors{ 0u, 0u, 3u - b, 2u + b };

		for (bitID = depth - 2; bitID >= 0; --bitID)
		{
			uint32 bitValue = Private::GetBitValue(heapIndex, bitID);
			neighbors = GetNeighbors(neighbors, bitValue);
		}

		return neighbors;
	}

	inline uint32 GetEdgeNeighbor(uint32 heapIndex)
	{
		return GetNeighbors(heapIndex).Edge;
	}

	struct DiamondIDs
	{
		uint32 Base;
		uint32 Top;
	};

	inline DiamondIDs GetDiamond(uint32 heapIndex)
	{
		uint32 parent = CBT::ParentID(heapIndex);
		uint32 edge = GetEdgeNeighbor(parent);
		edge = edge > 0u ? edge : parent;
		return DiamondIDs{ parent, edge };
	}

	inline void CBTSplitConformed(CBT& cbt, uint32 heapIndex)
	{
		if (!cbt.IsCeilNode(heapIndex))
		{
			const uint32 minNodeID = 1u;

			cbt.SplitNode(heapIndex);
			uint32 edgeNeighbor = GetEdgeNeighbor(heapIndex);

			while (edgeNeighbor > minNodeID)
			{
				cbt.SplitNode(edgeNeighbor);
				edgeNeighbor = CBT::ParentID(edgeNeighbor);
				if (edgeNeighbor > minNodeID)
				{
					cbt.SplitNode(edgeNeighbor);
					edgeNeighbor = GetEdgeNeighbor(edgeNeighbor);
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
				// If splitting/merging is not alternated, it causes bugs and this extra hack was necessary
				//cbt.MergeNode(cbt.RightChildID(diamond.Top));
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
