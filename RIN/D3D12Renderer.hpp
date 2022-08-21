#pragma once

#define NOMINMAX

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include <d3d12.h>
#include <dxgi1_4.h>
#include <barrier>

#include "Renderer.hpp"
#include "Debug.hpp"
#include "ThreadPool.hpp"
#include "FreeListAllocator.hpp"
#include "BumpAllocator.hpp"
#include "Pool.hpp"
#include "D3D12Camera.hpp"
#include "D3D12StaticMesh.hpp"
#include "D3D12DynamicMesh.hpp"
#include "D3D12Texture.hpp"

/*
TODO: Test zero config
*/

namespace RIN {
	class D3D12Renderer : public Renderer {
		friend Renderer* Renderer::create(HWND, const Config&, const Settings&);
		friend void Renderer::destroy(Renderer*) noexcept;

		/*
		Resources are assigned to specific copy queues to avoid conflicts
		and to keep workload consistent between queues
		*/
		static constexpr uint32_t COPY_QUEUE_COUNT = 4;

		typedef std::function<void(ID3D12GraphicsCommandList*)> upload_stream_job_type;

		struct UploadStreamRequest {
			upload_stream_job_type job;
			uint64_t size;
			uint32_t copyQueueIndex;
		};

		HWND hwnd;
		DWORD hwndStyle;
		RECT hwndRect{};

		ThreadPool threadPool;

		// Device
	#ifdef RIN_DEBUG
		ID3D12Debug1* debug{};
	#endif
		ID3D12Device4* device{};
		uint32_t rtvHeapStep;
		uint32_t dsvHeapStep;
		uint32_t cbvsrvuavHeapStep;

	#ifdef RIN_DEBUG
		// Debug queries
		ID3D12Resource* debugQueryDataBuffer{}; // Committed
		char* debugQueryData{};
		ID3D12QueryHeap* debugQueryPipelineHeap{};
	#endif

		// Swap chain
		ID3D12CommandQueue* graphicsQueue{};
		ID3D12Fence* graphicsFence{};
		uint64_t graphicsFenceValue;
		IDXGISwapChain3* swapChain{};
		ID3D12DescriptorHeap* backBufferDescHeap{};
		ID3D12Resource** backBuffers{};
		uint32_t numBackBuffers;
		uint32_t backBufferIndex;
		D3D12_VIEWPORT backBufferViewport{};
		D3D12_RECT backBufferScissorRect{};

		// Compute
		ID3D12CommandQueue* computeQueue{};
		ID3D12Fence* computeFence{};
		uint64_t computeFenceValue;

		/*
		Both sync and async copies must be synchronized with the frame
		and can only start after a frame has been submitted on the
		graphics queue, however, a sync copy must only begin after
		the frame finished on the GPU, whereas an async copy can
		begin before the frame is finished on the GPU

		NOTE:
		It is important that no upload submission span more than
		a single frame because large copies will delay rendering

		NEVER read from uploadBufferData because it points to mapped
		memory and reads from it are extremely slow on the CPU
		*/
		// Resource uploading
		ID3D12CommandQueue* copyQueues[COPY_QUEUE_COUNT]{};
		ID3D12Fence* copyFences[COPY_QUEUE_COUNT]{};
		uint64_t copyFenceValues[COPY_QUEUE_COUNT];
		ID3D12Resource* uploadBuffer{}; // Committed
		char* uploadBufferData{};
		uint64_t uploadCameraOffset;
		uint64_t uploadDynamicObjectOffset;
		uint64_t uploadStreamOffset;
		// Upload stream
		ID3D12CommandAllocator* uploadUpdateCommandAllocator{};
		ID3D12GraphicsCommandList* uploadUpdateCommandList{};
		BumpAllocator uploadStreamAllocator;
		std::mutex uploadStreamMutex;
		std::barrier<> uploadStreamBarrier{COPY_QUEUE_COUNT + 1};
		std::thread uploadStreamThreads[COPY_QUEUE_COUNT]{};
		std::queue<UploadStreamRequest> uploadStreamQueue;
		uint64_t uploadStreamBudget{};
		bool uploadStreamTerminate = false;

