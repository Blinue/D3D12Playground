#pragma once

// pch.h: 这是预编译标头文件。
// 下方列出的文件仅编译一次，提高了将来生成的生成性能。
// 这还将影响 IntelliSense 性能，包括代码完成和许多代码浏览功能。
// 但是，如果此处列出的文件中的任何一个在生成之间有更新，它们全部都将被重新编译。
// 请勿在此处添加要频繁更新的文件，这将使得性能优势无效。


// Windows
#include <SDKDDKVer.h>
#include <Windows.h>
#include <windowsx.h>

// DirectX
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_6.h>

// C++
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

// WIL
#include <wil/resource.h>
#include <wil/win32_helpers.h>
// 防止编译失败
#define WIL_ENABLE_EXCEPTIONS
#define RESOURCE_SUPPRESS_STL
#include <wil/stl.h>
#undef RESOURCE_SUPPRESS_STL
#undef WIL_ENABLE_EXCEPTIONS

// C++/WinRT
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.System.h>

namespace winrt {
using namespace Windows::Foundation;
using namespace Windows::Graphics::Display;
using namespace Windows::System;
}

using namespace std::string_literals;
using namespace std::string_view_literals;

enum class ComponentState {
	NoError,
	DeviceLost,
	Error
};

struct Size {
	uint32_t width;
	uint32_t height;

	bool operator==(const Size&) const noexcept = default;

	explicit operator SIZE() const noexcept {
		return { (LONG)width,(LONG)height };
	}
};

struct ColorInfo {
	winrt::AdvancedColorKind kind = winrt::AdvancedColorKind::StandardDynamicRange;
	// HDR 模式下最大亮度，1.0 表示 80nit
	float maxLuminance = 1.0f;
	// HDR 模式下 SDR 内容亮度，1.0 表示 80nit
	float sdrWhiteLevel = 1.0f;

	bool operator==(const ColorInfo& other) const = default;
};
