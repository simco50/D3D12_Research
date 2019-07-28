#pragma once

class StringHash
{
private:

	static constexpr uint32 val_const{ 0x811c9dc5 };
	static constexpr uint32 prime_const{ 0x1000193 };

	static inline constexpr uint32 Hash_Internal(const char* const str, const uint32 value) noexcept
	{
		return (str[0] == '\0') ? value : Hash_Internal(&str[1], (value ^ uint64(str[0])) * prime_const);
	}

public:
	static inline constexpr StringHash Hash(const char* const str) noexcept
	{
		return StringHash(Hash_Internal(str, val_const));
	}

	constexpr StringHash() noexcept
		: m_Hash(0)
	{
	}

	constexpr StringHash(const StringHash& other)
		: m_Hash(other.m_Hash)
	{
	}

	explicit constexpr StringHash(const uint32 hash) noexcept
		: m_Hash(hash)
	{
	}

	explicit constexpr StringHash(const char* const pText) noexcept
		: m_Hash(Hash_Internal(pText, val_const))
	{
	}

	explicit StringHash(const std::string& text)
		: m_Hash(Hash_Internal(text.c_str(), val_const))
	{
	}

	constexpr bool operator==(const StringHash& other)
	{
		return m_Hash == other.m_Hash;
	}

	constexpr bool operator!=(const StringHash& other)
	{
		return m_Hash != other.m_Hash;
	}

	inline constexpr operator uint32() const { return m_Hash; }

	inline bool operator==(const StringHash& rhs) const { return m_Hash == rhs.m_Hash; }
	inline bool operator!=(const StringHash& rhs) const { return m_Hash != rhs.m_Hash; }
	inline bool operator<(const StringHash& rhs) const { return m_Hash < rhs.m_Hash; }
	inline bool operator>(const StringHash& rhs) const { return m_Hash > rhs.m_Hash; }

	uint32 m_Hash;
};

namespace std
{
	template <>
	struct hash<StringHash>
	{
		uint32 operator()(const StringHash& hash) const
		{
			return hash.m_Hash;
		}
	};
}