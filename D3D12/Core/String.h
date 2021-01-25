#pragma once

template<typename CharSource, typename CharDest>
inline size_t StringConvert(const CharSource* pSource, CharDest* pDestination, int destinationSize)
{
	static_assert(0, "Not implemented");
}

template<>
inline size_t StringConvert(const wchar_t* pSource, char* pDestination, int destinationSize)
{
	size_t converted = 0;
	wcstombs_s(&converted, pDestination, destinationSize, pSource, destinationSize);
	return converted;
}

template<>
inline size_t StringConvert(const char* pSource, wchar_t* pDestination, int destinationSize)
{
	size_t converted = 0;
	mbstowcs_s(&converted, pDestination, destinationSize, pSource, destinationSize);
	return converted;
}

template<typename CharSource, typename CharDest>
struct StringConverter
{
	StringConverter(const CharSource* pStr)
		: m_String{}
	{
		StringConvert<CharSource, CharDest>(pStr, m_String, 128);
	}

	CharDest* Get() { return m_String; }

	const CharDest* operator*() const { return m_String; }
private:
	CharDest m_String[128];
};

using UnicodeToMultibyte = StringConverter<wchar_t, char>;
using MultibyteToUnicode = StringConverter<char, wchar_t>;
#define UNICODE_TO_MULTIBYTE(input) UnicodeToMultibyte(input).Get()
#define MULTIBYTE_TO_UNICODE(input) MultibyteToUnicode(input).Get()
