#pragma once

namespace BitOperations
{
	template<typename T>
	bool LeastSignificantBit(T mask, uint32* pIndex)
	{
		if (mask == 0)
		{
			return false;
		}
		*pIndex = 0;
		while (!(mask & 1))
		{
			mask >>= 1;
			++(*pIndex);
		}
		return true;
	}

	template<typename T>
	bool MostSignificantBit(T mask, uint32* pIndex)
	{
		if (mask == 0)
		{
			return false;
		}
		*pIndex = 0;
		while (mask)
		{
			mask >>= 1;
			++(*pIndex);
		}
		--(*pIndex);
		return true;
	}
}

template<uint32 Bits, typename Storage = uint32>
class BitField;

template<uint32 Bits, typename Storage>
class BitField
{
public:
	class SetBitsIterator
	{
	public:
		explicit SetBitsIterator(const BitField* pBitField)
			: m_CurrentIndex(0), m_pBitField(pBitField)
		{
		}

		void operator++()
		{
			while (m_CurrentIndex < Bits)
			{
				++m_CurrentIndex;
				if (m_pBitField->GetBit(m_CurrentIndex))
				{
					break;
				}
			}
		}

		bool Valid() const
		{
			return m_CurrentIndex < Bits;
		}

		int Value() const
		{
			return m_CurrentIndex;
		}
	private:
		uint32 m_CurrentIndex;
		const BitField* m_pBitField;
	};

	BitField()
	{
		ClearAll();
	}

	explicit BitField(const bool set)
	{
		if (set)
		{
			SetAll();
		}
		else
		{
			ClearAll();
		}
	}

	inline void SetBit(const uint32 bit)
	{
		Data[StorageIndexOfBit(bit)] |= MakeBitmaskForStorage(bit);
	}

	void SetBitAndUp(const uint32 bit)
	{
		uint32 storageIndex = StorageIndexOfBit(bit);
		for (uint32 i = storageIndex + 1; i < Elements(); ++i)
		{
			Data[i] = ((Storage)~0);
		}
		Data[storageIndex] |= ((Storage)~0) << IndexOfBitInStorage(bit);
	}

	void SetBitAndDown(const uint32 bit)
	{
		uint32 storageIndex = StorageIndexOfBit(bit + 1);
		for (int i = storageIndex - 1; i >= 0; --i)
		{
			Data[i] = ((Storage)~0);
		}
		Data[storageIndex] |= ((Storage)~0) >> (BitsPerStorage() - IndexOfBitInStorage(bit + 1));
	}

	inline void ClearBit(const uint32 bit)
	{
		Data[StorageIndexOfBit(bit)] &= ~MakeBitmaskForStorage(bit);
	}

	inline bool GetBit(const uint32 bit) const
	{
		return (Data[StorageIndexOfBit(bit)] & MakeBitmaskForStorage(bit)) != 0;
	}

	void AssignBit(const uint32 bit, const bool set)
	{
		set ? SetBit(bit) : ClearBit(bit);
	}

	void ClearAll()
	{
		for (uint32 i = 0; i < Elements(); ++i)
		{
			Data[i] = Storage();
		}
	}

	void SetAll()
	{
		for (uint32 i = 0; i < Elements(); ++i)
		{
			Data[i] = ((Storage)~0);
		}
	}

	SetBitsIterator GetSetBitsIterator() const
	{
		return SetBitsIterator(this);
	}

	bool IsEqual(const BitField& other) const
	{
		for (uint32 i = 0; i < Elements(); ++i)
		{
			if (Data[i] != other.Data[i])
			{
				return false;
			}
		}
		return true;
	}

	bool AnyBitSet() const
	{
		for (uint32 i = 0; i < Elements(); ++i)
		{
			if (Data[i] > 0)
			{
				return true;
			}
		}
		return false;
	}

	bool NoBitSet() const
	{
		for (uint32 i = 0; i < Elements(); ++i)
		{
			if (Data[i] > 0)
			{
				return false;
			}
		}
		return true;
	}

	bool MostSignificantBit(size_t* pIndex)
	{
		for (size_t i = Elements() - 1; i >= 0; --i)
		{
			if (BitOperations::MostSignificantBit(Data[i], pIndex) == true)
			{
				*pIndex += i * BitsPerStorage();
				return true;
			}
		}
		return false;
	}

	bool LeastSignificantBit(size_t* pIndex)
	{
		for (size_t i = 0; i < Elements(); ++i)
		{
			if (BitOperations::LeastSignificantBit(Data[i], pIndex) == true)
			{
				*pIndex += i * BitsPerStorage();
				return true;
			}
		}
		return false;
	}

	bool operator==(const BitField& other) const
	{
		for (uint32 i = 0; i < Elements(); ++i)
		{
			if (Data[i] != other.Data[i])
			{
				return false;
			}
		}
		return true;
	}

	bool operator!=(const BitField& other) const
	{
		for (uint32 i = 0; i < Elements(); ++i)
		{
			if (Data[i] != other.Data[i])
			{
				return true;
			}
		}
		return false;
	}

	BitField& operator&=(const BitField& other)
	{
		for (uint32 i = 0; i < Elements(); ++i)
		{
			Data[i] &= other.Data[i];
		}
		return *this;
	}

	BitField& operator|=(const BitField& other)
	{
		for (uint32 i = 0; i < Elements(); ++i)
		{
			Data[i] |= other.Data[i];
		}
		return *this;
	}

	BitField& operator^=(const BitField& other)
	{
		for (uint32 i = 0; i < Elements(); ++i)
		{
			Data[i] ^= other.Data[i];
		}
		return *this;
	}

	BitField operator&(const BitField& other) const
	{
		BitField out;
		for (uint32 i = 0; i < Elements(); ++i)
		{
			out.Data[i] = Data[i] & other.Data[i];
		}
		return out;
	}

	BitField operator|(const BitField& other) const
	{
		BitField out;
		for (uint32 i = 0; i < Elements(); ++i)
		{
			out.Data[i] = Data[i] | other.Data[i];
		}
		return out;
	}

	BitField operator^(const BitField& other) const
	{
		BitField out;
		for (uint32 i = 0; i < Elements(); ++i)
		{
			out.Data[i] = Data[i] ^ other.Data[i];
		}
		return out;
	}

	BitField operator~() const
	{
		BitField out;
		for (uint32 i = 0; i < Elements(); ++i)
		{
			out.Data[i] = ~Data[i];
		}
		return out;
	}

private:
	static constexpr uint32 StorageIndexOfBit(uint32 bit)
	{
		return bit / BitsPerStorage();
	}

	static constexpr uint32 IndexOfBitInStorage(uint32 bit)
	{
		return bit % BitsPerStorage();
	}

	static constexpr uint32 BitsPerStorage()
	{
		return sizeof(Storage) * 8;
	}

	static constexpr uint32 MakeBitmaskForStorage(uint32 bit)
	{
		return 1 << IndexOfBitInStorage(bit);
	}

	static constexpr uint32 Elements()
	{
		return (Bits + BitsPerStorage() - 1) / BitsPerStorage();
	}

	Storage Data[Elements()];
};