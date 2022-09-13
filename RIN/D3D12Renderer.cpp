#include "D3D12Renderer.hpp"

// Library
#include <iostream>
#ifdef RIN_DEBUG
#include <sstream>
#endif

// Shaders
#include "shaders/DepthMIPCS.h"
#include "shaders/CullStaticCS.h"
#include "shaders/CullDynamicCS.h"
#include "shaders/CullSkinnedCS.h"
#include "shaders/LightClusterCS.h"
#include "shaders/PBRStaticVS.h"
#include "shaders/PBRDynamicVS.h"
#include "shaders/PBRSkinnedVS.h"
#include "shaders/PBRPS.h"
#include "shaders/SkyboxVS.h"
#include "shaders/SkyboxPS.h"
#include "shaders/PostVS.h"
#include "shaders/PostPS.h"

// Data
#include "data/Geometry.h"
#include "data/DFGLUT.h"

// Project
#include "Error.hpp"
#include "D3D12ShaderData.hpp"

constexpr DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT DEPTH_FORMAT_DSV = DXGI_FORMAT_D32_FLOAT;
constexpr DXGI_FORMAT DEPTH_FORMAT_SRV = DXGI_FORMAT_R32_FLOAT;
constexpr DXGI_FORMAT INDEX_FORMAT = DXGI_FORMAT_R32_UINT;

// (Sync) For copying camera data, static and dynamic object data, and light data
constexpr uint32_t COPY_QUEUE_CAMERA_STATIC_DYNAMIC_SKINNED_OB_LB_INDEX = 0;
// (Async) For copying static vertices and dynamic and skinned indices
constexpr uint32_t COPY_QUEUE_STATIC_VB_DYNAMIC_SKINNED_IB_INDEX = 1;
// (Async) For copying dynamic and skinned vertices and static indices
constexpr uint32_t COPY_QUEUE_DYNAMIC_SKINNED_VB_STATIC_IB_INDEX = 2;
// (Async) For copying textures
constexpr uint32_t COPY_QUEUE_TEXTURE_INDEX = 3;

constexpr uint32_t CULL_THREAD_GROUP_SIZE = 128;
constexpr uint32_t SCENE_STATIC_COMMAND_SIZE = sizeof(uint32_t) + sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
constexpr uint32_t SCENE_DYNAMIC_COMMAND_SIZE = sizeof(uint32_t) + sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
constexpr uint32_t SCENE_SKINNED_COMMAND_SIZE = sizeof(uint32_t) + sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
constexpr uint32_t UAV_COUNTER_SIZE = sizeof(uint32_t);

static_assert(SCENE_STATIC_COMMAND_SIZE >= UAV_COUNTER_SIZE, "SCENE_STATIC_COMMAND_SIZE cannot be less than UAV_COUNTER_SIZE");
static_assert(SCENE_DYNAMIC_COMMAND_SIZE >= UAV_COUNTER_SIZE, "SCENE_DYNAMIC_COMMAND_SIZE cannot be less than UAV_COUNTER_SIZE");
static_assert(SCENE_SKINNED_COMMAND_SIZE >= UAV_COUNTER_SIZE, "SCENE_SKINNED_COMMAND_SIZE cannot be less than UAV_COUNTER_SIZE");

constexpr uint32_t DEPTH_MIP_THREAD_GROUP_SIZE_X = 32;
constexpr uint32_t DEPTH_MIP_THREAD_GROUP_SIZE_Y = 32;
constexpr uint32_t SCENE_DEPTH_HIERARCHY_MIP_COUNT = 16;

constexpr uint32_t LIGHT_CLUSTER_THREAD_GROUP_SIZE_X = 16;
constexpr uint32_t LIGHT_CLUSTER_THREAD_GROUP_SIZE_Y = 8;
constexpr uint32_t LIGHT_CLUSTER_THREAD_GROUP_SIZE_Z = 8;
constexpr uint32_t SCENE_LIGHT_CLUSTER_LIGHT_COUNT = 63;
constexpr uint32_t SCENE_LIGHT_CLUSTER_SIZE = sizeof(uint32_t) + SCENE_LIGHT_CLUSTER_LIGHT_COUNT * sizeof(uint32_t);

static_assert(SCENE_FRUSTUM_CLUSTER_WIDTH % LIGHT_CLUSTER_THREAD_GROUP_SIZE_X == 0, "Invalid light cluster thread group x");
static_assert(SCENE_FRUSTUM_CLUSTER_HEIGHT % LIGHT_CLUSTER_THREAD_GROUP_SIZE_Y == 0, "Invalid light cluster thread group y");
static_assert(SCENE_FRUSTUM_CLUSTER_DEPTH % LIGHT_CLUSTER_THREAD_GROUP_SIZE_Z == 0, "Invalud light cluster thread group z");

/*
0: (SRV) sceneBackBuffer
1: (SRV) sceneDepthBuffer
2: (SRV) sceneDepthHierarchy
3-18: (SRV) sceneDepthHierarchy MIP
19-34: (UAV) sceneDepthHierarchy MIP
35: (UAV) sceneStaticCommandBuffer
36: (UAV) sceneDynamicCommandBuffer
37: (UAV) sceneSkinnedCommandBuffer
38: (UAV) sceneLightClusterBuffer
39: (SRV) sceneDFGLUT
40: (SRV) sceneSkyboxTexture
41: (SRV) sceneIBLDiffuseTexture
42: (SRV) sceneIBLSpecularTexture
43-unbounded: (SRV) sceneTexture
*/
constexpr uint32_t SCENE_BACK_BUFFER_SRV_OFFSET = 0;
constexpr uint32_t SCENE_DEPTH_BUFFER_SRV_OFFSET = 1;
constexpr uint32_t SCENE_DEPTH_HIERARCHY_SRV_OFFSET = 2;
constexpr uint32_t SCENE_DEPTH_HIERARCHY_MIP_SRV_OFFSET = 3;
constexpr uint32_t SCENE_DEPTH_HIERARCHY_MIP_UAV_OFFSET = 19;
constexpr uint32_t SCENE_STATIC_COMMAND_BUFFER_UAV_OFFSET = 35;
constexpr uint32_t SCENE_DYNAMIC_COMMAND_BUFFER_UAV_OFFSET = 36;
constexpr uint32_t SCENE_SKINNED_COMMAND_BUFFER_UAV_OFFSET = 37;
constexpr uint32_t SCENE_LIGHT_CLUSTER_BUFFER_UAV_OFFSET = 38;
constexpr uint32_t SCENE_DFG_LUT_SRV_OFFSET = 39;
constexpr uint32_t SCENE_SKYBOX_TEXTURE_SRV_OFFSET = 40;
constexpr uint32_t SCENE_IBL_DIFFUSE_TEXTURE_SRV_OFFSET = 41;
constexpr uint32_t SCENE_IBL_SPECULAR_TEXTURE_SRV_OFFSET = 42;
constexpr uint32_t SCENE_TEXTURE_SRV_OFFSET = 43;

#ifdef RIN_DEBUG
constexpr uint32_t DEBUG_QUERY_PIPELINE_STATIC_RENDER = 0;
constexpr uint32_t DEBUG_QUERY_PIPELINE_DYNAMIC_RENDER = 1;
constexpr uint32_t DEBUG_QUERY_PIPELINE_SKINNED_RENDER = 2;
constexpr uint32_t DEBUG_QUERY_PIPELINE_COUNT = 3;
#endif

namespace RIN {
	D3D12Renderer::D3D12Renderer(HWND hwnd, const Config& config, const Settings& settings) :
		Renderer(config, settings),
		hwnd(hwnd),
		hwndStyle(GetWindowLong(hwnd, GWL_STYLE)),
		uploadStreamAllocator(config.uploadStreamSize),
		sceneStaticVertexAllocator(config.staticVertexCount * sizeof(StaticVertex)),
		sceneStaticIndexAllocator(config.staticIndexCount * sizeof(index_type)),
		sceneDynamicVertexAllocator(config.dynamicVertexCount * sizeof(DynamicVertex)),
		sceneDynamicIndexAllocator(config.dynamicIndexCount * sizeof(index_type)),
		sceneSkinnedVertexAllocator(config.skinnedVertexCount * sizeof(SkinnedVertex)),
		sceneSkinnedIndexAllocator(config.skinnedIndexCount * sizeof(index_type)),
		sceneBoneAllocator(config.boneCount),
		sceneTextureAllocator(ALIGN_TO(config.texturesSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)),
		sceneStaticMeshPool(config.staticMeshCount),
		sceneStaticObjectPool(config.staticObjectCount),
		sceneDynamicMeshPool(config.dynamicMeshCount),
		sceneDynamicObjectPool(config.dynamicObjectCount),
		sceneSkinnedMeshPool(config.skinnedMeshCount),
		sceneSkinnedObjectPool(config.skinnedObjectCount),
		sceneArmaturePool(config.armatureCount),
		sceneTexturePool(config.textureCount),
		sceneMaterialPool(config.materialCount),
		sceneLightPool(config.lightCount),
		sceneBones(new Bone[config.boneCount]{})
	{
		// Make sure that every call which can fail calls destroy()
		// before throwing an error
		// We want to be sure that if the constructor ever fails, all
		// resources it allocated are deallocated by the time control
		// is handed back to the caller

		// VBV and IBV structs SizeInBytes member is a UINT
		if(sceneStaticVertexAllocator.getSize() > UINT_MAX) RIN_ERROR("Scene static vertex buffer size exceeded UINT_MAX");
		if(sceneDynamicVertexAllocator.getSize() > UINT_MAX) RIN_ERROR("Scene dynamic vertex buffer size exceeded UINT_MAX");
		if(sceneSkinnedVertexAllocator.getSize() > UINT_MAX) RIN_ERROR("Scene skinned vertex buffer size exceeded UINT_MAX");
		if(sceneStaticIndexAllocator.getSize() > UINT_MAX) RIN_ERROR("Scene static index buffer size exceeded UINT_MAX");
		if(sceneDynamicIndexAllocator.getSize() > UINT_MAX) RIN_ERROR("Scene dynamic index buffer size exceeded UINT_MAX");
		if(sceneSkinnedIndexAllocator.getSize() > UINT_MAX) RIN_ERROR("Scene skinned index buffer size exceeded UINT_MAX");

		// DRAW_INDEXED_ARGUMENTS has an int32 index offset, so we can't allow the full uint32 range
		if(config.staticVertexCount > INT32_MAX) RIN_ERROR("Maximum allowable static vertex count is INT32_MAX");
		if(config.dynamicVertexCount > INT32_MAX) RIN_ERROR("Maximum allowable dynamic vertex count is INT32_MAX");
		if(config.skinnedVertexCount > INT32_MAX) RIN_ERROR("Maximum allowable skinned vertex count is INT32_MAX");
		if(config.staticObjectCount % CULL_THREAD_GROUP_SIZE) RIN_ERROR("Static object count must be a multiple of 128");
		if(config.dynamicObjectCount % CULL_THREAD_GROUP_SIZE) RIN_ERROR("Dynamic object count must be a multiple of 128");
		if(config.skinnedObjectCount % CULL_THREAD_GROUP_SIZE) RIN_ERROR("Skinned object count must be a multiple of 128");

		if(settings.backBufferCount < 2) RIN_ERROR("Back buffer count must be at least 2");

		if(!GetWindowRect(hwnd, &hwndRect)) RIN_ERROR("Failed to get hwnd rect");

		HRESULT result;
	#ifdef RIN_DEBUG
		// Enable debug layer
		result = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
		if(FAILED(result)) RIN_ERROR("Failed to create debug layer");
		debug->EnableDebugLayer();
		debug->SetEnableGPUBasedValidation(true);
		debug->SetEnableSynchronizedCommandQueueValidation(true);
	#endif

		// Create device
		result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to create d3d12 device");
		}
		RIN_DEBUG_NAME(device, "Device");

		rtvHeapStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		dsvHeapStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		cbvsrvuavHeapStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// Debug queries
	#ifdef RIN_DEBUG
		// Create debug query buffer
		D3D12_HEAP_PROPERTIES debugHeapProperties{};
		debugHeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
		debugHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		debugHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		debugHeapProperties.CreationNodeMask = 0;
		debugHeapProperties.VisibleNodeMask = 0;

