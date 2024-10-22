#include "Renderer.h"

using namespace Microsoft::WRL;
using namespace DirectX;

void Renderer::Init()
{
	OF::InitFramework(d3d11Device, spriteBatch, window);
	OF::LoadFont(FontPath);
}

void Renderer::Tick()
{
	if (OF::CheckHotkey(HelperButton, HelperButtonMod))
	{
		showTranslations = !showTranslations;
	}

	if (OF::CheckHotkey(TranslateButton, TranslateButtonMod))
	{
		if (cleanNeeded) {
			TranslateClient::ClearEntries();
		}
		else {
			Blob* blob = new Blob;

			CreateScreenshot(blob);
			CreateThread(0, 0, &TranslateClient::SendRequest, blob, 0, NULL);

			showInProgress = true;
		}
	}

	std::vector<TranslateClient::TranslationEntry> entries;

	TranslateClient::PullEntries(&entries);

	bool showing = cleanNeeded = entries.size() != 0;

	if (showing) {
		if (showInProgress) {
			showInProgress = false;
		}

		OF::DrawText(
			"D",
			5,
			5,
			30,
			30,
			Colors::IndianRed);
	}
	else if (showInProgress) {
		OF::DrawText(
			"...",
			5,
			5,
			30,
			30,
			Colors::LightYellow);
	}
	else {
		OF::DrawText(
			"R",
			5,
			5,
			30,
			30,
			Colors::LightGreen);
	}

	for (auto entry = entries.begin(); entry != entries.end(); entry++)
	{
		try {
			OF::DrawText(
				showTranslations ? entry->translation.c_str() : entry->message.c_str(),
				entry->x,
				entry->y,
				entry->w,
				entry->h,
				showTranslations ? Colors::LightSkyBlue : Colors::LightGreen);
		}
		catch (std::logic_error e) {
			logger.Log(e.what());
		}
	}
}

bool Renderer::CreateScreenshot(Blob* blob)
{
	ComPtr<ID3D11Texture2D> backBufferTex;
	ScratchImage image;

	HRESULT hr = swapChain->GetBuffer(bufferIndex, __uuidof(ID3D11Texture2D), (LPVOID*)&backBufferTex);

	if (FAILED(hr))
	{
		return false;
	}

	CaptureTexture(d3d11Device.Get(), d3d11Context.Get(), backBufferTex.Get(), image);

	const Image* img = image.GetImage(0, 0, 0);

	hr = SaveToWICMemory(*img, WIC_FLAGS_NONE, GUID_ContainerFormatBmp, *blob);

	if (FAILED(hr))
	{
		return false;
	}

	return true;
}

void Renderer::OnPresent(IDXGISwapChain* pThis, UINT syncInterval, UINT flags)
{
	if (mustInitializeD3DResources)
	{
		if (!InitD3DResources(pThis))
		{
			return;
		}
		mustInitializeD3DResources = false;
	}

	Render();
}

void Renderer::OnResizeBuffers(IDXGISwapChain* pThis, UINT bufferCount, UINT width, UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags)
{
	logger.Log("ResizeBuffers was called!");
	ReleaseViewsBuffersAndContext();
	mustInitializeD3DResources = true;
}

void Renderer::SetCommandQueue(ID3D12CommandQueue* commandQueue)
{
	this->commandQueue = commandQueue;
}

void Renderer::SetGetCommandQueueCallback(void (*callback)())
{
	callbackGetCommandQueue = callback;
}

bool Renderer::InitD3DResources(IDXGISwapChain* swapChain)
{
	logger.Log("Initializing D3D resources...");

	try
	{
		if (!isDeviceRetrieved)
		{
			this->swapChain = swapChain;
			isDeviceRetrieved = RetrieveD3DDeviceFromSwapChain();
		}

		if (WaitForCommandQueueIfRunningD3D12())
		{
			return false;
		}

		GetSwapChainDescription();
		GetBufferCount();
		GetSwapchainWindowInfo();
		CreateViewport();
		InitD3D();
	}
	catch (std::string errorMsg)
	{
		logger.Log(errorMsg);
		return false;
	}

	firstTimeInitPerformed = true;
	logger.Log("Successfully initialized D3D resources");
	return true;
}

