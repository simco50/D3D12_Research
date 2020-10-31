#pragma once
#include <stdint.h>
#include <assert.h>
#include <type_traits>

using int8 = int8_t;
using int16 = int16_t;
using int32 = int32_t;
using int64 = int64_t;

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

namespace BitOperations
{
	template<typename T>
	bool LeastSignificantBit(T mask, uint32* pIndex)
	{
		*pIndex = ~0u;
		while (mask)
		{
			*pIndex += 1;
			if ((mask & 1) == 1)
			{
				return true;
			}
			mask >>= 1;
		}
		return false;
	}

	template<typename T>
	bool MostSignificantBit(T mask, uint32* pIndex)
	{
		*pIndex = ~0u;
		while (mask)
		{
			mask >>= 1;
			++(*pIndex);
		}
		return *pIndex != ~0;
	}
}

template<uint32 Bits, typename Storage = uint32>
class BitField;

using BitField32 = BitField<32, uint32>;
using BitField64 = BitField<64, uint32>;

template<uint32 Bits, typename Storage>
class BitField
{
public:
	class SetBitsIterator
	{
	public:
		explicit SetBitsIterator(const BitField* pBitField, bool end = false)
			: m_CurrentIndex(INVALID), m_pBitField(pBitField)
		{
			if (!end)
			{
				pBitField->LeastSignificantBit(&m_CurrentIndex);
			}
		}

		void operator++()
		{
			while (++m_CurrentIndex < Bits)
			{
				if (m_pBitField->GetBit(m_CurrentIndex))
				{
					return;
				}
			}
			m_CurrentIndex = INVALID;
		}

		bool operator!=(const SetBitsIterator& other)
		{
			return m_CurrentIndex != other.m_CurrentIndex;
		}

		bool Valid() const
		{
			return m_CurrentIndex < Bits;
		}

		uint32 Value() const
		{
			return m_CurrentIndex;
		}

		uint32 operator*() const
		{
			return m_CurrentIndex;
		}

		static constexpr uint32 INVALID = ~0u;

	private:
		uint32 m_CurrentIndex;
		const BitField* m_pBitField;
	};

	BitField()
	{
		ClearAll();
	}

	explicit BitField(bool set)
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

	template<uint32, typename> friend class BitField;

	template<typename T>
	explicit BitField(T value)
	{
		static_assert(std::is_integral_v<T>, "Not an integral type");
		ClearAll();
		uint32 size = sizeof(value) < sizeof(Storage) * Elements() ? sizeof(value) : sizeof(Storage) * Elements();
		memcpy(Data, &value, size);
	}

	template<uint32 OtherNumBits, typename OtherStorage>
	BitField(const BitField<OtherNumBits, OtherStorage>& other)
	{
		ClearAll();
		static_assert(Bits <= OtherNumBits, "Source can't have more bits");
		uint32 size = Bits <= OtherNumBits ? Bits : OtherNumBits;
		memcpy(Data, other.Data, size / 8);
	}

	void ClearAll()
	{
		memset(Data, 0x00000000, sizeof(Storage) * Elements());
	}

	void SetAll()
	{
		memset(Data, 0xFFFFFFFF, sizeof(Storage) * Elements());
	}

	inline void SetBit(uint32 bit)
	{
		assert(bit < Size());
		Data[StorageIndexOfBit(bit)] |= MakeBitmaskForStorage(bit);
	}

	inline void ClearBit(uint32 bit)
	{
		assert(bit < Size());
		Data[StorageIndexOfBit(bit)] &= ~MakeBitmaskForStorage(bit);
	}

	inline bool GetBit(uint32 bit) const
	{
		assert(bit < Size());
		return (Data[StorageIndexOfBit(bit)] & MakeBitmaskForStorage(bit)) != 0;
	}

	void AssignBit(uint32 bit, bool set)
	{
		set ? SetBit(bit) : ClearBit(bit);
	}

	void SetRange(uint32 from, uint32 to, bool set = true)
	{
		assert(from < Size());
		assert(to <= Size());
		assert(from <= to);
		while (from < to)
		{
			uint32 fromInStorage = from % BitsPerStorage();
			uint32 storageIndex = StorageIndexOfBit(from);
			uint32 maxBitInStorage = (storageIndex + 1) * BitsPerStorage();
			Storage mask = (Storage)~0 << fromInStorage;
			if (to < maxBitInStorage)
			{
				Storage mask2 = ((Storage)1 << (to % BitsPerStorage())) - (Storage)1;
				mask &= mask2;
			}
			if (set)
			{
				Data[storageIndex] |= mask;
			}
			else
			{
				Data[storageIndex] &= ~mask;
			}
			from = maxBitInStorage;
		}
	}

	void SetBitAndUp(uint32 bit, uint32 count = ~0)
	{
		assert(bit < Size());
		count = count < Size() - bit ? count : Size() - bit;
		SetRange(bit, bit + count);
	}

	void SetBitAndDown(uint32 bit, uint32 count = ~0)
	{
		assert(bit < Size());
		count = bit < count ? bit : count;
		SetRange(bit - count, bit);
	}

	SetBitsIterator GetSetBitsIterator() const
	{
		return SetBitsIterator(this);
	}

	bool HasAnyBitSet() const
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

	bool HasNoBitSet() const
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

	bool MostSignificantBit(uint32* pIndex) const
	{
		for (int32 i = (int)Elements() - 1; i >= 0; --i)
		{
			if (BitOperations::MostSignificantBit(Data[i], pIndex))
			{
				*pIndex += i * BitsPerStorage();
				return true;
			}
		}
		return false;
	}

	bool LeastSignificantBit(uint32* pIndex) const
	{
		for (uint32 i = 0; i < Elements(); ++i)
		{
			if (BitOperations::LeastSignificantBit(Data[i], pIndex))
			{
				*pIndex += i * BitsPerStorage();
				return true;
			}
		}
		return false;
	}

	SetBitsIterator begin() const
	{
		return SetBitsIterator(this);
	}

	SetBitsIterator end() const
	{
		return SetBitsIterator(this, true);
	}

	bool operator[](uint32 index) const
	{
		return GetBit(index);
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

	static constexpr uint32 Size()
	{
		return Bits;
	}

	static constexpr uint32 Capacity()
	{
		return Bits;
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

	static constexpr Storage MakeBitmaskForStorage(uint32 bit)
	{
		return (Storage)1 << IndexOfBitInStorage(bit);
	}

	static constexpr uint32 Elements()
	{
		return (Bits + BitsPerStorage() - 1) / BitsPerStorage();
	}

	Storage Data[Elements()];
};