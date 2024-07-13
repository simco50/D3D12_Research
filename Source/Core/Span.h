#pragma once

template<typename T>
class Span
{
public:
	Span() :
		m_pValue(nullptr), m_Count(0)
	{}

	Span(const std::initializer_list<std::remove_cv_t<T>>& list) :
		m_pValue(list.begin()), m_Count((uint32)list.size())
	{}

	Span(const Array<std::remove_cv_t<T>>& v) :
		m_pValue(v.data()), m_Count((uint32)v.size())
	{}

	template<size_t N>
	Span(const StaticArray<std::remove_cv_t<T>, N>& v) :
		m_pValue(v.data()), m_Count((uint32)v.size())
	{}

	Span(const T* pValue, uint32 size) :
		m_pValue(pValue), m_Count(size)
	{}

	template<size_t N>
	Span(const T(&arr)[N])
		: m_pValue(arr), m_Count(N)
	{}

	Span(const T& value) :
		m_pValue(&value), m_Count(1)
	{}

	Span Subspan(uint32 from, uint32 count = 0xFFFFFFFF) const
	{
		uint32 num = count == 0xFFFFFFFF ? m_Count - from : count;
		gAssert(from <= m_Count);
		gAssert(from + count <= m_Count);
		return Span(m_pValue + from, num);
	}

	Array<T> Copy() const
	{
		Array<T> result(GetSize());
		for (uint32 i = 0; i < GetSize(); ++i)
		{
			result[i] = m_pValue[i];
		}
		return result;
	}

	const T& operator[](uint32 idx) const
	{
		gAssert(idx < m_Count);
		return m_pValue[idx];
	}

	const T* begin() const { return m_pValue; }
	const T* end() const { return m_pValue + m_Count; }

	uint32 IndexOf(const T* pValue) const
	{
		gAssert(pValue >= m_pValue && pValue < m_pValue + m_Count);
		return uint32(pValue - m_pValue);
	}
	const T* GetData() const { return m_pValue; }
	uint32 GetSize() const { return m_Count; }

private:
	const T* m_pValue;
	uint32 m_Count;
};
