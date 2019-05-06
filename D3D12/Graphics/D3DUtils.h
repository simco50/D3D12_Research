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
		std::wcout << L"Error: " << errorMsg << std::endl;
	}
	__debugbreak();
	return false;
}