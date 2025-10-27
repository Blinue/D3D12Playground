#include "pch.h"
#include "DirectXHelper.h"
#include "Renderer.h"
#include "shaders/SimplePS.h"
#include "shaders/SimpleVS.h"

Renderer::~Renderer() {
	// 等待 GPU
	_presenter.reset();
}

bool Renderer::Initialize(HWND hwndAttach, uint32_t width, uint32_t height, float dpiScale) noexcept {
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

	if (FAILED(_device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_commandAllocator)))) {
		return false;
	}

	if (FAILED(_device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&_commandList)))) {
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

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {
		.pRootSignature = _rootSignature.get(),
		.VS = { .pShaderBytecode = SimpleVS, .BytecodeLength = sizeof(SimpleVS) },
		.PS = { .pShaderBytecode = SimplePS, .BytecodeLength = sizeof(SimplePS) },
		.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
		.SampleMask = UINT_MAX,
		.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
		.InputLayout = {
			.pInputElementDescs = inputElementDescs,
			.NumElements = (UINT)std::size(inputElementDescs)
		},
		.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		.NumRenderTargets = 1,
		.RTVFormats = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
		.SampleDesc = { .Count = 1 }
	};
	if (FAILED(_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pipelineState)))) {
		return false;
	}

	{
		const UINT vertexBufferSize = sizeof(_Vertex) * 22;

		// Note: using upload heaps to transfer static data like vert buffers is not 
		// recommended. Every time the GPU needs it, the upload heap will be marshalled 
		// over. Please read up on Default Heap usage. An upload heap is used here for 
		// code simplicity and because there are very few verts to actually transfer.
		CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
		if (FAILED(_device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&_vertexBuffer)
		))) {
			return false;
		}

		_vertexBufferView.BufferLocation = _vertexBuffer->GetGPUVirtualAddress();
		_vertexBufferView.StrideInBytes = sizeof(_Vertex);
		_vertexBufferView.SizeInBytes = vertexBufferSize;
	}

	_presenter.emplace();
	if (!_presenter->Initialize(_device.get(), _commandQueue.get(), dxgiFactory.get(), hwndAttach, width, height)) {
		return false;
	}

	return _LoadSizeDependentResources(width, height, dpiScale);
}

bool Renderer::Render() noexcept {
	winrt::com_ptr<ID3D12Resource> frameTex;
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	_presenter->BeginFrame(frameTex, rtvHandle);

	HRESULT hr = _commandAllocator->Reset();
	if (FAILED(hr)) {
		return false;
	}
	hr = _commandList->Reset(_commandAllocator.get(), _pipelineState.get());
	if (FAILED(hr)) {
		return false;
	}

	_commandList->SetGraphicsRootSignature(_rootSignature.get());
	_commandList->RSSetViewports(1, &_viewport);
	_commandList->RSSetScissorRects(1, &_scissorRect);

	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			frameTex.get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		_commandList->ResourceBarrier(1, &barrier);
	}

	_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	{
		const float clearColor[] = { 0.8f, 0.8f, 0.6f, 1.0f };
		_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	}

	_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	_commandList->IASetVertexBuffers(0, 1, &_vertexBufferView);
	_commandList->DrawInstanced(22, 1, 0, 0);
	
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			frameTex.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
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

	_presenter->EndFrame();
	return true;
}

bool Renderer::Resize(uint32_t width, uint32_t height, float dpiScale) noexcept {
	if (width == (uint32_t)_scissorRect.right && height == (uint32_t)_scissorRect.bottom) {
		return true;
	}

	if (!_presenter->Resize(width, height)) {
		return false;
	}

	if (!_LoadSizeDependentResources(width, height, dpiScale)) {
		return false;
	}

	return Render();
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

bool Renderer::_LoadSizeDependentResources(uint32_t width, uint32_t height, float dpiScale) noexcept {
	{
		const float squareWidth = 200.0f * dpiScale / width * 2.0f;
		const float squareHeight = 200.0f * dpiScale / height * 2.0f;
		_Vertex triangleVertices[] = {
			// 左上
			{ { -1.0f, 1.0f }, { 0.0f, 0.0f } },
			{ { -1.0f + squareWidth, 1.0f }, { 1.0f, 0.0f } },
			{ { -1.0f, 1.0f - squareHeight }, { 0.0f, 1.0f } },
			{ { -1.0f + squareWidth, 1.0f - squareHeight }, { 1.0f, 1.0f } },
			// 退化
			{ { -1.0f + squareWidth, 1.0f - squareHeight }, { 0.0f, 0.0f } },
			{ { 1.0f - squareWidth, 1.0f }, { 0.0f, 0.0f } },
			// 右上
			{ { 1.0f - squareWidth, 1.0f }, { 1.0f, 0.0f } },
			{ { 1.0f, 1.0f }, { 0.0f, 0.0f } },
			{ { 1.0f - squareWidth, 1.0f - squareHeight }, { 1.0f, 1.0f } },
			{ { 1.0f, 1.0f - squareHeight }, { 0.0f, 1.0f } },
			// 退化
			{ { 1.0f, 1.0f - squareHeight }, { 0.0f, 0.0f } },
			{ { 1.0f - squareWidth, -1.0f + squareHeight }, { 0.0f, 0.0f } },
			// 右下
			{ { 1.0f - squareWidth, -1.0f + squareHeight }, { 1.0f, 1.0f } },
			{ { 1.0f, -1.0f + squareHeight }, { 0.0f, 1.0f } },
			{ { 1.0f - squareWidth, -1.0f }, { 1.0f, 0.0f } },
			{ { 1.0f, -1.0f }, { 0.0f, 0.0f } },
			// 退化
			{ { 1.0f, -1.0f }, { 0.0f, 0.0f } },
			{ { -1.0f, -1.0f + squareHeight }, { 0.0f, 0.0f } },
			// 左下
			{ { -1.0f, -1.0f + squareHeight }, { 0.0f, 1.0f } },
			{ { -1.0f + squareWidth, -1.0f + squareHeight }, { 1.0f, 1.0f } },
			{ { -1.0f, -1.0f }, { 0.0f, 0.0f } },
			{ { -1.0f + squareWidth, -1.0f }, { 1.0f, 0.0f } },
		};

		void* pVertexDataBegin = nullptr;
		CD3DX12_RANGE readRange(0, 0);
		if (FAILED(_vertexBuffer->Map(0, &readRange, &pVertexDataBegin))) {
			return false;
		}
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		_vertexBuffer->Unmap(0, nullptr);
	}

	_viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)width, (float)height);
	_scissorRect = CD3DX12_RECT(0, 0, (LONG)width, (LONG)height);

	return true;
}
