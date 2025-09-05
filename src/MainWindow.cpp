#include "pch.h"
#include "MainWindow.h"
#include <Uxtheme.h>

bool MainWindow::Create() noexcept {
	static const wchar_t* MAIN_WINDOW_CLASS_NAME = L"D3D12Playground_Main";

	const HINSTANCE hInst = wil::GetModuleInstanceHandle();

	WNDCLASSEXW wcex{
	   .cbSize = sizeof(WNDCLASSEX),
	   .style = CS_HREDRAW | CS_VREDRAW,
	   .lpfnWndProc = _WndProc,
	   .hInstance = hInst,
	   .hCursor = LoadCursor(nullptr, IDC_ARROW),
	   .hbrBackground = HBRUSH(COLOR_WINDOW + 1),
	   .lpszClassName = MAIN_WINDOW_CLASS_NAME
	};
	if (!RegisterClassEx(&wcex)) {
		return false;
	}

	CreateWindowEx(
		WS_EX_NOREDIRECTIONBITMAP,
		MAIN_WINDOW_CLASS_NAME,
		L"D3D12Playground",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL,
		NULL,
		hInst,
		this
	);
	if (!Handle()) {
		return false;
	}

	RECT clientRect;
	GetClientRect(Handle(), &clientRect);

	if (!_renderer.Initialize(Handle(),
		clientRect.right - clientRect.left, clientRect.bottom - clientRect.top)) {
		return false;
	}

	if (!_renderer.Render()) {
		return false;
	}

	ShowWindow(Handle(), SW_SHOWNORMAL);
	return true;
}

int MainWindow::MessageLoop() noexcept {
	MSG msg;
	while (true) {
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				Destroy();
				return (int)msg.wParam;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (!_renderer.Render()) {
			PostQuitMessage(1);
		}
	}
}

LRESULT MainWindow::_MessageHandler(UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
	switch (msg) {
	case WM_CREATE:
	{
		const DWORD attributes = WTNCA_NODRAWICON | WTNCA_NOSYSMENU;
		SetWindowThemeNonClientAttributes(Handle(), attributes, attributes);
		return 0;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	}

	return base_type::_MessageHandler(msg, wParam, lParam);
}