		// Scene
		ID3D12DescriptorHeap* sceneDescHeap{};
		// Depth MIP Mapping
		ID3D12RootSignature* depthMIPRootSignature{};
		ID3D12PipelineState* depthMIPPipelineState{};
		ID3D12CommandAllocator* depthMIPCommandAllocator{};
		ID3D12GraphicsCommandList* depthMIPCommandList{};
		// Culling
		ID3D12RootSignature* cullStaticRootSignature{};
		ID3D12PipelineState* cullStaticPipelineState{};
		ID3D12RootSignature* cullDynamicRootSignature{};
		ID3D12PipelineState* cullDynamicPipelineState{};
		ID3D12CommandAllocator* cullStaticCommandAllocator{};
		ID3D12GraphicsCommandList* cullStaticCommandList{};
		ID3D12CommandAllocator* cullDynamicCommandAllocator{};
		ID3D12GraphicsCommandList* cullDynamicCommandList{};
		// Scene rendering
		ID3D12DescriptorHeap* sceneRTVDescHeap{};
		ID3D12DescriptorHeap* sceneDSVDescHeap{};
		ID3D12RootSignature* sceneStaticRootSignature{};
		ID3D12CommandSignature* sceneStaticCommandSignature{};
		ID3D12PipelineState* sceneStaticPBRPipelineState{};
		ID3D12RootSignature* sceneDynamicRootSignature{};
		ID3D12CommandSignature* sceneDynamicCommandSignature{};
		ID3D12PipelineState* sceneDynamicPBRPipelineState{};
		ID3D12CommandAllocator* sceneStaticCommandAllocator{};
		ID3D12GraphicsCommandList* sceneStaticCommandList{};
		ID3D12CommandAllocator* sceneDynamicCommandAllocator{};
		ID3D12GraphicsCommandList* sceneDynamicCommandList{};
		// Skybox
		ID3D12RootSignature* skyboxRootSignature{};
		ID3D12PipelineState* skyboxPipelineState{};
		ID3D12CommandAllocator* skyboxCommandAllocator{};
		ID3D12GraphicsCommandList* skyboxCommandList{};
		// Post processing
		ID3D12RootSignature* postRootSignature{};
		ID3D12PipelineState* postPipelineState{};
		ID3D12CommandAllocator* postCommandAllocator{};
		ID3D12GraphicsCommandList* postCommandList{};
		// Scene back buffer
		ID3D12Heap* sceneBackBufferHeap{};
		ID3D12Resource* sceneBackBuffer{};
		ID3D12Resource* sceneDepthBuffer{};
		ID3D12Resource* sceneDepthHierarchy{}; // Committed
		D3D12_VIEWPORT sceneBackBufferViewport{};
		D3D12_RECT sceneBackBufferScissorRect{};
		uint32_t sceneDepthHierarchyWidth{};
		uint32_t sceneDepthHierarchyHeight{};
		uint32_t sceneDepthHierarchyLevels{};
		// Resource management
		ID3D12Heap* sceneBufferHeap{};
		ID3D12Resource* sceneZeroBuffer{}; // For clearing uav counters
		ID3D12Resource* sceneCameraBuffer{};
		ID3D12Resource* sceneStaticCommandBuffer{};
		ID3D12Resource* sceneDynamicCommandBuffer{};
		ID3D12Resource* sceneStaticVertexBuffer{};
		ID3D12Resource* sceneDynamicVertexBuffer{};
		ID3D12Resource* sceneStaticIndexBuffer{};
		ID3D12Resource* sceneDynamicIndexBuffer{};
		ID3D12Resource* sceneStaticObjectBuffer{};
		ID3D12Resource* sceneDynamicObjectBuffer{};
		D3D12_VERTEX_BUFFER_VIEW postScreenQuadVBV{};
		D3D12_VERTEX_BUFFER_VIEW skyboxVBV{};
		D3D12_VERTEX_BUFFER_VIEW sceneStaticVBV{};
		D3D12_VERTEX_BUFFER_VIEW sceneDynamicVBV{};
		D3D12_INDEX_BUFFER_VIEW sceneStaticIBV{};
		D3D12_INDEX_BUFFER_VIEW sceneDynamicIBV{};
		ID3D12Heap* sceneTextureHeap{};

		FreeListAllocator sceneStaticVertexAllocator;
		FreeListAllocator sceneStaticIndexAllocator;
		FreeListAllocator sceneDynamicVertexAllocator;
		FreeListAllocator sceneDynamicIndexAllocator;
		FreeListAllocator sceneTextureAllocator;

