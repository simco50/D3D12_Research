
#define CBT_BITS_PER_ELEMENT 32

#ifndef CBT_WRITE
#define CBT_WRITE 1
#endif

struct CBT
{
#if CBT_WRITE
	RWByteAddressBuffer Storage;
#else
	ByteAddressBuffer Storage;
#endif
	uint NumElements;

#if CBT_WRITE
	void Init(RWByteAddressBuffer buffer, uint numElements)
#else
	void Init(ByteAddressBuffer buffer, uint numElements)
#endif
	{
		Storage = buffer;
		NumElements = numElements;
	}
	
	uint GetMaxDepth()
	{
		return firstbitlow(Storage.Load(0));
	}

	bool IsCeilNode(uint heapIndex)
	{
		uint msb = firstbithigh(heapIndex);
		return msb == GetMaxDepth();
	}

	uint NumNodes()
	{
		return GetData(1);
	}

	uint BitIndexFromHeap(uint heapIndex, uint depth)
	{
		uint a = 2u << depth;
		uint b = 1u + GetMaxDepth() - depth;
		return a + heapIndex * b;
	}

	uint BitfieldGet_Single(uint elementIndex, uint bitOffset, uint bitCount)
	{
		uint bitMask = ~(~0u << bitCount);
		return (Storage.Load(elementIndex * 4) >> bitOffset) & bitMask;
	}

	void BitfieldSet_Single(uint elementIndex, uint bitOffset, uint bitCount, uint value)
	{
#if CBT_WRITE
		uint bitMask = ~(~(~0u << bitCount) << bitOffset);
		Storage.InterlockedAnd(elementIndex * 4, bitMask);
		Storage.InterlockedOr(elementIndex * 4, value << bitOffset);
#endif
	}

	uint BinaryHeapGet(uint bitOffset, uint bitCount)
	{
		uint elementIndex = bitOffset / CBT_BITS_PER_ELEMENT;
		uint elementOffsetLSB = bitOffset % CBT_BITS_PER_ELEMENT;
		uint bitCountLSB = min(bitCount, CBT_BITS_PER_ELEMENT - elementOffsetLSB);
		uint bitCountMSB = bitCount - bitCountLSB;
		uint valueLSB = BitfieldGet_Single(elementIndex, elementOffsetLSB, bitCountLSB);
		uint valueMSB = BitfieldGet_Single(min(elementIndex + 1, NumElements - 1), 0, bitCountMSB);
		return valueLSB | (valueMSB << bitCountLSB);
	}

	void BinaryHeapSet(uint bitOffset, uint bitCount, uint value)
	{
		uint elementIndex = bitOffset / CBT_BITS_PER_ELEMENT;
		uint elementOffsetLSB = bitOffset % CBT_BITS_PER_ELEMENT;
		uint bitCountLSB = min(bitCount, CBT_BITS_PER_ELEMENT - elementOffsetLSB);
		uint bitCountMSB = bitCount - bitCountLSB;
		BitfieldSet_Single(elementIndex, elementOffsetLSB, bitCountLSB, value);
		BitfieldSet_Single(min(elementIndex + 1, NumElements - 1), 0, bitCountMSB, value >> bitCountLSB);
	}

	void GetDataRange(uint heapIndex, out uint offset, out uint size)
	{
		uint depth = GetDepth(heapIndex);
		size = GetMaxDepth() - depth + 1;
		offset = (2u << depth) + heapIndex * size;
	}

	uint GetData(uint heapIndex)
	{
		uint offset, size;
		GetDataRange(heapIndex, offset, size);
		return BinaryHeapGet(offset, size);
	}

	void SetData(uint heapIndex, uint value)
	{
		uint offset, size;
		GetDataRange(heapIndex, offset, size);
		BinaryHeapSet(offset, size, value);
	}

	uint LeafToHeapIndex(uint leafIndex)
	{
		uint heapIndex = 1;
		while(GetData(heapIndex) > 1)
		{
			uint leftChild = LeftChildIndex(heapIndex);
			uint leftChildValue = GetData(leftChild);
			uint bit = leafIndex < leftChildValue ? 0 : 1;
			heapIndex = leftChild | bit;
			leafIndex -= bit * leftChildValue;
		}
		return heapIndex;
	}
	
	uint BitfieldHeapIndex(uint heapIndex)
	{
		uint msb = firstbithigh(heapIndex);
		return heapIndex * (1u << (GetMaxDepth() - msb));
	}
	
	void SplitNode_Single(uint heapIndex)
	{
		uint rightChild = RightChildIndex(heapIndex);
		uint bit = BitfieldHeapIndex(rightChild);
		SetData(bit, 1u);
	}
	
	void MergeNode_Single(uint heapIndex)
	{
		uint rightSibling = heapIndex | 1u;
		uint bit = BitfieldHeapIndex(rightSibling);
		SetData(bit, 0u);
	}

	uint GetDepth(uint heapIndex)
	{
		return firstbithigh(heapIndex);
	}
	
	// Helpers
	uint LeftChildIndex(uint heapIndex)
	{
		return heapIndex << 1u;
	}
	
	uint RightChildIndex(uint heapIndex)
	{
		return (heapIndex << 1u) | 1u;
	}
	
