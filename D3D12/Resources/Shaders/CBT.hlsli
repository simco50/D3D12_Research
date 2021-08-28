#define CBT_BITS_PER_ELEMENT 32

struct CBT
{
    RWByteAddressBuffer Storage;
    uint NumElements;

    void Init(RWByteAddressBuffer buffer, uint numElements)
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

    uint BitfieldGet_Single(uint elementIndex, uint bitOffset, uint bitCount)
    {
        uint bitMask = ~(~0u << bitCount);
        return (Storage.Load(elementIndex * 4) >> bitOffset) & bitMask;
    }

    void BitfieldSet_Single(uint elementIndex, uint bitOffset, uint bitCount, uint value)
    {
        uint bitMask = ~(~(~0u << bitCount) << bitOffset);
        uint t;
        Storage.InterlockedAnd(elementIndex * 4, bitMask, t);
        Storage.InterlockedOr(elementIndex * 4, value << bitOffset, t);
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
        offset = exp2(depth + 1) + heapIndex * size;
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
        	uint leftChildValue = GetData(LeftChildIndex(heapIndex));
            if(leafIndex < leftChildValue)
            {
            	heapIndex = LeftChildIndex(heapIndex);
            }
            else
            {
            	leafIndex -= leftChildValue;
                heapIndex = RightChildIndex(heapIndex);
            }
        }
        return heapIndex;
    }
    
    uint BitfieldHeapIndex(uint heapIndex)
    {
    	uint msb = firstbithigh(heapIndex);
        return heapIndex * exp2(GetMaxDepth() - msb);
    }
    
    void SplitNode_Single(uint heapIndex)
    {
    	uint rightChild = RightChildIndex(heapIndex);
        uint bit = BitfieldHeapIndex(rightChild);
        SetData(bit, 1);
    }
    
    void MergeNode_Single(uint heapIndex)
    {
    	uint rightChild = RightChildIndex(heapIndex);
        uint bit = BitfieldHeapIndex(rightChild);
        SetData(bit, 0);
    }

    uint GetDepth(uint heapIndex)
    {
        return floor(log2(heapIndex));
    }
    
    // Helpers
    uint LeftChildIndex(uint heapIndex)
    {
    	return heapIndex << 1;
    }
    
    uint RightChildIndex(uint heapIndex)
    {
    	return (heapIndex << 1) | 1;
    }
    
    uint ParentIndex(uint heapIndex)
    {
    	return heapIndex >> 1;
    }
    
    uint SiblingIndex(uint heapIndex)
    {
    	return heapIndex ^ 1;
    }
};
