#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <DirectXTex.h>
#include <d3dx12.h>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "DirectXTex.lib")

#ifdef _DEBUG
#include <iostream>
#endif

using namespace std;
using namespace DirectX;

const static int WINDOW_WIDTH = 1920;
const static int WINDOW_HEIGHT = 1080;

constexpr size_t pmdvertex_size = 38;

ID3D12Device* _dev = nullptr;
IDXGIFactory6* _dxgiFactory = nullptr;
IDXGISwapChain4* _swapchain = nullptr;
ID3D12CommandAllocator* _cmdAllocator = nullptr;
ID3D12GraphicsCommandList* _cmdList = nullptr;
ID3D12CommandQueue* _cmdQueue = nullptr;
ID3D12DescriptorHeap* _rtvHeaps = nullptr;
ID3D12Fence* _fence = nullptr;
ID3D12Resource* _vertBuff = nullptr;
UINT64 _fenceVal = 0;
vector<ID3D12Resource*> _backBuffers;

struct Vertex
{
	XMFLOAT3 pos;
	XMFLOAT2 uv;
};

LRESULT WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_DESTROY)
	{
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

void EnableDebugLayer()
{
	ID3D12Debug* debugLayer = nullptr;
	auto result = D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer));
	debugLayer->EnableDebugLayer();
	debugLayer->Release();
}

struct TexRGBA
{
	unsigned char R, G, B, A;
};

// PMDヘッダー構造体
struct PMDHeader
{
	float version;
	char model_name[20];
	char comment[256];
};

// P<D頂点構造体
struct PMDVertex
{
	XMFLOAT3 pos;
	XMFLOAT3 normal;
	XMFLOAT2 uv;
	unsigned short boneNo[2];
	unsigned char boneWeight;
	unsigned char edgeFlg;
};

size_t AlignmentedSize(size_t size, size_t alignment)
{
	return size + alignment - size % alignment;
}

#ifdef _DEBUG
int main()
{
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#endif

	// ウィンドウクラスの生成&登録
	WNDCLASSEX w = {};
	w.cbSize = sizeof(WNDCLASSEX);
	w.lpfnWndProc = (WNDPROC)WindowProcedure;
	w.lpszClassName = TEXT("DX12Sample");
	w.hInstance = GetModuleHandle(nullptr);

	RegisterClassEx(&w);

	RECT wrc = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };

	// ウィンドウのサイズ補正
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ウィンドウオブジェクトの生成
	HWND hwnd = CreateWindow(
		w.lpszClassName,
		TEXT("DX12テスト"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		w.hInstance,
		nullptr);

	// ウィンドウ表示
	ShowWindow(hwnd, SW_SHOW);

	HRESULT result = S_FALSE;

#ifdef _DEBUG
	EnableDebugLayer();
	result = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&_dxgiFactory));
