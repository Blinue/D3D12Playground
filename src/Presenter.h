#pragma once
#include <d3d11on12.h>
#include <dcomp.h>

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
		uint32_t height
	) noexcept;

	bool BeginFrame(
		winrt::com_ptr<ID3D12Resource>& frameTex,
		D3D12_RESOURCE_STATES& texState,
		CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvHandle
	) noexcept;

	void EndFrame(bool waitForGpu = false) noexcept;

	bool Resize(uint32_t width, uint32_t height, bool onResizing) noexcept;

private:
	bool _WaitForGpu() noexcept;

	bool _LoadSwapChainSizeDependentResources() noexcept;

	bool _ResizeSwapChain(uint32_t width, uint32_t height) noexcept;

	bool _ResizeDCompVisual(uint32_t width, uint32_t height) noexcept;

	// 和 DwmFlush 效果相同但更准确
	static void _WaitForDwmComposition() noexcept;

	ID3D12Device* _device = nullptr;
	ID3D12CommandQueue* _commandQueue = nullptr;

	winrt::com_ptr<IDXGISwapChain4> _swapChain;
	wil::unique_event_nothrow _frameLatencyWaitableObject;
	std::array<winrt::com_ptr<ID3D12Resource>, 2> _renderTargets;
	UINT _frameIndex = 0;

	// 调整大小时使用
	winrt::com_ptr<ID3D11Device> _d3d11Device;
	winrt::com_ptr<ID3D11DeviceContext> _d3d11DC;
	winrt::com_ptr<ID3D11On12Device2> _d3d11On12Device;
	winrt::com_ptr<IDCompositionDesktopDevice> _dcompDevice;
	winrt::com_ptr<IDCompositionTarget> _dcompTarget;
	winrt::com_ptr<IDCompositionVisual2> _dcompVisual;
	winrt::com_ptr<IDCompositionVirtualSurface> _dcompSurface;
	winrt::com_ptr<ID3D12Resource> _dcompRenderTarget;
	winrt::com_ptr<ID3D11Texture2D> _wrappedDcompRenderTarget;
	winrt::com_ptr<ID3D11Texture2D> _d3d11Tex;
	POINT _drawOffset;

	winrt::com_ptr<ID3D12DescriptorHeap> _rtvHeap;
	UINT _rtvDescriptorSize = 0;

	winrt::com_ptr<ID3D12Fence1> _fence;
	wil::unique_event_nothrow _fenceEvent;
	UINT64 _fenceValue = 0;

	bool _isDCompPresenting = false;
	bool _isResized = false;
	bool _isframeLatencyWaited = false;
	bool _isSwitchingToSwapChain = false;
};
