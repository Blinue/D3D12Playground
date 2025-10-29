#include "pch.h"
#include "Renderer.h"
#include "DirectXHelper.h"
#include "shaders/sRGB_PS.h"
#include "shaders/AdvancedColor_PS.h"
#include "shaders/SimpleVS.h"
#include "Win32Helper.h"
#include <dispatcherqueue.h>
#include <windows.graphics.display.interop.h>

Renderer::~Renderer() {
	// 等待 GPU
	_presenter.reset();
}

bool Renderer::Initialize(HWND hwndMain, uint32_t width, uint32_t height, float dpiScale) noexcept {
	_hwndMain = hwndMain;
	_hCurMonitor = MonitorFromWindow(hwndMain, MONITOR_DEFAULTTONEAREST);
	
#ifdef _DEBUG
	{
		winrt::com_ptr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
		}
	}
#endif

	if (!_CreateDXGIFactory()) {
		return false;
	}

	if (!_CreateD3DDevice()) {
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

	if (Win32Helper::GetOSVersion().Is22H2OrNewer()) {
		// 从 Win11 22H2 开始 DisplayInformation 支持桌面窗口。失败则回落到传统方法
		_InitializeDisplayInformation();
	}

	if (!_UpdateAdvancedColor(true)) {
		return false;
	}

	_presenter.emplace();
	if (!_presenter->Initialize(_device.get(), _commandQueue.get(), _dxgiFactory.get(),
		hwndMain, width, height, _curAcKind)) {
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

	if (_curAcKind != winrt::AdvancedColorKind::StandardDynamicRange) {
		// HDR 下提高彩色正方形亮度
		float boost = _curAcKind == winrt::AdvancedColorKind::WideColorGamut ?
			1.0f : std::min(_sdrWhiteLevel + 1, _maxLuminance);
		_commandList->SetGraphicsRoot32BitConstants(0, 1, &boost, 0);
	}

	_commandList->RSSetViewports(1, &_viewport);
	_commandList->RSSetScissorRects(1, &_scissorRect);

	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			frameTex.get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		_commandList->ResourceBarrier(1, &barrier);
	}

	_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	{
		const float clearColor[] = {
			0.8f * _sdrWhiteLevel,
			0.8f * _sdrWhiteLevel,
			0.6f * _sdrWhiteLevel,
			1.0f
		};
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

bool Renderer::OnSizeChanged(uint32_t width, uint32_t height, float dpiScale) noexcept {
	if (width == (uint32_t)_scissorRect.right && height == (uint32_t)_scissorRect.bottom) {
		return true;
	}

	if (!_presenter->RecreateBuffers(width, height, _curAcKind)) {
		return false;
	}

	if (!_LoadSizeDependentResources(width, height, dpiScale)) {
		return false;
	}

	return Render();
}

void Renderer::OnWindowPosChanged() noexcept {
	if (_displayInfo) {
		return;
	}

	HMONITOR hCurMonitor = MonitorFromWindow(_hwndMain, MONITOR_DEFAULTTONEAREST);
	if (_hCurMonitor != hCurMonitor) {
		_hCurMonitor = hCurMonitor;
		_UpdateAdvancedColor();
	}
}

void Renderer::OnDisplayChanged() noexcept {
	if (!_displayInfo) {
		_UpdateAdvancedColor();
	}
}

bool Renderer::_CreateDXGIFactory() noexcept {
	UINT dxgiFactoryFlags = 0;
#ifdef _DEBUG
	dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	return SUCCEEDED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&_dxgiFactory)));
}

bool Renderer::_CreateD3DDevice() noexcept {
	// 枚举查找第一个支持 FL11 的显卡
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
			return true;
		}
	}

	// 作为最后手段，回落到 CPU 渲染 (WARP)
	// https://docs.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp
	if (FAILED(_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) {
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

bool Renderer::_InitializeDisplayInformation() noexcept {
	// DisplayInformation 需要 DispatcherQueue
	winrt::DispatcherQueueController dqc{ nullptr };
	HRESULT hr = CreateDispatcherQueueController(
		DispatcherQueueOptions{
			.dwSize = sizeof(DispatcherQueueOptions),
			.threadType = DQTYPE_THREAD_CURRENT
		},
		(PDISPATCHERQUEUECONTROLLER*)winrt::put_abi(dqc)
	);
	if (FAILED(hr)) {
		return false;
	}

	winrt::com_ptr<IDisplayInformationStaticsInterop> interop =
		winrt::try_get_activation_factory<winrt::DisplayInformation, IDisplayInformationStaticsInterop>();
	if (!interop) {
		return false;
	}

	if (FAILED(interop->GetForWindow(_hwndMain, winrt::guid_of<winrt::DisplayInformation>(), winrt::put_abi(_displayInfo)))) {
		return false;
	}

	_acInfoChangedRevoker = _displayInfo.AdvancedColorInfoChanged(
		winrt::auto_revoke,
		[this](winrt::DisplayInformation const&, winrt::IInspectable const&) {
			_UpdateAdvancedColor();
		}
	);
	return true;
}

static float GetSDRWhiteLevel(std::wstring_view monitorName) noexcept {
	UINT32 pathCount = 0, modeCount = 0;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
		return 1.0f;
	}

	std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) {
		return 1.0f;
	}
	
	for (const DISPLAYCONFIG_PATH_INFO& path : paths) {
		DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {
			.header = {
				.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
				.size = sizeof(sourceName),
				.adapterId = path.sourceInfo.adapterId,
				.id = path.sourceInfo.id
			}
		};
		if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS) {
			continue;
		}

		if (monitorName == sourceName.viewGdiDeviceName) {
			DISPLAYCONFIG_SDR_WHITE_LEVEL sdr = {
				.header = {
					.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,
					.size = sizeof(sdr),
					.adapterId = path.targetInfo.adapterId,
					.id = path.targetInfo.id
				}
			};
			if (DisplayConfigGetDeviceInfo(&sdr.header) == ERROR_SUCCESS) {
				return sdr.SDRWhiteLevel / 1000.0f;
			} else {
				return 1.0f;
			}
		}
	}

	return 1.0f;
}

