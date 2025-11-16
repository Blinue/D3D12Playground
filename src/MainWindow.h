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
	std::optional<Renderer> _renderer;
	float _dpiScale = 1.0f;
	// 用于区分调整大小和移动
	bool _isPreparingForResize = false;
	bool _isResizing = false;
};
