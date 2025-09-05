#pragma once

class Renderer {
public:
	Renderer() = default;
	Renderer(const Renderer&) = delete;
	Renderer(Renderer&&) = default;

	~Renderer();

	bool Initialize(HWND hwndAttach, UINT width, UINT height) noexcept;

	bool Render() noexcept;

private:
	bool _CreateD3DDevice(IDXGIFactory7* dxgiFactory) noexcept;

	bool _WaitForPreviousFrame() noexcept;

	winrt::com_ptr<ID3D12Device5> _device;
	winrt::com_ptr<ID3D12CommandQueue> _commandQueue;
	winrt::com_ptr<IDXGISwapChain4> _swapChain;
	wil::unique_event_nothrow _frameLatencyWaitableObject;
	winrt::com_ptr<ID3D12DescriptorHeap> _rtvHeap;
	std::array<winrt::com_ptr<ID3D12Resource>, 2> _renderTargets;
	winrt::com_ptr<ID3D12CommandAllocator> _commandAllocator;
	winrt::com_ptr<ID3D12GraphicsCommandList> _commandList;
	UINT _rtvDescriptorSize = 0;

	winrt::com_ptr<ID3D12Fence1> _fence;
	wil::unique_event_nothrow _fenceEvent;
	UINT64 _fenceValue = 0;
	UINT _frameIndex = 0;
};