bool Renderer::RetrieveD3DDeviceFromSwapChain()
{
	logger.Log("Retrieving D3D device...");

	bool d3d11DeviceRetrieved = SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D11Device), (void**)d3d11Device.GetAddressOf()));
	if (d3d11DeviceRetrieved)
	{
		logger.Log("Retrieved D3D11 device");
		return true;
	}
	
	bool d3d12DeviceRetrieved = SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D12Device), (void**)d3d12Device.GetAddressOf()));
	if (d3d12DeviceRetrieved)
	{
		logger.Log("Retrieved D3D12 device");
		isRunningD3D12 = true;
		return true;
	}

	throw("Failed to retrieve D3D device");
}

void Renderer::GetSwapChainDescription()
{
	ZeroMemory(&swapChainDesc, sizeof(DXGI_SWAP_CHAIN_DESC));
	swapChain->GetDesc(&swapChainDesc);
}

void Renderer::GetBufferCount()
{
	if (isRunningD3D12)
	{
		bufferCount = swapChainDesc.BufferCount;
	}
	else
	{
		bufferCount = 1;
	}
}

void Renderer::GetSwapchainWindowInfo()
{
	RECT hwndRect;
	GetClientRect(swapChainDesc.OutputWindow, &hwndRect);
	windowWidth = hwndRect.right - hwndRect.left;
	windowHeight = hwndRect.bottom - hwndRect.top;
	logger.Log("Window width: %i", windowWidth);
	logger.Log("Window height: %i", windowHeight);
	window = swapChainDesc.OutputWindow;
}

void Renderer::CreateViewport()
{
	ZeroMemory(&viewport, sizeof(D3D11_VIEWPORT));
	viewport.Width = windowWidth;
	viewport.Height = windowHeight;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
}

void Renderer::InitD3D()
{
	if (!isRunningD3D12)
	{
		InitD3D11();
	}
	else
	{
		InitD3D12();
	}
}

void Renderer::InitD3D11()
{
	logger.Log("Initializing D3D11...");

	if (!firstTimeInitPerformed)
	{
		CreateD3D11Context();
		CreateSpriteBatch();
	}
	CreateD3D11RenderTargetView();

	logger.Log("Initialized D3D11");
}

void Renderer::CreateD3D11Context()
{
	d3d11Device->GetImmediateContext(&d3d11Context);
}

void Renderer::CreateSpriteBatch()
{
	spriteBatch = std::make_shared<SpriteBatch>(d3d11Context.Get());
}

void Renderer::CreateD3D11RenderTargetView()
{
	ComPtr<ID3D11Texture2D> backbuffer;
	swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backbuffer.GetAddressOf());
	d3d11RenderTargetViews = std::vector<ComPtr<ID3D11RenderTargetView>>(1, nullptr);
	d3d11Device->CreateRenderTargetView(backbuffer.Get(), nullptr, d3d11RenderTargetViews[0].GetAddressOf());
	backbuffer.ReleaseAndGetAddressOf();
}

void Renderer::InitD3D12()
{
	logger.Log("Initializing D3D12...");
	
	if (!firstTimeInitPerformed)
	{
		CreateD3D11On12Device();
		CheckSuccess(swapChain->QueryInterface(__uuidof(IDXGISwapChain3), &swapChain3));
		CreateSpriteBatch();
	}
	CreateD3D12Buffers();

	logger.Log("Initialized D3D12");
}

bool Renderer::WaitForCommandQueueIfRunningD3D12()
{
	if (isRunningD3D12)
	{
		if (commandQueue.Get() == nullptr)
		{
			logger.Log("Waiting for command queue...");
			if (!getCommandQueueCalled && callbackGetCommandQueue != nullptr)
			{
				callbackGetCommandQueue();
				getCommandQueueCalled = true;
			}
			return true;
		}
	}
	return false;
}

