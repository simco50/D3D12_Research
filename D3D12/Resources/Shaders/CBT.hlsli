
struct CBT
{
	RWByteAddressBuffer Storage;
	uint NumElements;
	uint MaxDepth;

	void Init(RWByteAddressBuffer buffer, uint numElements)
	{
		Storage = buffer;
		NumElements = numElements;
		MaxDepth = firstbitlow(buffer.Load(0u));
	}

	void SplitNode_Single(uint heapIndex)
	{
		uint rightChild = RightChildIndex(heapIndex);
		uint bit = NodeBitIndex(CeilNode(rightChild));
		BitfieldSet_Single(bit >> 5u, bit & 31u, 1u, 1u);
	}
	
	void MergeNode_Single(uint heapIndex)
	{
		uint rightSibling = RightSiblingIndex(heapIndex);
		uint bitfieldHeapIndex = CeilNode(rightSibling);
		uint bit = NodeBitIndex(bitfieldHeapIndex);
		BitfieldSet_Single(bit >> 5u, bit & 31u, 1u, 0u);
	}

	uint BitfieldGet_Single(uint elementIndex, uint bitOffset, uint bitCount)
	{
		uint bitMask = ~(~0u << bitCount);
		return (Storage.Load(elementIndex * 4u) >> bitOffset) & bitMask;
	}

	void BitfieldSet_Single(uint elementIndex, uint bitOffset, uint bitCount, uint value)
	{
		uint bitMask = ~(~(~0u << bitCount) << bitOffset);
		Storage.InterlockedAnd(elementIndex * 4u, bitMask);
		Storage.InterlockedOr(elementIndex * 4u, value << bitOffset);
	}

	struct DataMutateArgs
	{
		uint ElementIndexLSB;
		uint ElementIndexMSB;
		uint ElementOffsetLSB;
		uint BitCountLSB;
		uint BitCountMSB;
	};

	DataMutateArgs GetDataArgs(uint bitOffset, uint bitCount)
	{
		DataMutateArgs args;
		args.ElementIndexLSB = bitOffset >> 5u;
		args.ElementIndexMSB = min(args.ElementIndexLSB + 1u, NumElements - 1u);
		args.ElementOffsetLSB = bitOffset & 31u;
		args.BitCountLSB = min(bitCount, 32u - args.ElementOffsetLSB);
		args.BitCountMSB = bitCount - args.BitCountLSB;
		return args;
	}

	uint BinaryHeapGet(uint bitOffset, uint bitCount)
	{
		DataMutateArgs args = GetDataArgs(bitOffset, bitCount);
		uint valueLSB = BitfieldGet_Single(args.ElementIndexLSB, args.ElementOffsetLSB, args.BitCountLSB);
		uint valueMSB = BitfieldGet_Single(args.ElementIndexMSB, 0u, args.BitCountMSB);
		return valueLSB | (valueMSB << args.BitCountLSB);
	}

	void BinaryHeapSet(uint bitOffset, uint bitCount, uint value)
	{
		DataMutateArgs args = GetDataArgs(bitOffset, bitCount);
		BitfieldSet_Single(args.ElementIndexLSB, args.ElementOffsetLSB, args.BitCountLSB, value);
		BitfieldSet_Single(args.ElementIndexMSB, 0u, args.BitCountMSB, value >> args.BitCountLSB);
	}

	uint GetData(uint heapIndex)
	{
		return BinaryHeapGet(NodeBitIndex(heapIndex), NodeBitSize(heapIndex));
	}

	void SetData(uint heapIndex, uint value)
	{
		BinaryHeapSet(NodeBitIndex(heapIndex), NodeBitSize(heapIndex), value);
	}

	uint LeafToHeapIndex(uint leafIndex)
	{
		uint heapIndex = 1u;
		while(GetData(heapIndex) > 1u)
		{
			uint leftChild = LeftChildIndex(heapIndex);
			uint leftChildValue = GetData(leftChild);
			uint bit = leafIndex < leftChildValue ? 0u : 1u;
			heapIndex = leftChild | bit;
			leafIndex -= bit * leftChildValue;
		}
		return heapIndex;
	}
	