#endif
	result = CreateDXGIFactory(IID_PPV_ARGS(&_dxgiFactory));

	vector<IDXGIAdapter*> adapters;
	IDXGIAdapter* tmpAdapter = nullptr;

	for (int i = 0; _dxgiFactory->EnumAdapters(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
		adapters.push_back(tmpAdapter);
	}

	for (auto adpt : adapters) {
		DXGI_ADAPTER_DESC adesc = {};
		adpt->GetDesc(&adesc);
		wstring strDesc = adesc.Description;

		if (strDesc.find(L"NVIDIA") != string::npos) {
			tmpAdapter = adpt;
			break;
		}
	}

	// テクスチャデータの作成
	vector<TexRGBA> texturedata(256 * 256);
	for (auto& rgba : texturedata)
	{
		rgba.R = rand() % 256;
		rgba.G = rand() % 256;
		rgba.B = rand() % 256;
		rgba.A = 255;
	}

	// テクスチャの読み込み
	TexMetadata metadata = {};
	ScratchImage scratchImg = {};
	result = LoadFromWICFile(
		L"img/textest.png",
		WIC_FLAGS_NONE,
		&metadata,
		scratchImg
	);
	const Image* img = scratchImg.GetImage(0, 0, 0);

	// pmdデータの読み込み
	PMDHeader pmdheader = {};
	char signature[3] = {};
	FILE* fp = nullptr;
	fopen_s(&fp, "Model/初音ミク.pmd", "rb");
	fread(signature, sizeof(signature), 1, fp);
	fread(&pmdheader, sizeof(pmdheader), 1, fp);
	unsigned int vertNum;
	fread(&vertNum, sizeof(vertNum), 1, fp);
	vector<unsigned char> vertices(vertNum * pmdvertex_size);
	fread(vertices.data(), vertices.size(), 1, fp);

	// Direct3Dデバイスの初期化
	D3D_FEATURE_LEVEL featureLevel;
	D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};

	for (auto lv : levels) {
		if (D3D12CreateDevice(tmpAdapter, lv, IID_PPV_ARGS(&_dev)) == S_OK) {
			featureLevel = lv;
			break;
		}
	}

	// コマンドリスト&コマンドアロケータの初期化
	result = _dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_cmdAllocator));
	result = _dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _cmdAllocator, nullptr, IID_PPV_ARGS(&_cmdList));

	// コマンドキューの初期化
	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
	cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cmdQueueDesc.NodeMask = 0;
	cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	result = _dev->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&_cmdQueue));

	// スワップチェーン初期化
	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.Width = WINDOW_WIDTH;
	swapchainDesc.Height = WINDOW_HEIGHT;
	swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchainDesc.Stereo = false;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
	swapchainDesc.BufferCount = 2;
	swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	result = _dxgiFactory->CreateSwapChainForHwnd(_cmdQueue, hwnd, &swapchainDesc, nullptr, nullptr, (IDXGISwapChain1**)&_swapchain);

	// レンダーターゲット用ディスクリプタヒープの作成
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = 2;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	result = _dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_rtvHeaps));

	// シェーダーリソース用ディスクリプタヒープの作成
	ID3D12DescriptorHeap* basicDescHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descHeapDesc.NodeMask = 0;
	descHeapDesc.NumDescriptors = 2;
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	result = _dev->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&basicDescHeap));

	// フェンスの作成
	result = _dev->CreateFence(_fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));

	// スワップチェーンとディスクリプタの紐づけ
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	DXGI_SWAP_CHAIN_DESC swcDesc = {};
	result = _swapchain->GetDesc(&swcDesc);
	_backBuffers.resize(swcDesc.BufferCount);
	for (int i = 0; i < swcDesc.BufferCount; ++i) {
		result = _swapchain->GetBuffer(i, IID_PPV_ARGS(&_backBuffers[i]));

		D3D12_CPU_DESCRIPTOR_HANDLE handle = _rtvHeaps->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += i * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		_dev->CreateRenderTargetView(_backBuffers[i], &rtvDesc, handle);
	}

	unsigned short indices[] = {
		0, 1, 2,
		2, 1, 3,
	};

	// カメラ設定
	XMMATRIX matrix = XMMatrixRotationY(XM_PIDIV4);
	XMFLOAT3 eye(0, 10, -15);
	XMFLOAT3 target(0, 10, 0);
	XMFLOAT3 up(0, 1, 0);
	matrix *= XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMLoadFloat3(&up));
	matrix *= XMMatrixPerspectiveFovLH(
		XM_PIDIV2,
		static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT),
		1.0f,
		100.0f
	);

	// 頂点バッファーの生成
	D3D12_HEAP_PROPERTIES vertHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC vertResDesc = CD3DX12_RESOURCE_DESC::Buffer(vertices.size());
	result = _dev->CreateCommittedResource(
		&vertHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&vertResDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_vertBuff)
	);

	// インデックスバッファー
	D3D12_HEAP_PROPERTIES idxHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC idxResDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(indices));
	ID3D12Resource* idxBuff = nullptr;
	result = _dev->CreateCommittedResource(
		&idxHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&idxResDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&idxBuff)
	);

	// テクスチャアップロードバッファー
	D3D12_HEAP_PROPERTIES uploadHeapProp = {};
	uploadHeapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
	uploadHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	uploadHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	uploadHeapProp.CreationNodeMask = 0;
	uploadHeapProp.VisibleNodeMask = 0;
	D3D12_RESOURCE_DESC uploadResDesc = {};
	uploadResDesc.Format = DXGI_FORMAT_UNKNOWN;
	uploadResDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	uploadResDesc.Width = AlignmentedSize(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) * img->height;
	uploadResDesc.Height = 1;
	uploadResDesc.DepthOrArraySize = 1;
	uploadResDesc.MipLevels = 1;
	uploadResDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	uploadResDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	uploadResDesc.SampleDesc.Count = 1;
	uploadResDesc.SampleDesc.Quality = 0;
	ID3D12Resource* uploadBuff = nullptr;
	result = _dev->CreateCommittedResource(
		&uploadHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&uploadResDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadBuff)
	);

	// テクスチャ読み出し用リソース
	D3D12_HEAP_PROPERTIES texHeapprop = {};
	texHeapprop.Type = D3D12_HEAP_TYPE_DEFAULT;
	texHeapprop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	texHeapprop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	texHeapprop.CreationNodeMask = 0;
	texHeapprop.VisibleNodeMask = 0;
	D3D12_RESOURCE_DESC texResDesc = {};
	texResDesc.Format = metadata.format;
	texResDesc.Width = metadata.width;
	texResDesc.Height = metadata.height;
	texResDesc.DepthOrArraySize = metadata.arraySize;
	texResDesc.MipLevels = metadata.mipLevels;
	texResDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);
	texResDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texResDesc.SampleDesc.Count = 1;
	texResDesc.SampleDesc.Quality = 0;
	ID3D12Resource* texBuff = nullptr;
	result = _dev->CreateCommittedResource(
		&texHeapprop,
		D3D12_HEAP_FLAG_NONE,
		&texResDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&texBuff)
	);

	// 定数バッファー
	ID3D12Resource* constBuff = nullptr;
	D3D12_HEAP_PROPERTIES constHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC constResDesc = CD3DX12_RESOURCE_DESC::Buffer((sizeof(matrix) + 0xff) & ~0xff);
	result = _dev->CreateCommittedResource(
		&constHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&constResDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&constBuff)
	);

	// 頂点バッファーMap
	unsigned char* vertMap = nullptr;
	result = _vertBuff->Map(0, nullptr, (void**)&vertMap);
	copy(begin(vertices), end(vertices), vertMap);
	_vertBuff->Unmap(0, nullptr);

	// インデックスバッファーMap
	unsigned short* mappedIdx = nullptr;
	result = idxBuff->Map(0, nullptr, (void**)&mappedIdx);
	copy(begin(indices), end(indices), mappedIdx);
	idxBuff->Unmap(0, nullptr);

	// テクスチャバッファーマップ
	uint8_t* mapforImg = nullptr;
	result = uploadBuff->Map(0, nullptr, (void**)&mapforImg);
	auto srcAddress = img->pixels;
	auto rowPitch = AlignmentedSize(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
	for (int y = 0; y < img->height; ++y) {
		copy_n(srcAddress, rowPitch, mapforImg);
		srcAddress += img->rowPitch;
		mapforImg += rowPitch;
	}
	uploadBuff->Unmap(0, nullptr);

	// 定数バッファーマップ
	XMMATRIX* mapMatrix = nullptr;
	result = constBuff->Map(0, nullptr, (void**)&mapMatrix);
	*mapMatrix = matrix;

	// シェーダー読み込み
	ID3DBlob* vsBlob = nullptr;
	ID3DBlob* psBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	result = D3DCompileFromFile(
		L"source/BasicVertexShader.hlsl",
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"BasicVS", "vs_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&vsBlob,
		&errorBlob
	);
	result = D3DCompileFromFile(
		L"source/BasicPixelShader.hlsl",
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"BasicPS", "ps_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&psBlob,
		&errorBlob
	);

	// 頂点レイアウト
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{
			"POSITION",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
		},
		{
			"NORMAL",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
		},
		{
			"TEXCOORD",
			0,
			DXGI_FORMAT_R32G32_FLOAT,
			0,
			D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
		},
		{
			"BONE_NO",
			0,
			DXGI_FORMAT_R16G16_UINT,
			0,
			D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
		},
		{
			"WEIGHT",
			0,
			DXGI_FORMAT_R8_UINT,
			0,
			D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
		},
		{
			"EDGE_FLG",
			0,
			DXGI_FORMAT_R8_UINT,
			0,
			D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
		},
	};

	// 頂点バッファービュー作成
	D3D12_VERTEX_BUFFER_VIEW vbView = {};
	vbView.BufferLocation = _vertBuff->GetGPUVirtualAddress();
	vbView.SizeInBytes = vertices.size();
	vbView.StrideInBytes = pmdvertex_size;

	// インデックスバッファビュー
	D3D12_INDEX_BUFFER_VIEW ibView = {};
	ibView.BufferLocation = idxBuff->GetGPUVirtualAddress();
	ibView.Format = DXGI_FORMAT_R16_UINT;
	ibView.SizeInBytes = sizeof(indices);

	// シェーダーリソースビュー
	auto basicHeapHandle = basicDescHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	_dev->CreateShaderResourceView(texBuff, &srvDesc, basicDescHeap->GetCPUDescriptorHandleForHeapStart());

	// 定数バッファービュー
	basicHeapHandle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = constBuff->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = constBuff->GetDesc().Width;
	_dev->CreateConstantBufferView(&cbvDesc, basicHeapHandle);

	// ルートパラメータ
	D3D12_DESCRIPTOR_RANGE descTblRange[2] = {};
	descTblRange[0].NumDescriptors = 1;
	descTblRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descTblRange[0].BaseShaderRegister = 0;
	descTblRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	descTblRange[1].NumDescriptors = 1;
	descTblRange[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	descTblRange[1].BaseShaderRegister = 0;
	descTblRange[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	D3D12_ROOT_PARAMETER rootparam = {};
	rootparam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootparam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootparam.DescriptorTable.pDescriptorRanges = descTblRange;
	rootparam.DescriptorTable.NumDescriptorRanges = 2;

	// サンプラー
	D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MinLOD = 0.f;
	samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

	// ルートシグネチャ
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.pParameters = &rootparam;
	rootSignatureDesc.NumParameters = 1;
	rootSignatureDesc.pStaticSamplers = &samplerDesc;
	rootSignatureDesc.NumStaticSamplers = 1;
	ID3DBlob* rootSigBlob = nullptr;
	result = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1_0,
		&rootSigBlob,
		&errorBlob
	);
	ID3D12RootSignature* rootsignature = nullptr;
	result = _dev->CreateRootSignature(
		0,
		rootSigBlob->GetBufferPointer(),
		rootSigBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootsignature)
	);

	// グラフィックパイプラインステート
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline = {};
	gpipeline.VS.pShaderBytecode = vsBlob->GetBufferPointer();
	gpipeline.VS.BytecodeLength = vsBlob->GetBufferSize();
	gpipeline.PS.pShaderBytecode = psBlob->GetBufferPointer();
	gpipeline.PS.BytecodeLength = psBlob->GetBufferSize();
	gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	gpipeline.RasterizerState.MultisampleEnable = false;
	gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	gpipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	gpipeline.RasterizerState.DepthClipEnable = true;
	gpipeline.BlendState.AlphaToCoverageEnable = false;
	gpipeline.BlendState.IndependentBlendEnable = false;
	D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc = {};
	renderTargetBlendDesc.BlendEnable = false;
	renderTargetBlendDesc.LogicOpEnable = false;
	renderTargetBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	gpipeline.BlendState.RenderTarget[0] = renderTargetBlendDesc;
	gpipeline.InputLayout.pInputElementDescs = inputLayout;
	gpipeline.InputLayout.NumElements = _countof(inputLayout);
	gpipeline.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
	gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gpipeline.NumRenderTargets = 1;
	gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	gpipeline.SampleDesc.Count = 1;
	gpipeline.SampleDesc.Quality = 0;
	gpipeline.pRootSignature = rootsignature;

	// グラフィックパイプラインステートオブジェクトの生成
	ID3D12PipelineState* pipelineState = nullptr;
	result = _dev->CreateGraphicsPipelineState(&gpipeline, IID_PPV_ARGS(&pipelineState));

	// ビューポート設定
	D3D12_VIEWPORT viewport = {};
	viewport.Width = WINDOW_WIDTH;
	viewport.Height = WINDOW_HEIGHT;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MaxDepth = 1.f;
	viewport.MinDepth = 0.f;
	D3D12_RECT scissorRect = {};
	scissorRect.top - 0;
	scissorRect.left = 0;
	scissorRect.right = scissorRect.left + WINDOW_WIDTH;
	scissorRect.bottom = scissorRect.top + WINDOW_HEIGHT;

	// テクスチャコピー設定
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = uploadBuff;
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint.Offset = 0;
	src.PlacedFootprint.Footprint.Width = metadata.width;
	src.PlacedFootprint.Footprint.Height = metadata.height;
	src.PlacedFootprint.Footprint.Depth = metadata.depth;
	src.PlacedFootprint.Footprint.RowPitch = AlignmentedSize(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
	src.PlacedFootprint.Footprint.Format = img->format;
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = texBuff;
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	// バリア設定（テクスチャコピー）
	D3D12_RESOURCE_BARRIER barrierDesc = {};
	barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrierDesc.Transition.pResource = texBuff;
	barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	_cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	_cmdList->ResourceBarrier(1, &barrierDesc);
	_cmdList->Close();
	ID3D12CommandList* cmdLists[] = { _cmdList };
	_cmdQueue->ExecuteCommandLists(1, cmdLists);
	_cmdQueue->Signal(_fence, ++_fenceVal);
	if (_fence->GetCompletedValue() != _fenceVal) {
		auto event = CreateEvent(nullptr, false, false, nullptr);
		_fence->SetEventOnCompletion(_fenceVal, event);
		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}
	_cmdAllocator->Reset();
	_cmdList->Reset(_cmdAllocator, nullptr);


	MSG msg = {};

	while (true) {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (msg.message == WM_QUIT) {
			break;
		}

		// バックバッファのインデックス取得
		int bbIdx = _swapchain->GetCurrentBackBufferIndex();

		// バリア設定
		barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrierDesc.Transition.pResource = _backBuffers[bbIdx];
		barrierDesc.Transition.Subresource = 0;
		barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		_cmdList->ResourceBarrier(1, &barrierDesc);

		// レンダーターゲットを設定
		auto rtvH = _rtvHeaps->GetCPUDescriptorHandleForHeapStart();
		rtvH.ptr += bbIdx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		_cmdList->OMSetRenderTargets(1, &rtvH, true, nullptr);

		// 画面のクリア
		float clearColor[] = { 1.f, 1.f, 1.f, 1.f };
		_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);

		// 描画命令
		_cmdList->SetPipelineState(pipelineState);
		_cmdList->SetGraphicsRootSignature(rootsignature);
		_cmdList->SetDescriptorHeaps(1, &basicDescHeap);
		_cmdList->SetGraphicsRootDescriptorTable(0, basicDescHeap->GetGPUDescriptorHandleForHeapStart());
		_cmdList->RSSetViewports(1, &viewport);
		_cmdList->RSSetScissorRects(1, &scissorRect);
		_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		_cmdList->IASetVertexBuffers(0, 1, &vbView);
		_cmdList->IASetIndexBuffer(&ibView);

		_cmdList->DrawInstanced(vertNum, 1, 0, 0);

		// バリア設定
		barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		_cmdList->ResourceBarrier(1, &barrierDesc);

		// コマンド実行
		_cmdList->Close();
		ID3D12CommandList* cmdLists2[] = { _cmdList };
		_cmdQueue->ExecuteCommandLists(1, cmdLists2);

		// GPU処理待ち
		_cmdQueue->Signal(_fence, ++_fenceVal);
		if (_fence->GetCompletedValue() != _fenceVal) {
			auto event = CreateEvent(nullptr, false, false, nullptr);
			_fence->SetEventOnCompletion(_fenceVal, event);
			WaitForSingleObject(event, INFINITE);
			CloseHandle(event);
		}

		// コマンドリスト・アロケータリセット
		_cmdAllocator->Reset();
		_cmdList->Reset(_cmdAllocator, nullptr);

		// スワップチェーン実行
		_swapchain->Present(1, 0);
	}

	UnregisterClass(w.lpszClassName, w.hInstance);
	return 0;
}