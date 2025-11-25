#pragma once
#include "SwapChain.h"

class Renderer {
public:
	Renderer() = default;
	Renderer(const Renderer&) = delete;
	Renderer(Renderer&&) = default;

	~Renderer();

	bool Initialize(HWND hwndMain, uint32_t width, uint32_t height, float dpiScale) noexcept;

	bool Render(bool onHandlingDeviceLost = false) noexcept;

	bool OnSizeChanged(uint32_t width, uint32_t height, float dpiScale) noexcept;

	void OnResizeStarted() noexcept;

	bool OnResizeEnded() noexcept;

	bool OnWindowPosChanged() noexcept;

	bool OnDisplayChanged() noexcept;

private:
	HRESULT _CreateDXGIFactory() noexcept;

	bool _CreateD3DDevice() noexcept;

	HRESULT _UpdateSizeDependentResources() noexcept;

	bool _InitializeDisplayInformation() noexcept;

	HRESULT _UpdateAdvancedColorInfo() noexcept;

	HRESULT _UpdateAdvancedColor(bool onInit = false) noexcept;

	HRESULT _CheckDeviceLost(HRESULT hr, bool onHandlingDeviceLost = false) noexcept;

	bool _HandleDeviceLost() noexcept;

	void _ReleaseD3DResources() noexcept;

	uint32_t _width = 0;
	uint32_t _height = 0;
	float _dpiScale = 1.0f;

	D3D_ROOT_SIGNATURE_VERSION _rootSignatureVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

	winrt::com_ptr<IDXGIFactory7> _dxgiFactory;
	winrt::com_ptr<ID3D12Device5> _device;
	winrt::com_ptr<ID3D12CommandQueue> _commandQueue;

	std::vector<winrt::com_ptr<ID3D12CommandAllocator>> _commandAllocators;
	winrt::com_ptr<ID3D12GraphicsCommandList> _commandList;
	
	winrt::com_ptr<ID3D12RootSignature> _rootSignature;
	winrt::com_ptr<ID3D12PipelineState> _pipelineState;
	winrt::com_ptr<ID3D12Resource> _vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW _vertexBufferView{};

	std::optional<SwapChain> _swapChain;

	HWND _hwndMain = NULL;
	winrt::DispatcherQueueController _dispatcherQueueController{ nullptr };
	winrt::DisplayInformation _displayInfo{ nullptr };
	winrt::DisplayInformation::AdvancedColorInfoChanged_revoker _acInfoChangedRevoker;
	HMONITOR _hCurMonitor = NULL;
	winrt::AdvancedColorKind _curAcKind = winrt::AdvancedColorKind::StandardDynamicRange;
	// HDR 模式下最大亮度缩放
	float _maxLuminance = 1.0f;
	// HDR 模式下 SDR 内容亮度缩放
	float _sdrWhiteLevel = 1.0f;

	bool _isUsingWarp = false;
};
