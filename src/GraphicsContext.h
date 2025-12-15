#pragma once

class GraphicsContext {
public:
	GraphicsContext() = default;
	GraphicsContext(const GraphicsContext&) = delete;
	GraphicsContext(GraphicsContext&&) = default;

	bool Initialize(uint32_t maxInFlightFrameCount) noexcept;

	IDXGIFactory7* GetDXGIFactory() const noexcept {
		return _dxgiFactory.get();
	}

	IDXGIFactory7* GetDXGIFactoryForEnumingAdapters() noexcept;

	ID3D12Device5* GetDevice() const noexcept {
		return _device.get();
	}

	ID3D12CommandQueue* GetCommandQueue() const noexcept {
		return _commandQueue.get();
	}

	ID3D12GraphicsCommandList* GetCommandList() const noexcept {
		return _commandList.get();
	}

	D3D_ROOT_SIGNATURE_VERSION GetRootSignatureVersion() const noexcept {
		return _rootSignatureVersion;
	}

	bool IsUMA() const noexcept {
		return _isUMA;
	}

	bool IsHeapFlagCreateNotZeroedSupported() const noexcept {
		return _isHeapFlagCreateNotZeroedSupported;
	}

	uint32_t GetMaxInFlightFrameCount() const noexcept {
		return (uint32_t)_commandAllocators.size();
	}

	HRESULT Signal(uint64_t& fenceValue) noexcept;

	HRESULT WaitForFenceValue(uint64_t fenceValue) noexcept;

	HRESULT WaitForGpu() noexcept;

	HRESULT BeginFrame(uint32_t& curFrameIndex, ID3D12PipelineState* initialState = nullptr) noexcept;

	HRESULT EndFrame() noexcept;

	bool CheckForBetterAdapter() noexcept;

private:
	HRESULT _CreateDXGIFactory() noexcept;

	bool _CreateD3DDevice() noexcept;

	winrt::com_ptr<IDXGIFactory7> _dxgiFactory;
	winrt::com_ptr<ID3D12Device5> _device;
	winrt::com_ptr<ID3D12CommandQueue> _commandQueue;

	std::vector<winrt::com_ptr<ID3D12CommandAllocator>> _commandAllocators;
	winrt::com_ptr<ID3D12GraphicsCommandList> _commandList;

	winrt::com_ptr<ID3D12Fence1> _fence;
	uint64_t _curFenceValue = 0;

	std::vector<uint64_t> _frameFenceValues;
	uint32_t _curFrameIndex = 0;

	D3D_ROOT_SIGNATURE_VERSION _rootSignatureVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

	bool _isWarp = false;
	bool _isUMA = false;
	bool _isHeapFlagCreateNotZeroedSupported = false;
};