void Renderer::CreateD3D11On12Device()
{
	D3D_FEATURE_LEVEL featureLevels = { D3D_FEATURE_LEVEL_11_0 };
	bool d3d11On12DeviceCreated = CheckSuccess(
		D3D11On12CreateDevice(
			d3d12Device.Get(),
			NULL,
			&featureLevels,
			1,
			reinterpret_cast<IUnknown**>(commandQueue.GetAddressOf()),
			1,
			0,
			d3d11Device.GetAddressOf(),
			d3d11Context.GetAddressOf(),
			nullptr));

	bool d3d11On12DeviceChecked = CheckSuccess(d3d11Device.As(&d3d11On12Device));

	if (!d3d11On12DeviceCreated || !d3d11On12DeviceChecked)
	{
		throw("Failed to create D3D11On12 device");
	}
}

void Renderer::CreateD3D12Buffers()
{
	d3d12RenderTargets = std::vector<ComPtr<ID3D12Resource>>(bufferCount, nullptr);
	d3d11WrappedBackBuffers = std::vector<ComPtr<ID3D11Resource>>(bufferCount, nullptr);
	d3d11RenderTargetViews = std::vector<ComPtr<ID3D11RenderTargetView>>(bufferCount, nullptr);

	ComPtr<ID3D12DescriptorHeap> rtvHeap = CreateD3D12RtvHeap();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
	UINT rtvDescriptorSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (UINT i = 0; i < bufferCount; i++)
	{
		CreateD3D12RenderTargetView(i, rtvHandle);
		CreateD3D11WrappedBackBuffer(i);
		CreateD3D11RenderTargetViewWithWrappedBackBuffer(i);
		rtvHandle.ptr = SIZE_T(INT64(rtvHandle.ptr) + INT64(1) * INT64(rtvDescriptorSize));
	}
}

ComPtr<ID3D12DescriptorHeap> Renderer::CreateD3D12RtvHeap()
{
	ComPtr<ID3D12DescriptorHeap> rtvHeap;
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = bufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	CheckSuccess(d3d12Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.GetAddressOf())));
	return rtvHeap;
}

void Renderer::CreateD3D12RenderTargetView(UINT bufferIndex, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle)
{
	if (!CheckSuccess(swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&d3d12RenderTargets[bufferIndex]))))
	{
		throw("Failed to create D3D12 render target view");
	}
	d3d12Device->CreateRenderTargetView(d3d12RenderTargets[bufferIndex].Get(), nullptr, rtvHandle);
}

void Renderer::CreateD3D11WrappedBackBuffer(UINT bufferIndex)
{
	D3D11_RESOURCE_FLAGS d3d11Flags = { D3D11_BIND_RENDER_TARGET };
	if (!CheckSuccess(
		d3d11On12Device->CreateWrappedResource(
			d3d12RenderTargets[bufferIndex].Get(),
			&d3d11Flags,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT,
			IID_PPV_ARGS(&d3d11WrappedBackBuffers[bufferIndex]))))
	{
		throw "Failed to create D3D11 wrapped backbuffer";
	}
}

void Renderer::CreateD3D11RenderTargetViewWithWrappedBackBuffer(UINT bufferIndex)
{
	if (!CheckSuccess(
		d3d11Device->CreateRenderTargetView(
			d3d11WrappedBackBuffers[bufferIndex].Get(),
			nullptr,
			d3d11RenderTargetViews[bufferIndex].GetAddressOf())))
	{
		throw "Failed to create D3D11 render target view";
	}
}

void Renderer::Render()
{
	PreRender();
	Tick();
	PostRender();
}

void Renderer::PreRender()
{
	if (isRunningD3D12)
	{
		bufferIndex = swapChain3->GetCurrentBackBufferIndex();
		d3d11On12Device->AcquireWrappedResources(d3d11WrappedBackBuffers[bufferIndex].GetAddressOf(), 1);
	}

	d3d11Context->OMSetRenderTargets(1, d3d11RenderTargetViews[bufferIndex].GetAddressOf(), 0);
	d3d11Context->RSSetViewports(1, &viewport);

	if (!defaultsLoaded)
	{
		CreatePipeline();
		Init();

		defaultsLoaded = true;
	}
}

void Renderer::PostRender()
{
	if (isRunningD3D12)
	{
		d3d11On12Device->ReleaseWrappedResources(d3d11WrappedBackBuffers[bufferIndex].GetAddressOf(), 1);
		d3d11Context->Flush();
	}
}

