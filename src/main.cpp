#include "pch.h"
#include "MainWindow.h"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619; }
// D3D12 相关 dll 不能放在 dll 搜索目录，否则如果 OS 的 D3D12 运行时更新将会错误
// 加载随程序部署的旧版本依赖 dll（包括 D3D12SDKLayers.dll 和 d3d10warp.dll）。
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

int APIENTRY wWinMain(
	_In_ HINSTANCE /*hInstance*/,
	_In_opt_ HINSTANCE /*hPrevInstance*/,
	_In_ LPWSTR /*lpCmdLine*/,
	_In_ int /*nCmdShow*/
) {
	winrt::init_apartment(winrt::apartment_type::single_threaded);

	MainWindow mainWindow;
	if (!mainWindow.Create()) {
		return 1;
	}

	return mainWindow.MessageLoop();
}
