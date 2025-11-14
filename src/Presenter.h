#pragma once

class Presenter {
public:
	Presenter() = default;
	Presenter(const Presenter&) = delete;
	Presenter(Presenter&&) = default;

	~Presenter();

	bool Initialize(
		ID3D12Device5* device,
		ID3D12CommandQueue* commandQueue,
		IDXGIFactory7* dxgiFactory,
		HWND hwndAttach,
		uint32_t width,
		uint32_t height,
		winrt::AdvancedColorKind acKind
	) noexcept;

	HRESULT BeginFrame(
		ID3D12Resource** frameTex,
		CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvHandle,
		uint32_t& bufferIndex
	) noexcept;

	HRESULT EndFrame() noexcept;

	HRESULT RecreateBuffers(uint32_t width, uint32_t height, winrt::AdvancedColorKind acKind) noexcept;

	static constexpr inline uint32_t BUFFER_COUNT = 2;

private:
	HRESULT _WaitForGpu() noexcept;

	HRESULT _LoadBufferResources(winrt::AdvancedColorKind acKind) noexcept;

	static void _WaitForDwmComposition() noexcept;

	ID3D12Device* _device = nullptr;
	ID3D12CommandQueue* _commandQueue = nullptr;

	winrt::com_ptr<IDXGISwapChain4> _swapChain;
	wil::unique_event_nothrow _frameLatencyWaitableObject;
	std::array<winrt::com_ptr<ID3D12Resource>, BUFFER_COUNT> _renderTargets;
	std::array<UINT64, BUFFER_COUNT> _bufferFenceValues{};
	UINT _bufferIndex = 0;

	winrt::com_ptr<ID3D12DescriptorHeap> _rtvHeap;
	UINT _rtvDescriptorSize = 0;

	winrt::com_ptr<ID3D12Fence1> _fence;
	UINT64 _curFenceValue = 0;
	wil::unique_event_nothrow _fenceEvent;
	
	bool _isRecreated = false;
};
