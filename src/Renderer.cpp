#include "pch.h"
#include "DirectXHelper.h"
#include "Renderer.h"
#include "shaders/SimplePS.h"
#include "shaders/SimpleVS.h"

Renderer::~Renderer() {
	_WaitForPreviousFrame();
}

bool Renderer::Initialize(HWND hwndAttach, UINT width, UINT height) noexcept {
	UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
	{
		winrt::com_ptr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
		}
	}

	dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	winrt::com_ptr<IDXGIFactory7> dxgiFactory;
	if (FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)))) {
		return false;
	}

	if (!_CreateD3DDevice(dxgiFactory.get())) {
		return false;
	}

	{
		D3D12_COMMAND_QUEUE_DESC queueDesc{
			.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
			.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
		};
		if (FAILED(_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_commandQueue)))) {
			return false;
		}
	}

	{
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
			_commandQueue.get(),
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
	}

	_frameLatencyWaitableObject.reset(_swapChain->GetFrameLatencyWaitableObject());
	if (!_frameLatencyWaitableObject) {
		return false;
	}

	dxgiFactory->MakeWindowAssociation(hwndAttach, DXGI_MWA_NO_ALT_ENTER);

	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.NumDescriptors = (UINT)_renderTargets.size()
		};
		if (FAILED(_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_rtvHeap)))) {
			return false;
		}
	}

	_rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(_rtvHeap->GetCPUDescriptorHandleForHeapStart());
		for (UINT i = 0; i < (UINT)_renderTargets.size(); ++i) {
			if (FAILED(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_renderTargets[i])))) {
				return false;
			}
			_device->CreateRenderTargetView(_renderTargets[i].get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, _rtvDescriptorSize);
		}
	}

	if (FAILED(_device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_commandAllocator)))) {
		return false;
	}

	if (FAILED(_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		_commandAllocator.get(), nullptr, IID_PPV_ARGS(&_commandList)))) {
		return false;
	}

	if (FAILED(_commandList->Close())) {
		return false;
	}

	_frameIndex = _swapChain->GetCurrentBackBufferIndex();

	if (FAILED(_device->CreateFence(_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)))) {
		return false;
	}

	if (FAILED(_fenceEvent.create())) {
		return false;
	}

	{
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(0, nullptr, 0, nullptr,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		winrt::com_ptr<ID3DBlob> signature;
		winrt::com_ptr<ID3DBlob> error;
		if (FAILED(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, signature.put(), error.put()))) {
			return false;
		}
		if (FAILED(_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&_rootSignature)))) {
			return false;
		}
	}

	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {
		.pRootSignature = _rootSignature.get(),
		.VS = CD3DX12_SHADER_BYTECODE(SimpleVS, sizeof(SimpleVS)),
		.PS = CD3DX12_SHADER_BYTECODE(SimplePS, sizeof(SimplePS)),
		.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
		.SampleMask = UINT_MAX,
		.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
		.InputLayout = { inputElementDescs, _countof(inputElementDescs) },
		.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		.NumRenderTargets = 1,
		.RTVFormats = { DXGI_FORMAT_R8G8B8A8_UNORM },
		.SampleDesc = { .Count = 1 }
	};
	if (FAILED(_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pipelineState)))) {
		return false;
	}

	return true;
}

bool Renderer::Render() noexcept {
	_frameLatencyWaitableObject.wait(1000);
	_WaitForPreviousFrame();

	HRESULT hr = _commandAllocator->Reset();
	if (FAILED(hr)) {
		return false;
	}
	hr = _commandList->Reset(_commandAllocator.get(), nullptr);
	if (FAILED(hr)) {
		return false;
	}

	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(_renderTargets[_frameIndex].get(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		_commandList->ResourceBarrier(1, &barrier);
	}

	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
			_rtvHeap->GetCPUDescriptorHandleForHeapStart(), _frameIndex, _rtvDescriptorSize);

		const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
		_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	}
	
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(_renderTargets[_frameIndex].get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		_commandList->ResourceBarrier(1, &barrier);
	}

	hr = _commandList->Close();
	if (FAILED(hr)) {
		return false;
	}

	{
		ID3D12CommandList* t = _commandList.get();
		_commandQueue->ExecuteCommandLists(1, &t);
	}

	hr = _swapChain->Present(1, 0);
	if (FAILED(hr)) {
		return false;
	}

	_frameIndex = _swapChain->GetCurrentBackBufferIndex();

	UINT64 newFenceValue = _fenceValue + 1;
	hr = _commandQueue->Signal(_fence.get(), newFenceValue);
	if (FAILED(hr)) {
		return false;
	}
	_fenceValue = newFenceValue;

	return true;
}

bool Renderer::_CreateD3DDevice(IDXGIFactory7* dxgiFactory) noexcept {
	// 枚举查找第一个支持 FL11 的显卡
	winrt::com_ptr<IDXGIAdapter1> adapter;
	for (UINT adapterIdx = 0;
		SUCCEEDED(dxgiFactory->EnumAdapters1(adapterIdx, adapter.put()));
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
			return true;
		}
	}

	// 作为最后手段，回落到 CPU 渲染 (WARP)
	// https://docs.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp
	if (FAILED(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) {
		return false;
	}

	return SUCCEEDED(D3D12CreateDevice(
		adapter.get(),
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&_device)
	));
}

bool Renderer::_WaitForPreviousFrame() noexcept {
	if (_fence->GetCompletedValue() >= _fenceValue) {
		return true;
	}

	HRESULT hr = _fence->SetEventOnCompletion(_fenceValue, _fenceEvent.get());
	if (FAILED(hr)) {
		return false;
	}

	_fenceEvent.wait();
	return true;
}
