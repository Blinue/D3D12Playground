#include "pch.h"
#include "Presenter.h"
#include "Win32Helper.h"
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
	uint32_t height
) noexcept {
	_device = device;
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
			.NumDescriptors = (UINT)_renderTargets.size() + 1
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

	return _LoadSwapChainSizeDependentResources();
}

bool Presenter::BeginFrame(
	winrt::com_ptr<ID3D12Resource>& frameTex,
	D3D12_RESOURCE_STATES& texState,
	CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvHandle
) noexcept {
	if (_isDCompPresenting) {
		if (!_WaitForGpu()) {
			return false;
		}

		HRESULT hr = _dcompSurface->BeginDraw(nullptr, IID_PPV_ARGS(&_d3d11Tex), &_drawOffset);
		if (FAILED(hr)) {
			return false;
		}
		
		_d3d11DC->Flush();

		frameTex = _dcompRenderTarget;
		texState = D3D12_RESOURCE_STATE_COPY_SOURCE;
		rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
			_rtvHeap->GetCPUDescriptorHandleForHeapStart(), 2, _rtvDescriptorSize);
	} else {
		if (!_isframeLatencyWaited) {
			_frameLatencyWaitableObject.wait(1000);
			_isframeLatencyWaited = true;
		}

		if (!_WaitForGpu()) {
			return false;
		}

		frameTex = _renderTargets[_frameIndex];
		texState = D3D12_RESOURCE_STATE_PRESENT;
		rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
			_rtvHeap->GetCPUDescriptorHandleForHeapStart(), _frameIndex, _rtvDescriptorSize);
	}

	return true;
}

void Presenter::EndFrame(bool waitForGpu) noexcept {
	if (_isDCompPresenting) {
		ID3D11Resource* resources[1] = { _wrappedDcompRenderTarget.get() };
		_d3d11On12Device->AcquireWrappedResources(resources, 1);
		_d3d11DC->CopySubresourceRegion(_d3d11Tex.get(), 0, _drawOffset.x, _drawOffset.y, 0, _wrappedDcompRenderTarget.get(), 0, nullptr);
		_dcompSurface->EndDraw();
		_d3d11On12Device->ReleaseWrappedResources(resources, 1);
		_d3d11DC->Flush();
	}

	if (waitForGpu || _isResized) {
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
		_WaitForGpu();

		// 等待 DWM 开始合成新一帧
		_WaitForDwmComposition();

		_isResized = false;
	}

	if (_isDCompPresenting) {
		_dcompDevice->Commit();
	} else {
		_swapChain->Present(1, 0);
		_isframeLatencyWaited = false;

		_frameIndex = _swapChain->GetCurrentBackBufferIndex();

		if (_isSwitchingToSwapChain) {
			_isSwitchingToSwapChain = false;

			// 等待交换链呈现新帧
			_WaitForGpu();
			_WaitForDwmComposition();

			// 清除 DirectCompostion 内容
			_dcompVisual->SetContent(nullptr);
			_dcompDevice->Commit();
		}
	}
}

bool Presenter::Resize(uint32_t width, uint32_t height, bool onResizing) noexcept {
	_isResized = true;

	if (onResizing) {
		// 切换到 DirectComposition 呈现，失败则回落到交换链
		_isDCompPresenting = _ResizeDCompVisual(width, height);
		if (_isDCompPresenting) {
			D3D12_RESOURCE_DESC texDesc = {
				.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
				.Width = width,
				.Height = height,
				.DepthOrArraySize = 1,
				.MipLevels = 1,
				.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
				.SampleDesc = {
					.Count = 1
				},
				.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			};

			D3D12_HEAP_PROPERTIES heapProps = {
				.Type = D3D12_HEAP_TYPE_DEFAULT
			};

			D3D12_CLEAR_VALUE clearValue{
				.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
				.Color = { 0.9f,0.9f,0.8f,1.0f }
			};

			HRESULT hr = _device->CreateCommittedResource(
				&heapProps,
				D3D12_HEAP_FLAG_NONE,
				&texDesc,
				D3D12_RESOURCE_STATE_COPY_SOURCE,
				&clearValue,
				IID_PPV_ARGS(&_dcompRenderTarget)
			);
			if (FAILED(hr)) {
				return false;
			}

			_device->CreateRenderTargetView(_dcompRenderTarget.get(), nullptr,
				CD3DX12_CPU_DESCRIPTOR_HANDLE(_rtvHeap->GetCPUDescriptorHandleForHeapStart(), 2, _rtvDescriptorSize));

			D3D11_RESOURCE_FLAGS flags{};
			hr = _d3d11On12Device->CreateWrappedResource(_dcompRenderTarget.get(), &flags,
				D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE, IID_PPV_ARGS(&_wrappedDcompRenderTarget));
			if (FAILED(hr)) {
				return false;
			}

			return true;
		}
	}

	return _ResizeSwapChain(width, height);
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

bool Presenter::_LoadSwapChainSizeDependentResources() noexcept {
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(_rtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < (UINT)_renderTargets.size(); ++i) {
		if (FAILED(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_renderTargets[i])))) {
			return false;
		}
		_device->CreateRenderTargetView(_renderTargets[i].get(), nullptr, rtvHandle);
		rtvHandle.Offset(1, _rtvDescriptorSize);
	}

	return true;
}

bool Presenter::_ResizeSwapChain(uint32_t width, uint32_t height) noexcept {
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

	return _LoadSwapChainSizeDependentResources();
}

bool Presenter::_ResizeDCompVisual(uint32_t width, uint32_t height) noexcept {
	if (_dcompSurface) {
		// 使用 IDCompositionVirtualSurface 而不是 IDCompositionSurface 的原因是
		// IDCompositionDevice2::CreateSurface 有时相当慢，最坏情况下要几十毫秒。
		HRESULT hr = _dcompSurface->Resize(width, height);
		if (FAILED(hr)) {
			return false;
		}
	} else {
		UINT flags = 0;
#ifdef _DEBUG
		flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
		HRESULT hr = D3D11On12CreateDevice(_device, flags, nullptr, 0,
			(IUnknown**)&_commandQueue, 1, 0, _d3d11Device.put(), _d3d11DC.put(), nullptr);
		if (FAILED(hr)) {
			return false;
		}

		_d3d11Device.try_as(_d3d11On12Device);

		// 初始化 DirectComposition
		hr = DCompositionCreateDevice3(
			_d3d11Device.get(), IID_PPV_ARGS(&_dcompDevice));
		if (FAILED(hr)) {
			return false;
		}

		HWND hwndAttach;
		hr = _swapChain->GetHwnd(&hwndAttach);
		if (FAILED(hr)) {
			return false;
		}

		hr = _dcompDevice->CreateTargetForHwnd(hwndAttach, TRUE, _dcompTarget.put());
		if (FAILED(hr)) {
			return false;
		}

		hr = _dcompDevice->CreateVisual(_dcompVisual.put());
		if (FAILED(hr)) {
			return false;
		}

		hr = _dcompTarget->SetRoot(_dcompVisual.get());
		if (FAILED(hr)) {
			return false;
		}

		hr = _dcompDevice->CreateVirtualSurface(
			width,
			height,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_ALPHA_MODE_IGNORE,
			_dcompSurface.put()
		);
		if (FAILED(hr)) {
			return false;
		}
	}

	HRESULT hr = _dcompVisual->SetContent(_dcompSurface.get());
	if (FAILED(hr)) {
		return false;
	}

	return true;
}

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
