#include "pch.h"
#include "Win32Helper.h"

const Win32Helper::OSVersion& Win32Helper::GetOSVersion() noexcept {
	static OSVersion version = [] {
		const auto rtlGetVersion =
			LoadSystemFunction<LONG WINAPI(PRTL_OSVERSIONINFOW)>(L"ntdll.dll", "RtlGetVersion");
		if (!rtlGetVersion) {
			return OSVersion(0);
		}

		RTL_OSVERSIONINFOW versionInfo{ .dwOSVersionInfoSize = sizeof(versionInfo) };
		rtlGetVersion(&versionInfo);

		return OSVersion(versionInfo.dwBuildNumber);
	}();

	return version;
}

const std::filesystem::path& Win32Helper::GetExePath() noexcept {
	static std::filesystem::path result = [] {
		std::wstring exePath;
		FAIL_FAST_IF_FAILED(wil::GetModuleFileNameW(NULL, exePath));
		return std::filesystem::path(std::move(exePath));
	}();
	return result;
}
