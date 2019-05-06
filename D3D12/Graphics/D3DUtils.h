#pragma once

#define HR(hr) \
LogHRESULT(hr)

static bool LogHRESULT(HRESULT hr)
{
	if (hr == S_OK)
		return true;

	WCHAR* errorMsg;
	if (FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&errorMsg, 0, nullptr) != 0)
	{
		OutputDebugStringW(errorMsg);
		std::wcout << L"Error: " << errorMsg << std::endl;
	}
	__debugbreak();
	return false;
}

inline void SetD3DObjectName(ID3D12Object* pObject, const char* pName)
{
#ifdef _DEBUG
	if (pObject)
	{
		wchar_t name[256];
		size_t written = 0;
		mbstowcs_s(&written, name, pName, 256);
		pObject->SetName(name);
	}
#endif
}