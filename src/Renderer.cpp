#include "pch.h"
#include "Renderer.h"
#include "shaders/AdvancedColor_PS.h"
#include "shaders/AdvancedColor_PS_SM5.h"
#include "shaders/SimpleVS.h"
#include "shaders/SimpleVS_SM5.h"
#include "shaders/sRGB_PS.h"
#include "shaders/sRGB_PS_SM5.h"
#include <dispatcherqueue.h>
#include <windows.graphics.display.interop.h>

static constexpr float SCENE_REFERRED_SDR_WHITE_LEVEL = 80.0f;

struct VertexPositionTexture {
	DirectX::XMFLOAT2 position;
	DirectX::XMFLOAT2 texCoord;

	static constexpr D3D12_INPUT_ELEMENT_DESC InputElements[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
};

Renderer::~Renderer() {
	_d3d12Context.WaitForGpu();
}

bool Renderer::Initialize(HWND hwndMain, Size size, float dpiScale) noexcept {
	_hwndMain = hwndMain;
	_dpiScale = dpiScale;
	_size = size;

	_hCurMonitor = MonitorFromWindow(hwndMain, MONITOR_DEFAULTTONEAREST);
	
	[[maybe_unused]] static int _ = [] {
#ifdef _DEBUG
		{
			winrt::com_ptr<ID3D12Debug1> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
				debugController->EnableDebugLayer();
				// 启用 GPU-based validation，但会产生警告消息，而且这个消息无法轻易禁用
				debugController->SetEnableGPUBasedValidation(TRUE);

				// Win11 开始支持生成默认名字，包含资源的基本属性
				if (winrt::com_ptr<ID3D12Debug5> debugController5 = debugController.try_as<ID3D12Debug5>()) {
					debugController5->SetEnableAutoName(TRUE);
				}
			}
		}
#endif
		// 声明支持 TDR 恢复
		DXGIDeclareAdapterRemovalSupport();

		return 0;
	}();

	if (!_d3d12Context.Initialize(2)) {
		return false;
	}

	ID3D12Device5* device = _d3d12Context.GetDevice();

	{
		const UINT vertexBufferSize = sizeof(VertexPositionTexture) * 22;

		D3D12_HEAP_FLAGS heapFlag = _d3d12Context.IsHeapFlagCreateNotZeroedSupported() ?
			D3D12_HEAP_FLAG_CREATE_NOT_ZEROED : D3D12_HEAP_FLAG_NONE;
		CD3DX12_HEAP_PROPERTIES heapProperties(
			_d3d12Context.IsGPUUploadHeapSupported() ? D3D12_HEAP_TYPE_GPU_UPLOAD : D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

		if (FAILED(device->CreateCommittedResource(
			&heapProperties,
			heapFlag,
			&bufferDesc,
			_d3d12Context.IsGPUUploadHeapSupported() ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&_vertexUploadBuffer)
		))) {
			return false;
		}

		// 无需解除映射
		D3D12_RANGE readRange{};
		if (FAILED(_vertexUploadBuffer->Map(0, &readRange, &_vertexUploadBufferData))) {
			return false;
		}

		// 集成显卡可高效使用上传堆
		if (_d3d12Context.IsGPUUploadHeapSupported() || _d3d12Context.IsUMA()) {
			_vertexBufferView.BufferLocation = _vertexUploadBuffer->GetGPUVirtualAddress();
		} else {
			heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
			if (FAILED(device->CreateCommittedResource(
				&heapProperties,
				heapFlag,
				&bufferDesc,
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&_vertexBuffer)
			))) {
				return false;
			}

			_vertexBufferView.BufferLocation = _vertexBuffer->GetGPUVirtualAddress();
		}
		
		_vertexBufferView.StrideInBytes = sizeof(VertexPositionTexture);
		_vertexBufferView.SizeInBytes = vertexBufferSize;
	}

	// 失败则回落到使用传统方法获取颜色显示能力
	_TryInitDisplayInfo();

	if (!_UpdateColorInfo()) {
		return false;
	}

	_UpdateWindowTitle();

	if (!_swapChain.Initialize(_d3d12Context, hwndMain, size, _colorInfo)) {
		return false;
	}

	if (FAILED(_InitializePSO())) {
		return false;
	}

	return true;
}

