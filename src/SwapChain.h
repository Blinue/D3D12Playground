#pragma once

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
		bool useScRGB
	) noexcept;

	void BeginFrame(ID3D12Resource** frameTex, CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvHandle) noexcept;

	HRESULT EndFrame() noexcept;

	HRESULT RecreateBuffers(uint32_t width, uint32_t height, bool useScRGB) noexcept;

	void OnResizeStarted() noexcept;

	HRESULT OnResizeEnded() noexcept;

private:
	HRESULT _LoadBufferResources(uint32_t bufferCount, bool useScRGB) noexcept;

	GraphicsContext* _graphicContext = nullptr;

	winrt::com_ptr<IDXGISwapChain4> _dxgiSwapChain;
	wil::unique_event_nothrow _frameLatencyWaitableObject;
	std::vector<winrt::com_ptr<ID3D12Resource>> _frameBuffers;
	
	winrt::com_ptr<ID3D12DescriptorHeap> _rtvHeap;
	uint32_t _rtvDescriptorSize = 0;
	
	bool _isRecreated = true;
	bool _isResizing = false;
};
