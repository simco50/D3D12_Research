#pragma once


struct Symbol
{
	uint64 Address;
	char Name[255];
	char FilePath[MAX_PATH];
	uint32 LineNumber;
};

namespace StackTrace
{
	uint32 Trace(void** pStackData, uint32 stackSize, uint32 skipDepth);
	void Resolve(Span<uint64> stackFrame, uint32 numFrames, Symbol* outSymbols);
}


template<uint32 Size>
class Callstack
{
public:
	void Trace(uint32 skipDepth = 0)
	{
		m_Resolved = false;
		m_NumFrames = StackTrace::Trace((void**)&m_Stack, Size, skipDepth + 1);
	}

	Span<const Symbol> Resolve()
	{
		if (!m_Resolved)
		{
			StackTrace::Resolve(Span(m_Stack), m_NumFrames, m_Symbols);
			m_Resolved = true;
		}
		return m_Symbols;
	}

	std::string ToString()
	{
		Resolve();
		std::string output;
		for (uint32 i = 0; i < m_NumFrames; i++)
		{
			const Symbol& s = m_Symbols[i];
			output += Sprintf("0x%x - %s() - Line %d\n", s.Address, s.Name, s.LineNumber);
		}
		return output;
	}

private:
	bool m_Resolved = false;
	uint32 m_NumFrames = 0;
	Symbol m_Symbols[Size];
	uint64 m_Stack[Size];
};
