#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#ifdef _DEBUG
#include <iostream>
#endif

using namespace std;

const static int WINDOW_WIDTH = 1920;
const static int WINDOW_HEIGHT = 1080;

ID3D12Device* _dev = nullptr;
IDXGIFactory6* _dxgiFactory = nullptr;
IDXGISwapChain4* _swapchain = nullptr;
ID3D12CommandAllocator* _cmdAllocator = nullptr;
ID3D12GraphicsCommandList* _cmdList = nullptr;

LRESULT WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_DESTROY)
	{
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

#ifdef _DEBUG
int main()
{
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#endif

	auto result = CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory));

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

	// コマンドリスト
	result = _dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_cmdAllocator));
	result = _dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _cmdAllocator, nullptr, IID_PPV_ARGS(&_cmdList));

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

	MSG msg = {};

	while (true) {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (msg.message == WM_QUIT) {
			break;
		}
	}

	UnregisterClass(w.lpszClassName, w.hInstance);
	return 0;
}