#include "pch.h"
#include "Presenter.h"
#include "Win32Helper.h"
#include <dcomp.h>
#include <dwmapi.h>

Presenter::~Presenter() {
	_WaitForGpu();
}

bool Presenter::Initialize(
	ID3D12Device5* device,
	ID3D12CommandQueue* commandQueue,
	IDXGIFactory7* dxgiFactory,
	HWND hwndAttach,
	uint32_t width,
	uint32_t height,
	winrt::AdvancedColorKind acKind
) noexcept {
	_device = device;
	_commandQueue = commandQueue;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {
		.Width = width,
		.Height = height,
		.Format = acKind == winrt::AdvancedColorKind::StandardDynamicRange ?
		DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT,
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
	
	return SUCCEEDED(_LoadBufferResources(acKind));
}

HRESULT Presenter::BeginFrame(
	winrt::com_ptr<ID3D12Resource>& frameTex,
	CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvHandle
) noexcept {
	if (!_isframeLatencyWaited) {
		_frameLatencyWaitableObject.wait(1000);
		_isframeLatencyWaited = true;
	}

	HRESULT hr = _WaitForGpu();
	if (FAILED(hr)) {
		return hr;
	}

	frameTex = _renderTargets[_frameIndex];
	rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		_rtvHeap->GetCPUDescriptorHandleForHeapStart(), _frameIndex, _rtvDescriptorSize);

	return S_OK;
}

HRESULT Presenter::EndFrame(bool waitForGpu) noexcept {
	if (waitForGpu || _isRecreated) {
		// 下面两个调用用于减少调整窗口尺寸时的边缘闪烁。
		// 
		// 我们希望 DWM 绘制新的窗口框架时刚好合成新帧，但这不是我们能控制的，尤其是混合架构
		// 下需要在显卡间传输帧数据，无法预测 Present/Commit 后多久 DWM 能收到。我们只能尽
		// 可能为 DWM 合成新帧预留时间，这包括两个步骤：
		// 
		// 1. 首先等待渲染完成，确保新帧对 DWM 随时可用。
		// 2. 然后在新一轮合成开始时提交，这让 DWM 有更多时间合成新帧。
		// 
		// 目前看来除非像 UWP 一般有 DWM 协助，否则彻底摆脱闪烁是不可能的。
		// 
		// https://github.com/Blinue/Magpie/pull/1071#issuecomment-2718314731 讨论了 UWP
		// 调整尺寸的方法，测试表明可以彻底解决闪烁问题。不过它使用了很不稳定的私有接口，没有
		// 实用价值。

		// 等待渲染完成
		HRESULT hr = _WaitForGpu();
		if (FAILED(hr)) {
			return hr;
		}

		// 等待 DWM 开始合成新一帧
		_WaitForDwmComposition();

		_isRecreated = false;
	}

	HRESULT hr = _swapChain->Present(1, 0);
	if (FAILED(hr)) {
		return hr;
	}

	_frameIndex = _swapChain->GetCurrentBackBufferIndex();
	return S_OK;
}

HRESULT Presenter::RecreateBuffers(uint32_t width, uint32_t height, winrt::AdvancedColorKind acKind) noexcept {
	_isRecreated = true;

	if (!_isframeLatencyWaited) {
		_frameLatencyWaitableObject.wait(1000);
		_isframeLatencyWaited = true;
	}

	HRESULT hr = _WaitForGpu();
	if (FAILED(hr)) {
		return hr;
	}

	_renderTargets.fill(nullptr);

	DXGI_FORMAT format = acKind == winrt::AdvancedColorKind::StandardDynamicRange ?
		DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
	hr = _swapChain->ResizeBuffers(0, width, height,
		format, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
	if (FAILED(hr)) {
		return hr;
	}

	hr = _swapChain->SetColorSpace1(acKind == winrt::AdvancedColorKind::StandardDynamicRange ?
		DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
	if (FAILED(hr)) {
		return hr;
	}

	_frameIndex = _swapChain->GetCurrentBackBufferIndex();

	return _LoadBufferResources(acKind);
}

HRESULT Presenter::_WaitForGpu() noexcept {
	if (!_fence) {
		return S_OK;
	}

	UINT64 newFenceValue = _fenceValue + 1;
	HRESULT hr = _commandQueue->Signal(_fence.get(), newFenceValue);
	if (FAILED(hr)) {
		return hr;
	}
	_fenceValue = newFenceValue;

	if (_fence->GetCompletedValue() >= _fenceValue) {
		return S_OK;
	}

	hr = _fence->SetEventOnCompletion(_fenceValue, _fenceEvent.get());
	if (FAILED(hr)) {
		return hr;
	}

	_fenceEvent.wait();
	return S_OK;
}

HRESULT Presenter::_LoadBufferResources(winrt::AdvancedColorKind acKind) noexcept {
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(_rtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < (UINT)_renderTargets.size(); ++i) {
		HRESULT hr = _swapChain->GetBuffer(i, IID_PPV_ARGS(&_renderTargets[i]));
		if (FAILED(hr)) {
			return hr;
		}

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {
			.Format = acKind == winrt::AdvancedColorKind::StandardDynamicRange ?
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R16G16B16A16_FLOAT,
			.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D
		};
		_device->CreateRenderTargetView(_renderTargets[i].get(), &rtvDesc, rtvHandle);
		rtvHandle.Offset(1, _rtvDescriptorSize);
	}

	return S_OK;
}

// 和 DwmFlush 效果相同但更准确
void Presenter::_WaitForDwmComposition() noexcept {
	// Win11 可以使用准确的 DCompositionWaitForCompositorClock
	if (Win32Helper::GetOSVersion().IsWin11()) {
		static const auto dCompositionWaitForCompositorClock =
			Win32Helper::LoadSystemFunction<decltype(DCompositionWaitForCompositorClock)>(
				L"dcomp.dll", "DCompositionWaitForCompositorClock");
		if (dCompositionWaitForCompositorClock) {
			dCompositionWaitForCompositorClock(0, nullptr, INFINITE);
			return;
		}
	}

	LARGE_INTEGER qpf;
	QueryPerformanceFrequency(&qpf);
	qpf.QuadPart /= 10000000;

	DWM_TIMING_INFO info{};
	info.cbSize = sizeof(info);
	DwmGetCompositionTimingInfo(NULL, &info);

	LARGE_INTEGER time;
	QueryPerformanceCounter(&time);

	if (time.QuadPart >= (LONGLONG)info.qpcCompose) {
		return;
	}

	// 提前 1ms 结束然后忙等待
	time.QuadPart += 10000;
	if (time.QuadPart < (LONGLONG)info.qpcCompose) {
		LARGE_INTEGER liDueTime{
			.QuadPart = -((LONGLONG)info.qpcCompose - time.QuadPart) / qpf.QuadPart
		};
		static HANDLE timer = CreateWaitableTimerEx(nullptr, nullptr,
			CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		SetWaitableTimerEx(timer, &liDueTime, 0, NULL, NULL, 0, 0);
		WaitForSingleObject(timer, INFINITE);
	} else {
		Sleep(0);
	}

	while (true) {
		QueryPerformanceCounter(&time);

		if (time.QuadPart >= (LONGLONG)info.qpcCompose) {
			return;
		}

		Sleep(0);
	}
}