	uint ParentIndex(uint heapIndex)
	{
		return heapIndex >> 1u;
	}
	
	uint SiblingIndex(uint heapIndex)
	{
		return heapIndex ^ 1u;
	}
};

namespace LEB
{
	bool GetBitValue(uint value, uint bit)
	{
		return (value >> bit) & 1u;
	}

	float3x3 GetSplitMatrix(uint bit)
	{
		float b = bit;
		float c = 1.0f - bit;
		return float3x3(
			c,      b,      0.0f,
			0.5f,   0.0f,   0.5f,
			0.0f,   c,      b
		);
	}

	float3x3 GetWindingMatrix(uint bit)
	{
		float b = 1.0f - bit;
		float c = bit;
		return float3x3(
			c,      0,      b,
			0.0f,   1.0f,   0.0f,
			b,      0.0f,   c
		);
	}
	
	float3x3 GetSquareMatrix(uint quadBit)
	{
		float b = quadBit;
		float c = 1.0f - quadBit;
		return float3x3(
			c,      0,      b,
			b,      c,      b,
			b,      0.0f,   c
		);
	}
	
	float3x3 GetMatrix(uint heapIndex)
	{
		int depth = firstbithigh(heapIndex);
		int bitID = max(0, depth - 1);
		float3x3 m = GetSquareMatrix(GetBitValue(heapIndex, bitID));
		for(bitID = depth - 2; bitID >= 0; --bitID)
		{
			float3x3 splitMatrix = GetSplitMatrix(GetBitValue(heapIndex, bitID));
			m = mul(splitMatrix, m);
		}
		float3x3 windingMatrix = GetWindingMatrix((depth ^ 1u) & 1u); 
		return mul(windingMatrix, m);
	}

	float3x3 GetTriangleVertices(uint heapIndex)
	{
		float3x3 baseTriangle = float3x3(
			0, 0, 1,
			0, 0, 0,
			1, 0, 0
		);

		float3x3 triMatrix = GetMatrix(heapIndex);
		return mul(triMatrix, baseTriangle);
	}

	struct NeighborIDs
	{
		uint Left;
		uint Right;
		uint Edge;
		uint Current;
	};

	NeighborIDs GetNeighbors(uint heapIndex)
	{
		int depth = firstbithigh(heapIndex);
		int bitID = depth > 0 ? depth - 1 : 0;
		uint b = GetBitValue(heapIndex, bitID);

		NeighborIDs neighbors;
		neighbors.Left = 0u;
		neighbors.Right = 0u;
		neighbors.Edge = 3u - b;
		neighbors.Current = 2u + b;

		for(bitID = depth - 2; bitID >= 0; --bitID)
		{
			uint n1 = neighbors.Left;
			uint n2 = neighbors.Right;
			uint n3 = neighbors.Edge;
			uint n4 = neighbors.Current;
			uint b2 = n2 == 0 ? 0 : 1;
			uint b3 = n3 == 0 ? 0 : 1;
			if(GetBitValue(heapIndex, bitID) == 0)
			{
				neighbors.Left = (n4 << 1) | 1;
				neighbors.Right = (n3 << 1) | b3;
				neighbors.Edge = (n2 << 1) | b2;
				neighbors.Current = (n4 << 1);
			}
			else
			{
				neighbors.Left = (n3 << 1);
				neighbors.Right = (n4 << 1);
				neighbors.Edge = (n1 << 1);
				neighbors.Current = (n4 << 1) | 1;
			}
		}
		return neighbors;
	}

	uint GetEdgeNeighbor(uint heapIndex)
	{
		return GetNeighbors(heapIndex).Edge;
	}

	struct DiamondIDs
	{
		uint Base;
		uint Top;
	};

	DiamondIDs GetDiamond(uint heapIndex)
	{
		uint parent = heapIndex >> 1u;
		uint edge = GetEdgeNeighbor(parent);
		edge = edge > 0 ? edge : parent;
		DiamondIDs diamond;
		diamond.Base = parent;
		diamond.Top = edge;
		return diamond;
	}

	void CBTSplitConformed(CBT cbt, uint heapIndex)
	{
		if (!cbt.IsCeilNode(heapIndex))
		{
			const uint minNodeID = 1u;

			cbt.SplitNode_Single(heapIndex);
			uint edgeNeighbor = GetEdgeNeighbor(heapIndex);

			while (edgeNeighbor > minNodeID)
			{
				cbt.SplitNode_Single(edgeNeighbor);
				edgeNeighbor = edgeNeighbor >> 1u;
				if (edgeNeighbor > minNodeID)
				{
					cbt.SplitNode_Single(edgeNeighbor);
					edgeNeighbor = GetEdgeNeighbor(edgeNeighbor);
				}
			}
		}
	}

	void CBTMergeConformed(CBT cbt, uint heapIndex)
	{
		if (cbt.GetDepth(heapIndex) > 1)
		{
			DiamondIDs diamond = GetDiamond(heapIndex);
			if(cbt.GetData(diamond.Base) <= 2 && cbt.GetData(diamond.Top) <= 2)
			{
				cbt.MergeNode_Single(heapIndex);
				// Bug?
				cbt.MergeNode_Single(cbt.RightChildIndex(diamond.Top));
			}
		}
	}
}