ComponentState Renderer::Render(bool waitForGpu) noexcept {
	if (_state != ComponentState::NoError) {
		return _state;
	}

	// SwapChain::BeginFrame 和 D3D12Context::BeginFrame 无顺序要求，不过
	// 前者通常等待时间更久，将它放在前面可以减少等待次数。
	ID3D12Resource* frameTex;
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	_swapChain.BeginFrame(&frameTex, rtvHandle);

	uint32_t frameIndex;
	if (!_CheckResult(_d3d12Context.BeginFrame(frameIndex, _pipelineState.get()))) {
		return _state;
	}

	ID3D12GraphicsCommandList* commandList = _d3d12Context.GetCommandList();
	
	if (_shouldUpdateSizeDependentResources) {
		_shouldUpdateSizeDependentResources = false;
		_UpdateSizeDependentResources(commandList);
	}

	commandList->SetGraphicsRootSignature(_rootSignature.get());

	if (_colorInfo.kind != winrt::AdvancedColorKind::StandardDynamicRange) {
		// HDR 下提高彩色正方形亮度
		float boost = _colorInfo.kind == winrt::AdvancedColorKind::WideColorGamut ?
			1.0f : std::min(_colorInfo.sdrWhiteLevel + 1, _colorInfo.maxLuminance);
		commandList->SetGraphicsRoot32BitConstants(0, 1, &boost, 0);
	}

	{
		CD3DX12_VIEWPORT viewport(0.0f, 0.0f, (float)_size.width, (float)_size.height);
		commandList->RSSetViewports(1, &viewport);
	}
	{
		CD3DX12_RECT scissorRect(0, 0, (LONG)_size.width, (LONG)_size.height);
		commandList->RSSetScissorRects(1, &scissorRect);
	}
	
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			frameTex, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		commandList->ResourceBarrier(1, &barrier);
	}

	commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	{
		const float clearColor[] = {
			0.8f * _colorInfo.sdrWhiteLevel,
			0.8f * _colorInfo.sdrWhiteLevel,
			0.6f * _colorInfo.sdrWhiteLevel,
			1.0f
		};
		commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	}

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	commandList->IASetVertexBuffers(0, 1, &_vertexBufferView);
	commandList->DrawInstanced(22, 1, 0, 0);
	
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			frameTex, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		commandList->ResourceBarrier(1, &barrier);
	}

	if (!_CheckResult(commandList->Close())) {
		return _state;
	}

	_d3d12Context.GetCommandQueue()->ExecuteCommandLists(1, CommandListCast(&commandList));

	if (!_CheckResult(_swapChain.EndFrame(waitForGpu))) {
		return _state;
	}

	// D3D12Context::EndFrame 必须在 SwapChain::EndFrame 之后
	_CheckResult(_d3d12Context.EndFrame());
	return _state;
}

void Renderer::OnResizeStarted() noexcept {
	if (_state != ComponentState::NoError) {
		return;
	}

	_swapChain.OnResizeStarted();
}

void Renderer::OnResizeEnded() noexcept {
	if (_state != ComponentState::NoError) {
		return;
	}

	_CheckResult(_swapChain.OnResizeEnded());
}

void Renderer::OnResized(Size size, float dpiScale) noexcept {
	// 确保尺寸变化后调用
	assert(size.width > 0 && size.height > 0 && size != _size);

	if (_state != ComponentState::NoError) {
		return;
	}

	_size = size;
	_dpiScale = dpiScale;
	_shouldUpdateSizeDependentResources = true;
	
	if (!_CheckResult(_swapChain.OnResized(size))) {
		return;
	}
}

void Renderer::OnMsgWindowPosChanged() noexcept {
	// winrt::DisplayInformation 可用时已通过事件监听颜色配置变化
	if (_state != ComponentState::NoError || _displayInfo) {
		return;
	}

	HMONITOR hCurMonitor = MonitorFromWindow(_hwndMain, MONITOR_DEFAULTTONEAREST);
	if (_hCurMonitor == hCurMonitor) {
		return;
	}
	_hCurMonitor = hCurMonitor;

	_CheckResult(_UpdateColorSpace());
}

void Renderer::OnMsgDisplayChanged() noexcept {
	if (_state != ComponentState::NoError) {
		return;
	}

	// 如果正在使用 WARP 渲染则检测是否有显卡连接了
	if (_d3d12Context.CheckForBetterAdapter()) {
		// 强制重新创建 D3D 设备
		_state = ComponentState::DeviceLost;
		return;
	}

	// winrt::DisplayInformation 可用时已通过事件监听颜色配置变化
	if (!_displayInfo) {
		_CheckResult(_UpdateColorSpace());
	}
}

