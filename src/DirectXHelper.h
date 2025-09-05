#pragma once

struct DirectXHelper {
	static bool IsWARP(const DXGI_ADAPTER_DESC1& desc) noexcept {
		// 不要检查 DXGI_ADAPTER_FLAG_SOFTWARE 标志，如果系统没有安装任何显卡，WARP 没有这个标志。
		// 这两个值来自 https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi#new-info-about-enumerating-adapters-for-windows-8
		return desc.VendorId == 0x1414 && desc.DeviceId == 0x8c;
	}
};
