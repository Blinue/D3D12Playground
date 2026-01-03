#include "pch.h"
#include "GraphicsContext.h"
#include "DirectXHelper.h"

bool GraphicsContext::Initialize(uint32_t maxInFlightFrameCount) noexcept {
	if (FAILED(_CreateDXGIFactory())) {
		return false;
	}

	if (!_CreateD3DDevice()) {
		return false;
	}

	// 检查根签名版本
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE data = { .HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1 };
		if (SUCCEEDED(_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &data, sizeof(data)))) {
			_rootSignatureVersion = data.HighestVersion;
		}
	}

	// 检查是否是集成显卡
	{
		D3D12_FEATURE_DATA_ARCHITECTURE1 data{};
		if (SUCCEEDED(_device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &data, sizeof(data)))) {
			_isUMA = data.UMA;
		}
	}

	// 检查 D3D12_HEAP_FLAG_CREATE_NOT_ZEROED 支持
	// https://devblogs.microsoft.com/directx/coming-to-directx-12-more-control-over-memory-allocation/
	_isHeapFlagCreateNotZeroedSupported = (bool)_device.try_as<ID3D12Device8>();
	
	// 检查 Resizable BAR 支持
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS16 data{};
		if (SUCCEEDED(_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &data, sizeof(data)))) {
			_isGPUUploadHeapSupported = data.GPUUploadHeapSupported;
		}
	}

	{
		D3D12_COMMAND_QUEUE_DESC queueDesc = {
			.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
			.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
		};
		if (FAILED(_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_commandQueue)))) {
			return false;
		}
	}

	if (FAILED(_device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&_commandList)))) {
		return false;
	}

	_commandAllocators.resize(maxInFlightFrameCount);
	for (winrt::com_ptr<ID3D12CommandAllocator>& commandAllocator : _commandAllocators) {
		if (FAILED(_device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)))) {
			return false;
		}
	}

	if (FAILED(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)))) {
		return false;
	}

	_frameFenceValues.resize(maxInFlightFrameCount);

	return true;
}

IDXGIFactory7* GraphicsContext::GetDXGIFactoryForEnumingAdapters() noexcept {
	if (!_dxgiFactory->IsCurrent()) {
		HRESULT hr = _CreateDXGIFactory();
		if (FAILED(hr)) {
			return nullptr;
		}
	}

	return _dxgiFactory.get();
}

HRESULT GraphicsContext::Signal(uint64_t& fenceValue) noexcept {
	fenceValue = ++_curFenceValue;
	return _commandQueue->Signal(_fence.get(), _curFenceValue);
}

HRESULT GraphicsContext::WaitForFenceValue(uint64_t fenceValue) noexcept {
	if (_fence->GetCompletedValue() >= fenceValue) {
		return S_OK;
	} else {
		return _fence->SetEventOnCompletion(fenceValue, nullptr);
	}
}

HRESULT GraphicsContext::WaitForGpu() noexcept {
	HRESULT hr = _commandQueue->Signal(_fence.get(), ++_curFenceValue);
	if (FAILED(hr)) {
		return hr;
	}

	return WaitForFenceValue(_curFenceValue);
}

HRESULT GraphicsContext::BeginFrame(uint32_t& curFrameIndex, ID3D12PipelineState* initialState) noexcept {
	HRESULT hr = WaitForFenceValue(_frameFenceValues[_curFrameIndex]);
	if (FAILED(hr)) {
		return hr;
	}

	hr = _commandAllocators[_curFrameIndex]->Reset();
	if (FAILED(hr)) {
		return hr;
	}

	hr = _commandList->Reset(_commandAllocators[_curFrameIndex].get(), initialState);
	if (FAILED(hr)) {
		return hr;
	}

	curFrameIndex = _curFrameIndex;
	return S_OK;
}

HRESULT GraphicsContext::EndFrame() noexcept {
	HRESULT hr = Signal(_frameFenceValues[_curFrameIndex]);
	if (FAILED(hr)) {
		return hr;
	}

	_curFrameIndex = (_curFrameIndex + 1) % (uint32_t)_commandAllocators.size();
	return S_OK;
}

bool GraphicsContext::CheckForBetterAdapter() noexcept {
	if (!_isWarp || _dxgiFactory->IsCurrent()) {
		return false;
	}

	HRESULT hr = _CreateDXGIFactory();
	if (FAILED(hr)) {
		return false;
	}

	// 查找是否有支持 D3D12 的显卡
	winrt::com_ptr<IDXGIAdapter1> adapter;
	for (UINT adapterIdx = 0;
		SUCCEEDED(_dxgiFactory->EnumAdapters1(adapterIdx, adapter.put()));
		++adapterIdx
	) {
		DXGI_ADAPTER_DESC1 desc;
		hr = adapter->GetDesc1(&desc);
		if (FAILED(hr) || DirectXHelper::IsWARP(desc)) {
			continue;
		}

		if (SUCCEEDED(D3D12CreateDevice(
			adapter.get(),
			D3D_FEATURE_LEVEL_11_0,
			winrt::guid_of<ID3D12Device>(),
			nullptr
		))) {
			// 改为使用显卡渲染
			adapter = nullptr;
			return true;
		}
	}

	return false;
}

HRESULT GraphicsContext::_CreateDXGIFactory() noexcept {
	UINT dxgiFactoryFlags = 0;
#ifdef _DEBUG
	dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
	return CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&_dxgiFactory));
}

bool GraphicsContext::_CreateD3DDevice() noexcept {
	// 枚举查找第一个支持 D3D12 的显卡
	winrt::com_ptr<IDXGIAdapter1> adapter;
	for (UINT adapterIdx = 0;
		SUCCEEDED(_dxgiFactory->EnumAdapters1(adapterIdx, adapter.put()));
		++adapterIdx
		) {
		DXGI_ADAPTER_DESC1 desc;
		HRESULT hr = adapter->GetDesc1(&desc);
		if (FAILED(hr) || DirectXHelper::IsWARP(desc)) {
			continue;
		}

		if (SUCCEEDED(D3D12CreateDevice(
			adapter.get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&_device)
		))) {
			_isWarp = false;
			return true;
		}
	}

	// 作为最后手段，回落到 CPU 渲染 (WARP)
	// https://docs.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp
	if (FAILED(_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) {
		return false;
	}

	if (SUCCEEDED(D3D12CreateDevice(
		adapter.get(),
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&_device)
	))) {
		_isWarp = true;
		return true;
	}

	return false;
}
