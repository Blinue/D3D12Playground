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

	const long clientWidth = std::lroundf(_dpiScale * 900);
	const long clientHeight = std::lroundf(_dpiScale * 600);
	{
		RECT windowRect{ 0,0,clientWidth,clientHeight };
		AdjustWindowRectExForDpi(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0,
			(UINT)std::lroundf(_dpiScale * USER_DEFAULT_SCREEN_DPI));
		SetWindowPos(Handle(), NULL, 0, 0, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
			SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER);
	}

	_renderer.emplace();
	if (!_renderer->Initialize(Handle(), clientWidth, clientHeight, _dpiScale)) {
		return false;
	}

	if (!_renderer->Render()) {
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

		if (!_renderer->Render()) {
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

		_dpiScale = GetDpiForWindow(Handle()) / float(USER_DEFAULT_SCREEN_DPI);

		return 0;
	}
	case WM_DPICHANGED:
	{
		_dpiScale = HIWORD(wParam) / float(USER_DEFAULT_SCREEN_DPI);

		RECT* newRect = (RECT*)lParam;
		SetWindowPos(
			Handle(),
			NULL,
			newRect->left,
			newRect->top,
			newRect->right - newRect->left,
			newRect->bottom - newRect->top,
			SWP_NOZORDER | SWP_NOACTIVATE
		);

		return 0;
	}
	case WM_GETMINMAXINFO:
	{
		// 设置窗口最小尺寸
		const long minClientSize = std::lroundf(400 * _dpiScale);
		RECT windowRect{ 0,0,minClientSize,minClientSize };
		AdjustWindowRectExForDpi(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0,
			(UINT)std::lroundf(_dpiScale * USER_DEFAULT_SCREEN_DPI));

		((MINMAXINFO*)lParam)->ptMinTrackSize = {
			windowRect.right - windowRect.left,
			windowRect.bottom - windowRect.top
		};

		return 0;
	}
	case WM_NCCALCSIZE:
	{
		if (!wParam) {
			return 0;
		}

		LRESULT ret = DefWindowProc(Handle(), WM_NCCALCSIZE, wParam, lParam);
		if (ret != 0) {
			return ret;
		}

		if (_renderer) {
			NCCALCSIZE_PARAMS& params = *(NCCALCSIZE_PARAMS*)lParam;
			// 此时第一个成员是新窗口矩形
			const RECT& clientRect = params.rgrc[0];
			_renderer->Resize(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, _dpiScale);
		}

		return 0;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	}

	return base_type::_MessageHandler(msg, wParam, lParam);
}
