#pragma once
#include "ColorInfo.h"

class GraphicsContext;

class SwapChain {
public:
	SwapChain() = default;
	SwapChain(const SwapChain&) = delete;
	SwapChain(SwapChain&&) = default;

	bool Initialize(
		GraphicsContext& graphicContext,
		HWND hwndAttach,
		uint32_t width,
		uint32_t height,
		const ColorInfo& colorInfo
	) noexcept;

	void BeginFrame(ID3D12Resource** frameTex, CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvHandle) noexcept;

	HRESULT EndFrame() noexcept;

	void OnResizeStarted() noexcept;

	HRESULT OnResizeEnded() noexcept;

	HRESULT OnResized(uint32_t width, uint32_t height) noexcept;

	HRESULT OnColorInfoChanged(const ColorInfo& colorInfo) noexcept;

private:
	HRESULT _RecreateBuffers() noexcept;

	HRESULT _LoadBufferResources() noexcept;

	GraphicsContext* _graphicContext = nullptr;

	winrt::com_ptr<IDXGISwapChain4> _dxgiSwapChain;
	wil::unique_event_nothrow _frameLatencyWaitableObject;
	std::vector<winrt::com_ptr<ID3D12Resource>> _frameBuffers;
	
	winrt::com_ptr<ID3D12DescriptorHeap> _rtvHeap;
	uint32_t _rtvDescriptorSize = 0;

	uint32_t _width = 0;
	uint32_t _height = 0;
	uint32_t _bufferCount = 0;
	bool _isScRGB = false;
	
	bool _isTearingSupported = false;
	bool _isRecreated = true;
	bool _isResizing = false;
};