		D3D12Camera sceneCamera;
		DynamicPool<D3D12StaticMesh> sceneStaticMeshPool;
		DynamicPool<StaticObject> sceneStaticObjectPool;
		DynamicPool<D3D12DynamicMesh> sceneDynamicMeshPool;
		DynamicPool<DynamicObject> sceneDynamicObjectPool;
		DynamicPool<D3D12Texture> sceneTexturePool;
		DynamicPool<Material> sceneMaterialPool;
		D3D12Texture* skyboxTexture{};
		D3D12Texture* diffuseIBLTexture{};
		D3D12Texture* specularIBLTexture{};
		D3D12Texture* brdfLUT{};
		// It is unlikely that the brdfLUT will change, so if it does just record the
		// skybox commands again anyway to avoid extra logic and bookkeeping
		bool skyboxDirty = false;

		// GUI

		// Initialization
		D3D12Renderer(HWND hwnd, const Config& config, const Settings& settings);
		D3D12Renderer(const D3D12Renderer&) = delete;
		~D3D12Renderer();
		void destroy() noexcept;
		void createUploadStream();
		void destroyUploadStream() noexcept;
		void createScenePipeline();
		void destroyScenePipeline() noexcept;
		void createGUIPipeline();
		void destroyGUIPipeline() noexcept;
		void createSwapChainDependencies();
		void destroySwapChainDependencies() noexcept;
		void createSceneBackBuffer();
		void destroySceneBackBuffer() noexcept;

		// Command list recording
		void recordDepthMIPCommandList();
		void recordCullStaticCommandList();
		void recordCullDynamicCommandList();
		void recordSceneStaticCommandList();
		void recordSceneDynamicCommandList();
		void recordSkyboxCommandList();
		void recordPostCommandList();

		// Upload stream
		void uploadStreamWork(uint32_t copyQueueIndex);
		void uploadDynamicObjectHelper(uint32_t startIndex, uint32_t endIndex);
		void destroyDeadTextures();

		// Misc
		D3D12_CPU_DESCRIPTOR_HANDLE getSceneDescHeapCPUHandle(uint32_t offset);
		D3D12_GPU_DESCRIPTOR_HANDLE getSceneDescHeapGPUHandle(uint32_t offset);

		// Synchronization
		void wait();
	public:
		// Resource uploading
		// Scene
		Camera& getCamera() override;
		const Camera& getCamera() const override;
		StaticMesh* addStaticMesh(
			const BoundingSphere& boundingSphere,
			const StaticVertex* vertices,
			const uint32_t* vertexCounts,
			const index_type* indices,
			const uint32_t* indexCounts,
			uint32_t lodCount
		) override;
		void removeStaticMesh(StaticMesh* mesh) override;
		StaticObject* addStaticObject(StaticMesh* mesh, Material* material) override;
		void removeStaticObject(StaticObject* object) override;
		void updateStaticObject(StaticObject* object) override;
		DynamicMesh* addDynamicMesh(
			const BoundingSphere& boundingSphere,
			const DynamicVertex* vertices,
			const uint32_t* vertexCounts,
			const index_type* indices,
			const uint32_t* indexCounts,
			uint32_t lodCount
		) override;
		void removeDynamicMesh(DynamicMesh* mesh) override;
		DynamicObject* addDynamicObject(DynamicMesh* mesh, Material* material) override;
		void removeDynamicObject(DynamicObject* object) override;
		// Setting mipCount to -1 will use the full mip chain
		Texture* addTexture(
			TEXTURE_TYPE type,
			TEXTURE_FORMAT format,
			uint32_t width,
			uint32_t height,
			uint32_t mipCount,
			const char* textureData
		) override;
		void removeTexture(Texture* texture) override;
		Material* addMaterial(
			MATERIAL_TYPE type,
			Texture* baseColor,
			Texture* normal,
			Texture* roughnessAO,
			Texture* metallic,
			Texture* height,
			Texture* special
		) override;
		void removeMaterial(Material* material) override;
		void setSkybox(Texture* skybox, Texture* diffuseIBL, Texture* specularIBL) override;
		void setBRDFLUT(Texture* texture) override;
		// GUI
		// Update and upload commit
		void update() override;

		// Rendering
		void render() override;
		void resizeSwapChain() override;
		void toggleFullScreen() override;
		void showWindow() override;

		// Settings
		void applySettings(const Settings& settings) override;
	};
}