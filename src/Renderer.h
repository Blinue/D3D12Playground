#pragma once
#include "Presenter.h"

class Renderer {
public:
	Renderer() = default;
	Renderer(const Renderer&) = delete;
	Renderer(Renderer&&) = default;

	~Renderer();

	bool Initialize(HWND hwndAttach, uint32_t width, uint32_t height, float dpiScale) noexcept;

	bool Render() noexcept;

	bool Resize(uint32_t width, uint32_t height, float dpiScale) noexcept;

private:
	bool _CreateD3DDevice(IDXGIFactory7* dxgiFactory) noexcept;

	bool _LoadSizeDependentResources(uint32_t width, uint32_t height, float dpiScale) noexcept;

	struct _Vertex {
		DirectX::XMFLOAT2 position;
		DirectX::XMFLOAT2 coord;
	};

	winrt::com_ptr<ID3D12Device5> _device;
	winrt::com_ptr<ID3D12CommandQueue> _commandQueue;
	
	winrt::com_ptr<ID3D12CommandAllocator> _commandAllocator;
	winrt::com_ptr<ID3D12GraphicsCommandList> _commandList;
	
	winrt::com_ptr<ID3D12RootSignature> _rootSignature;
	winrt::com_ptr<ID3D12PipelineState> _pipelineState;
	winrt::com_ptr<ID3D12Resource> _vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW _vertexBufferView;

	CD3DX12_VIEWPORT _viewport;
	CD3DX12_RECT _scissorRect;

	std::optional<Presenter> _presenter;
};