// 调用前需等待 GPU 完成
void Renderer::_UpdateSizeDependentResources(ID3D12GraphicsCommandList* commandList) noexcept {
	const float squareWidth = 200.0f * _dpiScale / _size.width * 2.0f;
	const float squareHeight = 200.0f * _dpiScale / _size.height * 2.0f;
	alignas(64) VertexPositionTexture triangleVertices[] = {
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

	memcpy(_vertexUploadBufferData, triangleVertices, sizeof(triangleVertices));

	if (_d3d12Context.IsGPUUploadHeapSupported()) {
		if (!_isVertexBufferInitialized) {
			// 创建时是 COMMON 状态，需一次转换
			D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				_vertexUploadBuffer.get(),
				D3D12_RESOURCE_STATE_COMMON,
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
			);
			commandList->ResourceBarrier(1, &barrier);

			_isVertexBufferInitialized = true;
		}
	} else if (!_d3d12Context.IsUMA()) {
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			_vertexBuffer.get(),
			_isVertexBufferInitialized ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER : D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_DEST
		);
		commandList->ResourceBarrier(1, &barrier);

		commandList->CopyResource(_vertexBuffer.get(), _vertexUploadBuffer.get());

		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		commandList->ResourceBarrier(1, &barrier);

		_isVertexBufferInitialized = true;
	}
}

bool Renderer::_TryInitDisplayInfo() noexcept {
	// 从 Win11 22H2 开始支持
	winrt::com_ptr<IDisplayInformationStaticsInterop> interop =
		winrt::try_get_activation_factory<winrt::DisplayInformation, IDisplayInformationStaticsInterop>();
	if (!interop) {
		return false;
	}

	// DisplayInformation 需要 DispatcherQueue
	static winrt::DispatcherQueueController dispatcherQueueController = [] {
		winrt::DispatcherQueueController result{ nullptr };
		CreateDispatcherQueueController(
			DispatcherQueueOptions{
				.dwSize = sizeof(DispatcherQueueOptions),
				.threadType = DQTYPE_THREAD_CURRENT
			},
			(PDISPATCHERQUEUECONTROLLER*)winrt::put_abi(result)
		);
		return result;
	}();
	if (!dispatcherQueueController) {
		return false;
	}

	if (FAILED(interop->GetForWindow(_hwndMain, winrt::guid_of<winrt::DisplayInformation>(), winrt::put_abi(_displayInfo)))) {
		return false;
	}

	_acInfoChangedRevoker = _displayInfo.AdvancedColorInfoChanged(
		winrt::auto_revoke,
		[this](winrt::DisplayInformation const&, winrt::IInspectable const&) {
			if (_state == ComponentState::NoError) {
				_CheckResult(_UpdateColorSpace());
			}
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

bool Renderer::_UpdateColorInfo() noexcept {
	if (_displayInfo) {
		winrt::AdvancedColorInfo acInfo = _displayInfo.GetAdvancedColorInfo();

		_colorInfo.kind = acInfo.CurrentAdvancedColorKind();
		if (_colorInfo.kind == winrt::AdvancedColorKind::HighDynamicRange) {
			_colorInfo.maxLuminance = acInfo.MaxLuminanceInNits() / SCENE_REFERRED_SDR_WHITE_LEVEL;
			_colorInfo.sdrWhiteLevel = acInfo.SdrWhiteLevelInNits() / SCENE_REFERRED_SDR_WHITE_LEVEL;
		} else {
			_colorInfo.maxLuminance = 1.0f;
			_colorInfo.sdrWhiteLevel = 1.0f;
		}
		
		return true;
	}

	IDXGIFactory7* dxgiFactory = _d3d12Context.GetDXGIFactoryForEnumingAdapters();
	if (!dxgiFactory) {
		return false;
	}
	
	winrt::com_ptr<IDXGIAdapter1> adapter;
	winrt::com_ptr<IDXGIOutput> output;
	for (UINT adapterIdx = 0;
		SUCCEEDED(dxgiFactory->EnumAdapters1(adapterIdx, adapter.put()));
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
						_colorInfo.kind = winrt::AdvancedColorKind::HighDynamicRange;
						_colorInfo.maxLuminance = desc.MaxLuminance / SCENE_REFERRED_SDR_WHITE_LEVEL;
						_colorInfo.sdrWhiteLevel = GetSDRWhiteLevel(desc.DeviceName);
					} else {
						_colorInfo.kind = winrt::AdvancedColorKind::StandardDynamicRange;
						_colorInfo.maxLuminance = 1.0f;
						_colorInfo.sdrWhiteLevel = 1.0f;
					}
					
					return true;
				}
			}
		}
	}

	// 未找到视为 SDR
	_colorInfo.kind = winrt::AdvancedColorKind::StandardDynamicRange;
	_colorInfo.maxLuminance = 1.0f;
	_colorInfo.sdrWhiteLevel = 1.0f;
	return true;
}

