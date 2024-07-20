#pragma once

#include <type_traits>

template<typename Enum, typename StorageType = std::underlying_type_t<Enum>>
class EnumSet
{
public:
	template<typename... Args>
	constexpr static bool IsEnumType = (std::is_same_v<Enum, std::decay_t<Args>> && ...);

	constexpr static int NumBits = sizeof(StorageType) * 4;

	constexpr EnumSet() = default;

	template<typename... Args>
		requires IsEnumType<Args...>
	constexpr EnumSet(Args... args)
	{
		Add(args...);
	}

	template<typename... Args>
		requires IsEnumType<Args...>
	void Add(Args... args)
	{
		([&] {
			Add(args);
			}(), ...);
	}

	template<typename... Args>
		requires IsEnumType<Args...>
	void Remove(Args... args)
	{
		([&] {
			Remove(args);
			}(), ...);
	}

	void Add(Enum value)
	{
		gAssert((StorageType)value < NumBits);
		Value |= (1u << (StorageType)value);
	}

	void Remove(Enum value)
	{
		gAssert((StorageType)value < NumBits);
		StorageType mask = ~(1u << (StorageType)value);
		Value &= mask;
	}

	template<typename... Args>
		requires IsEnumType<Args...>
	constexpr bool ContainsAll(Args... args) const
	{
		EnumSet rhs(args...);
		return (rhs.Value & Value) == rhs.Value;
	}

	template<typename... Args>
		requires IsEnumType<Args...>
	constexpr bool ContainsAny(Args... args) const
	{
		EnumSet rhs(args...);
		return (rhs.Value & Value) != 0;
	}

	constexpr bool Contains(Enum value) const
	{
		return ContainsAll(value);
	}

	StorageType Value = (StorageType)0;
};


#define DEFINE_ENUM_SET(name, enum_name) using name = EnumSet<enum_name>;

#define DEFINE_ENUM_SET_EX(name, enum_name, storage_type) using name = EnumSet<enum_name, storage_type>;
