#pragma once
#include "Presenter.h"

class Renderer {
public:
	Renderer() = default;
	Renderer(const Renderer&) = delete;
	Renderer(Renderer&&) = default;

	~Renderer();

	bool Initialize(HWND hwndMain, uint32_t width, uint32_t height, float dpiScale) noexcept;

	bool Render() noexcept;

	bool OnSizeChanged(uint32_t width, uint32_t height, float dpiScale) noexcept;

	bool OnWindowPosChanged() noexcept;

	bool OnDisplayChanged() noexcept;

private:
	HRESULT _CreateDXGIFactory() noexcept;

	bool _CreateD3DDevice() noexcept;

	HRESULT _UpdateSizeDependentResources() noexcept;

	bool _InitializeDisplayInformation() noexcept;

	HRESULT _UpdateAdvancedColorInfo() noexcept;

	HRESULT _UpdateAdvancedColor(bool onInit = false) noexcept;

	HRESULT _CheckResult(HRESULT hr) noexcept;

	bool _HandleDeviceLost() noexcept;

	winrt::com_ptr<IDXGIFactory7> _dxgiFactory;
	winrt::com_ptr<ID3D12Device5> _device;
	winrt::com_ptr<ID3D12CommandQueue> _commandQueue;
	
	winrt::com_ptr<ID3D12CommandAllocator> _commandAllocator;
	winrt::com_ptr<ID3D12GraphicsCommandList> _commandList;
	
	winrt::com_ptr<ID3D12RootSignature> _rootSignature;
	winrt::com_ptr<ID3D12PipelineState> _pipelineState;
	winrt::com_ptr<ID3D12Resource> _vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW _vertexBufferView{};

	CD3DX12_VIEWPORT _viewport;
	CD3DX12_RECT _scissorRect;

	std::optional<Presenter> _presenter;

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

	float _dpiScale = 1.0f;

	bool _isUsingWarp = false;
};
