#include "pch.h"
#include "MainWindow.h"

// Debug 配置下使用 Agility SDK 辅助调试
#ifdef _DEBUG
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 618; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }
#endif

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
