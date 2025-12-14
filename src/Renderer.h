#pragma once
#include "ColorInfo.h"
#include "GraphicsContext.h"
#include "SwapChain.h"

enum class RendererState {
	NoError,
	DeviceLost,
	Error
};

class Renderer {
public:
	Renderer() = default;
	Renderer(const Renderer&) = delete;
	Renderer(Renderer&&) = default;

	~Renderer();

	bool Initialize(HWND hwndMain, uint32_t width, uint32_t height, float dpiScale) noexcept;

	RendererState Render() noexcept;

	void OnResizeStarted() noexcept;

	void OnResizeEnded() noexcept;

	void OnResized(uint32_t width, uint32_t height, float dpiScale) noexcept;

	void OnMsgWindowPosChanged() noexcept;

	void OnMsgDisplayChanged() noexcept;

private:
	void _UpdateSizeDependentResources(ID3D12GraphicsCommandList* commandList) noexcept;

	bool _TryInitDisplayInfo() noexcept;

	bool _UpdateColorInfo() noexcept;

	HRESULT _UpdateColorSpace() noexcept;

	void _UpdateWindowTitle() const noexcept;

	HRESULT _InitializePSO() noexcept;

	bool _CheckResult(bool success) noexcept;

	bool _CheckResult(HRESULT hr) noexcept;

	RendererState _state = RendererState::NoError;

	uint32_t _width = 0;
	uint32_t _height = 0;
	float _dpiScale = 1.0f;

	GraphicsContext _graphicsContext;
	SwapChain _swapChain;

	winrt::com_ptr<ID3D12RootSignature> _rootSignature;
	winrt::com_ptr<ID3D12PipelineState> _pipelineState;
	winrt::com_ptr<ID3D12Resource> _vertexUploadBuffer;
	void* _vertexUploadBufferData = nullptr;
	winrt::com_ptr<ID3D12Resource> _vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW _vertexBufferView{};

	HWND _hwndMain = NULL;
	winrt::DisplayInformation _displayInfo{ nullptr };
	winrt::DisplayInformation::AdvancedColorInfoChanged_revoker _acInfoChangedRevoker;
	HMONITOR _hCurMonitor = NULL;
	ColorInfo _colorInfo;

	bool _shouldUpdateSizeDependentResources = true;
	bool _isVertexBufferInitialized = false;
};
