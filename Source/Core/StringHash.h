#pragma once

template<bool CaseSensitive>
class TStringHash
{
private:
	static constexpr uint32 val_const{ 0x811c9dc5 };
	static constexpr uint32 prime_const{ 0x1000193 };

	static inline constexpr uint32 Hash_Internal(const char* const str, const uint32 value) noexcept
	{
		if (!str)
			return 0;
		if constexpr (CaseSensitive)
		{
			return (str[0] == '\0') ? value : Hash_Internal(&str[1], (value ^ uint64(str[0])) * prime_const);
		}
		else
		{
			char lowerCase = CString::ToLower(str[0]);
			return (lowerCase == '\0') ? value : Hash_Internal(&str[1], (value ^ uint64(lowerCase)) * prime_const);
		}
	}

public:
	static inline constexpr TStringHash Hash(const char* const str) noexcept
	{
		return TStringHash(Hash_Internal(str, val_const));
	}

	constexpr TStringHash() noexcept
		: m_Hash(0)
	{
	}

	constexpr TStringHash(const TStringHash& other)
		: m_Hash(other.m_Hash)
	{
	}

	explicit constexpr TStringHash(const uint32 hash) noexcept
		: m_Hash(hash)
	{
	}

	constexpr TStringHash(const char* const pText) noexcept
		: m_Hash(Hash_Internal(pText, val_const))
	{
	}

	explicit TStringHash(const String& text)
		: m_Hash(Hash_Internal(text.c_str(), val_const))
	{
	}

	constexpr bool operator==(const TStringHash& other)
	{
		return m_Hash == other.m_Hash;
	}

	constexpr bool operator!=(const TStringHash& other)
	{
		return m_Hash != other.m_Hash;
	}

	constexpr void Combine(uint32 other)
	{
		m_Hash ^= other + 0x9e3779b9 + (m_Hash << 6) + (m_Hash >> 2);
	}

	inline constexpr operator uint32() const { return m_Hash; }

	inline bool operator==(const TStringHash& rhs) const { return m_Hash == rhs.m_Hash; }
	inline bool operator!=(const TStringHash& rhs) const { return m_Hash != rhs.m_Hash; }
	inline bool operator<(const TStringHash& rhs) const { return m_Hash < rhs.m_Hash; }
	inline bool operator>(const TStringHash& rhs) const { return m_Hash > rhs.m_Hash; }

	uint32 m_Hash;
};

using StringHash = TStringHash<true>;

namespace std
{
	template<>
	struct hash<TStringHash<true>>
	{
		size_t operator()(const TStringHash<true>& hash) const
		{
			return (size_t)hash.m_Hash;
		}
	};

	template<>
	struct hash<TStringHash<false>>
	{
		size_t operator()(const TStringHash<false>& hash) const
		{
			return (size_t)hash.m_Hash;
		}
	};
}
