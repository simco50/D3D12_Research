#include "stdafx.h"
#include "Callstack.h"
#include <DbgHelp.h>

namespace StackTrace
{
	using SymFromAddrFn = BOOL(*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
	static SymFromAddrFn sSymFromAddr;
	using SymGetLineFromAddr64Fn = BOOL(*)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
	static SymGetLineFromAddr64Fn sSymGetLineFromAddr64;
	using SymInitializeFn = BOOL(*)(HANDLE, PCSTR, BOOL);
	static SymInitializeFn sSymInitialize;

	static std::mutex sResolveLock;
	static HashMap<uint64, Symbol> sSymbolMap;

	uint32 Trace(void** pStackData, uint32 stackSize, uint32 skipDepth)
	{
		if (!sSymFromAddr)
		{
			HMODULE mod = LoadLibraryA("dbghelp.dll");
			sSymFromAddr = (SymFromAddrFn)GetProcAddress(mod, "SymFromAddr");
			sSymGetLineFromAddr64 = (SymGetLineFromAddr64Fn)GetProcAddress(mod, "SymGetLineFromAddr64");
			sSymInitialize = (SymInitializeFn)GetProcAddress(mod, "SymInitialize");
		}

		HANDLE process = GetCurrentProcess();
		sSymInitialize(process, NULL, TRUE);
		return RtlCaptureStackBackTrace(skipDepth + 1, stackSize, (PVOID*)pStackData, NULL);
	}

	static void Resolve(uint64 stackFrame, Symbol& outSymbol)
	{
		auto it = sSymbolMap.find(stackFrame);
		if (it != sSymbolMap.end())
		{
			outSymbol = it->second;
			return;
		}
		
		static char symbolData[sizeof(SYMBOL_INFO) + 256 * sizeof(char)];
		SYMBOL_INFO* symbol = (SYMBOL_INFO*)&symbolData;
		symbol->MaxNameLen = 255;
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

		static char lineData[sizeof(IMAGEHLP_LINE64)];
		IMAGEHLP_LINE64* line = (IMAGEHLP_LINE64*)&lineData;
		line->SizeOfStruct = sizeof(IMAGEHLP_LINE64);
		DWORD displacement;

		HANDLE process = GetCurrentProcess();
		if (SUCCEEDED(sSymFromAddr(process, stackFrame, 0, symbol)))
		{
			outSymbol.Address = stackFrame;
			strcpy_s(outSymbol.Name, symbol->Name);
		}

		if (SUCCEEDED(sSymGetLineFromAddr64(process, stackFrame, &displacement, line)))
		{
			outSymbol.LineNumber = line->LineNumber;
			strcpy_s(outSymbol.FilePath, line->FileName);
		}

		sSymbolMap[stackFrame] = outSymbol;
	}

	void Resolve(Span<uint64> stackFrame, uint32 numFrames, Symbol* outSymbols)
	{
		std::lock_guard l(sResolveLock);

		for (uint32 i = 0; i < numFrames; i++)
			Resolve(stackFrame[i], outSymbols[i]);
	}
}