		D3D12_RESOURCE_DESC debugResourceDesc{};
		debugResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		debugResourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		debugResourceDesc.Width = DEBUG_QUERY_PIPELINE_COUNT * sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
		debugResourceDesc.Height = 1;
		debugResourceDesc.DepthOrArraySize = 1;
		debugResourceDesc.MipLevels = 1;
		debugResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		debugResourceDesc.SampleDesc.Count = 1;
		debugResourceDesc.SampleDesc.Quality = 0;
		debugResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		debugResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		result = device->CreateCommittedResource(
			&debugHeapProperties,
			D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
			&debugResourceDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&debugQueryDataBuffer)
		);
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to create debug query data buffer");
		}
		RIN_DEBUG_NAME(debugQueryDataBuffer, "Debug Query Data Buffer");

		// Map the debug query data buffer
		result = debugQueryDataBuffer->Map(0, nullptr, (void**)&debugQueryData);
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to map debug query data buffer");
		}

		// Create debug query pipeline heap
		D3D12_QUERY_HEAP_DESC queryHeapDesc{};
		queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
		queryHeapDesc.Count = DEBUG_QUERY_PIPELINE_COUNT;
		queryHeapDesc.NodeMask = 0;

		result = device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&debugQueryPipelineHeap));
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to create debug query pipeline heap");
		}
		RIN_DEBUG_NAME(debugQueryPipelineHeap, "Debug Query Pipeline Heap");
	#endif

		// Create graphics command queue
		D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
		commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
		commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		commandQueueDesc.NodeMask = 0;

		result = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&graphicsQueue));
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to create graphics command queue");
		}
		RIN_DEBUG_NAME(graphicsQueue, "Graphics Queue");

		// Create graphics fence
		graphicsFenceValue = 0;
		result = device->CreateFence(graphicsFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&graphicsFence));
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to create graphics fence");
		}
		RIN_DEBUG_NAME(graphicsFence, "Graphics Fence");

		// Create dxgi factory
		IDXGIFactory4* factory;
		result = CreateDXGIFactory2(
		#ifdef RIN_DEBUG
			DXGI_CREATE_FACTORY_DEBUG,
		#else
			0,
		#endif
			IID_PPV_ARGS(&factory)
		);
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to create dxgi factory");
		}

		// Create swap chain
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		swapChainDesc.Width = 0;
		swapChainDesc.Height = 0;
		swapChainDesc.Format = BACK_BUFFER_FORMAT;
		swapChainDesc.Stereo = false;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = settings.backBufferCount;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

		IDXGISwapChain1* baseSwapChain;
		result = factory->CreateSwapChainForHwnd(
			graphicsQueue,
			hwnd,
			&swapChainDesc,
			nullptr,
			nullptr,
			&baseSwapChain
		);
		if(FAILED(result)) {
			factory->Release();
			destroy();
			RIN_ERROR("Failed to create swap chain");
		}

		// Prevent legacy alt+enter
		result = factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
		factory->Release();
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to make window association");
		}

		// Upgrade swap chain
		result = baseSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain));
		baseSwapChain->Release();
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to upgrade swap chain");
		}

		// Set swap chain dependent values
		backBufferIndex = swapChain->GetCurrentBackBufferIndex();

		result = swapChain->GetDesc1(&swapChainDesc);
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to get the swap chain desc");
		}

		backBufferViewport.TopLeftX = 0.0f;
		backBufferViewport.TopLeftY = 0.0f;
		backBufferViewport.Width = (float)swapChainDesc.Width;
		backBufferViewport.Height = (float)swapChainDesc.Height;
		backBufferViewport.MinDepth = 0.0f;
		backBufferViewport.MaxDepth = 1.0f;
		backBufferScissorRect.left = 0;
		backBufferScissorRect.top = 0;
		backBufferScissorRect.right = swapChainDesc.Width;
		backBufferScissorRect.bottom = swapChainDesc.Height;

		// Create back buffer handles
		try {
			createSwapChainDependencies();
		} catch(...) {
			destroy();
			throw;
		}

		// Compute
		
		// Create queue
		commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

		result = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&computeQueue));
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to create compute command queue");
		}
		RIN_DEBUG_NAME(computeQueue, "Compute Queue");

		// Create fence
		computeFenceValue = 0;
		result = device->CreateFence(computeFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&computeFence));
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to create compute fence");
		}
		RIN_DEBUG_NAME(computeFence, "Compute Fence");

		// Resource uploading

		// Create copy command queues and fences
		commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;

		for(uint32_t i = 0; i < COPY_QUEUE_COUNT; ++i) {
			// Create queue
			result = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&copyQueues[i]));
			if(FAILED(result)) {
				destroy();
				RIN_ERROR("Failed to create copy command queue");
			}
			RIN_DEBUG_NAME(copyQueues[i], "Copy Queue");

			// Create fence
			copyFenceValues[i] = 0;
			result = device->CreateFence(copyFenceValues[i], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copyFences[i]));
			if(FAILED(result)) {
				destroy();
				RIN_ERROR("Failed to create copy fence");
			}
			RIN_DEBUG_NAME(copyFences[i], "Copy Fence");
		}

		// Upload offsets
		uploadCameraOffset = 0;
		constexpr uint64_t uploadCameraSize = sizeof(D3D12CameraData);
		
		uploadDynamicObjectOffset = uploadCameraOffset + uploadCameraSize;
		const uint64_t uploadDynamicObjectSize = config.dynamicObjectCount * sizeof(D3D12DynamicObjectData);

		uploadBoneOffset = uploadDynamicObjectOffset + uploadDynamicObjectSize;
		const uint64_t uploadBoneSize = config.boneCount * sizeof(D3D12BoneData);

		uploadLightOffset = uploadBoneOffset + uploadBoneSize;
		const uint64_t uploadLightSize = config.lightCount * sizeof(D3D12LightData);
		
		uploadStreamOffset = uploadLightOffset + uploadLightSize;

		if(UINT64_MAX - uploadStreamOffset < config.uploadStreamSize)
			RIN_ERROR("Upload buffer size exceeded UINT64_MAX");

		// Create a committed upload resource
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProperties.CreationNodeMask = 0;
		heapProperties.VisibleNodeMask = 0;

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		resourceDesc.Width = uploadStreamOffset + config.uploadStreamSize;
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		result = device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&uploadBuffer)
		);
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to create data upload buffer");
		}
		RIN_DEBUG_NAME(uploadBuffer, "Upload Buffer");

		// Map the data upload buffer
		D3D12_RANGE range{};
		result = uploadBuffer->Map(0, &range, (void**)&uploadBufferData);
		if(FAILED(result)) {
			destroy();
			RIN_ERROR("Failed to map data upload buffer");
		}

		try {
			createUploadStream();
			createScenePipeline();
		} catch(...) {
			destroy();
			throw;
		}
	}

	D3D12Renderer::~D3D12Renderer() {
		wait(); // May throw, but if it does we are in big trouble so let it terminate
		destroy();
	}

	void D3D12Renderer::destroy() noexcept {
	#ifdef RIN_DEBUG
		if(debug) {
			debug->Release();
			debug = nullptr;
		}
	#endif
		if(device) {
			device->Release();
			device = nullptr;
		}
	#ifdef RIN_DEBUG
		if(debugQueryDataBuffer) {
			D3D12_RANGE range{};
			debugQueryDataBuffer->Unmap(0, &range);
			debugQueryDataBuffer->Release();
			debugQueryDataBuffer = nullptr;
		}
		if(debugQueryPipelineHeap) {
			debugQueryPipelineHeap->Release();
			debugQueryPipelineHeap = nullptr;
		}
	#endif
		if(graphicsQueue) {
			graphicsQueue->Release();
			graphicsQueue = nullptr;
		}
		if(graphicsFence) {
			graphicsFence->Release();
			graphicsFence = nullptr;
		}
		if(swapChain) {
			swapChain->Release();
			swapChain = nullptr;
		}
		destroySwapChainDependencies();
		if(computeQueue) {
			computeQueue->Release();
			computeQueue = nullptr;
		}
		if(computeFence) {
			computeFence->Release();
			computeFence = nullptr;
		}
		for(uint32_t i = 0; i < COPY_QUEUE_COUNT; ++i) {
			if(copyQueues[i]) {
				copyQueues[i]->Release();
				copyQueues[i] = nullptr;
			}
			if(copyFences[i]) {
				copyFences[i]->Release();
				copyFences[i] = nullptr;
			}
		}
		if(uploadBuffer) {
			uploadBuffer->Unmap(0, nullptr);
			uploadBuffer->Release();
			uploadBuffer = nullptr;
		}
		destroyUploadStream();
		destroyScenePipeline();
	}

	void D3D12Renderer::createUploadStream() {
		// Create upload update command allocator
		HRESULT result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&uploadUpdateCommandAllocator));
		if(FAILED(result)) RIN_ERROR("Failed to create upload update command allocator");
		RIN_DEBUG_NAME(uploadUpdateCommandAllocator, "Upload Update Command Allocator");

		// Create closed upload update command list
		result = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_COPY, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&uploadUpdateCommandList));
		if(FAILED(result)) RIN_ERROR("Failed to create upload update command list");
		RIN_DEBUG_NAME(uploadUpdateCommandList, "Upload Update Command List");

		for(uint32_t i = 0; i < COPY_QUEUE_COUNT; ++i)
			uploadStreamThreads[i] = std::thread(&D3D12Renderer::uploadStreamWork, this, i);
	}

	void D3D12Renderer::destroyUploadStream() noexcept {
		if(uploadUpdateCommandAllocator) {
			uploadUpdateCommandAllocator->Release();
			uploadUpdateCommandAllocator = nullptr;
		}
		if(uploadUpdateCommandList) {
			uploadUpdateCommandList->Release();
			uploadUpdateCommandList = nullptr;
		}

		// Let upload stream threads know to stop
		uploadStreamTerminate = true;
		uploadStreamBarrier.arrive_and_drop();

		for(uint32_t i = 0; i < COPY_QUEUE_COUNT; ++i)
			if(uploadStreamThreads[i].joinable()) uploadStreamThreads[i].join();
	}

	void D3D12Renderer::createScenePipeline() {
		// Create culling descriptor heap
		D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
		descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		descriptorHeapDesc.NumDescriptors = SCENE_TEXTURE_SRV_OFFSET + config.textureCount;
		descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		descriptorHeapDesc.NodeMask = 0;

		HRESULT result = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&sceneDescHeap));
		if(FAILED(result)) RIN_ERROR("Failed to create scene descriptor heap");
		RIN_DEBUG_NAME(sceneDescHeap, "Scene Descriptor Heap");

		// Depth MIP Mapping

		// Create depth MIP mapping root signature
		// Located in DepthMIPCS.hlsl
		result = device->CreateRootSignature(0, RINShaderBytesDepthMIPCS, sizeof(RINShaderBytesDepthMIPCS), IID_PPV_ARGS(&depthMIPRootSignature));
		if(FAILED(result)) RIN_ERROR("Failed to create depth MIP mapping root signature");
		RIN_DEBUG_NAME(depthMIPRootSignature, "Depth MIP Root Signature");

		// Create depth MIP mapping pipeline state
		D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc{};
		computePipelineStateDesc.pRootSignature = depthMIPRootSignature;
		computePipelineStateDesc.CS.pShaderBytecode = RINShaderBytesDepthMIPCS;
		computePipelineStateDesc.CS.BytecodeLength = sizeof(RINShaderBytesDepthMIPCS);
		computePipelineStateDesc.NodeMask = 0;
		computePipelineStateDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		result = device->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&depthMIPPipelineState));
		if(FAILED(result)) RIN_ERROR("Failed to create depth MIP mapping pipeline state");
		RIN_DEBUG_NAME(depthMIPPipelineState, "Depth MIP Pipeline State");

		// Create depth MIP mapping command allocator
		result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&depthMIPCommandAllocator));
		if(FAILED(result)) RIN_ERROR("Failed to create depth MIP mapping command allocator");
		RIN_DEBUG_NAME(depthMIPCommandAllocator, "Depth MIP Command Allocator");

		// Create closed depth MIP mapping command list
		result = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&depthMIPCommandList));
		if(FAILED(result)) RIN_ERROR("Failed to create depth MIP mapping command list");
		RIN_DEBUG_NAME(depthMIPCommandList, "Depth MIP Command List");

		// Culling

		// Create static culling root signature
		// Located in CullStaticCS.hlsl
		result = device->CreateRootSignature(0, RINShaderBytesCullStaticCS, sizeof(RINShaderBytesCullStaticCS), IID_PPV_ARGS(&cullStaticRootSignature));
		if(FAILED(result)) RIN_ERROR("Failed to create static culling root signature");
		RIN_DEBUG_NAME(cullStaticRootSignature, "Cull Static Root Signature");

		// Create static culling pipeline state
		computePipelineStateDesc.pRootSignature = cullStaticRootSignature;
		computePipelineStateDesc.CS.pShaderBytecode = RINShaderBytesCullStaticCS;
		computePipelineStateDesc.CS.BytecodeLength = sizeof(RINShaderBytesCullStaticCS);

		result = device->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&cullStaticPipelineState));
		if(FAILED(result)) RIN_ERROR("Failed to create static culling pipeline state");
		RIN_DEBUG_NAME(cullStaticPipelineState, "Cull Static Pipeline State");

		// Create dynamic culling root signature
		// Located in CullDynamicCS.hlsl
		result = device->CreateRootSignature(0, RINShaderBytesCullDynamicCS, sizeof(RINShaderBytesCullDynamicCS), IID_PPV_ARGS(&cullDynamicRootSignature));
		if(FAILED(result)) RIN_ERROR("Failed to create dynamic culling root signature");
		RIN_DEBUG_NAME(cullDynamicRootSignature, "Cull Dynamic Root Signature");

		// Create dynamic culling pipeline state
		computePipelineStateDesc.pRootSignature = cullDynamicRootSignature;
		computePipelineStateDesc.CS.pShaderBytecode = RINShaderBytesCullDynamicCS;
		computePipelineStateDesc.CS.BytecodeLength = sizeof(RINShaderBytesCullDynamicCS);

		result = device->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&cullDynamicPipelineState));
		if(FAILED(result)) RIN_ERROR("Failed to create dynamic culling pipeline state");
		RIN_DEBUG_NAME(cullDynamicPipelineState, "Cull Dynamic Pipeline State");

		// Create skinned culling root signature
		// Located in CullSkinnedCS.hlsl
		result = device->CreateRootSignature(0, RINShaderBytesCullSkinnedCS, sizeof(RINShaderBytesCullSkinnedCS), IID_PPV_ARGS(&cullSkinnedRootSignature));
		if(FAILED(result)) RIN_ERROR("Failed to create skinned culling root signature");
		RIN_DEBUG_NAME(cullSkinnedRootSignature, "Cull Skinned Root Signature");

		// Create skinned culling pipeline state
		computePipelineStateDesc.pRootSignature = cullSkinnedRootSignature;
		computePipelineStateDesc.CS.pShaderBytecode = RINShaderBytesCullSkinnedCS;
		computePipelineStateDesc.CS.BytecodeLength = sizeof(RINShaderBytesCullSkinnedCS);

		result = device->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&cullSkinnedPipelineState));
		if(FAILED(result)) RIN_ERROR("Failed to create skinned culling pipeline state");
		RIN_DEBUG_NAME(cullSkinnedPipelineState, "Cull Skinned Pipeline State");

		// Create static culling command allocator
		result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&cullStaticCommandAllocator));
		if(FAILED(result)) RIN_ERROR("Failed to create static culling command allocator");
		RIN_DEBUG_NAME(cullStaticCommandAllocator, "Cull Static Command Allocator");

		// Create closed static culling command list
		result = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cullStaticCommandList));
		if(FAILED(result)) RIN_ERROR("Failed to create static culling command list");
		RIN_DEBUG_NAME(cullStaticCommandList, "Cull Static Command List");

		// Create dynamic culling command allocator
		result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&cullDynamicCommandAllocator));
		if(FAILED(result)) RIN_ERROR("Failed to create dynamic culling command allocator");
		RIN_DEBUG_NAME(cullDynamicCommandAllocator, "Cull Dynamic Command Allocator");

		// Create closed dynamic culling command list
		result = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cullDynamicCommandList));
		if(FAILED(result)) RIN_ERROR("Failed to create dynamic culling command list");
		RIN_DEBUG_NAME(cullDynamicCommandList, "Cull Dynamic Command List");

		// Create skinned culling command allocator
		result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&cullSkinnedCommandAllocator));
		if(FAILED(result)) RIN_ERROR("Failed to create skinned culling command allocator");
		RIN_DEBUG_NAME(cullSkinnedCommandAllocator, "Cull Skinned Command Allocator");

		// Create closed skinned culling command list
		result = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cullSkinnedCommandList));
		if(FAILED(result)) RIN_ERROR("Failed to create skinned culling command list");
		RIN_DEBUG_NAME(cullSkinnedCommandList, "Cull Skinned Command List");

		// Light clustering
		
		// Create light clustering root signature
		// Located in LightClusterCS.hlsl
		result = device->CreateRootSignature(0, RINShaderBytesLightClusterCS, sizeof(RINShaderBytesLightClusterCS), IID_PPV_ARGS(&lightClusterRootSignature));
		if(FAILED(result)) RIN_ERROR("Failed to create light clustering root signature");
		RIN_DEBUG_NAME(lightClusterRootSignature, "Light Cluster Root Signature");

		// Create light clustering pipeline state
		computePipelineStateDesc.pRootSignature = lightClusterRootSignature;
		computePipelineStateDesc.CS.pShaderBytecode = RINShaderBytesLightClusterCS;
		computePipelineStateDesc.CS.BytecodeLength = sizeof(RINShaderBytesLightClusterCS);

		result = device->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&lightClusterPipelineState));
		if(FAILED(result)) RIN_ERROR("Failed to create light clustering pipeline state");
		RIN_DEBUG_NAME(lightClusterPipelineState, "Light Cluster Pipeline State");

		// Create light clustering command allocator
		result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&lightClusterCommandAllocator));
		if(FAILED(result)) RIN_ERROR("Failed to create light clustering command allocator");
		RIN_DEBUG_NAME(lightClusterCommandAllocator, "Light Cluster Command Allocator");

		// Create closed light clustering command list
		result = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&lightClusterCommandList));
		if(FAILED(result)) RIN_ERROR("Failed to create light clustering command list");
		RIN_DEBUG_NAME(lightClusterCommandList, "Light Cluster Command List");

		// Scene rendering
		
		/*
		0: (RTV) sceneBackBuffer
		*/
		// Create scene rendering render target view descriptor heap
		descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		descriptorHeapDesc.NumDescriptors = 1;
		descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		descriptorHeapDesc.NodeMask = 0;

		result = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&sceneRTVDescHeap));
		if(FAILED(result)) RIN_ERROR("Failed to create scene rendering rtv descriptor heap");
		RIN_DEBUG_NAME(sceneRTVDescHeap, "Scene RTV Descriptor Heap");

		/*
		0: (DSV) sceneDepthBuffer
		*/
		// Create scene rendering depth stencil view descriptor heap
		descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		descriptorHeapDesc.NumDescriptors = 1;
		descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		descriptorHeapDesc.NodeMask = 0;

		result = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&sceneDSVDescHeap));
		if(FAILED(result)) RIN_ERROR("Failed to create scene rendering dsv descriptor heap");
		RIN_DEBUG_NAME(sceneDSVDescHeap, "Scene DSV Descriptor Heap");

		// Create static scene rendering root signature
		// Located in PBRStaticVS.hlsl
		result = device->CreateRootSignature(0, RINShaderBytesPBRStaticVS, sizeof(RINShaderBytesPBRStaticVS), IID_PPV_ARGS(&sceneStaticRootSignature));
		if(FAILED(result)) RIN_ERROR("Failed to create static scene rendering root signature");
		RIN_DEBUG_NAME(sceneStaticRootSignature, "Scene Static Root Signature");
		
		// Create static scene rendering command signature
		D3D12_INDIRECT_ARGUMENT_DESC sceneStaticIndirectArguments[2]{};
		sceneStaticIndirectArguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
		sceneStaticIndirectArguments[0].Constant.RootParameterIndex = 0;
		sceneStaticIndirectArguments[0].Constant.DestOffsetIn32BitValues = 0;
		sceneStaticIndirectArguments[0].Constant.Num32BitValuesToSet = 1;
		sceneStaticIndirectArguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

		D3D12_COMMAND_SIGNATURE_DESC commandSignature{};
		commandSignature.ByteStride = SCENE_STATIC_COMMAND_SIZE;
		commandSignature.NumArgumentDescs = _countof(sceneStaticIndirectArguments);
		commandSignature.pArgumentDescs = sceneStaticIndirectArguments;
		commandSignature.NodeMask = 0;

		result = device->CreateCommandSignature(&commandSignature, sceneStaticRootSignature, IID_PPV_ARGS(&sceneStaticCommandSignature));
		if(FAILED(result)) RIN_ERROR("Failed to create static scene rendering command signature");
		RIN_DEBUG_NAME(sceneStaticCommandSignature, "Scene Static Command Signature");

		// Create static pbr scene rendering pipeline state
		D3D12_INPUT_ELEMENT_DESC staticPBRInputElementDescs[3]{};
		staticPBRInputElementDescs[0].SemanticName = "POSITION";
		staticPBRInputElementDescs[0].SemanticIndex = 0;
		staticPBRInputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		staticPBRInputElementDescs[0].InputSlot = 0;
		staticPBRInputElementDescs[0].AlignedByteOffset = 0;
		staticPBRInputElementDescs[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		staticPBRInputElementDescs[0].InstanceDataStepRate = 0;
		staticPBRInputElementDescs[1].SemanticName = "NORMAL";
		staticPBRInputElementDescs[1].SemanticIndex = 0;
		staticPBRInputElementDescs[1].Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		staticPBRInputElementDescs[1].InputSlot = 0;
		staticPBRInputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		staticPBRInputElementDescs[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		staticPBRInputElementDescs[1].InstanceDataStepRate = 0;
		staticPBRInputElementDescs[2].SemanticName = "TANGENT";
		staticPBRInputElementDescs[2].SemanticIndex = 0;
		staticPBRInputElementDescs[2].Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		staticPBRInputElementDescs[2].InputSlot = 0;
		staticPBRInputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		staticPBRInputElementDescs[2].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		staticPBRInputElementDescs[2].InstanceDataStepRate = 0;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC scenePipelineStateDesc{};
		scenePipelineStateDesc.pRootSignature = nullptr;
		scenePipelineStateDesc.VS.pShaderBytecode = RINShaderBytesPBRStaticVS;
		scenePipelineStateDesc.VS.BytecodeLength = sizeof(RINShaderBytesPBRStaticVS);
		scenePipelineStateDesc.PS.pShaderBytecode = RINShaderBytesPBRPS;
		scenePipelineStateDesc.PS.BytecodeLength = sizeof(RINShaderBytesPBRPS);
		scenePipelineStateDesc.BlendState.AlphaToCoverageEnable = false;
		scenePipelineStateDesc.BlendState.IndependentBlendEnable = false;
		scenePipelineStateDesc.BlendState.RenderTarget[0].BlendEnable = false;
		scenePipelineStateDesc.BlendState.RenderTarget[0].LogicOpEnable = false;
		scenePipelineStateDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		scenePipelineStateDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
		scenePipelineStateDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		scenePipelineStateDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		scenePipelineStateDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		scenePipelineStateDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		scenePipelineStateDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
		scenePipelineStateDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		scenePipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		scenePipelineStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		scenePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		scenePipelineStateDesc.RasterizerState.FrontCounterClockwise = false;
		scenePipelineStateDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		scenePipelineStateDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		scenePipelineStateDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		scenePipelineStateDesc.RasterizerState.DepthClipEnable = true;
		scenePipelineStateDesc.RasterizerState.MultisampleEnable = false;
		scenePipelineStateDesc.RasterizerState.AntialiasedLineEnable = false;
		scenePipelineStateDesc.RasterizerState.ForcedSampleCount = 0;
		scenePipelineStateDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		scenePipelineStateDesc.DepthStencilState.DepthEnable = true;
		scenePipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		scenePipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		scenePipelineStateDesc.DepthStencilState.StencilEnable = false;
		scenePipelineStateDesc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
		scenePipelineStateDesc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
		scenePipelineStateDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		scenePipelineStateDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		scenePipelineStateDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		scenePipelineStateDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		scenePipelineStateDesc.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		scenePipelineStateDesc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		scenePipelineStateDesc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		scenePipelineStateDesc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		scenePipelineStateDesc.InputLayout.pInputElementDescs = staticPBRInputElementDescs;
		scenePipelineStateDesc.InputLayout.NumElements = _countof(staticPBRInputElementDescs);
		scenePipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		scenePipelineStateDesc.NumRenderTargets = 1;
		scenePipelineStateDesc.RTVFormats[0] = BACK_BUFFER_FORMAT;
		scenePipelineStateDesc.DSVFormat = DEPTH_FORMAT_DSV;
		scenePipelineStateDesc.SampleDesc.Count = 1;
		scenePipelineStateDesc.SampleDesc.Quality = 0;
		scenePipelineStateDesc.NodeMask = 0;
		scenePipelineStateDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		result = device->CreateGraphicsPipelineState(&scenePipelineStateDesc, IID_PPV_ARGS(&sceneStaticPBRPipelineState));
		if(FAILED(result)) RIN_ERROR("Failed to create static pbr scene rendering pipeline state");
		RIN_DEBUG_NAME(sceneStaticPBRPipelineState, "Scene Static PBR Pipeline State");

		// Create dynamic scene rendering root signature
		// Located in PBRDynamicVS.hlsl
		result = device->CreateRootSignature(0, RINShaderBytesPBRDynamicVS, sizeof(RINShaderBytesPBRDynamicVS), IID_PPV_ARGS(&sceneDynamicRootSignature));
		if(FAILED(result)) RIN_ERROR("Failed to create dynamic scene rendering root signature");
		RIN_DEBUG_NAME(sceneDynamicRootSignature, "Scene Dynamic Root Signature");

		// Create dynamic scene rendering command signature
		D3D12_INDIRECT_ARGUMENT_DESC sceneDynamicIndirectArguments[2]{};
		sceneDynamicIndirectArguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
		sceneDynamicIndirectArguments[0].Constant.RootParameterIndex = 0;
		sceneDynamicIndirectArguments[0].Constant.DestOffsetIn32BitValues = 0;
		sceneDynamicIndirectArguments[0].Constant.Num32BitValuesToSet = 1;
		sceneDynamicIndirectArguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

		commandSignature.ByteStride = SCENE_DYNAMIC_COMMAND_SIZE;
		commandSignature.NumArgumentDescs = _countof(sceneDynamicIndirectArguments);
		commandSignature.pArgumentDescs = sceneDynamicIndirectArguments;
		commandSignature.NodeMask = 0;

		result = device->CreateCommandSignature(&commandSignature, sceneDynamicRootSignature, IID_PPV_ARGS(&sceneDynamicCommandSignature));
		if(FAILED(result)) RIN_ERROR("Failed to create dynamic scene rendering command signature");
		RIN_DEBUG_NAME(sceneDynamicCommandSignature, "Scene Dynamic Command Signature");

		// Create dynamic pbr scene rendering pipeline state
		D3D12_INPUT_ELEMENT_DESC dynamicPBRInputElementDescs[3]{};
		dynamicPBRInputElementDescs[0].SemanticName = "POSITION";
		dynamicPBRInputElementDescs[0].SemanticIndex = 0;
		dynamicPBRInputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		dynamicPBRInputElementDescs[0].InputSlot = 0;
		dynamicPBRInputElementDescs[0].AlignedByteOffset = 0;
		dynamicPBRInputElementDescs[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		dynamicPBRInputElementDescs[0].InstanceDataStepRate = 0;
		dynamicPBRInputElementDescs[1].SemanticName = "NORMAL";
		dynamicPBRInputElementDescs[1].SemanticIndex = 0;
		dynamicPBRInputElementDescs[1].Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		dynamicPBRInputElementDescs[1].InputSlot = 0;
		dynamicPBRInputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		dynamicPBRInputElementDescs[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		dynamicPBRInputElementDescs[1].InstanceDataStepRate = 0;
		dynamicPBRInputElementDescs[2].SemanticName = "TANGENT";
		dynamicPBRInputElementDescs[2].SemanticIndex = 0;
		dynamicPBRInputElementDescs[2].Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		dynamicPBRInputElementDescs[2].InputSlot = 0;
		dynamicPBRInputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		dynamicPBRInputElementDescs[2].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		dynamicPBRInputElementDescs[2].InstanceDataStepRate = 0;

		scenePipelineStateDesc.VS.pShaderBytecode = RINShaderBytesPBRDynamicVS;
		scenePipelineStateDesc.VS.BytecodeLength = sizeof(RINShaderBytesPBRDynamicVS);
		scenePipelineStateDesc.PS.pShaderBytecode = RINShaderBytesPBRPS;
		scenePipelineStateDesc.PS.BytecodeLength = sizeof(RINShaderBytesPBRPS);
		scenePipelineStateDesc.InputLayout.pInputElementDescs = dynamicPBRInputElementDescs;
		scenePipelineStateDesc.InputLayout.NumElements = _countof(dynamicPBRInputElementDescs);

		result = device->CreateGraphicsPipelineState(&scenePipelineStateDesc, IID_PPV_ARGS(&sceneDynamicPBRPipelineState));
		if(FAILED(result)) RIN_ERROR("Failed to create dynamic pbr scene rendering pipeline state");
		RIN_DEBUG_NAME(sceneDynamicPBRPipelineState, "Scene Dynamic PBR Pipeline State");

		// Create skinned scene rendering root signature
		// Located in PBRSkinnedVS.hlsl
		result = device->CreateRootSignature(0, RINShaderBytesPBRSkinnedVS, sizeof(RINShaderBytesPBRSkinnedVS), IID_PPV_ARGS(&sceneSkinnedRootSignature));
		if(FAILED(result)) RIN_ERROR("Failed to create skinned scene rendering root signature");
		RIN_DEBUG_NAME(sceneSkinnedRootSignature, "Scene Skinned Root Signature");

		// Create skinned scene rendering command signature
		D3D12_INDIRECT_ARGUMENT_DESC sceneSkinnedIndirectArguments[2]{};
		sceneSkinnedIndirectArguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
		sceneSkinnedIndirectArguments[0].Constant.RootParameterIndex = 0;
		sceneSkinnedIndirectArguments[0].Constant.DestOffsetIn32BitValues = 0;
		sceneSkinnedIndirectArguments[0].Constant.Num32BitValuesToSet = 1;
		sceneSkinnedIndirectArguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

		commandSignature.ByteStride = SCENE_SKINNED_COMMAND_SIZE;
		commandSignature.NumArgumentDescs = _countof(sceneSkinnedIndirectArguments);
		commandSignature.pArgumentDescs = sceneSkinnedIndirectArguments;
		commandSignature.NodeMask = 0;

		result = device->CreateCommandSignature(&commandSignature, sceneSkinnedRootSignature, IID_PPV_ARGS(&sceneSkinnedCommandSignature));
		if(FAILED(result)) RIN_ERROR("Failed to create skinned scene rendering command signature");
		RIN_DEBUG_NAME(sceneSkinnedCommandSignature, "Scene Skinned Command Signature");

		// Create skinned pbr scene rendering pipeline state
		D3D12_INPUT_ELEMENT_DESC skinnedPBRInputElementDescs[5]{};
		skinnedPBRInputElementDescs[0].SemanticName = "POSITION";
		skinnedPBRInputElementDescs[0].SemanticIndex = 0;
		skinnedPBRInputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		skinnedPBRInputElementDescs[0].InputSlot = 0;
		skinnedPBRInputElementDescs[0].AlignedByteOffset = 0;
		skinnedPBRInputElementDescs[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		skinnedPBRInputElementDescs[0].InstanceDataStepRate = 0;
		skinnedPBRInputElementDescs[1].SemanticName = "NORMAL";
		skinnedPBRInputElementDescs[1].SemanticIndex = 0;
		skinnedPBRInputElementDescs[1].Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		skinnedPBRInputElementDescs[1].InputSlot = 0;
		skinnedPBRInputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		skinnedPBRInputElementDescs[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		skinnedPBRInputElementDescs[1].InstanceDataStepRate = 0;
		skinnedPBRInputElementDescs[2].SemanticName = "TANGENT";
		skinnedPBRInputElementDescs[2].SemanticIndex = 0;
		skinnedPBRInputElementDescs[2].Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		skinnedPBRInputElementDescs[2].InputSlot = 0;
		skinnedPBRInputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		skinnedPBRInputElementDescs[2].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		skinnedPBRInputElementDescs[2].InstanceDataStepRate = 0;
		skinnedPBRInputElementDescs[3].SemanticName = "BLENDINDICES";
		skinnedPBRInputElementDescs[3].SemanticIndex = 0;
		skinnedPBRInputElementDescs[3].Format = DXGI_FORMAT_R8G8B8A8_UINT;
		skinnedPBRInputElementDescs[3].InputSlot = 0;
		skinnedPBRInputElementDescs[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		skinnedPBRInputElementDescs[3].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		skinnedPBRInputElementDescs[3].InstanceDataStepRate = 0;
		skinnedPBRInputElementDescs[4].SemanticName = "BLENDWEIGHTS";
		skinnedPBRInputElementDescs[4].SemanticIndex = 0;
		skinnedPBRInputElementDescs[4].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		skinnedPBRInputElementDescs[4].InputSlot = 0;
		skinnedPBRInputElementDescs[4].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		skinnedPBRInputElementDescs[4].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		skinnedPBRInputElementDescs[4].InstanceDataStepRate = 0;

		scenePipelineStateDesc.VS.pShaderBytecode = RINShaderBytesPBRSkinnedVS;
		scenePipelineStateDesc.VS.BytecodeLength = sizeof(RINShaderBytesPBRSkinnedVS);
		scenePipelineStateDesc.PS.pShaderBytecode = RINShaderBytesPBRPS;
		scenePipelineStateDesc.PS.BytecodeLength = sizeof(RINShaderBytesPBRPS);
		scenePipelineStateDesc.InputLayout.pInputElementDescs = skinnedPBRInputElementDescs;
		scenePipelineStateDesc.InputLayout.NumElements = _countof(skinnedPBRInputElementDescs);

		result = device->CreateGraphicsPipelineState(&scenePipelineStateDesc, IID_PPV_ARGS(&sceneSkinnedPBRPipelineState));
		if(FAILED(result)) RIN_ERROR("Failed to create skinned pbr scene rendering pipeline state");
		RIN_DEBUG_NAME(sceneSkinnedPBRPipelineState, "Scene Skinned PBR Pipeline State");

		// Create static scene rendering command allocator
		result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&sceneStaticCommandAllocator));
		if(FAILED(result)) RIN_ERROR("Failed to create static scene rendering command allocator");
		RIN_DEBUG_NAME(sceneStaticCommandAllocator, "Scene Static Command Allocator");

		// Create closed static scene rendering command list
		result = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&sceneStaticCommandList));
		if(FAILED(result)) RIN_ERROR("Failed to create static scene rendering command list");
		RIN_DEBUG_NAME(sceneStaticCommandList, "Scene Static Command List");

		// Create dynamic scene rendering command allocator
		result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&sceneDynamicCommandAllocator));
		if(FAILED(result)) RIN_ERROR("Failed to create dynamic scene rendering command allocator");
		RIN_DEBUG_NAME(sceneDynamicCommandAllocator, "Scene Static Command Allocator");

		// Create closed dynamic scene rendering command list
		result = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&sceneDynamicCommandList));
		if(FAILED(result)) RIN_ERROR("Failed to create dynamic scene rendering command list");
		RIN_DEBUG_NAME(sceneDynamicCommandList, "Scene Dynamic Command List");

		// Create skinned scene rendering command allocator
		result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&sceneSkinnedCommandAllocator));
		if(FAILED(result)) RIN_ERROR("Failed to create skinned scene rendering command allocator");
		RIN_DEBUG_NAME(sceneSkinnedCommandAllocator, "Scene Skinned Command Allocator");

		// Create closed skinned scene rendering command list
		result = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&sceneSkinnedCommandList));
		if(FAILED(result)) RIN_ERROR("Failed to create skinned scene rendering command list");
		RIN_DEBUG_NAME(sceneSkinnedCommandList, "Scene Skinned Command List");

		// Skybox

		// Create skybox root signature
		// Located in SkyboxVS.hlsl
		result = device->CreateRootSignature(0, RINShaderBytesSkyboxVS, sizeof(RINShaderBytesSkyboxVS), IID_PPV_ARGS(&skyboxRootSignature));
		if(FAILED(result)) RIN_ERROR("Failed to create skybox root signature");
		RIN_DEBUG_NAME(skyboxRootSignature, "Skybox Root Signature");

		// Create skybox pipeline state
		D3D12_INPUT_ELEMENT_DESC skyboxInputElementDescs[1]{};
		skyboxInputElementDescs[0].SemanticName = "POSITION";
		skyboxInputElementDescs[0].SemanticIndex = 0;
		skyboxInputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		skyboxInputElementDescs[0].InputSlot = 0;
		skyboxInputElementDescs[0].AlignedByteOffset = 0;
		skyboxInputElementDescs[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		skyboxInputElementDescs[0].InstanceDataStepRate = 0;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC skyboxPipelineStateDesc{};
		skyboxPipelineStateDesc.pRootSignature = skyboxRootSignature;
		skyboxPipelineStateDesc.VS.pShaderBytecode = RINShaderBytesSkyboxVS;
		skyboxPipelineStateDesc.VS.BytecodeLength = sizeof(RINShaderBytesSkyboxVS);
		skyboxPipelineStateDesc.PS.pShaderBytecode = RINShaderBytesSkyboxPS;
		skyboxPipelineStateDesc.PS.BytecodeLength = sizeof(RINShaderBytesSkyboxPS);
		skyboxPipelineStateDesc.BlendState.AlphaToCoverageEnable = false;
		skyboxPipelineStateDesc.BlendState.IndependentBlendEnable = false;
		skyboxPipelineStateDesc.BlendState.RenderTarget[0].BlendEnable = false;
		skyboxPipelineStateDesc.BlendState.RenderTarget[0].LogicOpEnable = false;
		skyboxPipelineStateDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		skyboxPipelineStateDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
		skyboxPipelineStateDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		skyboxPipelineStateDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		skyboxPipelineStateDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		skyboxPipelineStateDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		skyboxPipelineStateDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
		skyboxPipelineStateDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		skyboxPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		skyboxPipelineStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		skyboxPipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		skyboxPipelineStateDesc.RasterizerState.FrontCounterClockwise = false;
		skyboxPipelineStateDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		skyboxPipelineStateDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		skyboxPipelineStateDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		skyboxPipelineStateDesc.RasterizerState.DepthClipEnable = false;
		skyboxPipelineStateDesc.RasterizerState.MultisampleEnable = false;
		skyboxPipelineStateDesc.RasterizerState.AntialiasedLineEnable = false;
		skyboxPipelineStateDesc.RasterizerState.ForcedSampleCount = 0;
		skyboxPipelineStateDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		skyboxPipelineStateDesc.DepthStencilState.DepthEnable = true;
		skyboxPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		skyboxPipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		skyboxPipelineStateDesc.DepthStencilState.StencilEnable = false;
		skyboxPipelineStateDesc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
		skyboxPipelineStateDesc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
		skyboxPipelineStateDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		skyboxPipelineStateDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		skyboxPipelineStateDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		skyboxPipelineStateDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		skyboxPipelineStateDesc.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		skyboxPipelineStateDesc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		skyboxPipelineStateDesc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		skyboxPipelineStateDesc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		skyboxPipelineStateDesc.InputLayout.pInputElementDescs = skyboxInputElementDescs;
		skyboxPipelineStateDesc.InputLayout.NumElements = _countof(skyboxInputElementDescs);
		skyboxPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		skyboxPipelineStateDesc.NumRenderTargets = 1;
		skyboxPipelineStateDesc.RTVFormats[0] = BACK_BUFFER_FORMAT;
		skyboxPipelineStateDesc.DSVFormat = DEPTH_FORMAT_DSV;
		skyboxPipelineStateDesc.SampleDesc.Count = 1;
		skyboxPipelineStateDesc.SampleDesc.Quality = 0;
		skyboxPipelineStateDesc.NodeMask = 0;
		skyboxPipelineStateDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		result = device->CreateGraphicsPipelineState(&skyboxPipelineStateDesc, IID_PPV_ARGS(&skyboxPipelineState));
		if(FAILED(result)) RIN_ERROR("Failed to create skybox pipeline state");
		RIN_DEBUG_NAME(skyboxPipelineState, "Skybox Pipeline State");

		// Create skybox command allocator
		result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&skyboxCommandAllocator));
		if(FAILED(result)) RIN_ERROR("Failed to create skybox command allocator");
		RIN_DEBUG_NAME(skyboxCommandAllocator, "Skybox Command Allocator");

		// Create closed skybox command list
		result = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&skyboxCommandList));
		if(FAILED(result)) RIN_ERROR("Failed to create skybox command list");
		RIN_DEBUG_NAME(skyboxCommandList, "Skybox Command List");

		// Post processing

		// Create post processing root signature
		// Located in PostVS.hlsl
		result = device->CreateRootSignature(0, RINShaderBytesPostVS, sizeof(RINShaderBytesPostVS), IID_PPV_ARGS(&postRootSignature));
		if(FAILED(result)) RIN_ERROR("Failed to create post processing root signature");
		RIN_DEBUG_NAME(postRootSignature, "Post Root Signature");

		// Create post processing pipeline state
		D3D12_INPUT_ELEMENT_DESC postInputElementDescs[2]{};
		postInputElementDescs[0].SemanticName = "POSITION";
		postInputElementDescs[0].SemanticIndex = 0;
		postInputElementDescs[0].Format = DXGI_FORMAT_R32G32_FLOAT;
		postInputElementDescs[0].InputSlot = 0;
		postInputElementDescs[0].AlignedByteOffset = 0;
		postInputElementDescs[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		postInputElementDescs[0].InstanceDataStepRate = 0;
		postInputElementDescs[1].SemanticName = "TEXCOORD";
		postInputElementDescs[1].SemanticIndex = 0;
		postInputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
		postInputElementDescs[1].InputSlot = 0;
		postInputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		postInputElementDescs[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		postInputElementDescs[1].InstanceDataStepRate = 0;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC postPipelineStateDesc{};
		postPipelineStateDesc.pRootSignature = nullptr;
		postPipelineStateDesc.VS.pShaderBytecode = RINShaderBytesPostVS;
		postPipelineStateDesc.VS.BytecodeLength = sizeof(RINShaderBytesPostVS);
		postPipelineStateDesc.PS.pShaderBytecode = RINShaderBytesPostPS;
		postPipelineStateDesc.PS.BytecodeLength = sizeof(RINShaderBytesPostPS);
		postPipelineStateDesc.BlendState.AlphaToCoverageEnable = false;
		postPipelineStateDesc.BlendState.IndependentBlendEnable = false;
		postPipelineStateDesc.BlendState.RenderTarget[0].BlendEnable = false;
		postPipelineStateDesc.BlendState.RenderTarget[0].LogicOpEnable = false;
		postPipelineStateDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		postPipelineStateDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
		postPipelineStateDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		postPipelineStateDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		postPipelineStateDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		postPipelineStateDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		postPipelineStateDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
		postPipelineStateDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		postPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		postPipelineStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		postPipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		postPipelineStateDesc.RasterizerState.FrontCounterClockwise = false;
		postPipelineStateDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		postPipelineStateDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		postPipelineStateDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		postPipelineStateDesc.RasterizerState.DepthClipEnable = false;
		postPipelineStateDesc.RasterizerState.MultisampleEnable = false;
		postPipelineStateDesc.RasterizerState.AntialiasedLineEnable = false;
		postPipelineStateDesc.RasterizerState.ForcedSampleCount = 0;
		postPipelineStateDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		postPipelineStateDesc.InputLayout.pInputElementDescs = postInputElementDescs;
		postPipelineStateDesc.InputLayout.NumElements = _countof(postInputElementDescs);
		postPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		postPipelineStateDesc.NumRenderTargets = 1;
		postPipelineStateDesc.RTVFormats[0] = BACK_BUFFER_FORMAT;
		postPipelineStateDesc.SampleDesc.Count = 1;
		postPipelineStateDesc.SampleDesc.Quality = 0;
		postPipelineStateDesc.NodeMask = 0;
		postPipelineStateDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		result = device->CreateGraphicsPipelineState(&postPipelineStateDesc, IID_PPV_ARGS(&postPipelineState));
		if(FAILED(result)) RIN_ERROR("Failed to create post processing pipeline state");
		RIN_DEBUG_NAME(postPipelineState, "Post Pipeline State");

		// Create post processing command allocator
		result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&postCommandAllocator));
		if(FAILED(result)) RIN_ERROR("Failed to create post processing command allocator");
		RIN_DEBUG_NAME(postCommandAllocator, "Post Command Allocator");

		// Create post processing command list
		// We are going to hijack this command list to do the
		// initialization upload so start it in the recording state
		result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, postCommandAllocator, nullptr, IID_PPV_ARGS(&postCommandList));
		if(FAILED(result)) RIN_ERROR("Failed to create post processing command list");
		RIN_DEBUG_NAME(postCommandList, "Post Command List");

		// Scene back buffer

		createSceneBackBuffer();

		sceneBackBufferViewport.TopLeftX = 0.0f;
		sceneBackBufferViewport.TopLeftY = 0.0f;
		sceneBackBufferViewport.MinDepth = 0.0f;
		sceneBackBufferViewport.MaxDepth = 1.0f;
		sceneBackBufferScissorRect.left = 0;
		sceneBackBufferScissorRect.top = 0;

		// Resource management

		// Heap offsets
		constexpr uint64_t zeroBufferOffset = 0;
		constexpr uint64_t zeroBufferSize = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

		constexpr uint64_t cameraBufferOffset = zeroBufferOffset + zeroBufferSize;
		constexpr uint64_t cameraBufferSize = ALIGN_TO(sizeof(D3D12CameraData), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		// Counter placed at start of buffer
		constexpr uint64_t staticCommandBufferOffset = cameraBufferOffset + cameraBufferSize;
		const uint64_t staticCommandBufferSize = ALIGN_TO(SCENE_STATIC_COMMAND_SIZE * (config.staticObjectCount + 1), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		// Counter placed at start of buffer
		const uint64_t dynamicCommandBufferOffset = staticCommandBufferOffset + staticCommandBufferSize;
		const uint64_t dynamicCommandBufferSize = ALIGN_TO(SCENE_DYNAMIC_COMMAND_SIZE * (config.dynamicObjectCount + 1), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		// Counter placed at start of buffer
		const uint64_t skinnedCommandBufferOffset = dynamicCommandBufferOffset + dynamicCommandBufferSize;
		const uint64_t skinnedCommandBufferSize = ALIGN_TO(SCENE_SKINNED_COMMAND_SIZE * (config.skinnedObjectCount + 1), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		// Geometry buffer and static vertex buffer share a resource
		constexpr uint64_t geometryBufferSize = (uint64_t)SCREEN_QUAD_SIZE + SKYBOX_SIZE;

		const uint64_t staticVertexBufferOffset = skinnedCommandBufferOffset + skinnedCommandBufferSize;
		const uint64_t staticVertexBufferSize = ALIGN_TO(sceneStaticVertexAllocator.getSize() + geometryBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		// Geometry is placed at end of static vertex buffer
		const uint64_t geometryBufferOffset = staticVertexBufferSize - geometryBufferSize;

		const uint64_t dynamicVertexBufferOffset = staticVertexBufferOffset + staticVertexBufferSize;
		const uint64_t dynamicVertexBufferSize = ALIGN_TO(sceneDynamicVertexAllocator.getSize(), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		const uint64_t skinnedVertexBufferOffset = dynamicVertexBufferOffset + dynamicVertexBufferSize;
		const uint64_t skinnedVertexBufferSize = ALIGN_TO(sceneSkinnedVertexAllocator.getSize(), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		const uint64_t staticIndexBufferOffset = skinnedVertexBufferOffset + skinnedVertexBufferSize;
		const uint64_t staticIndexBufferSize = ALIGN_TO(sceneStaticIndexAllocator.getSize(), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		const uint64_t dynamicIndexBufferOffset = staticIndexBufferOffset + staticIndexBufferSize;
		const uint64_t dynamicIndexBufferSize = ALIGN_TO(sceneDynamicIndexAllocator.getSize(), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		const uint64_t skinnedIndexBufferOffset = dynamicIndexBufferOffset + dynamicIndexBufferSize;
		const uint64_t skinnedIndexBufferSize = ALIGN_TO(sceneSkinnedIndexAllocator.getSize(), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		const uint64_t staticObjectBufferOffset = skinnedIndexBufferOffset + skinnedIndexBufferSize;
		const uint64_t staticObjectBufferSize = ALIGN_TO(config.staticObjectCount * sizeof(D3D12StaticObjectData), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		const uint64_t dynamicObjectBufferOffset = staticObjectBufferOffset + staticObjectBufferSize;
		const uint64_t dynamicObjectBufferSize = ALIGN_TO(config.dynamicObjectCount * sizeof(D3D12DynamicObjectData), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		const uint64_t skinnedObjectBufferOffset = dynamicObjectBufferOffset + dynamicObjectBufferSize;
		const uint64_t skinnedObjectBufferSize = ALIGN_TO(config.skinnedObjectCount * sizeof(D3D12SkinnedObjectData), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		const uint64_t boneBufferOffset = skinnedObjectBufferOffset + skinnedObjectBufferSize;
		const uint64_t boneBufferSize = ALIGN_TO(config.boneCount * sizeof(D3D12BoneData), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		const uint64_t lightBufferOffset = boneBufferOffset + boneBufferSize;
		const uint64_t lightBufferSize = ALIGN_TO(config.lightCount * sizeof(D3D12LightData), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		const uint64_t lightClusterBufferOffset = lightBufferOffset + lightBufferSize;
		const uint64_t lightClusterBufferSize = ALIGN_TO(
			SCENE_FRUSTUM_CLUSTER_WIDTH * SCENE_FRUSTUM_CLUSTER_HEIGHT * SCENE_FRUSTUM_CLUSTER_DEPTH * SCENE_LIGHT_CLUSTER_SIZE,
			D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
		);

		// Create scene buffer heap
		D3D12_HEAP_DESC heapDesc{};
		heapDesc.SizeInBytes = lightClusterBufferOffset + lightClusterBufferSize;
		heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapDesc.Properties.CreationNodeMask = 0;
		heapDesc.Properties.VisibleNodeMask = 0;
		heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS; // Create it zeroed so zero buffer and object buffers start in a valid state

		result = device->CreateHeap(&heapDesc, IID_PPV_ARGS(&sceneBufferHeap));
		if(FAILED(result)) RIN_ERROR("Failed to create scene buffer heap");
		RIN_DEBUG_NAME(sceneBufferHeap, "Scene Buffer Heap");

		// Create scene zero buffer
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		resourceDesc.Width = zeroBufferSize;
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			zeroBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneZeroBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene zero buffer");
		RIN_DEBUG_NAME(sceneZeroBuffer, "Scene Zero Buffer");

		// Create scene camera buffer
		resourceDesc.Width = cameraBufferSize;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			cameraBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneCameraBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene camera buffer");
		RIN_DEBUG_NAME(sceneCameraBuffer, "Scene Camera Buffer");

		// Create scene static command buffer
		resourceDesc.Width = staticCommandBufferSize;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			staticCommandBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneStaticCommandBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene static command buffer");
		RIN_DEBUG_NAME(sceneStaticCommandBuffer, "Scene Static Command Buffer");

		// Create scene dynamic command buffer
		resourceDesc.Width = dynamicCommandBufferSize;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			dynamicCommandBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneDynamicCommandBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene dynamic command buffer");
		RIN_DEBUG_NAME(sceneDynamicCommandBuffer, "Scene Dynamic Command Buffer");

		// Create scene skinned command buffer
		resourceDesc.Width = skinnedCommandBufferSize;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			skinnedCommandBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneSkinnedCommandBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene skinned command buffer");
		RIN_DEBUG_NAME(sceneSkinnedCommandBuffer, "Scene Skinned Command Buffer");

		// Create scene static vertex buffer
		resourceDesc.Width = staticVertexBufferSize;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		
		result = device->CreatePlacedResource(
			sceneBufferHeap,
			staticVertexBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneStaticVertexBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene static vertex buffer");
		RIN_DEBUG_NAME(sceneStaticVertexBuffer, "Scene Static Vertex Buffer");

		// Create scene dynamic vertex buffer
		resourceDesc.Width = dynamicVertexBufferSize;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			dynamicVertexBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneDynamicVertexBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene dynamic vertex buffer");
		RIN_DEBUG_NAME(sceneDynamicVertexBuffer, "Scene Dynamic Vertex Buffer");

		// Create scene skinned vertex buffer
		resourceDesc.Width = skinnedVertexBufferSize;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			skinnedVertexBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneSkinnedVertexBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene skinned vertex buffer");
		RIN_DEBUG_NAME(sceneSkinnedVertexBuffer, "Scene Skinned Vertex Buffer");

		// Create scene static index buffer
		resourceDesc.Width = staticIndexBufferSize;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			staticIndexBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneStaticIndexBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene static index buffer");
		RIN_DEBUG_NAME(sceneStaticIndexBuffer, "Scene Static Index Buffer");

		// Create scene dynamic index buffer
		resourceDesc.Width = dynamicIndexBufferSize;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			dynamicIndexBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneDynamicIndexBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene dynamic index buffer");
		RIN_DEBUG_NAME(sceneDynamicIndexBuffer, "Scene Dynamic Index Buffer");

		// Create scene skinned index buffer
		resourceDesc.Width = skinnedIndexBufferSize;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			skinnedIndexBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneSkinnedIndexBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene skinned index buffer");
		RIN_DEBUG_NAME(sceneSkinnedIndexBuffer, "Scene Skinned Index Buffer");

		// Create scene static object buffer
		resourceDesc.Width = staticObjectBufferSize;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			staticObjectBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneStaticObjectBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Faield to create scene static object buffer");
		RIN_DEBUG_NAME(sceneStaticObjectBuffer, "Scene Static Object Buffer");

		// Create scene dynamic object buffer
		resourceDesc.Width = dynamicObjectBufferSize;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			dynamicObjectBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneDynamicObjectBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Faield to create scene dynamic object buffer");
		RIN_DEBUG_NAME(sceneDynamicObjectBuffer, "Scene Dynamic Object Buffer");

		// Create scene skinned object buffer
		resourceDesc.Width = skinnedObjectBufferSize;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			skinnedObjectBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneSkinnedObjectBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene skinned object buffer");
		RIN_DEBUG_NAME(sceneSkinnedObjectBuffer, "Scene Skinned Object Buffer");

		// Create scene bone buffer
		resourceDesc.Width = boneBufferSize;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			boneBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneBoneBuffer)
		);
		if(FAILED(result)) RIN_ERROR("FAILED to create scene bone buffer");
		RIN_DEBUG_NAME(sceneBoneBuffer, "Scene Bone Buffer");

		// Create scene light buffer
		resourceDesc.Width = lightBufferSize;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			lightBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneLightBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene light buffer");
		RIN_DEBUG_NAME(sceneLightBuffer, "Scene Light Buffer");

		// Create scene light cluster buffer
		resourceDesc.Width = lightClusterBufferSize;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		result = device->CreatePlacedResource(
			sceneBufferHeap,
			lightClusterBufferOffset,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneLightClusterBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene light cluster buffer");
		RIN_DEBUG_NAME(sceneLightClusterBuffer, "Scene Light Cluster Buffer");

		// Create resource views

		/*
		Counter is located at element 0
		Buffer starts at element 1
		*/
		// Create scene static command buffer uav
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 1;
		uavDesc.Buffer.NumElements = config.staticObjectCount + 1;
		uavDesc.Buffer.StructureByteStride = SCENE_STATIC_COMMAND_SIZE;
		uavDesc.Buffer.CounterOffsetInBytes = 0;
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		device->CreateUnorderedAccessView(
			sceneStaticCommandBuffer,
			sceneStaticCommandBuffer,
			&uavDesc,
			getSceneDescHeapCPUHandle(SCENE_STATIC_COMMAND_BUFFER_UAV_OFFSET)
		);

		// Create scene dynamic command buffer uav
		uavDesc.Buffer.NumElements = config.dynamicObjectCount + 1;
		uavDesc.Buffer.StructureByteStride = SCENE_DYNAMIC_COMMAND_SIZE;

		device->CreateUnorderedAccessView(
			sceneDynamicCommandBuffer,
			sceneDynamicCommandBuffer,
			&uavDesc,
			getSceneDescHeapCPUHandle(SCENE_DYNAMIC_COMMAND_BUFFER_UAV_OFFSET)
		);

		// Create scene skinned command buffer uav
		uavDesc.Buffer.NumElements = config.skinnedObjectCount + 1;
		uavDesc.Buffer.StructureByteStride = SCENE_SKINNED_COMMAND_SIZE;

		device->CreateUnorderedAccessView(
			sceneSkinnedCommandBuffer,
			sceneSkinnedCommandBuffer,
			&uavDesc,
			getSceneDescHeapCPUHandle(SCENE_SKINNED_COMMAND_BUFFER_UAV_OFFSET)
		);

		// Create scene light cluster buffer uav
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = SCENE_FRUSTUM_CLUSTER_WIDTH * SCENE_FRUSTUM_CLUSTER_HEIGHT * SCENE_FRUSTUM_CLUSTER_DEPTH;
		uavDesc.Buffer.StructureByteStride = SCENE_LIGHT_CLUSTER_SIZE;

		device->CreateUnorderedAccessView(
			sceneLightClusterBuffer,
			nullptr,
			&uavDesc,
			getSceneDescHeapCPUHandle(SCENE_LIGHT_CLUSTER_BUFFER_UAV_OFFSET)
		);

		// Create scene geometry vbvs
		postScreenQuadVBV.BufferLocation = sceneStaticVertexBuffer->GetGPUVirtualAddress() + geometryBufferOffset;
		postScreenQuadVBV.SizeInBytes = SCREEN_QUAD_SIZE;
		postScreenQuadVBV.StrideInBytes = SCREEN_QUAD_STRIDE;

		skyboxVBV.BufferLocation = postScreenQuadVBV.BufferLocation + SCREEN_QUAD_SIZE;
		skyboxVBV.SizeInBytes = SKYBOX_SIZE;
		skyboxVBV.StrideInBytes = SKYBOX_STRIDE;

		// Create scene static vbv
		sceneStaticVBV.BufferLocation = sceneStaticVertexBuffer->GetGPUVirtualAddress();
		sceneStaticVBV.SizeInBytes = (uint32_t)sceneStaticVertexAllocator.getSize();
		sceneStaticVBV.StrideInBytes = sizeof(StaticVertex);

		// Create scene dynamic vbv
		sceneDynamicVBV.BufferLocation = sceneDynamicVertexBuffer->GetGPUVirtualAddress();
		sceneDynamicVBV.SizeInBytes = (uint32_t)sceneDynamicVertexAllocator.getSize();
		sceneDynamicVBV.StrideInBytes = sizeof(DynamicVertex);

		// Create scene skinned vbv
		sceneSkinnedVBV.BufferLocation = sceneSkinnedVertexBuffer->GetGPUVirtualAddress();
		sceneSkinnedVBV.SizeInBytes = (uint32_t)sceneSkinnedVertexAllocator.getSize();
		sceneSkinnedVBV.StrideInBytes = sizeof(SkinnedVertex);

		// Create scene static ibv
		sceneStaticIBV.BufferLocation = sceneStaticIndexBuffer->GetGPUVirtualAddress();
		sceneStaticIBV.SizeInBytes = (uint32_t)sceneStaticIndexAllocator.getSize();
		sceneStaticIBV.Format = INDEX_FORMAT;

		// Create scene dynamic ibv
		sceneDynamicIBV.BufferLocation = sceneDynamicIndexBuffer->GetGPUVirtualAddress();
		sceneDynamicIBV.SizeInBytes = (uint32_t)sceneDynamicIndexAllocator.getSize();
		sceneDynamicIBV.Format = INDEX_FORMAT;

		// Create scene skinned ibv
		sceneSkinnedIBV.BufferLocation = sceneSkinnedIndexBuffer->GetGPUVirtualAddress();
		sceneSkinnedIBV.SizeInBytes = (uint32_t)sceneSkinnedIndexAllocator.getSize();
		sceneSkinnedIBV.Format = INDEX_FORMAT;
		
		// Get allocation info
		D3D12_RESOURCE_DESC resourceDescs[2]{};
		resourceDescs[0].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDescs[0].Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		resourceDescs[0].Width = DFG_LUT_WIDTH;
		resourceDescs[0].Height = DFG_LUT_HEIGHT;
		resourceDescs[0].DepthOrArraySize = 1;
		resourceDescs[0].MipLevels = 1;
		resourceDescs[0].Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		resourceDescs[0].SampleDesc.Count = 1;
		resourceDescs[0].SampleDesc.Quality = 0;
		resourceDescs[0].Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDescs[0].Flags = D3D12_RESOURCE_FLAG_NONE;
		resourceDescs[1].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDescs[1].Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		resourceDescs[1].Width = 1;
		resourceDescs[1].Height = 1;
		resourceDescs[1].DepthOrArraySize = 6;
		resourceDescs[1].MipLevels = 1;
		resourceDescs[1].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		resourceDescs[1].SampleDesc.Count = 1;
		resourceDescs[1].SampleDesc.Quality = 0;
		resourceDescs[1].Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDescs[1].Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12_RESOURCE_ALLOCATION_INFO1 resourceInfo[_countof(resourceDescs)];
		D3D12_RESOURCE_ALLOCATION_INFO heapInfo = device->GetResourceAllocationInfo1(
			0,
			_countof(resourceDescs),
			resourceDescs,
			resourceInfo
		);

		sceneTextureOffset = heapInfo.SizeInBytes;

		if(UINT64_MAX - sceneTextureOffset < config.texturesSize)
			RIN_ERROR("Scene texture heap size exceeded UINT64_MAX");

		// Create scene texture heap
		heapDesc.SizeInBytes = sceneTextureOffset + config.texturesSize;
		heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapDesc.Properties.CreationNodeMask = 0;
		heapDesc.Properties.VisibleNodeMask = 0;
		heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

		result = device->CreateHeap(&heapDesc, IID_PPV_ARGS(&sceneTextureHeap));
		if(FAILED(result)) RIN_ERROR("Failed to create scene texture heap");
		RIN_DEBUG_NAME(sceneTextureHeap, "Scene Texture Heap");

		// Create scene DFG LUT
		result = device->CreatePlacedResource(
			sceneTextureHeap,
			resourceInfo[0].Offset,
			&resourceDescs[0],
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneDFGLUT)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene DFG LUT");
		RIN_DEBUG_NAME(sceneDFGLUT, "Scene DFG LUT");

		// Create scene zero cube texture
		result = device->CreatePlacedResource(
			sceneTextureHeap,
			resourceInfo[1].Offset,
			&resourceDescs[1],
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sceneZeroCubeTexture)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene zero cube texture");
		RIN_DEBUG_NAME(sceneZeroCubeTexture, "Scene Zero Cube Texture");

		// Create scene DFG LUT SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = resourceDescs[0].Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		device->CreateShaderResourceView(sceneDFGLUT, &srvDesc, getSceneDescHeapCPUHandle(SCENE_DFG_LUT_SRV_OFFSET));

		// Create scene skybox texture SRV
		srvDesc.Format = resourceDescs[1].Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.TextureCube.MostDetailedMip = 0;
		srvDesc.TextureCube.MipLevels = 1;
		srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

		device->CreateShaderResourceView(sceneZeroCubeTexture, &srvDesc, getSceneDescHeapCPUHandle(SCENE_SKYBOX_TEXTURE_SRV_OFFSET));

		// Create scene IBL diffuse texture SRV
		device->CreateShaderResourceView(sceneZeroCubeTexture, &srvDesc, getSceneDescHeapCPUHandle(SCENE_IBL_DIFFUSE_TEXTURE_SRV_OFFSET));

		// Create scene IBL specular texture SRV
		device->CreateShaderResourceView(sceneZeroCubeTexture, &srvDesc, getSceneDescHeapCPUHandle(SCENE_IBL_SPECULAR_TEXTURE_SRV_OFFSET));

		// Initialization upload
		// No need to make any allocations here
		const uint64_t alignedGeometrySize = ALIGN_TO((uint64_t)SCREEN_QUAD_SIZE + SKYBOX_SIZE, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
		const uint64_t alignedDFGLUTPitch = ALIGN_TO(DFG_LUT_WIDTH * sizeof(uint16_t) * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
		const uint64_t alignedDFGLUTSize = ALIGN_TO(alignedDFGLUTPitch * DFG_LUT_HEIGHT, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
		const uint64_t alignedZeroCubeSize = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT * 6;

		if(uploadStreamOffset + config.uploadStreamSize < alignedGeometrySize + alignedDFGLUTSize + alignedZeroCubeSize)
			RIN_ERROR("Failed to upload initialization data because the upload stream is too small");

		// Copy geometry data to the upload buffer
		memcpy(uploadBufferData, SCREEN_QUAD_VERTICES, SCREEN_QUAD_SIZE);
		memcpy(uploadBufferData + SCREEN_QUAD_SIZE, SKYBOX_VERTICES, SKYBOX_SIZE);

		// Copy from upload buffer to vertex buffer
		postCommandList->CopyBufferRegion(sceneStaticVertexBuffer, geometryBufferOffset, uploadBuffer, 0, geometryBufferSize);

		// Copy DFG LUT to the upload buffer
		char* alignedData = uploadBufferData + alignedGeometrySize;
		const uint16_t* dfgData = DFG_LUT;
		for(uint32_t row = 0; row < DFG_LUT_HEIGHT; ++row) {
			for(uint32_t i = 0; i < DFG_LUT_WIDTH; ++i) {
				memcpy(alignedData + i * sizeof(uint16_t) * 4, dfgData, sizeof(uint16_t) * 3);

				dfgData += 3;
			}

			alignedData += alignedDFGLUTPitch;
		}

		// Copy from upload buffer to texture
		D3D12_TEXTURE_COPY_LOCATION copyDest{};
		copyDest.pResource = sceneDFGLUT;
		copyDest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		copyDest.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION copySrc{};
		copySrc.pResource = uploadBuffer;
		copySrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		copySrc.PlacedFootprint.Offset = alignedGeometrySize;
		copySrc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		copySrc.PlacedFootprint.Footprint.Width = DFG_LUT_WIDTH;
		copySrc.PlacedFootprint.Footprint.Height = DFG_LUT_HEIGHT;
		copySrc.PlacedFootprint.Footprint.Depth = 1;
		copySrc.PlacedFootprint.Footprint.RowPitch = alignedDFGLUTPitch;

		postCommandList->CopyTextureRegion(&copyDest, 0, 0, 0, &copySrc, nullptr);

		// Copy scene zero cube texture to the upload buffer
		memset(uploadBufferData + alignedGeometrySize + alignedDFGLUTSize, 0, alignedZeroCubeSize);

		// Copy from upload buffer to texture
		copyDest.pResource = sceneZeroCubeTexture;
		copyDest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		copyDest.SubresourceIndex = 0;

		copySrc.pResource = uploadBuffer;
		copySrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		copySrc.PlacedFootprint.Offset = alignedGeometrySize + alignedDFGLUTSize;
		copySrc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		copySrc.PlacedFootprint.Footprint.Width = 1;
		copySrc.PlacedFootprint.Footprint.Height = 1;
		copySrc.PlacedFootprint.Footprint.Depth = 1;
		copySrc.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

		for(uint32_t slice = 0; slice < 6; ++slice) {
			postCommandList->CopyTextureRegion(&copyDest, 0, 0, 0, &copySrc, nullptr);

			++copyDest.SubresourceIndex;

			copySrc.PlacedFootprint.Offset += D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
		}

		// Non-simultaneous-access textures don't have common state decay
		// so we need to transition them back to common manually
		D3D12_RESOURCE_BARRIER barriers[2]{};
		barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barriers[0].Transition.pResource = sceneDFGLUT;
		barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
		barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barriers[1].Transition.pResource = sceneZeroCubeTexture;
		barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;

		postCommandList->ResourceBarrier(_countof(barriers), barriers);

		// Close command list once recording is done
		result = postCommandList->Close();
		if(FAILED(result)) RIN_ERROR("Failed to close post command list");

		// Execute copy on the graphics queue
		graphicsQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&postCommandList);

		result = graphicsQueue->Signal(graphicsFence, ++graphicsFenceValue);
		if(FAILED(result)) RIN_ERROR("Failed to signal graphics queue");

		// Record command lists here to minimize time spent waiting on the GPU
		threadPool.enqueueJob([this]() { recordCullStaticCommandList(); });
		threadPool.enqueueJob([this]() { recordCullDynamicCommandList(); });
		threadPool.enqueueJob([this]() { recordCullSkinnedCommandList(); });
		threadPool.enqueueJob([this]() { recordLightClusterCommandList(); });
		recordDepthMIPCommandList();
		threadPool.wait();

		// Wait for copy to finish
		// Need to block so that an upload allocation isn't made
		// and overwrites the data in the uploadData buffer
		if(graphicsFence->GetCompletedValue() < graphicsFenceValue) {
			result = graphicsFence->SetEventOnCompletion(graphicsFenceValue, nullptr);
			if(FAILED(result)) RIN_ERROR("Failed to set graphics fence event");
		}
	}

	void D3D12Renderer::destroyScenePipeline() noexcept {
		if(sceneDescHeap) {
			sceneDescHeap->Release();
			sceneDescHeap = nullptr;
		}
		if(depthMIPRootSignature) {
			depthMIPRootSignature->Release();
			depthMIPRootSignature = nullptr;
		}
		if(depthMIPPipelineState) {
			depthMIPPipelineState->Release();
			depthMIPPipelineState = nullptr;
		}
		if(depthMIPCommandAllocator) {
			depthMIPCommandAllocator->Release();
			depthMIPCommandAllocator = nullptr;
		}
		if(depthMIPCommandList) {
			depthMIPCommandList->Release();
			depthMIPCommandList = nullptr;
		}
		if(cullStaticRootSignature) {
			cullStaticRootSignature->Release();
			cullStaticRootSignature = nullptr;
		}
		if(cullStaticPipelineState) {
			cullStaticPipelineState->Release();
			cullStaticPipelineState = nullptr;
		}
		if(cullDynamicRootSignature) {
			cullDynamicRootSignature->Release();
			cullDynamicRootSignature = nullptr;
		}
		if(cullDynamicPipelineState) {
			cullDynamicPipelineState->Release();
			cullDynamicPipelineState = nullptr;
		}
		if(cullSkinnedRootSignature) {
			cullSkinnedRootSignature->Release();
			cullSkinnedRootSignature = nullptr;
		}
		if(cullSkinnedPipelineState) {
			cullSkinnedPipelineState->Release();
			cullSkinnedPipelineState = nullptr;
		}
		if(cullStaticCommandAllocator) {
			cullStaticCommandAllocator->Release();
			cullStaticCommandAllocator = nullptr;
		}
		if(cullStaticCommandList) {
			cullStaticCommandList->Release();
			cullStaticCommandList = nullptr;
		}
		if(cullDynamicCommandAllocator) {
			cullDynamicCommandAllocator->Release();
			cullDynamicCommandAllocator = nullptr;
		}
		if(cullDynamicCommandList) {
			cullDynamicCommandList->Release();
			cullDynamicCommandList = nullptr;
		}
		if(cullSkinnedCommandAllocator) {
			cullSkinnedCommandAllocator->Release();
			cullSkinnedCommandAllocator = nullptr;
		}
		if(cullSkinnedCommandList) {
			cullSkinnedCommandList->Release();
			cullSkinnedCommandList = nullptr;
		}
		if(lightClusterRootSignature) {
			lightClusterRootSignature->Release();
			lightClusterRootSignature = nullptr;
		}
		if(lightClusterPipelineState) {
			lightClusterPipelineState->Release();
			lightClusterPipelineState = nullptr;
		}
		if(lightClusterCommandAllocator) {
			lightClusterCommandAllocator->Release();
			lightClusterCommandAllocator = nullptr;
		}
		if(lightClusterCommandList) {
			lightClusterCommandList->Release();
			lightClusterCommandList = nullptr;
		}
		if(sceneRTVDescHeap) {
			sceneRTVDescHeap->Release();
			sceneRTVDescHeap = nullptr;
		}
		if(sceneDSVDescHeap) {
			sceneDSVDescHeap->Release();
			sceneDSVDescHeap = nullptr;
		}
		if(sceneStaticRootSignature) {
			sceneStaticRootSignature->Release();
			sceneStaticRootSignature = nullptr;
		}
		if(sceneStaticCommandSignature) {
			sceneStaticCommandSignature->Release();
			sceneStaticCommandSignature = nullptr;
		}
		if(sceneStaticPBRPipelineState) {
			sceneStaticPBRPipelineState->Release();
			sceneStaticPBRPipelineState = nullptr;
		}
		if(sceneDynamicRootSignature) {
			sceneDynamicRootSignature->Release();
			sceneDynamicRootSignature = nullptr;
		}
		if(sceneDynamicCommandSignature) {
			sceneDynamicCommandSignature->Release();
			sceneDynamicCommandSignature = nullptr;
		}
		if(sceneDynamicPBRPipelineState) {
			sceneDynamicPBRPipelineState->Release();
			sceneDynamicPBRPipelineState = nullptr;
		}
		if(sceneSkinnedRootSignature) {
			sceneSkinnedRootSignature->Release();
			sceneSkinnedRootSignature = nullptr;
		}
		if(sceneSkinnedCommandSignature) {
			sceneSkinnedCommandSignature->Release();
			sceneSkinnedCommandSignature = nullptr;
		}
		if(sceneSkinnedPBRPipelineState) {
			sceneSkinnedPBRPipelineState->Release();
			sceneSkinnedPBRPipelineState = nullptr;
		}
		if(sceneStaticCommandAllocator) {
			sceneStaticCommandAllocator->Release();
			sceneStaticCommandAllocator = nullptr;
		}
		if(sceneStaticCommandList) {
			sceneStaticCommandList->Release();
			sceneStaticCommandList = nullptr;
		}
		if(sceneDynamicCommandAllocator) {
			sceneDynamicCommandAllocator->Release();
			sceneDynamicCommandAllocator = nullptr;
		}
		if(sceneDynamicCommandList) {
			sceneDynamicCommandList->Release();
			sceneDynamicCommandList = nullptr;
		}
		if(sceneSkinnedCommandAllocator) {
			sceneSkinnedCommandAllocator->Release();
			sceneSkinnedCommandAllocator = nullptr;
		}
		if(sceneSkinnedCommandList) {
			sceneSkinnedCommandList->Release();
			sceneSkinnedCommandList = nullptr;
		}
		if(skyboxRootSignature) {
			skyboxRootSignature->Release();
			skyboxRootSignature = nullptr;
		}
		if(skyboxPipelineState) {
			skyboxPipelineState->Release();
			skyboxPipelineState = nullptr;
		}
		if(skyboxCommandAllocator) {
			skyboxCommandAllocator->Release();
			skyboxCommandAllocator = nullptr;
		}
		if(skyboxCommandList) {
			skyboxCommandList->Release();
			skyboxCommandList = nullptr;
		}
		if(postRootSignature) {
			postRootSignature->Release();
			postRootSignature = nullptr;
		}
		if(postPipelineState) {
			postPipelineState->Release();
			postPipelineState = nullptr;
		}
		if(postCommandAllocator) {
			postCommandAllocator->Release();
			postCommandAllocator = nullptr;
		}
		if(postCommandList) {
			postCommandList->Release();
			postCommandList = nullptr;
		}
		destroySceneBackBuffer();
		if(sceneBufferHeap) {
			sceneBufferHeap->Release();
			sceneBufferHeap = nullptr;
		}
		if(sceneZeroBuffer) {
			sceneZeroBuffer->Release();
			sceneZeroBuffer = nullptr;
		}
		if(sceneCameraBuffer) {
			sceneCameraBuffer->Release();
			sceneCameraBuffer = nullptr;
		}
		if(sceneStaticCommandBuffer) {
			sceneStaticCommandBuffer->Release();
			sceneStaticCommandBuffer = nullptr;
		}
		if(sceneDynamicCommandBuffer) {
			sceneDynamicCommandBuffer->Release();
			sceneDynamicCommandBuffer = nullptr;
		}
		if(sceneSkinnedCommandBuffer) {
			sceneSkinnedCommandBuffer->Release();
			sceneSkinnedCommandBuffer = nullptr;
		}
		if(sceneStaticVertexBuffer) {
			sceneStaticVertexBuffer->Release();
			sceneStaticVertexBuffer = nullptr;
		}
		if(sceneDynamicVertexBuffer) {
			sceneDynamicVertexBuffer->Release();
			sceneDynamicVertexBuffer = nullptr;
		}
		if(sceneSkinnedVertexBuffer) {
			sceneSkinnedVertexBuffer->Release();
			sceneSkinnedVertexBuffer = nullptr;
		}
		if(sceneStaticIndexBuffer) {
			sceneStaticIndexBuffer->Release();
			sceneStaticIndexBuffer = nullptr;
		}
		if(sceneDynamicIndexBuffer) {
			sceneDynamicIndexBuffer->Release();
			sceneDynamicIndexBuffer = nullptr;
		}
		if(sceneSkinnedIndexBuffer) {
			sceneSkinnedIndexBuffer->Release();
			sceneSkinnedIndexBuffer = nullptr;
		}
		if(sceneStaticObjectBuffer) {
			sceneStaticObjectBuffer->Release();
			sceneStaticObjectBuffer = nullptr;
		}
		if(sceneDynamicObjectBuffer) {
			sceneDynamicObjectBuffer->Release();
			sceneDynamicObjectBuffer = nullptr;
		}
		if(sceneSkinnedObjectBuffer) {
			sceneSkinnedObjectBuffer->Release();
			sceneSkinnedObjectBuffer = nullptr;
		}
		if(sceneBoneBuffer) {
			sceneBoneBuffer->Release();
			sceneBoneBuffer = nullptr;
		}
		if(sceneLightBuffer) {
			sceneLightBuffer->Release();
			sceneLightBuffer = nullptr;
		}
		if(sceneLightClusterBuffer) {
			sceneLightClusterBuffer->Release();
			sceneLightClusterBuffer = nullptr;
		}
		if(sceneTextureHeap) {
			sceneTextureHeap->Release();
			sceneTextureHeap = nullptr;
		}
		if(sceneDFGLUT) {
			sceneDFGLUT->Release();
			sceneDFGLUT = nullptr;
		}
		if(sceneZeroCubeTexture) {
			sceneZeroCubeTexture->Release();
			sceneZeroCubeTexture = nullptr;
		}
		destroyDeadTextures();
	}

	void D3D12Renderer::createSwapChainDependencies() {
		// Used to maintain the old back buffer count in the event that
		// the settings change
		numBackBuffers = settings.backBufferCount;

		// Create back buffer descriptor heap
		// Since this only exists on the CPU it is relatively cheap to have a
		// descriptor heap for only the back buffers
		// Ideally we might avoid creating/destroying this when resizing
		// if we don't change the back buffer count, but the cost is negligable
		// compared to resizeing the actual back buffers
		D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
		descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		descriptorHeapDesc.NumDescriptors = numBackBuffers;
		descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		descriptorHeapDesc.NodeMask = 0;

		HRESULT result = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&backBufferDescHeap));
		if(FAILED(result)) RIN_ERROR("Failed to create back buffer descriptor heap");
		RIN_DEBUG_NAME(backBufferDescHeap, "Back Buffer Descriptor Heap");

		// Create back buffer handles
		backBuffers = new ID3D12Resource*[numBackBuffers] {};
		D3D12_CPU_DESCRIPTOR_HANDLE backBufferHandle = backBufferDescHeap->GetCPUDescriptorHandleForHeapStart();
		for(uint32_t i = 0; i < numBackBuffers; ++i, backBufferHandle.ptr += rtvHeapStep) {
			result = swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i]));
			if(FAILED(result)) RIN_ERROR("Failed to get back buffer");
			device->CreateRenderTargetView(backBuffers[i], nullptr, backBufferHandle);
			RIN_DEBUG_NAME(backBuffers[i], "Back Buffer");
		}
	}

	void D3D12Renderer::destroySwapChainDependencies() noexcept {
		if(backBufferDescHeap) {
			backBufferDescHeap->Release();
			backBufferDescHeap = nullptr;
		}
		if(backBuffers) {
			for(uint32_t i = 0; i < numBackBuffers; ++i)
				if(backBuffers[i]) backBuffers[i]->Release();
			delete[] backBuffers;
			backBuffers = nullptr;
		}
	}

	void D3D12Renderer::createSceneBackBuffer() {
		// Get allocation info
		D3D12_RESOURCE_DESC resourceDescs[2]{};
		resourceDescs[0].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDescs[0].Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		resourceDescs[0].Width = settings.backBufferWidth;
		resourceDescs[0].Height = settings.backBufferHeight;
		resourceDescs[0].DepthOrArraySize = 1;
		resourceDescs[0].MipLevels = 1;
		resourceDescs[0].Format = BACK_BUFFER_FORMAT;
		resourceDescs[0].SampleDesc.Count = 1;
		resourceDescs[0].SampleDesc.Quality = 0;
		resourceDescs[0].Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDescs[0].Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		resourceDescs[1].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDescs[1].Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		resourceDescs[1].Width = settings.backBufferWidth;
		resourceDescs[1].Height = settings.backBufferHeight;
		resourceDescs[1].DepthOrArraySize = 1;
		resourceDescs[1].MipLevels = 1;
		resourceDescs[1].Format = DEPTH_FORMAT_DSV;
		resourceDescs[1].SampleDesc.Count = 1;
		resourceDescs[1].SampleDesc.Quality = 0;
		resourceDescs[1].Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDescs[1].Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_RESOURCE_ALLOCATION_INFO1 resourceInfo[_countof(resourceDescs)];
		D3D12_RESOURCE_ALLOCATION_INFO heapInfo = device->GetResourceAllocationInfo1(
			0,
			_countof(resourceDescs),
			resourceDescs,
			resourceInfo
		);

		// Create scene back buffer heap
		D3D12_HEAP_DESC heapDesc{};
		heapDesc.SizeInBytes = heapInfo.SizeInBytes;
		heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapDesc.Properties.CreationNodeMask = 0;
		heapDesc.Properties.VisibleNodeMask = 0;
		heapDesc.Alignment = heapInfo.Alignment;
		heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

		HRESULT result = device->CreateHeap(&heapDesc, IID_PPV_ARGS(&sceneBackBufferHeap));
		if(FAILED(result)) RIN_ERROR("Failed to create scene back buffer heap");
		RIN_DEBUG_NAME(sceneBackBufferHeap, "Scene Back Buffer Heap");

		// Create scene back buffer
		D3D12_CLEAR_VALUE clearValue{};
		clearValue.Format = BACK_BUFFER_FORMAT;
		clearValue.Color[0] = 0.0f;
		clearValue.Color[1] = 0.0f;
		clearValue.Color[2] = 0.0f;
		clearValue.Color[3] = 0.0f;

		result = device->CreatePlacedResource(
			sceneBackBufferHeap,
			resourceInfo[0].Offset,
			&resourceDescs[0],
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			&clearValue,
			IID_PPV_ARGS(&sceneBackBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene back buffer");
		RIN_DEBUG_NAME(sceneBackBuffer, "Scene Back Buffer");

		// Create scene depth buffer
		clearValue.Format = DEPTH_FORMAT_DSV;
		clearValue.DepthStencil.Depth = 1.0f;
		clearValue.DepthStencil.Stencil = 0;

		result = device->CreatePlacedResource(
			sceneBackBufferHeap,
			resourceInfo[1].Offset,
			&resourceDescs[1],
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			&clearValue,
			IID_PPV_ARGS(&sceneDepthBuffer)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene depth buffer");
		RIN_DEBUG_NAME(sceneDepthBuffer, "Scene Depth Buffer");

		// Create rtv for scene back buffer
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = BACK_BUFFER_FORMAT;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		rtvDesc.Texture2D.PlaneSlice = 0;

		device->CreateRenderTargetView(sceneBackBuffer, &rtvDesc, sceneRTVDescHeap->GetCPUDescriptorHandleForHeapStart());
		
		// Create srv for scene back buffer
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = BACK_BUFFER_FORMAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		device->CreateShaderResourceView(sceneBackBuffer, &srvDesc, getSceneDescHeapCPUHandle(SCENE_BACK_BUFFER_SRV_OFFSET));

		// Create dsv for scene depth buffer
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format = DEPTH_FORMAT_DSV;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
		dsvDesc.Texture2D.MipSlice = 0;

		device->CreateDepthStencilView(sceneDepthBuffer, &dsvDesc, sceneDSVDescHeap->GetCPUDescriptorHandleForHeapStart());

		// Create srv for scene depth buffer
		srvDesc.Format = DEPTH_FORMAT_SRV;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		device->CreateShaderResourceView(sceneDepthBuffer, &srvDesc, getSceneDescHeapCPUHandle(SCENE_DEPTH_BUFFER_SRV_OFFSET));

		// Set dependent values
		sceneBackBufferViewport.Width = (float)settings.backBufferWidth;
		sceneBackBufferViewport.Height = (float)settings.backBufferHeight;
		sceneBackBufferScissorRect.right = settings.backBufferWidth;
		sceneBackBufferScissorRect.bottom = settings.backBufferHeight;

		// Create committed scene depth hierarchy
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProperties.CreationNodeMask = 0;
		heapProperties.VisibleNodeMask = 0;

		// We use greatest power of 2 less than resource dimensions so that
		// the top of the hierarchy doesn't undersample the original depth buffer
		// and each level of the hierarchy doesn't undersample its parent
		unsigned long msb;
		if(!_BitScanReverse(&msb, settings.backBufferWidth))
			RIN_ERROR("Back buffer width was 0");
		sceneDepthHierarchyWidth = (uint64_t)1 << msb; // Greatest power of 2 less than backBufferWidth
		if(!_BitScanReverse(&msb, settings.backBufferHeight))
			RIN_ERROR("Back buffer height was 0");
		sceneDepthHierarchyHeight = (uint64_t)1 << msb; // Greatest power of 2 less than backBufferHeight

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		resourceDesc.Width = sceneDepthHierarchyWidth;
		resourceDesc.Height = sceneDepthHierarchyHeight;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 0;
		resourceDesc.Format = DEPTH_FORMAT_SRV;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		result = device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
			&resourceDesc,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&sceneDepthHierarchy)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create scene depth hierachy");
		RIN_DEBUG_NAME(sceneDepthHierarchy, "Scene Depth Hierarchy");

		resourceDesc = sceneDepthHierarchy->GetDesc();
		if(resourceDesc.MipLevels > SCENE_DEPTH_HIERARCHY_MIP_COUNT)
			RIN_ERROR("Back buffer dimensions too large");

		sceneDepthHierarchyLevels = resourceDesc.MipLevels;

		// Create srv for scene depth hierarchy
		srvDesc.Format = DEPTH_FORMAT_SRV;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = -1;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		device->CreateShaderResourceView(sceneDepthHierarchy, &srvDesc, getSceneDescHeapCPUHandle(SCENE_DEPTH_HIERARCHY_SRV_OFFSET));

		// Create srvs for scene depth hierarchy mips
		D3D12_CPU_DESCRIPTOR_HANDLE handle = getSceneDescHeapCPUHandle(SCENE_DEPTH_HIERARCHY_MIP_SRV_OFFSET);

		for(uint32_t i = 0; i < resourceDesc.MipLevels; ++i, handle.ptr += cbvsrvuavHeapStep) {
			srvDesc.Texture2D.MostDetailedMip = i;
			srvDesc.Texture2D.MipLevels = 1;

			device->CreateShaderResourceView(sceneDepthHierarchy, &srvDesc, handle);
		}

		// Create uavs for scene depth hierarchy mips
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DEPTH_FORMAT_SRV;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		uavDesc.Texture2D.PlaneSlice = 0;

		handle = getSceneDescHeapCPUHandle(SCENE_DEPTH_HIERARCHY_MIP_UAV_OFFSET);

		for(uint32_t i = 0; i < resourceDesc.MipLevels; ++i, handle.ptr += cbvsrvuavHeapStep) {
			uavDesc.Texture2D.MipSlice = i;

			device->CreateUnorderedAccessView(sceneDepthHierarchy, nullptr, &uavDesc, handle);
		}
	}

	void D3D12Renderer::destroySceneBackBuffer() noexcept {
		if(sceneBackBufferHeap) {
			sceneBackBufferHeap->Release();
			sceneBackBufferHeap = nullptr;
		}
		if(sceneBackBuffer) {
			sceneBackBuffer->Release();
			sceneBackBuffer = nullptr;
		}
		if(sceneDepthBuffer) {
			sceneDepthBuffer->Release();
			sceneDepthBuffer = nullptr;
		}
		if(sceneDepthHierarchy) {
			sceneDepthHierarchy->Release();
			sceneDepthHierarchy = nullptr;
		}
	}

	void D3D12Renderer::recordDepthMIPCommandList() {
		// Reset command allocator to reuse its memory
		HRESULT result = depthMIPCommandAllocator->Reset();
		if(FAILED(result)) RIN_ERROR("Failed to reset depth MIP mapping command allocator");

		// Put command list back in the recording state
		result = depthMIPCommandList->Reset(depthMIPCommandAllocator, depthMIPPipelineState);
		if(FAILED(result)) RIN_ERROR("Failed to reset depth MIP mapping command list");

		// Descriptor binding
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = getSceneDescHeapGPUHandle(SCENE_DEPTH_HIERARCHY_MIP_SRV_OFFSET);
		D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = getSceneDescHeapGPUHandle(SCENE_DEPTH_HIERARCHY_MIP_UAV_OFFSET);

		depthMIPCommandList->SetDescriptorHeaps(1, &sceneDescHeap);
		depthMIPCommandList->SetComputeRootSignature(depthMIPRootSignature);

		// Hierarchy base
		uint32_t size[2]{ sceneDepthHierarchyWidth, sceneDepthHierarchyHeight };
		depthMIPCommandList->SetComputeRoot32BitConstants(0, 2, size, 0);
		depthMIPCommandList->SetComputeRootDescriptorTable(1, getSceneDescHeapGPUHandle(SCENE_DEPTH_BUFFER_SRV_OFFSET));
		depthMIPCommandList->SetComputeRootDescriptorTable(2, uavHandle);

		D3D12_RESOURCE_BARRIER barriers[1]{};
		barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barriers[0].Transition.pResource = sceneDepthHierarchy;
		barriers[0].Transition.Subresource = 0;
		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

		depthMIPCommandList->ResourceBarrier(_countof(barriers), barriers);

		depthMIPCommandList->Dispatch(
			std::max(size[0] / DEPTH_MIP_THREAD_GROUP_SIZE_X, (uint32_t)1),
			std::max(size[1] / DEPTH_MIP_THREAD_GROUP_SIZE_Y, (uint32_t)1),
			1
		);

		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		depthMIPCommandList->ResourceBarrier(_countof(barriers), barriers);

		uavHandle.ptr += cbvsrvuavHeapStep;
		for(uint32_t i = 1; i < sceneDepthHierarchyLevels; ++i, srvHandle.ptr += cbvsrvuavHeapStep, uavHandle.ptr += cbvsrvuavHeapStep) {
			size[0] >>= 1;
			size[1] >>= 1;

			depthMIPCommandList->SetComputeRoot32BitConstants(0, 2, size, 0);
			depthMIPCommandList->SetComputeRootDescriptorTable(1, srvHandle);
			depthMIPCommandList->SetComputeRootDescriptorTable(2, uavHandle);

			barriers[0].Transition.Subresource = i;
			barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

			depthMIPCommandList->ResourceBarrier(_countof(barriers), barriers);

			depthMIPCommandList->Dispatch(
				std::max(size[0] / DEPTH_MIP_THREAD_GROUP_SIZE_X, (uint32_t)1),
				std::max(size[1] / DEPTH_MIP_THREAD_GROUP_SIZE_Y, (uint32_t)1),
				1
			);

			barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			depthMIPCommandList->ResourceBarrier(_countof(barriers), barriers);
		}

		// Close command list once recording is done
		result = depthMIPCommandList->Close();
		if(FAILED(result)) RIN_ERROR("Failed to close depth MIP mapping command list");
	}

	void D3D12Renderer::recordCullStaticCommandList() {
		// Reset command allocator to reuse its memory
		HRESULT result = cullStaticCommandAllocator->Reset();
		if(FAILED(result)) RIN_ERROR("Failed to reset static culling command allocator");

		// Put command list back in the recording state
		result = cullStaticCommandList->Reset(cullStaticCommandAllocator, cullStaticPipelineState);
		if(FAILED(result)) RIN_ERROR("Failed to reset static culling command list");

		// Descriptor binding
		cullStaticCommandList->SetDescriptorHeaps(1, &sceneDescHeap);
		cullStaticCommandList->SetComputeRootSignature(cullStaticRootSignature);
		uint32_t size[2]{ sceneDepthHierarchyWidth, sceneDepthHierarchyHeight };
		cullStaticCommandList->SetComputeRoot32BitConstants(0, 2, size, 0);
		cullStaticCommandList->SetComputeRootShaderResourceView(1, sceneStaticObjectBuffer->GetGPUVirtualAddress());
		cullStaticCommandList->SetComputeRootDescriptorTable(2, getSceneDescHeapGPUHandle(SCENE_STATIC_COMMAND_BUFFER_UAV_OFFSET));
		cullStaticCommandList->SetComputeRootConstantBufferView(3, sceneCameraBuffer->GetGPUVirtualAddress());
		cullStaticCommandList->SetComputeRootDescriptorTable(4, getSceneDescHeapGPUHandle(SCENE_DEPTH_HIERARCHY_SRV_OFFSET));

		// Reset uav counter
		cullStaticCommandList->CopyBufferRegion(sceneStaticCommandBuffer, 0, sceneZeroBuffer, 0, UAV_COUNTER_SIZE);

		// Transition indirect command buffer to unordered access
		D3D12_RESOURCE_BARRIER barriers[1]{};
		barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barriers[0].Transition.pResource = sceneStaticCommandBuffer;
		barriers[0].Transition.Subresource = 0;
		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

		cullStaticCommandList->ResourceBarrier(_countof(barriers), barriers);

		// Dispatch
		cullStaticCommandList->Dispatch(config.staticObjectCount / CULL_THREAD_GROUP_SIZE, 1, 1);

		// Close command list once recording is done
		result = cullStaticCommandList->Close();
		if(FAILED(result)) RIN_ERROR("Failed to close static culling command list");
	}

	void D3D12Renderer::recordCullDynamicCommandList() {
		// Reset command allocator to reuse its memory
		HRESULT result = cullDynamicCommandAllocator->Reset();
		if(FAILED(result)) RIN_ERROR("Failed to reset dynamic culling command allocator");

		// Put command list back in the recording state
		result = cullDynamicCommandList->Reset(cullDynamicCommandAllocator, cullDynamicPipelineState);
		if(FAILED(result)) RIN_ERROR("Failed to reset dynamic culling command list");

		// Descriptor binding
		cullDynamicCommandList->SetDescriptorHeaps(1, &sceneDescHeap);
		cullDynamicCommandList->SetComputeRootSignature(cullDynamicRootSignature);
		uint32_t size[2]{ sceneDepthHierarchyWidth, sceneDepthHierarchyHeight };
		cullDynamicCommandList->SetComputeRoot32BitConstants(0, 2, size, 0);
		cullDynamicCommandList->SetComputeRootShaderResourceView(1, sceneDynamicObjectBuffer->GetGPUVirtualAddress());
		cullDynamicCommandList->SetComputeRootDescriptorTable(2, getSceneDescHeapGPUHandle(SCENE_DYNAMIC_COMMAND_BUFFER_UAV_OFFSET));
		cullDynamicCommandList->SetComputeRootConstantBufferView(3, sceneCameraBuffer->GetGPUVirtualAddress());
		cullDynamicCommandList->SetComputeRootDescriptorTable(4, getSceneDescHeapGPUHandle(SCENE_DEPTH_HIERARCHY_SRV_OFFSET));

		// Reset uav counter
		cullDynamicCommandList->CopyBufferRegion(sceneDynamicCommandBuffer, 0, sceneZeroBuffer, 0, UAV_COUNTER_SIZE);

		// Transition indirect command buffer to unordered access
		D3D12_RESOURCE_BARRIER barriers[1]{};
		barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barriers[0].Transition.pResource = sceneDynamicCommandBuffer;
		barriers[0].Transition.Subresource = 0;
		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

		cullDynamicCommandList->ResourceBarrier(_countof(barriers), barriers);

		// Dispatch
		cullDynamicCommandList->Dispatch(config.dynamicObjectCount / CULL_THREAD_GROUP_SIZE, 1, 1);

		// Close command list once recording is done
		result = cullDynamicCommandList->Close();
		if(FAILED(result)) RIN_ERROR("Failed to close dynamic culling command list");
	}

	void D3D12Renderer::recordCullSkinnedCommandList() {
		// Reset command allocator to reuse its memory
		HRESULT result = cullSkinnedCommandAllocator->Reset();
		if(FAILED(result)) RIN_ERROR("Failed to reset skinned culling command allocator");

		// Put command list back in the recording state
		result = cullSkinnedCommandList->Reset(cullSkinnedCommandAllocator, cullSkinnedPipelineState);
		if(FAILED(result)) RIN_ERROR("Failed to reset skinned culling command list");

		// Descriptor binding
		cullSkinnedCommandList->SetDescriptorHeaps(1, &sceneDescHeap);
		cullSkinnedCommandList->SetComputeRootSignature(cullSkinnedRootSignature);
		uint32_t size[2]{ sceneDepthHierarchyWidth, sceneDepthHierarchyHeight };
		cullSkinnedCommandList->SetComputeRoot32BitConstants(0, 2, size, 0);
		cullSkinnedCommandList->SetComputeRootShaderResourceView(1, sceneSkinnedObjectBuffer->GetGPUVirtualAddress());
		cullSkinnedCommandList->SetComputeRootShaderResourceView(2, sceneBoneBuffer->GetGPUVirtualAddress());
		cullSkinnedCommandList->SetComputeRootDescriptorTable(3, getSceneDescHeapGPUHandle(SCENE_SKINNED_COMMAND_BUFFER_UAV_OFFSET));
		cullSkinnedCommandList->SetComputeRootConstantBufferView(4, sceneCameraBuffer->GetGPUVirtualAddress());
		cullSkinnedCommandList->SetComputeRootDescriptorTable(5, getSceneDescHeapGPUHandle(SCENE_DEPTH_HIERARCHY_SRV_OFFSET));

		// Reset uav counter
		cullSkinnedCommandList->CopyBufferRegion(sceneSkinnedCommandBuffer, 0, sceneZeroBuffer, 0, UAV_COUNTER_SIZE);

		// Transition indirect command buffer to unordered access
		D3D12_RESOURCE_BARRIER barriers[1]{};
		barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barriers[0].Transition.pResource = sceneSkinnedCommandBuffer;
		barriers[0].Transition.Subresource = 0;
		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

		cullSkinnedCommandList->ResourceBarrier(_countof(barriers), barriers);

		// Dispatch
		cullSkinnedCommandList->Dispatch(config.skinnedObjectCount / CULL_THREAD_GROUP_SIZE, 1, 1);

		// Close command list once recording is done
		result = cullSkinnedCommandList->Close();
		if(FAILED(result)) RIN_ERROR("Failed to close skinned culling command list");
	}

	void D3D12Renderer::recordLightClusterCommandList() {
		// Reset command allocator to reuse its memory
		HRESULT result = lightClusterCommandAllocator->Reset();
		if(FAILED(result)) RIN_ERROR("Failed to reset light cluster command allocator");

		// Put command list back in the recording state
		result = lightClusterCommandList->Reset(lightClusterCommandAllocator, lightClusterPipelineState);
		if(FAILED(result)) RIN_ERROR("Failed to reset light cluster command list");

		// Descriptor binding
		lightClusterCommandList->SetDescriptorHeaps(1, &sceneDescHeap);
		lightClusterCommandList->SetComputeRootSignature(lightClusterRootSignature);
		lightClusterCommandList->SetComputeRoot32BitConstant(0, config.lightCount, 0);
		lightClusterCommandList->SetComputeRootConstantBufferView(1, sceneCameraBuffer->GetGPUVirtualAddress());
		lightClusterCommandList->SetComputeRootShaderResourceView(2, sceneLightBuffer->GetGPUVirtualAddress());
		lightClusterCommandList->SetComputeRootDescriptorTable(3, getSceneDescHeapGPUHandle(SCENE_LIGHT_CLUSTER_BUFFER_UAV_OFFSET));

		// Dispatch
		lightClusterCommandList->Dispatch(
			SCENE_FRUSTUM_CLUSTER_WIDTH / LIGHT_CLUSTER_THREAD_GROUP_SIZE_X,
			SCENE_FRUSTUM_CLUSTER_HEIGHT / LIGHT_CLUSTER_THREAD_GROUP_SIZE_Y,
			SCENE_FRUSTUM_CLUSTER_DEPTH / LIGHT_CLUSTER_THREAD_GROUP_SIZE_Z
		);

		// Close command list once recording is done
		result = lightClusterCommandList->Close();
		if(FAILED(result)) RIN_ERROR("Failed to close light cluster command list");
	}

	void D3D12Renderer::recordSceneStaticCommandList() {
		// Reset command allocator to reuse its memory
		HRESULT result = sceneStaticCommandAllocator->Reset();
		if(FAILED(result)) RIN_ERROR("Failed to reset static scene rendering command allocator");

		// Put command list back in the recording state
		result = sceneStaticCommandList->Reset(sceneStaticCommandAllocator, sceneStaticPBRPipelineState);
		if(FAILED(result)) RIN_ERROR("Failed to reset static scene rendering command list");

		// Transition scene depth buffer from shader resource to depth write
		D3D12_RESOURCE_BARRIER barriers[1]{};
		barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barriers[0].Transition.pResource = sceneDepthBuffer;
		barriers[0].Transition.Subresource = 0;
		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;

		sceneStaticCommandList->ResourceBarrier(_countof(barriers), barriers);

		// Descriptor binding

		sceneStaticCommandList->SetDescriptorHeaps(1, &sceneDescHeap);
		sceneStaticCommandList->SetGraphicsRootSignature(sceneStaticRootSignature);
		sceneStaticCommandList->SetGraphicsRoot32BitConstant(1, sceneIBLSpecularMIPCount, 0);
		sceneStaticCommandList->SetGraphicsRootConstantBufferView(2, sceneCameraBuffer->GetGPUVirtualAddress());
		sceneStaticCommandList->SetGraphicsRootShaderResourceView(3, sceneStaticObjectBuffer->GetGPUVirtualAddress());
		sceneStaticCommandList->SetGraphicsRootShaderResourceView(4, sceneLightBuffer->GetGPUVirtualAddress());
		sceneStaticCommandList->SetGraphicsRootShaderResourceView(5, sceneLightClusterBuffer->GetGPUVirtualAddress());
		sceneStaticCommandList->SetGraphicsRootDescriptorTable(6, getSceneDescHeapGPUHandle(SCENE_DFG_LUT_SRV_OFFSET));

		// Input-Assembler
		sceneStaticCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		sceneStaticCommandList->IASetVertexBuffers(0, 1, &sceneStaticVBV);
		sceneStaticCommandList->IASetIndexBuffer(&sceneStaticIBV);

		// Raster State
		sceneStaticCommandList->RSSetViewports(1, &sceneBackBufferViewport);
		sceneStaticCommandList->RSSetScissorRects(1, &sceneBackBufferScissorRect);

		// Output Merger
		D3D12_CPU_DESCRIPTOR_HANDLE rtvDescHandle = sceneRTVDescHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE dsvDescHandle = sceneDSVDescHeap->GetCPUDescriptorHandleForHeapStart();
		sceneStaticCommandList->OMSetRenderTargets(1, &rtvDescHandle, true, &dsvDescHandle);

		// Draw
		constexpr float clearColor[] { 0.0f, 0.0f, 0.0f, 0.0f };
		sceneStaticCommandList->ClearRenderTargetView(rtvDescHandle, clearColor, 0, nullptr);
		sceneStaticCommandList->ClearDepthStencilView(dsvDescHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	#ifdef RIN_DEBUG
		sceneStaticCommandList->BeginQuery(debugQueryPipelineHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, DEBUG_QUERY_PIPELINE_STATIC_RENDER);
	#endif

		sceneStaticCommandList->ExecuteIndirect(
			sceneStaticCommandSignature,
			config.staticObjectCount,
			sceneStaticCommandBuffer,
			SCENE_STATIC_COMMAND_SIZE,
			sceneStaticCommandBuffer,
			0
		);

	#ifdef RIN_DEBUG
		sceneStaticCommandList->EndQuery(debugQueryPipelineHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, DEBUG_QUERY_PIPELINE_STATIC_RENDER);
	#endif

		// Close command list once recording is done
		result = sceneStaticCommandList->Close();
		if(FAILED(result)) RIN_ERROR("Failed to close static scene rendering command list");
	}

	void D3D12Renderer::recordSceneDynamicCommandList() {
		// Reset command allocator to reuse its memory
		HRESULT result = sceneDynamicCommandAllocator->Reset();
		if(FAILED(result)) RIN_ERROR("Failed to reset dynamic scene rendering command allocator");

		// Put command list back in the recording state
		result = sceneDynamicCommandList->Reset(sceneDynamicCommandAllocator, sceneDynamicPBRPipelineState);
		if(FAILED(result)) RIN_ERROR("Failed to reset dynamic scene rendering command list");

		// Descriptor binding
		sceneDynamicCommandList->SetDescriptorHeaps(1, &sceneDescHeap);
		sceneDynamicCommandList->SetGraphicsRootSignature(sceneDynamicRootSignature);
		sceneDynamicCommandList->SetGraphicsRoot32BitConstant(1, sceneIBLSpecularMIPCount, 0);
		sceneDynamicCommandList->SetGraphicsRootConstantBufferView(2, sceneCameraBuffer->GetGPUVirtualAddress());
		sceneDynamicCommandList->SetGraphicsRootShaderResourceView(3, sceneDynamicObjectBuffer->GetGPUVirtualAddress());
		sceneDynamicCommandList->SetGraphicsRootShaderResourceView(4, sceneLightBuffer->GetGPUVirtualAddress());
		sceneDynamicCommandList->SetGraphicsRootShaderResourceView(5, sceneLightClusterBuffer->GetGPUVirtualAddress());
		sceneDynamicCommandList->SetGraphicsRootDescriptorTable(6, getSceneDescHeapGPUHandle(SCENE_DFG_LUT_SRV_OFFSET));

		// Input-Assembler
		sceneDynamicCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		sceneDynamicCommandList->IASetVertexBuffers(0, 1, &sceneDynamicVBV);
		sceneDynamicCommandList->IASetIndexBuffer(&sceneDynamicIBV);

		// Raster State
		sceneDynamicCommandList->RSSetViewports(1, &sceneBackBufferViewport);
		sceneDynamicCommandList->RSSetScissorRects(1, &sceneBackBufferScissorRect);

		// Output Merger
		D3D12_CPU_DESCRIPTOR_HANDLE rtvDescHandle = sceneRTVDescHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE dsvDescHandle = sceneDSVDescHeap->GetCPUDescriptorHandleForHeapStart();
		sceneDynamicCommandList->OMSetRenderTargets(1, &rtvDescHandle, true, &dsvDescHandle);

	#ifdef RIN_DEBUG
		sceneDynamicCommandList->BeginQuery(debugQueryPipelineHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, DEBUG_QUERY_PIPELINE_DYNAMIC_RENDER);
	#endif

		// Draw
		sceneDynamicCommandList->ExecuteIndirect(
			sceneDynamicCommandSignature,
			config.dynamicObjectCount,
			sceneDynamicCommandBuffer,
			SCENE_DYNAMIC_COMMAND_SIZE,
			sceneDynamicCommandBuffer,
			0
		);

	#ifdef RIN_DEBUG
		sceneDynamicCommandList->EndQuery(debugQueryPipelineHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, DEBUG_QUERY_PIPELINE_DYNAMIC_RENDER);
	#endif

		// Close command list once recording is done
		result = sceneDynamicCommandList->Close();
		if(FAILED(result)) RIN_ERROR("Failed to close dynamic scene rendering command list");
	}

	void D3D12Renderer::recordSceneSkinnedCommandList() {
		// Reset command allocator to reuse its memory
		HRESULT result = sceneSkinnedCommandAllocator->Reset();
		if(FAILED(result)) RIN_ERROR("Failed to reset skinned scene rendering command allocator");

		// Put command list back in the recording state
		result = sceneSkinnedCommandList->Reset(sceneSkinnedCommandAllocator, sceneSkinnedPBRPipelineState);
		if(FAILED(result)) RIN_ERROR("Failed to reset skinned scene rendering command list");

		// Descriptor binding
		sceneSkinnedCommandList->SetDescriptorHeaps(1, &sceneDescHeap);
		sceneSkinnedCommandList->SetGraphicsRootSignature(sceneSkinnedRootSignature);
		sceneSkinnedCommandList->SetGraphicsRoot32BitConstant(1, sceneIBLSpecularMIPCount, 0);
		sceneSkinnedCommandList->SetGraphicsRootConstantBufferView(2, sceneCameraBuffer->GetGPUVirtualAddress());
		sceneSkinnedCommandList->SetGraphicsRootShaderResourceView(3, sceneSkinnedObjectBuffer->GetGPUVirtualAddress());
		sceneSkinnedCommandList->SetGraphicsRootShaderResourceView(4, sceneBoneBuffer->GetGPUVirtualAddress());
		sceneSkinnedCommandList->SetGraphicsRootShaderResourceView(5, sceneLightBuffer->GetGPUVirtualAddress());
		sceneSkinnedCommandList->SetGraphicsRootShaderResourceView(6, sceneLightClusterBuffer->GetGPUVirtualAddress());
		sceneSkinnedCommandList->SetGraphicsRootDescriptorTable(7, getSceneDescHeapGPUHandle(SCENE_DFG_LUT_SRV_OFFSET));

		// Input-Assembler
		sceneSkinnedCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		sceneSkinnedCommandList->IASetVertexBuffers(0, 1, &sceneSkinnedVBV);
		sceneSkinnedCommandList->IASetIndexBuffer(&sceneSkinnedIBV);

		// Raster State
		sceneSkinnedCommandList->RSSetViewports(1, &sceneBackBufferViewport);
		sceneSkinnedCommandList->RSSetScissorRects(1, &sceneBackBufferScissorRect);

		// Output Merger
		D3D12_CPU_DESCRIPTOR_HANDLE rtvDescHandle = sceneRTVDescHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE dsvDescHandle = sceneDSVDescHeap->GetCPUDescriptorHandleForHeapStart();
		sceneSkinnedCommandList->OMSetRenderTargets(1, &rtvDescHandle, true, &dsvDescHandle);

	#ifdef RIN_DEBUG
		sceneSkinnedCommandList->BeginQuery(debugQueryPipelineHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, DEBUG_QUERY_PIPELINE_SKINNED_RENDER);
	#endif

		// Draw
		sceneSkinnedCommandList->ExecuteIndirect(
			sceneSkinnedCommandSignature,
			config.skinnedObjectCount,
			sceneSkinnedCommandBuffer,
			SCENE_SKINNED_COMMAND_SIZE,
			sceneSkinnedCommandBuffer,
			0
		);

	#ifdef RIN_DEBUG
		sceneSkinnedCommandList->EndQuery(debugQueryPipelineHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, DEBUG_QUERY_PIPELINE_SKINNED_RENDER);
	#endif

		// Close command list once recording is done
		result = sceneSkinnedCommandList->Close();
		if(FAILED(result)) RIN_ERROR("Failed to close skinned scene rendering command list");
	}

	void D3D12Renderer::recordSkyboxCommandList() {
		// Reset command allocator to reuse its memory
		HRESULT result = skyboxCommandAllocator->Reset();
		if(FAILED(result)) RIN_ERROR("Failed to reset skybox command allocator");

		// Put command list back in the recording state
		result = skyboxCommandList->Reset(skyboxCommandAllocator, skyboxPipelineState);
		if(FAILED(result)) RIN_ERROR("Failed to reset skybox command list");

		// Transition scene depth buffer from depth write to depth read
		D3D12_RESOURCE_BARRIER barriers[1]{};
		barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barriers[0].Transition.pResource = sceneDepthBuffer;
		barriers[0].Transition.Subresource = 0;
		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_READ;

		skyboxCommandList->ResourceBarrier(_countof(barriers), barriers);

		// Descriptor binding
		skyboxCommandList->SetDescriptorHeaps(1, &sceneDescHeap);
		skyboxCommandList->SetGraphicsRootSignature(skyboxRootSignature);
		skyboxCommandList->SetGraphicsRootConstantBufferView(0, sceneCameraBuffer->GetGPUVirtualAddress());
		skyboxCommandList->SetGraphicsRootDescriptorTable(1, getSceneDescHeapGPUHandle(SCENE_SKYBOX_TEXTURE_SRV_OFFSET));

		// Input-Assembler
		skyboxCommandList->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		skyboxCommandList->IASetVertexBuffers(0, 1, &skyboxVBV);

		// Raster State
		skyboxCommandList->RSSetViewports(1, &sceneBackBufferViewport);
		skyboxCommandList->RSSetScissorRects(1, &sceneBackBufferScissorRect);

		// Output Merger
		D3D12_CPU_DESCRIPTOR_HANDLE rtvDescHandle = sceneRTVDescHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE dsvDescHandle = sceneDSVDescHeap->GetCPUDescriptorHandleForHeapStart();
		skyboxCommandList->OMSetRenderTargets(1, &rtvDescHandle, true, &dsvDescHandle);

		// Draw
		skyboxCommandList->DrawInstanced(SKYBOX_VERTEX_COUNT, 1, 0, 0);

		// Transition scene depth buffer from depth read to shader resource
		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_READ;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

		skyboxCommandList->ResourceBarrier(_countof(barriers), barriers);

		// Close command list once recording is done
		result = skyboxCommandList->Close();
		if(FAILED(result)) RIN_ERROR("Failed to close skybox command list");
	}

	void D3D12Renderer::recordPostCommandList() {
		// Reset command allocator to reuse its memory
		HRESULT result = postCommandAllocator->Reset();
		if(FAILED(result)) RIN_ERROR("Failed to reset post processing command allocator");

		// Put command list back in the recording state
		result = postCommandList->Reset(postCommandAllocator, postPipelineState);
		if(FAILED(result)) RIN_ERROR("Failed to reset post processing command list");

	#ifdef RIN_DEBUG
		postCommandList->ResolveQueryData(
			debugQueryPipelineHeap,
			D3D12_QUERY_TYPE_PIPELINE_STATISTICS,
			0,
			DEBUG_QUERY_PIPELINE_COUNT,
			debugQueryDataBuffer,
			0
		);
	#endif

		D3D12_RESOURCE_BARRIER barriers[2]{};
		// Transition back buffer from present to render target
		barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barriers[0].Transition.pResource = backBuffers[backBufferIndex];
		barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		// Transition scene back buffer from render target to shader resource
		barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barriers[1].Transition.pResource = sceneBackBuffer;
		barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

		postCommandList->ResourceBarrier(_countof(barriers), barriers);

		// Descriptor binding
		postCommandList->SetDescriptorHeaps(1, &sceneDescHeap);
		postCommandList->SetGraphicsRootSignature(postRootSignature);
		postCommandList->SetGraphicsRootDescriptorTable(0, getSceneDescHeapGPUHandle(SCENE_BACK_BUFFER_SRV_OFFSET));

		// Input-Assembler
		postCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		postCommandList->IASetVertexBuffers(0, 1, &postScreenQuadVBV);

		// Raster State
		postCommandList->RSSetViewports(1, &backBufferViewport);
		postCommandList->RSSetScissorRects(1, &backBufferScissorRect);

		// Output Merger
		D3D12_CPU_DESCRIPTOR_HANDLE rtvDescHandle = backBufferDescHeap->GetCPUDescriptorHandleForHeapStart();
		rtvDescHandle.ptr += (uint64_t)rtvHeapStep * backBufferIndex;

		postCommandList->OMSetRenderTargets(1, &rtvDescHandle, true, nullptr);

		// Draw
		postCommandList->DrawInstanced(SCREEN_QUAD_VERTEX_COUNT, 1, 0, 0);

		// Transition back buffer from render target to present
		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		// Transition scene back buffer from shader resource to render target
		barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

		postCommandList->ResourceBarrier(_countof(barriers), barriers);

		// Close command list once recording is done
		result = postCommandList->Close();
		if(FAILED(result)) RIN_ERROR("Failed to close post processing command list");
	}

	void D3D12Renderer::uploadStreamWork(uint32_t copyQueueIndex) {
		// Create upload stream command allocator
		ID3D12CommandAllocator* commandAllocator;
		HRESULT result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&commandAllocator));
		if(FAILED(result)) RIN_ERROR("Failed to create upload stream command allocator");
		RIN_DEBUG_NAME(commandAllocator, "Upload Stream Command Allocator");

		// Create closed upload stream command list
		ID3D12GraphicsCommandList* commandList;
		result = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_COPY, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&commandList));
		if(FAILED(result)) RIN_ERROR("Failed to create upload stream command list");
		RIN_DEBUG_NAME(commandList, "Upload Stream Command List");

		bool worked = true;
		
		while(true) {
			uploadStreamBarrier.arrive_and_wait(); // Barrier 1
			if(uploadStreamTerminate) break;

			// Begin recording
			if(worked) {
				result = commandAllocator->Reset();
				if(FAILED(result)) RIN_ERROR("Failed to reset upload stream command allocator");

				result = commandList->Reset(commandAllocator, nullptr);
				if(FAILED(result)) RIN_ERROR("Failed to reset upload stream command list");
			}
			worked = false;

			while(true) {
				upload_stream_job_type job;

				// Enter critical section
				uploadStreamMutex.lock();

				// Exit conditions
				if(uploadStreamQueue.empty()) {
					// Ready to submit
					uploadStreamMutex.unlock();
					break;
				} else {
					if(uploadStreamQueue.front().size > uploadStreamBudget) {
						// No space left, just submit now
						uploadStreamMutex.unlock();
						break;
					} else if(uploadStreamQueue.front().copyQueueIndex != copyQueueIndex) {
						// This thread can't handle this request, give it to another one
						uploadStreamMutex.unlock();
						std::this_thread::yield();
						continue;
					}
				}

				// Update state under the mutex
				uploadStreamBudget -= uploadStreamQueue.front().size;
				job = uploadStreamQueue.front().job;
				uploadStreamQueue.pop();

				// Exit critical section
				uploadStreamMutex.unlock();

				job(commandList);
				worked = true;
			}

			// Submit command list
			if(worked) {
				result = commandList->Close();
				if(FAILED(result)) RIN_ERROR("Failed to close upload stream command list");

				copyQueues[copyQueueIndex]->ExecuteCommandLists(1, (ID3D12CommandList**)&commandList);
			}

			uploadStreamBarrier.arrive_and_wait(); // Barrier 2
		}

		// Cleanup
		commandAllocator->Release();
		commandList->Release();
	}

	void D3D12Renderer::destroyDeadTextures() {
		for(uint32_t i = 0; i < config.textureCount; ++i) {
			D3D12Texture* texture = sceneTexturePool.at(i);
			if(texture) {
				if(texture->dead && texture->resident()) {
					texture->resource->Release();

					sceneTextureAllocator.free(texture->textureAlloc);

					sceneTexturePool.remove(texture);
				}
			}
		}
	}

	D3D12_CPU_DESCRIPTOR_HANDLE D3D12Renderer::getSceneDescHeapCPUHandle(uint32_t offset) {
		D3D12_CPU_DESCRIPTOR_HANDLE handle = sceneDescHeap->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += (uint64_t)cbvsrvuavHeapStep * offset;
		return handle;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE D3D12Renderer::getSceneDescHeapGPUHandle(uint32_t offset) {
		D3D12_GPU_DESCRIPTOR_HANDLE handle = sceneDescHeap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += (uint64_t)cbvsrvuavHeapStep * offset;
		return handle;
	}

	DXGI_FORMAT getFormat(TEXTURE_FORMAT format) {
		switch(format) {
		case TEXTURE_FORMAT::R8_UNORM:
			return DXGI_FORMAT_R8_UNORM;
		case TEXTURE_FORMAT::R16_FLOAT:
			return DXGI_FORMAT_R16_FLOAT;
		case TEXTURE_FORMAT::R32_FLOAT:
			return DXGI_FORMAT_R32_FLOAT;
		case TEXTURE_FORMAT::R8G8_UNORM:
			return DXGI_FORMAT_R8G8_UNORM;
		case TEXTURE_FORMAT::R16G16_FLOAT:
			return DXGI_FORMAT_R16G16_FLOAT;
		case TEXTURE_FORMAT::R32G32_FLOAT:
			return DXGI_FORMAT_R32G32_FLOAT;
		case TEXTURE_FORMAT::R8G8B8A8_UNORM:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case TEXTURE_FORMAT::R8G8B8A8_UNORM_SRGB:
			return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case TEXTURE_FORMAT::B8G8R8A8_UNORM:
			return DXGI_FORMAT_B8G8R8A8_UNORM;
		case TEXTURE_FORMAT::B8G8R8A8_UNORM_SRGB:
			return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		case TEXTURE_FORMAT::R16B16G16A16_FLOAT:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case TEXTURE_FORMAT::R32G32B32A32_FLOAT:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case TEXTURE_FORMAT::BC3_UNORM:
			return DXGI_FORMAT_BC3_UNORM;
		case TEXTURE_FORMAT::BC3_UNORM_SRGB:
			return DXGI_FORMAT_BC3_UNORM_SRGB;
		case TEXTURE_FORMAT::BC4_UNORM:
			return DXGI_FORMAT_BC4_UNORM;
		case TEXTURE_FORMAT::BC5_UNORM:
			return DXGI_FORMAT_BC5_UNORM;
		case TEXTURE_FORMAT::BC6H_FLOAT:
			return DXGI_FORMAT_BC6H_SF16;
		case TEXTURE_FORMAT::BC7_UNORM:
			return DXGI_FORMAT_BC7_UNORM;
		case TEXTURE_FORMAT::BC7_UNORM_SRGB:
			return DXGI_FORMAT_BC7_UNORM_SRGB;
		}
		return DXGI_FORMAT_UNKNOWN;
	}

	void D3D12Renderer::wait() {
		HRESULT result;

		// Signal copy queues
		for(uint32_t i = 0; i < COPY_QUEUE_COUNT; ++i) {
			result = copyQueues[i]->Signal(copyFences[i], ++copyFenceValues[i]);
			if(FAILED(result)) RIN_ERROR("Failed to signal copy queue");
		}

		// No need to signal graphics queue as it was already signaled at the end of the frame
		result = graphicsFence->SetEventOnCompletion(graphicsFenceValue, nullptr);
		if(FAILED(result)) RIN_ERROR("Failed to wait for graphics queue");

		// No need to signal compute queue as it was already signaled at the end of the frame
		result = computeFence->SetEventOnCompletion(computeFenceValue, nullptr);
		if(FAILED(result)) RIN_ERROR("Failed to wait for compute queue");

		// Wait for copy queues to finish
		result = device->SetEventOnMultipleFenceCompletion(copyFences, copyFenceValues, COPY_QUEUE_COUNT, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, nullptr);
		if(FAILED(result)) RIN_ERROR("Failed to wait for copy queues");
	}

	Camera& D3D12Renderer::getCamera() {
		return sceneCamera;
	}

	const Camera& D3D12Renderer::getCamera() const {
		return sceneCamera;
	}

	StaticMesh* D3D12Renderer::addStaticMesh(
		const BoundingSphere& boundingSphere,
		const StaticVertex* vertices,
		const uint32_t* vertexCounts,
		const index_type* indices,
		const uint32_t* indexCounts,
		uint32_t lodCount
	) {
		// Validation
		if(!lodCount) RIN_ERROR("LOD count must not be 0");

		if(lodCount > LOD_COUNT) {
			RIN_DEBUG_INFO("LOD count larger than supported, ignoring extra LODs");
			lodCount = LOD_COUNT;
		}

		for(uint32_t i = 0; i < lodCount; ++i) {
			if(!vertexCounts[i]) RIN_ERROR("Vertex count must not be 0");

			if(vertexCounts[i] * sizeof(StaticVertex) > config.uploadStreamSize) {
				RIN_DEBUG_ERROR("Static vertex upload too large");
				return nullptr;
			}
		}

		for(uint32_t i = 0; i < lodCount; ++i) {
			if(!indexCounts[i]) RIN_ERROR("Index count must not be 0");

			if(indexCounts[i] * sizeof(index_type) > config.uploadStreamSize) {
				RIN_DEBUG_ERROR("Static index upload too large");
				return nullptr;
			}
		}

		// Create mesh
		D3D12StaticMesh* mesh = sceneStaticMeshPool.insert(boundingSphere);
		if(!mesh) return nullptr;

		// Make all allocations
		bool failedAlloc = false;
		for(uint32_t i = 0; i < lodCount; ++i) {
			auto vertexAlloc = sceneStaticVertexAllocator.allocate(vertexCounts[i] * sizeof(StaticVertex));
			if(!vertexAlloc) {
				failedAlloc = true;
				break;
			}

			auto indexAlloc = sceneStaticIndexAllocator.allocate(indexCounts[i] * sizeof(index_type));
			if(!indexAlloc) {
				failedAlloc = true;
				sceneStaticVertexAllocator.free(vertexAlloc);
				break;
			}

			mesh->lods[i].emplace(vertexAlloc.value(), indexAlloc.value());
		}

		// Cleanup
		if(failedAlloc) {
			for(uint32_t i = 0; i < lodCount; ++i) {
				if(mesh->lods[i]) {
					sceneStaticVertexAllocator.free(mesh->lods[i]->vertexAlloc);
					sceneStaticIndexAllocator.free(mesh->lods[i]->indexAlloc);
				}
			}

			sceneStaticMeshPool.remove(mesh);

			return nullptr;
		}

		// Critical section
		std::lock_guard<std::mutex> lock(uploadStreamMutex);

		const StaticVertex* lodVertices = vertices;
		const index_type* lodIndices = indices;
		for(uint32_t i = 0; i < lodCount; ++i) {
			FreeListAllocator::Allocation vertexAlloc = mesh->lods[i]->vertexAlloc;
			FreeListAllocator::Allocation indexAlloc = mesh->lods[i]->indexAlloc;

			// Enqueue vertex upload
			uploadStreamQueue.push(
				{
					[this, vertexAlloc, lodVertices](ID3D12GraphicsCommandList* commandList) {
						auto uploadAlloc = uploadStreamAllocator.allocate(vertexAlloc.size);
						if(!uploadAlloc) RIN_ERROR("Upload static vertices anomaly: out of upload stream space");

						commandList->CopyBufferRegion(
							sceneStaticVertexBuffer,
							vertexAlloc.start,
							uploadBuffer,
							uploadStreamOffset + uploadAlloc->start,
							vertexAlloc.size
						);

						memcpy(uploadBufferData + uploadStreamOffset + uploadAlloc->start, lodVertices, vertexAlloc.size);
					},
					vertexAlloc.size,
					COPY_QUEUE_STATIC_VB_DYNAMIC_SKINNED_IB_INDEX
				}
			);

			// Enqueue index upload
			bool finalUpload = i == lodCount - 1;
			uploadStreamQueue.push(
				{
					[this, indexAlloc, lodIndices, finalUpload, mesh](ID3D12GraphicsCommandList* commandList) {
						auto uploadAlloc = uploadStreamAllocator.allocate(indexAlloc.size);
						if(!uploadAlloc) RIN_ERROR("Upload static indices anomaly: out of upload stream space");

						commandList->CopyBufferRegion(
							sceneStaticIndexBuffer,
							indexAlloc.start,
							uploadBuffer,
							uploadStreamOffset + uploadAlloc->start,
							indexAlloc.size
						);

						memcpy(uploadBufferData + uploadStreamOffset + uploadAlloc->start, lodIndices, indexAlloc.size);

						if(finalUpload) mesh->_resident = true;
					},
					indexAlloc.size,
					COPY_QUEUE_DYNAMIC_SKINNED_VB_STATIC_IB_INDEX
				}
			);

			lodVertices += vertexCounts[i];
			lodIndices += indexCounts[i];
		}

		return mesh;
	}

	void D3D12Renderer::removeStaticMesh(StaticMesh* m) {
		if(!m) return;

		D3D12StaticMesh* mesh = (D3D12StaticMesh*)m;

		for(uint32_t i = 0; i < LOD_COUNT; ++i) {
			if(mesh->lods[i]) {
				sceneStaticVertexAllocator.free(mesh->lods[i]->vertexAlloc);
				sceneStaticIndexAllocator.free(mesh->lods[i]->indexAlloc);
			}
		}

		// Do this after freeing the allocations so that another thread does
		// not write over the lods if it gets a pointer that aliases this one
		sceneStaticMeshPool.remove(mesh);
	}

	StaticObject* D3D12Renderer::addStaticObject(StaticMesh* mesh, Material* material) {
		if(!mesh || !material) return nullptr;

		StaticObject* object = sceneStaticObjectPool.insert(mesh, material);

		updateStaticObject(object);

		return object;
	}

	void D3D12Renderer::removeStaticObject(StaticObject* object) {
		if(!object) return;

		{
			// Critical section
			std::lock_guard<std::mutex> lock(uploadStreamMutex);

			// Enqueue object removal
			uploadStreamQueue.push(
				{
					[this, object](ID3D12GraphicsCommandList* commandList) {
					// Zero the object out
					commandList->CopyBufferRegion(
						sceneStaticObjectBuffer,
						sceneStaticObjectPool.getIndex(object) * sizeof(D3D12StaticObjectData),
						sceneZeroBuffer,
						0,
						sizeof(D3D12StaticObjectData)
					);
				},
				0,
					COPY_QUEUE_CAMERA_STATIC_DYNAMIC_SKINNED_OB_LB_INDEX
				}
			);
		}

		// Need to remove from the pool only after pushing to the queue
		// so that if any other thread claims the object in the same spot
		// any updates to its data on the GPU are serialized after the remove
		sceneStaticObjectPool.remove(object);
	}

	void D3D12Renderer::updateStaticObject(StaticObject* object) {
		if(!object) return;

		// Enter critical section
		std::lock_guard<std::mutex> lock(uploadStreamMutex);

		// Enqueue object upload
		uploadStreamQueue.push(
			{
				[this, object](ID3D12GraphicsCommandList* commandList) {
					auto uploadAlloc = uploadStreamAllocator.allocate(sizeof(D3D12StaticObjectData));
					if(!uploadAlloc) RIN_ERROR("Upload static object anomaly: out of upload stream space");

					commandList->CopyBufferRegion(
						sceneStaticObjectBuffer,
						sceneStaticObjectPool.getIndex(object) * sizeof(D3D12StaticObjectData),
						uploadBuffer,
						uploadStreamOffset + uploadAlloc->start,
						sizeof(D3D12StaticObjectData)
					);

					D3D12StaticObjectData* objectData = (D3D12StaticObjectData*)(uploadBufferData + uploadStreamOffset + uploadAlloc->start);

					// The mesh will always be this derived type
					D3D12StaticMesh* objectMesh = (D3D12StaticMesh*)object->mesh;

					objectData->boundingSphere.center = objectMesh->boundingSphere.center;
					objectData->boundingSphere.radius = objectMesh->boundingSphere.radius;

					// All guaranteed to have at least lod 0
					D3D12StaticMesh::LOD lod = objectMesh->lods[0].value();
					// Populate LOD data
					for(uint32_t i = 0; i < LOD_COUNT; ++i) {
						if(i && objectMesh->lods[i]) lod = objectMesh->lods[i].value();

						objectData->lods[i].startIndex = (uint32_t)(lod.indexAlloc.start / sizeof(index_type));
						objectData->lods[i].indexCount = (uint32_t)(lod.indexAlloc.size / sizeof(index_type));
						objectData->lods[i].vertexOffset = (uint32_t)(lod.vertexAlloc.start / sizeof(StaticVertex));
					}

					Material* material = object->material;
					// The textures will always be this derived type
					objectData->material.baseColorID = sceneTexturePool.getIndex((D3D12Texture*)material->baseColor);
					objectData->material.normalID = sceneTexturePool.getIndex((D3D12Texture*)material->normal);
					objectData->material.roughnessAOID = sceneTexturePool.getIndex((D3D12Texture*)material->roughnessAO);
					if(material->metallic) objectData->material.metallicID = sceneTexturePool.getIndex((D3D12Texture*)material->metallic);
					objectData->material.heightID = sceneTexturePool.getIndex((D3D12Texture*)material->height);
					if(material->special) objectData->material.specialID = sceneTexturePool.getIndex((D3D12Texture*)material->special);

					objectData->flags.show = 1;
					objectData->flags.materialType = (uint32_t)material->type;

					object->_resident = true;
				},
				sizeof(D3D12StaticObjectData),
				COPY_QUEUE_CAMERA_STATIC_DYNAMIC_SKINNED_OB_LB_INDEX
			}
		);
	}

	DynamicMesh* D3D12Renderer::addDynamicMesh(
		const BoundingSphere& boundingSphere,
		const DynamicVertex* vertices,
		const uint32_t* vertexCounts,
		const index_type* indices,
		const uint32_t* indexCounts,
		uint32_t lodCount
	) {
		// Validation
		if(!lodCount) RIN_ERROR("LOD count must not be 0");

		if(lodCount > LOD_COUNT) {
			RIN_DEBUG_INFO("LOD count larger than supported, ignoring extra LODs");
			lodCount = LOD_COUNT;
		}

		for(uint32_t i = 0; i < lodCount; ++i) {
			if(!vertexCounts[i]) RIN_ERROR("Vertex count must not be 0");

			if(vertexCounts[i] * sizeof(DynamicVertex) > config.uploadStreamSize) {
				RIN_DEBUG_ERROR("Dynamic vertex upload too large");
				return nullptr;
			}
		}

		for(uint32_t i = 0; i < lodCount; ++i) {
			if(!indexCounts[i]) RIN_ERROR("Index count must not be 0");

			if(indexCounts[i] * sizeof(index_type) > config.uploadStreamSize) {
				RIN_DEBUG_ERROR("Dynamic index upload too large");
				return nullptr;
			}
		}

		// Create mesh
		D3D12DynamicMesh* mesh = sceneDynamicMeshPool.insert(boundingSphere);
		if(!mesh) return nullptr;

		// Make all allocations
		bool failedAlloc = false;
		for(uint32_t i = 0; i < lodCount; ++i) {
			auto vertexAlloc = sceneDynamicVertexAllocator.allocate(vertexCounts[i] * sizeof(DynamicVertex));
			if(!vertexAlloc) {
				failedAlloc = true;
				break;
			}

			auto indexAlloc = sceneDynamicIndexAllocator.allocate(indexCounts[i] * sizeof(index_type));
			if(!indexAlloc) {
				failedAlloc = true;
				sceneDynamicVertexAllocator.free(vertexAlloc);
				break;
			}

			mesh->lods[i].emplace(vertexAlloc.value(), indexAlloc.value());
		}

		// Cleanup
		if(failedAlloc) {
			for(uint32_t i = 0; i < lodCount; ++i) {
				if(mesh->lods[i]) {
					sceneDynamicVertexAllocator.free(mesh->lods[i]->vertexAlloc);
					sceneDynamicIndexAllocator.free(mesh->lods[i]->indexAlloc);
				}
			}

			sceneDynamicMeshPool.remove(mesh);

			return nullptr;
		}

		// Critical section
		std::lock_guard<std::mutex> lock(uploadStreamMutex);

		const DynamicVertex* lodVertices = vertices;
		const index_type* lodIndices = indices;
		for(uint32_t i = 0; i < lodCount; ++i) {
			FreeListAllocator::Allocation vertexAlloc = mesh->lods[i]->vertexAlloc;
			FreeListAllocator::Allocation indexAlloc = mesh->lods[i]->indexAlloc;

			// Enqueue vertex upload
			uploadStreamQueue.push(
				{
					[this, vertexAlloc, lodVertices](ID3D12GraphicsCommandList* commandList) {
						auto uploadAlloc = uploadStreamAllocator.allocate(vertexAlloc.size);
						if(!uploadAlloc) RIN_ERROR("Upload dynamic vertices anomaly: out of upload stream space");

						commandList->CopyBufferRegion(
							sceneDynamicVertexBuffer,
							vertexAlloc.start,
							uploadBuffer,
							uploadStreamOffset + uploadAlloc->start,
							vertexAlloc.size
						);

						memcpy(uploadBufferData + uploadStreamOffset + uploadAlloc->start, lodVertices, vertexAlloc.size);
					},
					vertexAlloc.size,
					COPY_QUEUE_DYNAMIC_SKINNED_VB_STATIC_IB_INDEX
				}
			);

			// Enqueue index upload
			bool finalUpload = i == lodCount - 1;
			uploadStreamQueue.push(
				{
					[this, indexAlloc, lodIndices, finalUpload, mesh](ID3D12GraphicsCommandList* commandList) {
						auto uploadAlloc = uploadStreamAllocator.allocate(indexAlloc.size);
						if(!uploadAlloc) RIN_ERROR("Upload dynamic indices anomaly: out of upload stream space");

						commandList->CopyBufferRegion(
							sceneDynamicIndexBuffer,
							indexAlloc.start,
							uploadBuffer,
							uploadStreamOffset + uploadAlloc->start,
							indexAlloc.size
						);

						memcpy(uploadBufferData + uploadStreamOffset + uploadAlloc->start, lodIndices, indexAlloc.size);

						if(finalUpload) mesh->_resident = true;
					},
					indexAlloc.size,
					COPY_QUEUE_STATIC_VB_DYNAMIC_SKINNED_IB_INDEX
				}
			);

			lodVertices += vertexCounts[i];
			lodIndices += indexCounts[i];
		}

		return mesh;
	}

	void D3D12Renderer::removeDynamicMesh(DynamicMesh* m) {
		if(!m) return;

		D3D12DynamicMesh* mesh = (D3D12DynamicMesh*)m;

		for(uint32_t i = 0; i < LOD_COUNT; ++i) {
			if(mesh->lods[i]) {
				sceneDynamicVertexAllocator.free(mesh->lods[i]->vertexAlloc);
				sceneDynamicIndexAllocator.free(mesh->lods[i]->indexAlloc);
			}
		}

		// Do this after freeing the allocations so that another thread does
		// not write over the lods if it gets a pointer that aliases this one
		sceneDynamicMeshPool.remove(mesh);
	}

	DynamicObject* D3D12Renderer::addDynamicObject(DynamicMesh* mesh, Material* material) {
		if(!mesh || !material) return nullptr;

		DynamicObject* object = sceneDynamicObjectPool.insert(mesh, material);
		if(!object) return nullptr;
		
		object->_resident = true;

		return object;
	}

	void D3D12Renderer::removeDynamicObject(DynamicObject* object) {
		if(!object) return;

		sceneDynamicObjectPool.remove(object);
	}

	SkinnedMesh* D3D12Renderer::addSkinnedMesh(
		const BoundingSphere& boundingSphere,
		const SkinnedVertex* vertices,
		const uint32_t* vertexCounts,
		const index_type* indices,
		const uint32_t* indexCounts,
		uint32_t lodCount
	) {
		// Validation
		if(!lodCount) RIN_ERROR("LOD count must not be 0");

		if(lodCount > LOD_COUNT) {
			RIN_DEBUG_INFO("LOD count larger than supported, ignoring extra LODs");
			lodCount = LOD_COUNT;
		}

		for(uint32_t i = 0; i < lodCount; ++i) {
			if(!vertexCounts[i]) RIN_ERROR("Vertex count must not be 0");

			if(vertexCounts[i] * sizeof(SkinnedVertex) > config.uploadStreamSize) {
				RIN_DEBUG_ERROR("Skinned vertex upload too large");
				return nullptr;
			}
		}

		for(uint32_t i = 0; i < lodCount; ++i) {
			if(!indexCounts[i]) RIN_ERROR("Index count must not be 0");

			if(indexCounts[i] * sizeof(index_type) > config.uploadStreamSize) {
				RIN_DEBUG_ERROR("Skinned index upload too large");
				return nullptr;
			}
		}

		// Create mesh
		D3D12SkinnedMesh* mesh = sceneSkinnedMeshPool.insert(boundingSphere);
		if(!mesh) return nullptr;

		// Make all allocations
		bool failedAlloc = false;
		for(uint32_t i = 0; i < lodCount; ++i) {
			auto vertexAlloc = sceneSkinnedVertexAllocator.allocate(vertexCounts[i] * sizeof(SkinnedVertex));
			if(!vertexAlloc) {
				failedAlloc = true;
				break;
			}

			auto indexAlloc = sceneSkinnedIndexAllocator.allocate(indexCounts[i] * sizeof(index_type));
			if(!indexAlloc) {
				failedAlloc = true;
				sceneSkinnedVertexAllocator.free(vertexAlloc);
				break;
			}

			mesh->lods[i].emplace(vertexAlloc.value(), indexAlloc.value());
		}

		// Cleanup
		if(failedAlloc) {
			for(uint32_t i = 0; i < lodCount; ++i) {
				if(mesh->lods[i]) {
					sceneSkinnedVertexAllocator.free(mesh->lods[i]->vertexAlloc);
					sceneSkinnedIndexAllocator.free(mesh->lods[i]->indexAlloc);
				}
			}

			sceneSkinnedMeshPool.remove(mesh);

			return nullptr;
		}

		// Critical section
		std::lock_guard<std::mutex> lock(uploadStreamMutex);

		const SkinnedVertex* lodVertices = vertices;
		const index_type* lodIndices = indices;
		for(uint32_t i = 0; i < lodCount; ++i) {
			FreeListAllocator::Allocation vertexAlloc = mesh->lods[i]->vertexAlloc;
			FreeListAllocator::Allocation indexAlloc = mesh->lods[i]->indexAlloc;

			// Enqueue vertex upload
			uploadStreamQueue.push(
				{
					[this, vertexAlloc, lodVertices](ID3D12GraphicsCommandList* commandList) {
						auto uploadAlloc = uploadStreamAllocator.allocate(vertexAlloc.size);
						if(!uploadAlloc) RIN_ERROR("Upload skinned vertices anomaly: out of upload stream space");

						commandList->CopyBufferRegion(
							sceneSkinnedVertexBuffer,
							vertexAlloc.start,
							uploadBuffer,
							uploadStreamOffset + uploadAlloc->start,
							vertexAlloc.size
						);

						memcpy(uploadBufferData + uploadStreamOffset + uploadAlloc->start, lodVertices, vertexAlloc.size);
					},
					vertexAlloc.size,
					COPY_QUEUE_DYNAMIC_SKINNED_VB_STATIC_IB_INDEX
				}
			);

			// Enqueue index upload
			bool finalUpload = i == lodCount - 1;
			uploadStreamQueue.push(
				{
					[this, indexAlloc, lodIndices, finalUpload, mesh](ID3D12GraphicsCommandList* commandList) {
						auto uploadAlloc = uploadStreamAllocator.allocate(indexAlloc.size);
						if(!uploadAlloc) RIN_ERROR("Upload skinned indices anomaly: out of upload stream space");

						commandList->CopyBufferRegion(
							sceneSkinnedIndexBuffer,
							indexAlloc.start,
							uploadBuffer,
							uploadStreamOffset + uploadAlloc->start,
							indexAlloc.size
						);

						memcpy(uploadBufferData + uploadStreamOffset + uploadAlloc->start, lodIndices, indexAlloc.size);

						if(finalUpload) mesh->_resident = true;
					},
					indexAlloc.size,
					COPY_QUEUE_STATIC_VB_DYNAMIC_SKINNED_IB_INDEX
				}
			);

			lodVertices += vertexCounts[i];
			lodIndices += indexCounts[i];
		}

		return mesh;
	}

	void D3D12Renderer::removeSkinnedMesh(SkinnedMesh* m) {
		if(!m) return;

		D3D12SkinnedMesh* mesh = (D3D12SkinnedMesh*)m;

		for(uint32_t i = 0; i < LOD_COUNT; ++i) {
			if(mesh->lods[i]) {
				sceneSkinnedVertexAllocator.free(mesh->lods[i]->vertexAlloc);
				sceneSkinnedIndexAllocator.free(mesh->lods[i]->indexAlloc);
			}
		}

		// Do this after freeing the allocations so that another thread does
		// not write over the lods if it gets a pointer that aliases this one
		sceneSkinnedMeshPool.remove(mesh);
	}

	SkinnedObject* D3D12Renderer::addSkinnedObject(SkinnedMesh* mesh, Armature* armature, Material* material) {
		if(!mesh || !armature || !material) return nullptr;

		SkinnedObject* object = sceneSkinnedObjectPool.insert(mesh, armature, material);

		updateSkinnedObject(object);

		return object;
	}

	void D3D12Renderer::removeSkinnedObject(SkinnedObject* object) {
		if(!object) return;

		{
			// Critical section
			std::lock_guard<std::mutex> lock(uploadStreamMutex);

			// Enqueue object removal
			uploadStreamQueue.push(
				{
					[this, object](ID3D12GraphicsCommandList* commandList) {
						// Zero the object out
						commandList->CopyBufferRegion(
							sceneSkinnedObjectBuffer,
							sceneSkinnedObjectPool.getIndex(object) * sizeof(D3D12SkinnedObjectData),
							sceneZeroBuffer,
							0,
							sizeof(D3D12SkinnedObjectData)
						);
					},
					0,
					COPY_QUEUE_CAMERA_STATIC_DYNAMIC_SKINNED_OB_LB_INDEX
				}
			);
		}

		// Need to remove from the pool only after pushing to the queue
		// so that if any other thread claims the object in the same spot
		// any updates to its data on the GPU are serialized after the remove
		sceneSkinnedObjectPool.remove(object);
	}

	void D3D12Renderer::updateSkinnedObject(SkinnedObject* object) {
		if(!object) return;

		// Enter critical section
		std::lock_guard<std::mutex> lock(uploadStreamMutex);

		// Enqueue object upload
		uploadStreamQueue.push(
			{
				[this, object](ID3D12GraphicsCommandList* commandList) {
					auto uploadAlloc = uploadStreamAllocator.allocate(sizeof(D3D12SkinnedObjectData));
					if(!uploadAlloc) RIN_ERROR("Upload skinned object anomaly: out of upload stream space");

					commandList->CopyBufferRegion(
						sceneSkinnedObjectBuffer,
						sceneSkinnedObjectPool.getIndex(object) * sizeof(D3D12SkinnedObjectData),
						uploadBuffer,
						uploadStreamOffset + uploadAlloc->start,
						sizeof(D3D12SkinnedObjectData)
					);

					D3D12SkinnedObjectData* objectData = (D3D12SkinnedObjectData*)(uploadBufferData + uploadStreamOffset + uploadAlloc->start);

					// The mesh will always be this derived type
					D3D12SkinnedMesh* objectMesh = (D3D12SkinnedMesh*)object->mesh;

					objectData->boundingSphere.center = objectMesh->boundingSphere.center;
					objectData->boundingSphere.radius = objectMesh->boundingSphere.radius;

					// All guaranteed to have at least lod 0
					D3D12SkinnedMesh::LOD lod = objectMesh->lods[0].value();
					// Populate LOD data
					for(uint32_t i = 0; i < LOD_COUNT; ++i) {
						if(i && objectMesh->lods[i]) lod = objectMesh->lods[i].value();

						objectData->lods[i].startIndex = (uint32_t)(lod.indexAlloc.start / sizeof(index_type));
						objectData->lods[i].indexCount = (uint32_t)(lod.indexAlloc.size / sizeof(index_type));
						objectData->lods[i].vertexOffset = (uint32_t)(lod.vertexAlloc.start / sizeof(SkinnedVertex));
					}

					Material* material = object->material;
					// The textures will always be this derived type
					objectData->material.baseColorID = sceneTexturePool.getIndex((D3D12Texture*)material->baseColor);
					objectData->material.normalID = sceneTexturePool.getIndex((D3D12Texture*)material->normal);
					objectData->material.roughnessAOID = sceneTexturePool.getIndex((D3D12Texture*)material->roughnessAO);
					if(material->metallic) objectData->material.metallicID = sceneTexturePool.getIndex((D3D12Texture*)material->metallic);
					objectData->material.heightID = sceneTexturePool.getIndex((D3D12Texture*)material->height);
					if(material->special) objectData->material.specialID = sceneTexturePool.getIndex((D3D12Texture*)material->special);

					// The armature will always be this derived type
					objectData->boneIndex = (uint32_t)((D3D12Armature*)object->armature)->boneAlloc.start;

					objectData->flags.show = 1;
					objectData->flags.materialType = (uint32_t)material->type;

					object->_resident = true;
				},
				sizeof(D3D12SkinnedObjectData),
				COPY_QUEUE_CAMERA_STATIC_DYNAMIC_SKINNED_OB_LB_INDEX
			}
		);
	}

	Armature* D3D12Renderer::addArmature(uint8_t boneCount) {
		auto boneAlloc = sceneBoneAllocator.allocate(boneCount);
		if(!boneAlloc) return nullptr;

		D3D12Armature* armature = sceneArmaturePool.insert(sceneBones + boneAlloc->start, boneAlloc.value());
		if(!armature) {
			sceneBoneAllocator.free(boneAlloc);
			return nullptr;
		}

		armature->_resident = true;

		return armature;
	}

	void D3D12Renderer::removeArmature(Armature* a) {
		if(!a) return;

		D3D12Armature* armature = (D3D12Armature*)a;

		sceneBoneAllocator.free(armature->boneAlloc);

		// Do this after freeing the allocation so that another thread does
		// not write over the armature if it gets a pointer that aliases this one
		sceneArmaturePool.remove(armature);
	}

	Texture* D3D12Renderer::addTexture(
		TEXTURE_TYPE type,
		TEXTURE_FORMAT format,
		uint32_t width,
		uint32_t height,
		uint32_t mipCount,
		const char* textureData
	) {
		// Validation
		if(!width) RIN_ERROR("Texture width cannot be 0");
		if(!height) RIN_ERROR("Texture height cannot be 0");
		if(!mipCount) RIN_ERROR("Texture MIP count cannot be 0");
		
		// It seems that _Ceiling_of_log_2(1) returns 1 instead of 0, so this is a workaround
		uint32_t maxDim = std::max(width, height);
		if(maxDim == 1) mipCount = 1;
		else mipCount = std::min(mipCount, (uint32_t)std::_Ceiling_of_log_2(maxDim) + 1);

		uint16_t arraySize = 0;
		switch(type) {
		case TEXTURE_TYPE::TEXTURE_2D:
			arraySize = 1;
			break;
		case TEXTURE_TYPE::TEXTURE_CUBE:
			arraySize = 6;
			break;
		}

		// Get allocation info
		DXGI_FORMAT dxgiFormat = getFormat(format);

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		resourceDesc.Width = width;
		resourceDesc.Height = height;
		resourceDesc.DepthOrArraySize = arraySize;
		resourceDesc.MipLevels = mipCount; // mipCount will never be greater than 32, so it won't exceed UINT16_MAX
		resourceDesc.Format = dxgiFormat;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12_RESOURCE_ALLOCATION_INFO heapInfo = device->GetResourceAllocationInfo(0, 1, &resourceDesc);
		uint64_t alignedTextureSize = heapInfo.SizeInBytes;

		// Make allocation
		auto textureAlloc = sceneTextureAllocator.allocate(alignedTextureSize);
		if(!textureAlloc) return nullptr;

		// Create texture
		ID3D12Resource* resource;
		HRESULT result = device->CreatePlacedResource(
			sceneTextureHeap,
			sceneTextureOffset + textureAlloc->start,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&resource)
		);
		if(FAILED(result)) RIN_ERROR("Failed to create texture");

		D3D12Texture* texture = sceneTexturePool.insert(type, format, textureAlloc.value(), resource);
		if(!texture) {
			sceneTextureAllocator.free(textureAlloc);
			return nullptr;
		}

		// Create srv
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = dxgiFormat;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		switch(type) {
		case TEXTURE_TYPE::TEXTURE_2D:
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = mipCount;
			srvDesc.Texture2D.PlaneSlice = 0;
			srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
			break;
		case TEXTURE_TYPE::TEXTURE_CUBE:
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.MipLevels = mipCount;
			srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
			break;
		}

		device->CreateShaderResourceView(resource, &srvDesc, getSceneDescHeapCPUHandle(SCENE_TEXTURE_SRV_OFFSET + sceneTexturePool.getIndex(texture)));
		
		// Critical section
		std::lock_guard<std::mutex> lock(uploadStreamMutex);

		// Enqueue texture upload
		uploadStreamQueue.push(
			{
				[this, format, width, height, arraySize, mipCount, textureData, alignedTextureSize, texture, resource, dxgiFormat](ID3D12GraphicsCommandList* commandList) mutable {
					// Texture data must be aligned, so allocate extra space to ensure we can align it
					auto uploadAlloc = uploadStreamAllocator.allocate(alignedTextureSize + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
					if(!uploadAlloc) RIN_ERROR("Upload texture anomaly: out of upload stream space");

					uint64_t alignedStart = ALIGN_TO(uploadStreamOffset + uploadAlloc->start, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

					char* alignedData = uploadBufferData + alignedStart;

					D3D12_TEXTURE_COPY_LOCATION copyDest{};
					copyDest.pResource = resource;
					copyDest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
					copyDest.SubresourceIndex = 0;

					D3D12_TEXTURE_COPY_LOCATION copySrc{};
					copySrc.pResource = uploadBuffer;
					copySrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
					copySrc.PlacedFootprint.Offset = alignedStart;
					copySrc.PlacedFootprint.Footprint.Format = dxgiFormat;
					copySrc.PlacedFootprint.Footprint.Depth = 1;

					uint32_t blockWidth = Texture::getBlockWidth(format);
					uint32_t blockHeight = Texture::getBlockHeight(format);

					for(uint16_t slice = 0; slice < arraySize; ++slice) {
						for(uint32_t mip = 0; mip < mipCount; ++mip) {
							uint32_t sliceWidth = std::max(width >> mip, (uint32_t)1);
							uint32_t sliceHeight = std::max(height >> mip, (uint32_t)1);
							uint64_t pitch = Texture::getRowPitch(sliceWidth, format);
							uint64_t alignedPitch = ALIGN_TO(pitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
							uint64_t rowCount = Texture::getRowCount(sliceHeight, format);
							uint64_t mipSize = alignedPitch * rowCount;

							// Upload
							copySrc.PlacedFootprint.Footprint.Width = ALIGN_TO(sliceWidth, blockWidth);
							copySrc.PlacedFootprint.Footprint.Height = ALIGN_TO(sliceHeight, blockHeight);
							copySrc.PlacedFootprint.Footprint.RowPitch = (uint32_t)alignedPitch;

							commandList->CopyTextureRegion(&copyDest, 0, 0, 0, &copySrc, nullptr);

							++copyDest.SubresourceIndex;

							copySrc.PlacedFootprint.Offset += ALIGN_TO(mipSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

							// Copy
							for(uint32_t row = 0; row < rowCount; ++row) {
								memcpy(alignedData, textureData, pitch);
								alignedData += alignedPitch;
								textureData += pitch;
							}

							alignedData += mipSize % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
						}
					}

					texture->_resident = true;
				},
				alignedTextureSize,
				COPY_QUEUE_TEXTURE_INDEX
			}
		);

		return texture;
	}

	void D3D12Renderer::removeTexture(Texture* t) {
		if(!t) return;

		D3D12Texture* texture = (D3D12Texture*)t;

		texture->dead = true;
	}

	Material* D3D12Renderer::addMaterial(
		MATERIAL_TYPE type,
		Texture* baseColor,
		Texture* normal,
		Texture* roughnessAO,
		Texture* metallic,
		Texture* height,
		Texture* special
	) {
		Material* material = sceneMaterialPool.insert(type, baseColor, normal, roughnessAO, metallic, height, special);

		return material;
	}

	void D3D12Renderer::removeMaterial(Material* material) {
		if(!material) return;

		sceneMaterialPool.remove(material);
	}

	Light* D3D12Renderer::addLight() {
		Light* light = sceneLightPool.insert();
		if(!light) return nullptr;

		light->_resident = true;

		return light;
	}

	void D3D12Renderer::removeLight(Light* light) {
		if(!light) return;

		sceneLightPool.remove(light);
	}

	void D3D12Renderer::setSkybox(Texture* skybox, Texture* iblDiffuse, Texture* iblSpecular) {
		if(!skybox || !iblDiffuse || !iblSpecular) return;

		// Validation
		if(skybox->type != TEXTURE_TYPE::TEXTURE_CUBE) RIN_ERROR("Skybox texture must be type texture cube");
		if(iblDiffuse->type != TEXTURE_TYPE::TEXTURE_CUBE) RIN_ERROR("Diffuse IBL texture must be type texture cube");
		if(iblSpecular->type != TEXTURE_TYPE::TEXTURE_CUBE) RIN_ERROR("Specular IBL texture must be type texture cube");

		switch(skybox->format) {
		case TEXTURE_FORMAT::R16B16G16A16_FLOAT:
		case TEXTURE_FORMAT::R32G32B32A32_FLOAT:
		case TEXTURE_FORMAT::BC6H_FLOAT:
			break;
		default:
			RIN_ERROR("Invalid skybox texture format");
		}

		switch(iblDiffuse->format) {
		case TEXTURE_FORMAT::R16B16G16A16_FLOAT:
		case TEXTURE_FORMAT::R32G32B32A32_FLOAT:
		case TEXTURE_FORMAT::BC6H_FLOAT:
			break;
		default:
			RIN_ERROR("Invalid IBL diffuse texture format");
		}

		switch(iblSpecular->format) {
		case TEXTURE_FORMAT::R16B16G16A16_FLOAT:
		case TEXTURE_FORMAT::R32G32B32A32_FLOAT:
		case TEXTURE_FORMAT::BC6H_FLOAT:
			break;
		default:
			RIN_ERROR("Invalid IBL specular texture format");
		}

		// Create scene skybox texture SRV
		D3D12Texture* skyboxTexture = (D3D12Texture*)skybox;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = getFormat(skyboxTexture->format);
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.TextureCube.MostDetailedMip = 0;
		srvDesc.TextureCube.MipLevels = 1;
		srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

		device->CreateShaderResourceView(skyboxTexture->resource, &srvDesc, getSceneDescHeapCPUHandle(SCENE_SKYBOX_TEXTURE_SRV_OFFSET));

		// Create scene IBL diffuse texture SRV
		D3D12Texture* iblDiffuseTexture = (D3D12Texture*)iblDiffuse;

		srvDesc.Format = getFormat(iblDiffuseTexture->format);

		device->CreateShaderResourceView(iblDiffuseTexture->resource, &srvDesc, getSceneDescHeapCPUHandle(SCENE_IBL_DIFFUSE_TEXTURE_SRV_OFFSET));

		// Create scene IBL specular texture SRV
		ID3D12Resource* iblSpecularResource = ((D3D12Texture*)iblSpecular)->resource;
		D3D12_RESOURCE_DESC iblSpecularResourceDesc = iblSpecularResource->GetDesc();

		srvDesc.Format = iblSpecularResourceDesc.Format;
		srvDesc.TextureCube.MipLevels = iblSpecularResourceDesc.MipLevels;

		device->CreateShaderResourceView(iblSpecularResource, &srvDesc, getSceneDescHeapCPUHandle(SCENE_IBL_SPECULAR_TEXTURE_SRV_OFFSET));

		sceneIBLSpecularMIPCount = iblSpecularResourceDesc.MipLevels;

		skyboxDirty = true;
	}

	void D3D12Renderer::clearSkybox() {
		// Create scene skybox texture SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.TextureCube.MostDetailedMip = 0;
		srvDesc.TextureCube.MipLevels = 1;
		srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

		device->CreateShaderResourceView(sceneZeroCubeTexture, &srvDesc, getSceneDescHeapCPUHandle(SCENE_SKYBOX_TEXTURE_SRV_OFFSET));

		// Create scene IBL diffuse texture SRV
		device->CreateShaderResourceView(sceneZeroCubeTexture, &srvDesc, getSceneDescHeapCPUHandle(SCENE_IBL_DIFFUSE_TEXTURE_SRV_OFFSET));

		// Create scene IBL specular texture SRV
		device->CreateShaderResourceView(sceneZeroCubeTexture, &srvDesc, getSceneDescHeapCPUHandle(SCENE_IBL_SPECULAR_TEXTURE_SRV_OFFSET));

		sceneIBLSpecularMIPCount = 1;

		skyboxDirty = true;
	}

	void D3D12Renderer::uploadDynamicObjectHelper(uint32_t startIndex, uint32_t endIndex) {
		D3D12DynamicObjectData* dataStart = (D3D12DynamicObjectData*)(uploadBufferData + uploadDynamicObjectOffset);

		for(uint32_t i = startIndex; i < endIndex; ++i) {
			DynamicObject* object = sceneDynamicObjectPool.at(i);
			D3D12DynamicObjectData* objectData = dataStart + i;

			if(object && object->resident()) {
				DirectX::XMStoreFloat4x4A(&objectData->worldMatrix, object->worldMatrix);
				DirectX::XMStoreFloat4x4A(&objectData->invWorldMatrix, object->invWorldMatrix);

				// The mesh will always be this derived type
				D3D12DynamicMesh* mesh = (D3D12DynamicMesh*)object->mesh;

				objectData->boundingSphere.center = mesh->boundingSphere.center;
				objectData->boundingSphere.radius = mesh->boundingSphere.radius;

				// All guaranteed to have at least lod 0
				D3D12DynamicMesh::LOD lod = mesh->lods[0].value();
				// Populate LOD data
				for(uint32_t i = 0; i < LOD_COUNT; ++i) {
					if(i && mesh->lods[i]) lod = mesh->lods[i].value();

					objectData->lods[i].startIndex = (uint32_t)(lod.indexAlloc.start / sizeof(index_type));
					objectData->lods[i].indexCount = (uint32_t)(lod.indexAlloc.size / sizeof(index_type));
					objectData->lods[i].vertexOffset = (uint32_t)(lod.vertexAlloc.start / sizeof(DynamicVertex));
				}

				Material* material = object->material;
				// The textures will always be this derived type
				objectData->material.baseColorID = sceneTexturePool.getIndex((D3D12Texture*)material->baseColor);
				objectData->material.normalID = sceneTexturePool.getIndex((D3D12Texture*)material->normal);
				objectData->material.roughnessAOID = sceneTexturePool.getIndex((D3D12Texture*)material->roughnessAO);
				if(material->metallic) objectData->material.metallicID = sceneTexturePool.getIndex((D3D12Texture*)material->metallic);
				objectData->material.heightID = sceneTexturePool.getIndex((D3D12Texture*)material->height);
				if(material->special) objectData->material.specialID = sceneTexturePool.getIndex((D3D12Texture*)material->special);

				objectData->flags.show = 1;
				objectData->flags.materialType = (uint32_t)material->type;
			} else objectData->flags.data = 0;
		}
	}

	void D3D12Renderer::uploadBoneHelper(uint32_t startIndex, uint32_t endIndex) {
		D3D12BoneData* dataStart = (D3D12BoneData*)(uploadBufferData + uploadBoneOffset);

		for(uint32_t i = startIndex; i < endIndex; ++i) {
			Bone* bone = sceneBones + i;
			D3D12BoneData* boneData = dataStart + i;

			DirectX::XMStoreFloat4x4A(&boneData->worldMatrix, bone->worldMatrix);
			DirectX::XMStoreFloat4x4A(&boneData->invWorldMatrix, bone->invWorldMatrix);
		}
	}

	void D3D12Renderer::uploadLightHelper(uint32_t startIndex, uint32_t endIndex) {
		D3D12LightData* dataStart = (D3D12LightData*)(uploadBufferData + uploadLightOffset);

		for(uint32_t i = startIndex; i < endIndex; ++i) {
			Light* light = sceneLightPool.at(i);
			D3D12LightData* lightData = dataStart + i;

			if(light && light->resident()) {
				lightData->position = light->position;
				lightData->radius = light->radius;
				lightData->color = light->color;

				lightData->flags.show = 1;
			} else lightData->flags.data = 0;
		}
	}

	void D3D12Renderer::update() {
		// Permit new uploads to be scheduled
		uploadStreamAllocator.free();

		// No other thread has the mutex so this is safe
		uploadStreamBudget = uploadStreamAllocator.getSize();

		uploadStreamBarrier.arrive_and_wait(); // Barrier 1

		// Begin recording
		HRESULT result = uploadUpdateCommandAllocator->Reset();
		if(FAILED(result)) RIN_ERROR("Failed to reset upload update command allocator");

		result = uploadUpdateCommandList->Reset(uploadUpdateCommandAllocator, nullptr);
		if(FAILED(result)) RIN_ERROR("Failed to reset upload update command list");

		// Upload camera
		uploadUpdateCommandList->CopyBufferRegion(
			sceneCameraBuffer,
			0,
			uploadBuffer,
			uploadCameraOffset,
			sizeof(D3D12CameraData)
		);

		D3D12CameraData* cameraData = (D3D12CameraData*)(uploadBufferData + uploadCameraOffset);
		DirectX::XMStoreFloat4x4A(&cameraData->viewMatrix, sceneCamera.viewMatrix);
		DirectX::XMStoreFloat4x4A(&cameraData->projMatrix, sceneCamera.projMatrix);
		DirectX::XMStoreFloat4x4A(&cameraData->invProjMatrix, sceneCamera.invProjMatrix);
		DirectX::XMStoreFloat4x4A(&cameraData->viewProjMatrix, DirectX::XMMatrixMultiply(sceneCamera.viewMatrix, sceneCamera.projMatrix));
		DirectX::XMStoreFloat3A(&cameraData->position, sceneCamera.getPosition());

		cameraData->frustumXX = sceneCamera.frustumXX;
		cameraData->frustumXZ = sceneCamera.frustumXZ;
		cameraData->frustumYY = sceneCamera.frustumYY;
		cameraData->frustumYZ = sceneCamera.frustumYZ;

		cameraData->nearZ = sceneCamera.nearZ;
		cameraData->farZ = sceneCamera.farZ;

		cameraData->clusterConstantA = sceneCamera.clusterConstantA;
		cameraData->clusterConstantB = sceneCamera.clusterConstantB;

		// Upload dynamic objects
		uploadUpdateCommandList->CopyBufferRegion(
			sceneDynamicObjectBuffer,
			0,
			uploadBuffer,
			uploadDynamicObjectOffset,
			config.dynamicObjectCount * sizeof(D3D12DynamicObjectData)
		);

		// Upload bones
		uploadUpdateCommandList->CopyBufferRegion(
			sceneBoneBuffer,
			0,
			uploadBuffer,
			uploadBoneOffset,
			config.boneCount * sizeof(D3D12BoneData)
		);

		// Upload lights
		uploadUpdateCommandList->CopyBufferRegion(
			sceneLightBuffer,
			0,
			uploadBuffer,
			uploadLightOffset,
			config.lightCount * sizeof(D3D12LightData)
		);

		// Exclude the current thread to avoid an extra context switch
		const uint32_t spareThreads = COPY_QUEUE_COUNT >= threadPool.numThreads ? 0 : threadPool.numThreads - COPY_QUEUE_COUNT - 1;

		const uint32_t dynamicObjectStep = config.dynamicObjectCount / (spareThreads + 1);
		uint32_t dynamicObjectStartIndex = 0;
		for(uint32_t i = 0; i < spareThreads; ++i) {
			const uint32_t dynamicObjectEndIndex = dynamicObjectStartIndex + dynamicObjectStep;
			threadPool.enqueueJob([this, dynamicObjectStartIndex, dynamicObjectEndIndex]() { uploadDynamicObjectHelper(dynamicObjectStartIndex, dynamicObjectEndIndex); });
			dynamicObjectStartIndex = dynamicObjectEndIndex;
		}

		const uint32_t boneStep = config.boneCount / (spareThreads + 1);
		uint32_t boneStartIndex = 0;
		for(uint32_t i = 0; i < spareThreads; ++i) {
			const uint32_t boneEndIndex = boneStartIndex + boneStep;
			threadPool.enqueueJob([this, boneStartIndex, boneEndIndex]() { uploadBoneHelper(boneStartIndex, boneEndIndex); });
			boneStartIndex = boneEndIndex;
		}

		const uint32_t lightStep = config.lightCount / (spareThreads + 1);
		uint32_t lightStartIndex = 0;
		for(uint32_t i = 0; i < spareThreads; ++i) {
			const uint32_t lightEndIndex = lightStartIndex + lightStep;
			threadPool.enqueueJob([this, lightStartIndex, lightEndIndex]() { uploadLightHelper(lightStartIndex, lightEndIndex); });
			lightStartIndex = lightEndIndex;
		}

		uploadDynamicObjectHelper(dynamicObjectStartIndex, config.dynamicObjectCount);
		uploadBoneHelper(boneStartIndex, config.boneCount);
		uploadLightHelper(lightStartIndex, config.lightCount);

		if(spareThreads) threadPool.wait();

		// Submit command list
		result = uploadUpdateCommandList->Close();
		if(FAILED(result)) RIN_ERROR("Failed to close upload update command list");

		copyQueues[COPY_QUEUE_CAMERA_STATIC_DYNAMIC_SKINNED_OB_LB_INDEX]->ExecuteCommandLists(1, (ID3D12CommandList**)&uploadUpdateCommandList);

		uploadStreamBarrier.arrive_and_wait(); // Barrier 2
	}

	void D3D12Renderer::render() {
		HRESULT result;

		// Signal copy queues
		for(uint32_t i = 0; i < COPY_QUEUE_COUNT; ++i) {
			result = copyQueues[i]->Signal(copyFences[i], ++copyFenceValues[i]);
			if(FAILED(result)) RIN_ERROR("Failed to signal copy queue");
		}

		// Wait for copy queues to finish
		// These are likely still running
		result = device->SetEventOnMultipleFenceCompletion(copyFences, copyFenceValues, COPY_QUEUE_COUNT, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, nullptr);
		if(FAILED(result)) RIN_ERROR("Failed to wait for copy queues");

		// Ensure previous frame is done being drawn so we can reset the command allocators
		// Need to check this in case nothing was uploaded
		if(graphicsFence->GetCompletedValue() < graphicsFenceValue) {
			result = graphicsFence->SetEventOnCompletion(graphicsFenceValue, nullptr);
			if(FAILED(result)) RIN_ERROR("Failed to set graphics fence event");
		}

		// Release all of the dead textures since the previous frame is finished
		destroyDeadTextures();

		// Record commands
		if(skyboxDirty) {
			threadPool.enqueueJob([this]() { recordSceneStaticCommandList(); });
			threadPool.enqueueJob([this]() { recordSceneDynamicCommandList(); });
			threadPool.enqueueJob([this]() { recordSceneSkinnedCommandList(); });
			threadPool.enqueueJob([this]() { recordSkyboxCommandList(); });
		}
		recordPostCommandList();
		if(skyboxDirty) {
			threadPool.wait();
			skyboxDirty = false;
		}

		// Read debug query data
	#ifdef RIN_DEBUG
		D3D12_QUERY_DATA_PIPELINE_STATISTICS debugStaticRender = ((D3D12_QUERY_DATA_PIPELINE_STATISTICS*)debugQueryData)[DEBUG_QUERY_PIPELINE_STATIC_RENDER];
		D3D12_QUERY_DATA_PIPELINE_STATISTICS debugDynamicRender = ((D3D12_QUERY_DATA_PIPELINE_STATISTICS*)debugQueryData)[DEBUG_QUERY_PIPELINE_DYNAMIC_RENDER];
		D3D12_QUERY_DATA_PIPELINE_STATISTICS debugSkinnedRender = ((D3D12_QUERY_DATA_PIPELINE_STATISTICS*)debugQueryData)[DEBUG_QUERY_PIPELINE_SKINNED_RENDER];

		std::wostringstream windowTitle;
		windowTitle << "Debug Info: " << debugStaticRender.VSInvocations << " Static Vertices " <<
			debugDynamicRender.VSInvocations << " Dynamic Vertices " <<
			debugSkinnedRender.VSInvocations << " Skinned Vertices" << std::endl;

		SetWindowText(hwnd, windowTitle.str().c_str());
	#endif

		// Fence values when this frame is finished
		++graphicsFenceValue;

		// Submit scene commands
		ID3D12CommandList* computeCommands[]{ depthMIPCommandList, lightClusterCommandList };
		computeQueue->ExecuteCommandLists(_countof(computeCommands), computeCommands);

		// Culling
		ID3D12CommandList* cullCommands[]{ cullStaticCommandList, cullDynamicCommandList, cullSkinnedCommandList };
		computeQueue->ExecuteCommandLists(_countof(cullCommands), cullCommands);
		result = computeQueue->Signal(computeFence, ++computeFenceValue);
		if(FAILED(result)) RIN_ERROR("Failed to signal compute queue");
		
		// Scene rendering
		result = graphicsQueue->Wait(computeFence, computeFenceValue);
		if(FAILED(result)) RIN_ERROR("Failed to make graphics queue wait on compute queue");
		ID3D12CommandList* sceneCommands[]{ sceneStaticCommandList, sceneDynamicCommandList, sceneSkinnedCommandList, skyboxCommandList };
		graphicsQueue->ExecuteCommandLists(_countof(sceneCommands), sceneCommands);
			
		// Post processing
		graphicsQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&postCommandList);

		// Present without vsync
		/*
		NOTE:
		It appears that Present blocks the copy queue (maybe uses a copy command)
		when in windowed mode (for window composition), not only that, but not
		every card supports concurrent copy, therefore resource copies must
		be synchronized every frame, although it is fine to let them start
		while the current frame is being drawn if it is safe, which takes
		advantage of any copy concurrency
		*/
		result = swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
		if(FAILED(result)) RIN_DEBUG_ERROR("Failed to present frame on swap chain");

		// Stall any sync copies until rendering is completed
		copyQueues[COPY_QUEUE_CAMERA_STATIC_DYNAMIC_SKINNED_OB_LB_INDEX]->Wait(graphicsFence, graphicsFenceValue);

		// Frame synchronization
		result = graphicsQueue->Signal(graphicsFence, graphicsFenceValue);
		if(FAILED(result)) RIN_ERROR("Failed to signal graphics queue");
		
		backBufferIndex = swapChain->GetCurrentBackBufferIndex();
	}

	void D3D12Renderer::resizeSwapChain() {
		HRESULT result;

		// Wait until the swap chain is not in use
		if(graphicsFence->GetCompletedValue() < graphicsFenceValue) {
			result = graphicsFence->SetEventOnCompletion(graphicsFenceValue, nullptr);
			if(FAILED(result)) RIN_ERROR("Failed to set graphics fence event");
		}

		// Release old back buffer handles
		destroySwapChainDependencies();

		// Resize the swapchain
		result = swapChain->ResizeBuffers(settings.backBufferCount, 0, 0, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
		if(FAILED(result)) RIN_ERROR("Failed to resize swap chain");

		// Update swap chain dependent values
		backBufferIndex = swapChain->GetCurrentBackBufferIndex();

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
		result = swapChain->GetDesc1(&swapChainDesc);
		if(FAILED(result)) RIN_ERROR("Failed to get the swap chain desc");
		backBufferViewport.Width = (float)swapChainDesc.Width;
		backBufferViewport.Height = (float)swapChainDesc.Height;
		backBufferScissorRect.right = swapChainDesc.Width;
		backBufferScissorRect.bottom = swapChainDesc.Height;

		// Create new back buffer handles
		createSwapChainDependencies();
		
	#ifdef RIN_DEBUG
		std::cout << "RIN INFO:\tFS: " << (settings.fullscreen ? "Y" : "N")
			<< " BB: " << swapChainDesc.BufferCount
			<< " WxH: " << swapChainDesc.Width << "x" << swapChainDesc.Height << std::endl;
	#endif
	}

	void D3D12Renderer::toggleFullScreen() {
		settings.fullscreen = !settings.fullscreen;

		if(settings.fullscreen) {
			// Save hwnd rect
			if(!GetWindowRect(hwnd, &hwndRect)) {
				RIN_DEBUG_ERROR("Failed to get hwnd rect");
				return;
			}
		}

		showWindow();
	}

	void D3D12Renderer::showWindow() {
		if(settings.fullscreen) {
			// Borderless window style
			// See: https://docs.microsoft.com/en-us/windows/win32/winmsg/window-styles
			// hwndStyle \ WS_OVERLAPPEDWINDOW
			if(!SetWindowLong(hwnd, GWL_STYLE, hwndStyle & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX))) {
				RIN_DEBUG_ERROR("Failed to set hwnd style");
			}
			// Apply style
			if(!SetWindowPos(
				hwnd,
				HWND_TOPMOST,
				0, 0, 0, 0,
				SWP_FRAMECHANGED| SWP_NOMOVE | SWP_NOSIZE
			)) {
				RIN_DEBUG_ERROR("Failed to update hwnd");
				return;
			}
			ShowWindow(hwnd, SW_MAXIMIZE); // Triggers WM_SIZE
		} else {
			// Default style
			if(!SetWindowLong(hwnd, GWL_STYLE, hwndStyle)) {
				RIN_DEBUG_ERROR("Failed to set hwnd style");
				return;
			}
			// Apply style and rect
			if(!SetWindowPos(
				hwnd,
				HWND_NOTOPMOST,
				hwndRect.left,
				hwndRect.top,
				hwndRect.right - hwndRect.left,
				hwndRect.bottom - hwndRect.top,
				SWP_FRAMECHANGED
			)) {
				RIN_DEBUG_ERROR("Failed to update hwnd");
				return;
			}
			ShowWindow(hwnd, SW_NORMAL); // Triggers WM_SIZE
		}
	}

	void D3D12Renderer::applySettings(const Settings& other) {
		if(other.backBufferCount < 2) RIN_ERROR("Back buffer count must be at least 2");

		// Swap chain resizing
		settings.backBufferCount = other.backBufferCount;
		if(settings.fullscreen != other.fullscreen) {
			toggleFullScreen(); // Will result in resizeSwapChain being called
		} else if(settings.backBufferCount != numBackBuffers)
			resizeSwapChain();

		// Resolution resizing
		if(
			settings.backBufferWidth != other.backBufferWidth ||
			settings.backBufferHeight != other.backBufferHeight
		) {
			if(!other.backBufferWidth) RIN_ERROR("Cannot have back buffer width 0");
			if(!other.backBufferHeight) RIN_ERROR("Cannot have back buffer height 0");

			settings.backBufferWidth = other.backBufferWidth;
			settings.backBufferHeight = other.backBufferHeight;

			// Wait until the back buffer is not in use
			if(graphicsFence->GetCompletedValue() < graphicsFenceValue) {
				HRESULT result = graphicsFence->SetEventOnCompletion(graphicsFenceValue, nullptr);
				if(FAILED(result)) RIN_ERROR("Failed to set graphics fence event");
			}

			destroySceneBackBuffer();
			createSceneBackBuffer();

			// Record the scene rendering commands after the back buffer is recreated
			threadPool.enqueueJob([this]() { recordCullStaticCommandList(); });
			threadPool.enqueueJob([this]() { recordCullDynamicCommandList(); });
			threadPool.enqueueJob([this]() { recordCullSkinnedCommandList(); });
			threadPool.enqueueJob([this]() { recordSceneStaticCommandList(); });
			threadPool.enqueueJob([this]() { recordSceneDynamicCommandList(); });
			threadPool.enqueueJob([this]() { recordSceneSkinnedCommandList(); });
			threadPool.enqueueJob([this]() { recordSkyboxCommandList(); });
			recordDepthMIPCommandList();
			threadPool.wait();
		}
	}
}