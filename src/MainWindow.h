#pragma once
#include "Renderer.h"
#include "WindowBase.h"

class MainWindow : public WindowBaseT<MainWindow> {
	using base_type = WindowBaseT<MainWindow>;
	friend base_type;

public:
	bool Create() noexcept;

	int MessageLoop() noexcept;

protected:
	LRESULT _MessageHandler(UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

private:
	Renderer _renderer;
};
