#include "pch.h"
#include "Presenter.h"

Presenter::~Presenter() {
	_WaitForGpu();
}

bool Presenter::Initialize(
	ID3D12Device5* device,
	ID3D12CommandQueue* commandQueue,
	IDXGIFactory7* dxgiFactory,
	HWND hwndAttach,
	uint32_t width,
	uint32_t height
) noexcept {
	_commandQueue = commandQueue;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {
		.Width = width,
		.Height = height,
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.SampleDesc = {
			.Count = 1
		},
		.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
		.BufferCount = (UINT)_renderTargets.size(),
#ifdef _DEBUG
		// 我们应确保两种渲染方式可以无缝切换，DXGI_SCALING_NONE 使错误更容易观察到
		.Scaling = DXGI_SCALING_NONE,
#else
		// 如果两种渲染方式无法无缝切换，DXGI_SCALING_STRETCH 使视觉变化尽可能小
		.Scaling = DXGI_SCALING_STRETCH,
#endif
		.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
		.AlphaMode = DXGI_ALPHA_MODE_IGNORE,
		.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
	};

	winrt::com_ptr<IDXGISwapChain1> dxgiSwapChain;
	if (FAILED(dxgiFactory->CreateSwapChainForHwnd(
		commandQueue,
		hwndAttach,
		&swapChainDesc,
		nullptr,
		nullptr,
		dxgiSwapChain.put()
	))) {
		return false;
	}

	_swapChain = dxgiSwapChain.try_as<IDXGISwapChain4>();
	if (!_swapChain) {
		return false;
	}

	_frameLatencyWaitableObject.reset(_swapChain->GetFrameLatencyWaitableObject());
	if (!_frameLatencyWaitableObject) {
		return false;
	}

	_frameIndex = _swapChain->GetCurrentBackBufferIndex();

	dxgiFactory->MakeWindowAssociation(hwndAttach, DXGI_MWA_NO_ALT_ENTER);

	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.NumDescriptors = (UINT)_renderTargets.size()
		};
		if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_rtvHeap)))) {
			return false;
		}
	}

	_rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	if (FAILED(device->CreateFence(_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)))) {
		return false;
	}

	if (FAILED(_fenceEvent.create())) {
		return false;
	}
	
	return _LoadSizeDependentResources(device);
}

bool Presenter::BeginFrame(
	winrt::com_ptr<ID3D12Resource>& frameTex,
	CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvHandle
) noexcept {
	if (!_isframeLatencyWaited) {
		_frameLatencyWaitableObject.wait(1000);
		_isframeLatencyWaited = true;
	}

	if (!_WaitForGpu()) {
		return false;
	}

	frameTex = _renderTargets[_frameIndex];
	rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		_rtvHeap->GetCPUDescriptorHandleForHeapStart(), _frameIndex, _rtvDescriptorSize);

	return true;
}

void Presenter::EndFrame(bool /*waitForRenderComplete*/) noexcept {
	_swapChain->Present(1, 0);
	_isframeLatencyWaited = false;

	_frameIndex = _swapChain->GetCurrentBackBufferIndex();
}

bool Presenter::Resize(ID3D12Device5* device, uint32_t width, uint32_t height) noexcept {
	if (!_isframeLatencyWaited) {
		_frameLatencyWaitableObject.wait(1000);
		_isframeLatencyWaited = true;
	}

	_WaitForGpu();

	_renderTargets.fill(nullptr);

	HRESULT hr = _swapChain->ResizeBuffers(0, width, height,
		DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
	if (FAILED(hr)) {
		return false;
	}

	_frameIndex = _swapChain->GetCurrentBackBufferIndex();

	return _LoadSizeDependentResources(device);
}

bool Presenter::_WaitForGpu() noexcept {
	UINT64 newFenceValue = _fenceValue + 1;
	HRESULT hr = _commandQueue->Signal(_fence.get(), newFenceValue);
	if (FAILED(hr)) {
		return false;
	}
	_fenceValue = newFenceValue;

	if (_fence->GetCompletedValue() >= _fenceValue) {
		return true;
	}

	hr = _fence->SetEventOnCompletion(_fenceValue, _fenceEvent.get());
	if (FAILED(hr)) {
		return false;
	}

	_fenceEvent.wait();
	return true;
}

bool Presenter::_LoadSizeDependentResources(ID3D12Device5* device) noexcept {
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(_rtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < (UINT)_renderTargets.size(); ++i) {
		if (FAILED(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_renderTargets[i])))) {
			return false;
		}
		device->CreateRenderTargetView(_renderTargets[i].get(), nullptr, rtvHandle);
		rtvHandle.Offset(1, _rtvDescriptorSize);
	}

	return true;
}
