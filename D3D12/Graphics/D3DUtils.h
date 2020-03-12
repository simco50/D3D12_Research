#pragma once

#define HR(hr) \
LogHRESULT(hr)

#ifdef _DEBUG
#define D3D_SETNAME(obj, name) SetD3DObjectName(obj, name)
#else
#define D3D_SETNAME(obj, name)
#endif

inline bool LogHRESULT(HRESULT hr)
{
	if (hr == S_OK)
	{
		return true;
	}

	char* errorMsg;
	if (FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&errorMsg, 0, nullptr) != 0)
	{
		E_LOG(Error, "Error: %s", errorMsg);
	}
	__debugbreak();
	return false;
}

inline int ToMultibyte(const wchar_t* pStr, char* pOut, int len)
{
	return WideCharToMultiByte(CP_UTF8, 0, pStr, -1, pOut, len, nullptr, nullptr);
}

inline int ToWidechar(const char* pStr, wchar_t* pOut, int len)
{
	return MultiByteToWideChar(CP_UTF8, 0, pStr, -1, pOut, len);
}

inline void SetD3DObjectName(ID3D12Object* pObject, const char* pName)
{
	if (pObject)
	{
		wchar_t name[256];
		ToWidechar(pName, name, 256);
		pObject->SetName(name);
	}
}