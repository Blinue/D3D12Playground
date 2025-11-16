#pragma once

class SwapChain {
public:
	SwapChain() = default;
	SwapChain(const SwapChain&) = delete;
	SwapChain(SwapChain&&) = default;

	~SwapChain();

	bool Initialize(
		ID3D12Device5* device,
		ID3D12CommandQueue* commandQueue,
		IDXGIFactory7* dxgiFactory,
		HWND hwndAttach,
		uint32_t width,
		uint32_t height,
		bool useScRGB
	) noexcept;

	HRESULT BeginFrame(
		ID3D12Resource** frameTex,
		CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvHandle,
		uint32_t& bufferIndex
	) noexcept;

	HRESULT EndFrame() noexcept;

	HRESULT RecreateBuffers(uint32_t width, uint32_t height, bool useScRGB) noexcept;

	uint32_t GetBufferCount() const noexcept;

	void OnResizeStarted() noexcept;

	HRESULT OnResizeEnded() noexcept;

private:
	HRESULT _LoadBufferResources(uint32_t bufferCount, bool useScRGB) noexcept;

	HRESULT _WaitForGpu() noexcept;

	ID3D12Device* _device = nullptr;
	ID3D12CommandQueue* _commandQueue = nullptr;

	winrt::com_ptr<IDXGISwapChain4> _dxgiSwapChain;
	wil::unique_event_nothrow _frameLatencyWaitableObject;
	std::vector<winrt::com_ptr<ID3D12Resource>> _renderTargets;
	std::vector<UINT64> _bufferFenceValues;
	UINT _bufferIndex = 0;

	winrt::com_ptr<ID3D12DescriptorHeap> _rtvHeap;
	UINT _rtvDescriptorSize = 0;

	winrt::com_ptr<ID3D12Fence1> _fence;
	UINT64 _curFenceValue = 0;
	wil::unique_event_nothrow _fenceEvent;
	
	bool _isRecreated = true;
	bool _isResizing = false;
};