void Renderer::_UpdateAdvancedColorInfo() noexcept {
	if (_displayInfo) {
		winrt::AdvancedColorInfo acInfo = _displayInfo.GetAdvancedColorInfo();

		_curAcKind = acInfo.CurrentAdvancedColorKind();
		if (_curAcKind == winrt::AdvancedColorKind::HighDynamicRange) {
			_maxLuminance = acInfo.MaxLuminanceInNits() / 80.0f;
			_sdrWhiteLevel = acInfo.SdrWhiteLevelInNits() / 80.0f;
		} else {
			_maxLuminance = 1.0f;
			_sdrWhiteLevel = 1.0f;
		}
		
		return;
	}

	// 未找到视为 SDR
	_curAcKind = winrt::AdvancedColorKind::StandardDynamicRange;
	_maxLuminance = 1.0f;
	_sdrWhiteLevel = 1.0f;

	if (!_dxgiFactory->IsCurrent()) {
		_CreateDXGIFactory();
	}
	
	winrt::com_ptr<IDXGIAdapter1> adapter;
	winrt::com_ptr<IDXGIOutput> output;
	for (UINT adapterIdx = 0;
		SUCCEEDED(_dxgiFactory->EnumAdapters1(adapterIdx, adapter.put()));
		++adapterIdx
	) {
		for (UINT outputIdx = 0;
			SUCCEEDED(adapter->EnumOutputs(outputIdx, output.put()));
			++outputIdx
		) {
			DXGI_OUTPUT_DESC1 desc;
			if (SUCCEEDED(output.try_as<IDXGIOutput6>()->GetDesc1(&desc))) {
				if (desc.Monitor == _hCurMonitor) {
					// DXGI 将 WCG 视为 SDR
					if (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
						_curAcKind = winrt::AdvancedColorKind::HighDynamicRange;
						_maxLuminance = desc.MaxLuminance / 80.0f;
						_sdrWhiteLevel = GetSDRWhiteLevel(desc.DeviceName);
					}
					
					return;
				}
			}
		}
	}
}

bool Renderer::_UpdateAdvancedColor(bool onInit) noexcept {
	winrt::AdvancedColorKind oldAcKind = _curAcKind;
	_UpdateAdvancedColorInfo();

	if (!onInit && oldAcKind == _curAcKind) {
		return true;
	}

	// 更新窗口标题
	const wchar_t* title;
	if (_curAcKind == winrt::AdvancedColorKind::StandardDynamicRange) {
		title = L"D3D12Playground | SDR";
	} else if (_curAcKind == winrt::AdvancedColorKind::WideColorGamut) {
		title = L"D3D12Playground | WCG";
	} else {
		title = L"D3D12Playground | HDR";
	}
	SetWindowText(_hwndMain, title);

	// SDR<->其他的转换需要改变交换链格式和着色器
	const bool shouldUpdateResources =
		(oldAcKind == winrt::AdvancedColorKind::StandardDynamicRange) != (_curAcKind == winrt::AdvancedColorKind::StandardDynamicRange);

	if (!onInit && shouldUpdateResources) {
		// 等待 GPU 完成然后改变交换链格式
		if (!_presenter->RecreateBuffers((uint32_t)_scissorRect.right, (uint32_t)_scissorRect.bottom, _curAcKind)) {
			return false;
		}
	}

	if (onInit || shouldUpdateResources) {
		// 创建根签名
		winrt::com_ptr<ID3DBlob> signature;
		winrt::com_ptr<ID3DBlob> error;

		if (_curAcKind == winrt::AdvancedColorKind::StandardDynamicRange) {
			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(0, (D3D12_ROOT_PARAMETER1*)nullptr, 0, nullptr,
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
			if (FAILED(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, signature.put(), error.put()))) {
				return false;
			}
		} else {
			D3D12_ROOT_PARAMETER1 rootParam{
				.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
				.Constants = {
					.Num32BitValues = 1
				},
				.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL
			};
			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(1, &rootParam, 0, nullptr,
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
			if (FAILED(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, signature.put(), error.put()))) {
				return false;
			}
		}

		if (FAILED(_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&_rootSignature)))) {
			return false;
		}

		// 创建 PSO
		const D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {
			.pRootSignature = _rootSignature.get(),
			.VS = {.pShaderBytecode = SimpleVS, .BytecodeLength = sizeof(SimpleVS) },
			.PS = {
				.pShaderBytecode = _curAcKind == winrt::AdvancedColorKind::StandardDynamicRange ?
					sRGB_PS : AdvancedColor_PS,
				.BytecodeLength = _curAcKind == winrt::AdvancedColorKind::StandardDynamicRange ?
					sizeof(sRGB_PS) : sizeof(AdvancedColor_PS)
			},
			.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
			.SampleMask = UINT_MAX,
			.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
			.InputLayout = {
				.pInputElementDescs = inputElementDescs,
				.NumElements = (UINT)std::size(inputElementDescs)
			},
			.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
			.NumRenderTargets = 1,
			.RTVFormats = { _curAcKind == winrt::AdvancedColorKind::StandardDynamicRange ?
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R16G16B16A16_FLOAT },
			.SampleDesc = {.Count = 1 }
		};
		if (FAILED(_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pipelineState)))) {
			return false;
		}
	}

	return true;
}
