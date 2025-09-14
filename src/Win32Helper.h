#pragma once

struct Win32Helper {
	template<typename T, std::enable_if_t<std::is_function_v<T>, int> = 0>
	static T* LoadSystemFunction(const wchar_t* dllName, const char* funcName) noexcept {
		assert(dllName && funcName);

		HMODULE hMod = GetModuleHandle(dllName);
		if (!hMod) {
			hMod = LoadLibraryEx(dllName, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (!hMod) {
				return nullptr;
			}
		}

		const FARPROC address = GetProcAddress(hMod, funcName);
		if (!address) {
			return nullptr;
		}

		// 先转成 void* 以避免警告
		return reinterpret_cast<T*>(reinterpret_cast<void*>(address));
	}

	struct OSVersion {
		constexpr OSVersion(uint32_t build_) : build(build_) {}

		bool IsWin10() const noexcept {
			return !IsWin11();
		}

		bool IsWin11() const noexcept {
			return Is21H2OrNewer();
		}

		bool Is20H1OrNewer() const noexcept {
			return build >= 19041;
		}

		// 下面为 Win11
		// 不考虑代号相同的 Win10

		bool Is21H2OrNewer() const noexcept {
			return build >= 22000;
		}

		bool Is22H2OrNewer() const noexcept {
			return build >= 22621;
		}

		bool Is24H2OrNewer() const noexcept {
			return build >= 26100;
		}

		uint32_t build = 0;
	};

	static const OSVersion& GetOSVersion() noexcept;
};
