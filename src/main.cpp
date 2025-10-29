#include "pch.h"
#include "MainWindow.h"

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