HRESULT Renderer::_UpdateColorSpace() noexcept {
	ColorInfo oldColorInfo = _colorInfo;
	if (!_UpdateColorInfo()) {
		return E_FAIL;
	}

	if (oldColorInfo == _colorInfo) {
		return S_OK;
	}

	_UpdateWindowTitle();

	// 等待 GPU 完成然后改变交换链格式
	HRESULT hr = _swapChain.OnColorInfoChanged(_colorInfo);
	if (FAILED(hr)) {
		return hr;
	}

	// SDR<->其他的转换需要改变交换链格式和着色器
	const bool shouldUpdateResources =
		(oldColorInfo.kind == winrt::AdvancedColorKind::StandardDynamicRange) !=
		(_colorInfo.kind == winrt::AdvancedColorKind::StandardDynamicRange);

	if (shouldUpdateResources) {
		hr = _InitializePSO();
		if (FAILED(hr)) {
			return hr;
		}
	}

	return S_OK;
}

void Renderer::_UpdateWindowTitle() const noexcept {
	const wchar_t* title;
	if (_colorInfo.kind == winrt::AdvancedColorKind::StandardDynamicRange) {
		title = L"D3D12Playground | SDR";
	} else if (_colorInfo.kind == winrt::AdvancedColorKind::WideColorGamut) {
		title = L"D3D12Playground | WCG";
	} else {
		title = L"D3D12Playground | HDR";
	}
	SetWindowText(_hwndMain, title);
}

HRESULT Renderer::_InitializePSO() noexcept {
	// 创建根签名
	winrt::com_ptr<ID3DBlob> signature;
	if (_colorInfo.kind == winrt::AdvancedColorKind::StandardDynamicRange) {
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(
			0, (D3D12_ROOT_PARAMETER1*)nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		HRESULT hr = D3DX12SerializeVersionedRootSignature(
			&rootSignatureDesc, _d3d12Context.GetRootSignatureVersion(), signature.put(), nullptr);
		if (FAILED(hr)) {
			return hr;
		}
	} else {
		D3D12_ROOT_PARAMETER1 rootParam = {
			.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
			.Constants = {
				.Num32BitValues = 1
			},
			.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL
		};
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(
			1, &rootParam, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		HRESULT hr = D3DX12SerializeVersionedRootSignature(
			&rootSignatureDesc, _d3d12Context.GetRootSignatureVersion(), signature.put(), nullptr);
		if (FAILED(hr)) {
			return hr;
		}
	}

	ID3D12Device5* device = _d3d12Context.GetDevice();

	HRESULT hr = device->CreateRootSignature(
		0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&_rootSignature));
	if (FAILED(hr)) {
		return hr;
	}

	// 创建 PSO
	D3D12_SHADER_BYTECODE vsByteCode;
	D3D12_SHADER_BYTECODE psByteCode;
	if (_d3d12Context.IsSM6Supported()) {
		vsByteCode = { SimpleVS, sizeof(SimpleVS) };

		if (_colorInfo.kind == winrt::AdvancedColorKind::StandardDynamicRange) {
			psByteCode = { sRGB_PS, sizeof(sRGB_PS) };
		} else {
			psByteCode = { AdvancedColor_PS, sizeof(AdvancedColor_PS) };
		}
	} else {
		vsByteCode = { SimpleVS_SM5, sizeof(SimpleVS_SM5) };

		if (_colorInfo.kind == winrt::AdvancedColorKind::StandardDynamicRange) {
			psByteCode = { sRGB_PS_SM5, sizeof(sRGB_PS_SM5) };
		} else {
			psByteCode = { AdvancedColor_PS_SM5, sizeof(AdvancedColor_PS_SM5) };
		}
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {
		.pRootSignature = _rootSignature.get(),
		.VS = vsByteCode,
		.PS = psByteCode,
		.BlendState = {
			.RenderTarget = {{ .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL }}
		},
		.SampleMask = UINT_MAX,
		.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
		.InputLayout = {
			.pInputElementDescs = VertexPositionTexture::InputElements,
			.NumElements = (UINT)std::size(VertexPositionTexture::InputElements)
		},
		.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		.NumRenderTargets = 1,
		.RTVFormats = { _colorInfo.kind == winrt::AdvancedColorKind::StandardDynamicRange ?
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R16G16B16A16_FLOAT },
		.SampleDesc = { .Count = 1 }
	};
	return device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pipelineState));
}

bool Renderer::_CheckResult(bool success) noexcept {
	assert(_state == ComponentState::NoError);

	if (!success) {
		_state = ComponentState::Error;
	}
	return success;
}

bool Renderer::_CheckResult(HRESULT hr) noexcept {
	assert(_state == ComponentState::NoError);

	if (SUCCEEDED(hr)) {
		return true;
	}
	
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
		_state = ComponentState::DeviceLost;
	} else {
		_state = ComponentState::Error;
	}

	return false;
}