// Creates the necessary things for rendering the examples
void Renderer::CreatePipeline()
{
	ComPtr<ID3DBlob> vertexShaderBlob = LoadShader(shaderData, "vs_5_0", "VS").Get();
	ComPtr<ID3DBlob> pixelShaderTexturesBlob = LoadShader(shaderData, "ps_5_0", "PSTex").Get();
	ComPtr<ID3DBlob> pixelShaderBlob = LoadShader(shaderData, "ps_5_0", "PS").Get();

	d3d11Device->CreateVertexShader(
		vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize(),
		nullptr, 
		vertexShader.GetAddressOf());

	d3d11Device->CreatePixelShader(
		pixelShaderTexturesBlob->GetBufferPointer(),
		pixelShaderTexturesBlob->GetBufferSize(), 
		nullptr, 
		pixelShaderTextures.GetAddressOf());

	d3d11Device->CreatePixelShader(pixelShaderBlob->GetBufferPointer(),
		pixelShaderBlob->GetBufferSize(), nullptr, pixelShader.GetAddressOf());

	D3D11_INPUT_ELEMENT_DESC inputLayoutDesc[3] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	d3d11Device->CreateInputLayout(
		inputLayoutDesc,
		ARRAYSIZE(inputLayoutDesc),
		vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize(), 
		inputLayout.GetAddressOf());

	D3D11_SAMPLER_DESC samplerDesc;
	ZeroMemory(&samplerDesc, sizeof(D3D11_SAMPLER_DESC));
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	d3d11Device->CreateSamplerState(&samplerDesc, &samplerState);

	D3D11_TEXTURE2D_DESC dsDesc;
	dsDesc.Width = windowWidth;
	dsDesc.Height = windowHeight;
	dsDesc.MipLevels = 1;
	dsDesc.ArraySize = 1;
	dsDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsDesc.SampleDesc.Count = 1;
	dsDesc.SampleDesc.Quality = 0;
	dsDesc.Usage = D3D11_USAGE_DEFAULT;
	dsDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	dsDesc.CPUAccessFlags = 0;
	dsDesc.MiscFlags = 0;

	d3d11Device->CreateTexture2D(&dsDesc, 0, depthStencilBuffer.GetAddressOf());
	d3d11Device->CreateDepthStencilView(depthStencilBuffer.Get(), 0, depthStencilView.GetAddressOf());
}

ComPtr<ID3DBlob> Renderer::LoadShader(const char* shader, std::string targetShaderVersion, std::string shaderEntry)
{
	logger.Log("Loading shader: %s", shaderEntry.c_str());
	ComPtr<ID3DBlob> errorBlob = nullptr;
	ComPtr<ID3DBlob> shaderBlob;

	D3DCompile(
		shader, 
		strlen(shader), 
		0, 
		nullptr, 
		nullptr, 
		shaderEntry.c_str(), 
		targetShaderVersion.c_str(), 
		D3DCOMPILE_ENABLE_STRICTNESS, 
		0, 
		shaderBlob.GetAddressOf(), 
		errorBlob.GetAddressOf());

	if (errorBlob)
	{
		char error[256]{ 0 };
		memcpy(error, errorBlob->GetBufferPointer(), errorBlob->GetBufferSize());
		logger.Log("Shader error: %s", error);
		return nullptr;
	}

	return shaderBlob;
}

void Renderer::ReleaseViewsBuffersAndContext()
{
	for (int i = 0; i < bufferCount; i++)
	{
		if (d3d12Device.Get() == nullptr)
		{
			d3d11RenderTargetViews[i].ReleaseAndGetAddressOf();
		}
		else
		{
			d3d11RenderTargetViews[i].ReleaseAndGetAddressOf();
			d3d12RenderTargets[i].ReleaseAndGetAddressOf();
			d3d11WrappedBackBuffers[i].ReleaseAndGetAddressOf();
		}
	}
	
	if (d3d11Context.Get() != nullptr)
	{
		d3d11Context->Flush();
	}
}

bool Renderer::CheckSuccess(HRESULT hr)
{
	if (SUCCEEDED(hr))
	{
		return true;
	}
	_com_error err(hr);
	logger.Log("%s", err.ErrorMessage());
	return false;
}