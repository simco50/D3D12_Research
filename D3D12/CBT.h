#pragma once

struct CBT
{
	static uint32 Exp2(uint32 exp)
	{
		return (int32)exp2(exp);
	}

	static int32 MSB(uint32 value)
	{
		if (value == 0)
		{
			return -1;
		}
		uint32 msb = 0;
		while (value >>= 1)
		{
			msb++;
		}
		return msb;
	}

	CBT(uint32 maxDepth)
		: MaxDepth(maxDepth), Size(Exp2(maxDepth + 1))
	{
		Bits = new uint8[Size];
		InitAtDepth(0);
	}

	void InitAtDepth(uint32 depth)
	{
		assert(depth <= MaxDepth);
		memset(Bits, 0, Size * sizeof(uint8));
		Bits[0] = (uint8)MaxDepth;
		uint32 minRange = Exp2(depth);
		uint32 maxRange = Exp2(depth + 1);
		uint32 interval = Exp2(MaxDepth - depth);
		for (uint32 heapID = minRange; heapID < maxRange; ++heapID)
		{
			Bits[heapID * interval] = 1;
		}
		SumReduction();
	}

	void SumReduction()
	{
		int32 d = MaxDepth - 1;
		while (d >= 0)
		{
			uint32 minRange = Exp2(d);
			uint32 maxRange = Exp2(d + 1);
			for (uint32 k = minRange; k < maxRange; ++k)
			{
				Bits[k] = Bits[LeftChildID(k)] + Bits[RightChildID(k)];
			}
			--d;
		}
	}

	template<typename HeapFn>
	void Update(HeapFn&& fn)
	{
		uint32 numNodes = NumNodes();
		for (uint32 leafIndex = 0; leafIndex < numNodes; ++leafIndex)
		{
			uint32 heapIndex = LeafIndexToHeapIndex(leafIndex);
			fn(heapIndex);
		}
		SumReduction();
	}

	uint32 LeafIndexToHeapIndex(uint32 leafIndex) const
	{
		uint32 heapID = 1;
		while (Bits[heapID] > 1)
		{
			if (leafIndex < Bits[heapID])
			{
				heapID = 2 * heapID;
			}
			else
			{
				leafIndex -= Bits[heapID];
				heapID = 2 * heapID + 1;
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
				leafIndex = leafIndex + Bits[heapID ^ 1];
			}
			heapID = heapID / 2;
		}
		return leafIndex;
	}

	uint32 BitfieldHeapID(uint32 heapID) const
	{
		int32 msb = MSB(heapID);
		assert(msb != -1);
		return heapID * Exp2(MaxDepth - msb);
	}

	void SplitNode(uint32 heapID)
	{
		uint32 rightChild = RightChildID(heapID);
		uint32 bit = BitfieldHeapID(rightChild);
		Bits[bit] = 1;
	}

	void MergeNode(uint32 heapID)
	{
		uint32 rightSibling = heapID | 1;
		uint32 bit = BitfieldHeapID(rightSibling);
		Bits[bit] = 0;
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

	bool IsLeafNode(uint32 heapID)
	{
		return Bits[heapID] == 1;
	}

	uint32 NumNodes() const
	{
		return Bits[1];
	}

	uint32 NumBitfieldBits() const
	{
		return Exp2(MaxDepth);
	}

	~CBT()
	{
		delete[] Bits;
	}

	uint32 MaxDepth;
	uint32 Size;
	uint8* Bits = nullptr;
};