	// Returns the heap index of bitfield node for a heap node
	uint BitfieldHeapIndex(uint heapIndex)
	{
		uint msb = GetDepth(heapIndex);	
		return heapIndex * (1u << (GetMaxDepth() - msb));
	}

	// Returns the size in bits for a heap node
	uint NodeBitSize(uint heapIndex)
	{
		uint depth = GetDepth(heapIndex);
		return GetMaxDepth() - depth + 1u;
	}

	// Returns the offset in bits in CTB for a heap node
	uint NodeBitIndex(uint heapIndex)
	{
		uint depth = GetDepth(heapIndex);
		uint a = 2u << depth;
		uint b = 1u + GetMaxDepth() - depth;
		return a + heapIndex * b;
	}

	uint CeilNode(uint heapIndex)
	{
		uint depth = GetDepth(heapIndex);
		return heapIndex << (GetMaxDepth() - depth);
	}

	uint GetMaxDepth()
	{
		return MaxDepth;
	}

	bool IsCeilNode(uint heapIndex)
	{
		uint msb = GetDepth(heapIndex);
		return msb == GetMaxDepth();
	}

	uint NumNodes()
	{
		return GetData(1u);
	}

	uint GetDepth(uint heapIndex)
	{
		return firstbithigh(heapIndex);
	}
	
	uint LeftChildIndex(uint heapIndex)
	{
		return heapIndex << 1u;
	}
	
	uint RightChildIndex(uint heapIndex)
	{
		return (heapIndex << 1u) | 1u;
	}

	uint RightSiblingIndex(uint heapIndex)
	{
		return heapIndex | 1u;
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

	uint4 SplitNeighborIDs(uint4 neighbors, uint bit)
	{
#if 1
		uint b = bit;
		uint c = bit ^ 1u;
		bool cb = c;
		uint4 n;
		n.x = (neighbors[2 + b] << 1u) 	| (cb && neighbors[2 + b]);
		n.y = (neighbors[2 + c] << 1u) 	| (cb && neighbors[2 + c]);
		n.z = (neighbors[b] << 1u) 		| (cb && neighbors[b]);
		n.w = (neighbors[3] << 1u) 		| b;
		return n;
#else
		uint n1 = neighbors.x;
		uint n2 = neighbors.y;
		uint n3 = neighbors.z;
		uint n4 = neighbors.w;
		uint b2 = n2 == 0 ? 0 : 1;
		uint b3 = n3 == 0 ? 0 : 1;
		if(bit == 0)
		{
			neighbors.x = (n4 << 1) | 1;
			neighbors.y = (n3 << 1) | b3;
			neighbors.z = (n2 << 1) | b2;
			neighbors.w = (n4 << 1);
		}
		else
		{
			neighbors.x = (n3 << 1);
			neighbors.y = (n4 << 1);
			neighbors.z = (n1 << 1);
			neighbors.w = (n4 << 1) | 1;
		}
		return neighbors;
#endif
	}

	uint4 GetNeighbors(uint heapIndex)
	{
		int depth = firstbithigh(heapIndex);
		int bitID = depth > 0 ? depth - 1 : 0;
		uint b = GetBitValue(heapIndex, bitID);

		uint4 neighbors;
		neighbors.x = 0u;
		neighbors.y = 0u;
		neighbors.z = 3u - b;
		neighbors.w = 2u + b;

		for(bitID = depth - 2; bitID >= 0; --bitID)
		{
			neighbors = SplitNeighborIDs(neighbors, GetBitValue(heapIndex, bitID));
		}
		return neighbors;
	}

	uint GetEdgeNeighbor(uint heapIndex)
	{
		return GetNeighbors(heapIndex).z;
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
	
	float3x3 TransformAttributes(uint heapIndex, float3x3 attributes)
	{
		float3x3 transformMatrix = GetMatrix(heapIndex);
		return mul(transformMatrix, attributes);
	}
}
