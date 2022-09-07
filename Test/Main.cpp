/*
To integrate RIN you should follow these guidelines:

The main loop should follow this format:
 1. wndProc event processing
 2. make scene changes for the current frame with Renderer::update
 3. perform updates to the scene for the next frame
 4. render the current frame with Renderer::render

Your wndProc must respond to WM_SIZE and make a call to Renderer::resizeSwapChain there
*/

#define WIN32_LEAN_AND_MEAN

#include <fstream>
#include <iostream>

#include <windowsx.h>

#include <Renderer.hpp>

#include "Timer.hpp"
#include "Input.hpp"
#include "ThirdPersonCamera.hpp"
#include "FirstPersonCamera.hpp"
#include "SceneGraph.hpp"
#include "FilePool.hpp"

//#define TEST_ALLOC
//#define TEST_POOL
#ifdef TEST_ALLOC
#include "AllocationTest.hpp"
#elif defined(TEST_POOL)
#include "PoolTest.hpp"
#endif

constexpr float CAMERA_FOVY = DirectX::XM_PIDIV2;
constexpr float CAMERA_NEARZ = 0.1f;
constexpr float CAMERA_FARZ = 200.0f;

LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);

bool running = true;
bool visible = true;
Input* input{};
RIN::Renderer* renderer{};
FirstPersonCamera* camera{};

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd) {
	// Open up console and redirect std::cout to it
	AllocConsole();
	freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);

	// Tests
#ifdef TEST_ALLOC
	std::cout << "--- Free List Allocator ---" << std::endl;
	testFreeListAllocator();
	std::cout << "--- Pool Allocator ---" << std::endl;
	testPoolAllocator();
	std::cout << "--- Bump Allocator ---" << std::endl;
	testBumpAllocator();
	std::cout << "--- Multi-threaded Free List Allocator ---" << std::endl;
	testThreadedFLA();
	std::cout << "--- Multi-threaded Pool Allocator ---" << std::endl;
	testThreadedPA();
	std::cout << "--- Multi-threaded Bump Allocator ---" << std::endl;
	testThreadedBA();

	while(true);
	return 0;
#elif defined(TEST_POOL)
	std::cout << "--- Static Pool ---" << std::endl;
	testStaticPool();
	std::cout << "--- Dynamic Pool ---" << std::endl;
	testDynamicPool();
	std::cout << "--- Dynamic Pool Specialization ---" << std::endl;
	testDynamicPoolSpecialization();

	while(true);
	return 0;
#endif

	// Create window
	LPCWSTR programName = L"RIN Test";

	WNDCLASSEX wcx{};
	wcx.cbSize = sizeof(WNDCLASSEX);
	wcx.style = CS_HREDRAW | CS_VREDRAW;
	wcx.lpfnWndProc = wndProc;
	wcx.cbClsExtra = 0;
	wcx.cbWndExtra = 0;
	wcx.hInstance = hInstance;
	wcx.hIcon = nullptr;
	wcx.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcx.hbrBackground = (HBRUSH)COLOR_WINDOW;
	wcx.lpszMenuName = nullptr;
	wcx.lpszClassName = programName;
	wcx.hIconSm = nullptr;
	RegisterClassEx(&wcx);

	HWND hwnd = CreateWindow(
		programName,
		programName,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		nullptr,
		nullptr,
		hInstance,
		nullptr
	);

	// Create input
	input = new Input(hwnd);

	// Create renderer
	RIN::Config config{};
	config.engine = RIN::RENDER_ENGINE::D3D12;
	config.uploadStreamSize = 32000000;
	config.staticVertexCount = 10000000;
	config.staticIndexCount = 10000000;
	config.staticMeshCount = 128;
	config.staticObjectCount = 128;
	config.dynamicVertexCount = 1000000;
	config.dynamicIndexCount = 1000000;
	config.dynamicMeshCount = 128;
	config.dynamicObjectCount = 128;
	config.skinnedVertexCount = 1000000;
	config.skinnedIndexCount = 1000000;
	config.skinnedMeshCount = 128;
	config.skinnedObjectCount = 128;
	config.boneCount = 250;
	config.armatureCount = 3;
	config.texturesSize = 1000000000;
	config.textureCount = 100;
	config.materialCount = 10;
	config.lightCount = 32;

	RIN::Settings settings{};
	settings.backBufferWidth = 1920;
	settings.backBufferHeight = 1080;
	settings.backBufferCount = 2;
	settings.fullscreen = false;

	renderer = RIN::Renderer::create(hwnd, config, settings);

	camera = new FirstPersonCamera(renderer->getCamera(), input);
	camera->setPosition(0.0f, 0.0f, 2.0f);
	camera->setLookAngle(0.0f, 0.0f);
	camera->setPerspective(CAMERA_FOVY, 1.0f, CAMERA_NEARZ, CAMERA_FARZ);

	SceneGraph sceneGraph(config.dynamicObjectCount, config.lightCount, config.boneCount);
	
	FilePool filePool;

	// Working directory is RIN/Test/
	// Note that we skip reading the dds file header for simplicity

	// Read environment texture files
	FilePool::File environmentFiles[4];
	filePool.readFile("../res/environments/brdf.dds", environmentFiles[0]);
	filePool.readFile("../res/environments/panorama map/skybox.dds", environmentFiles[1]);
	filePool.readFile("../res/environments/panorama map/diffuseIBL.dds", environmentFiles[2]);
	filePool.readFile("../res/environments/panorama map/specularIBL.dds", environmentFiles[3]);

	// Wait for the files to finish reading
	filePool.wait();

	for(uint32_t i = 0; i < _countof(environmentFiles); ++i) {
		if(!environmentFiles[i].ready()) {
			std::cout << "Failed to open environment texture " << i << std::endl;
			return -1;
		}
	}

	// Upload textures
	RIN::Texture* environmentTextures[4]{};
	environmentTextures[0] = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::R16G16_FLOAT, 512, 512, 1, environmentFiles[0].data() + 148);
	environmentTextures[1] = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_CUBE, RIN::TEXTURE_FORMAT::R16B16G16A16_FLOAT, 512, 512, 1, environmentFiles[1].data() + 148);
	environmentTextures[2] = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_CUBE, RIN::TEXTURE_FORMAT::R16B16G16A16_FLOAT, 512, 512, 1, environmentFiles[2].data() + 148);
	environmentTextures[3] = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_CUBE, RIN::TEXTURE_FORMAT::R16B16G16A16_FLOAT, 512, 512, -1, environmentFiles[3].data() + 148);

	renderer->setBRDFLUT(environmentTextures[0]);
	renderer->setSkybox(environmentTextures[1], environmentTextures[2], environmentTextures[3]);

	// Read material texture files
	RIN::TEXTURE_FORMAT textureFormats[]{
		RIN::TEXTURE_FORMAT::BC7_UNORM_SRGB,
		RIN::TEXTURE_FORMAT::BC5_UNORM,
		RIN::TEXTURE_FORMAT::BC5_UNORM,
		RIN::TEXTURE_FORMAT::BC4_UNORM,
		RIN::TEXTURE_FORMAT::BC4_UNORM,
		RIN::TEXTURE_FORMAT::BC7_UNORM_SRGB
	};

	FilePool::File textureFiles[4][6];
	filePool.readFile("../res/materials/dirt/basecolor.dds", textureFiles[0][0]);
	filePool.readFile("../res/materials/dirt/normal.dds", textureFiles[0][1]);
	filePool.readFile("../res/materials/dirt/roughnessao.dds", textureFiles[0][2]);
	filePool.readFile("../res/materials/dirt/height.dds", textureFiles[0][4]);
	filePool.readFile("../res/materials/metal/basecolor.dds", textureFiles[1][0]);
	filePool.readFile("../res/materials/metal/normal.dds", textureFiles[1][1]);
	filePool.readFile("../res/materials/metal/roughnessao.dds", textureFiles[1][2]);
	filePool.readFile("../res/materials/metal/height.dds", textureFiles[1][4]);
	filePool.readFile("../res/materials/lava/basecolor.dds", textureFiles[2][0]);
	filePool.readFile("../res/materials/lava/normal.dds", textureFiles[2][1]);
	filePool.readFile("../res/materials/lava/roughnessao.dds", textureFiles[2][2]);
	filePool.readFile("../res/materials/lava/height.dds", textureFiles[2][4]);
	filePool.readFile("../res/materials/lava/emissive.dds", textureFiles[2][5]);
	filePool.readFile("../res/materials/wood/basecolor.dds", textureFiles[3][0]);
	filePool.readFile("../res/materials/wood/normal.dds", textureFiles[3][1]);
	filePool.readFile("../res/materials/wood/roughnessao.dds", textureFiles[3][2]);
	filePool.readFile("../res/materials/wood/metallic.dds", textureFiles[3][3]);
	filePool.readFile("../res/materials/wood/height.dds", textureFiles[3][4]);

	RIN::Texture* textures[4][6]{};
	constexpr uint32_t black = 0;
	constexpr uint32_t white = 0xFFFFFFFF;
	textures[0][3] = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::R8_UNORM, 1, 1, 1, (char*)&black);
	textures[1][3] = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::R8_UNORM, 1, 1, 1, (char*)&white);
	textures[2][3] = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::R8_UNORM, 1, 1, 1, (char*)&black);
	textures[3][5] = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::R8G8_UNORM, 1, 1, 1, (char*)&white);

	RIN::MATERIAL_TYPE materialTypes[]{ RIN::MATERIAL_TYPE::PBR_STANDARD, RIN::MATERIAL_TYPE::PBR_STANDARD, RIN::MATERIAL_TYPE::PBR_EMISSIVE, RIN::MATERIAL_TYPE::PBR_CLEAR_COAT };
	RIN::Material* materials[4]{};

	// Read mesh files
	FilePool::File staticFiles[9];
	filePool.readFile("../res/meshes/Cube.smesh", staticFiles[0]);
	filePool.readFile("../res/meshes/Cylinder.smesh", staticFiles[1]);
	filePool.readFile("../res/meshes/Plane.smesh", staticFiles[2]);
	filePool.readFile("../res/meshes/Sphere0.smesh", staticFiles[3]);
	filePool.readFile("../res/meshes/Sphere1.smesh", staticFiles[4]);
	filePool.readFile("../res/meshes/Sphere2.smesh", staticFiles[5]);
	filePool.readFile("../res/meshes/Torus0.smesh", staticFiles[6]);
	filePool.readFile("../res/meshes/Torus1.smesh", staticFiles[7]);
	filePool.readFile("../res/meshes/Torus2.smesh", staticFiles[8]);

	RIN::StaticMesh* staticMeshes[9]{};
	
	uint32_t staticObjectMaterials[]{ 3, 1, 0, 0, 1, 2, 1, 0, 3 };
	RIN::StaticObject* staticObjects[9]{};

	FilePool::File dynamicFiles[2];
	filePool.readFile("../res/meshes/Monster.dmesh", dynamicFiles[0]);
	filePool.readFile("../res/meshes/Torus0.dmesh", dynamicFiles[1]);

	RIN::DynamicMesh* dynamicMeshes[2]{};

	uint32_t dynamicObjectMaterials[]{ 2, 1 };
	RIN::DynamicObject* dynamicObjects[2]{};

	SceneGraph::DynamicObjectNode* dynamicObjectNodes[2]{};

	FilePool::File skinnedFiles[1];
	filePool.readFile("../res/meshes/Monster.skmesh", skinnedFiles[0]);

	RIN::SkinnedMesh* skinnedMeshes[1]{};

	FilePool::File armatureFiles[1];
	filePool.readFile("../res/armatures/Armature.arm", armatureFiles[0]);

	RIN::Armature* armatures[1]{};

	SceneGraph::BoneNode** boneNodes[1]{};

	uint32_t skinnedObjectMaterials[]{ 1 };
	RIN::SkinnedObject* skinnedObjects[1]{};

	// Add lights
	RIN::Light* lights[8]{};
	for(uint32_t i = 0; i < _countof(lights); ++i)
		lights[i] = renderer->addLight();

	lights[0]->position = { 3.0f, 1.0f, 4.0f };
	lights[0]->radius = 15.0f;
	lights[0]->color = { 50.0f, 0.0f, 0.0f };

	lights[1]->position = { 3.0f, -1.0f, 4.0f };
	lights[1]->radius = 15.0f;
	lights[1]->color = { 0.0f, 0.0f, 50.0f };

	lights[2]->position = { 4.0f, 0.0f, 4.0f };
	lights[2]->radius = 15.0f;
	lights[2]->color = { 0.0f, 50.0f, 0.0f };

	lights[3]->position = { 10.0f, 10.0f, 2.0f };
	lights[3]->radius = 6.0f;
	lights[3]->color = { 10.0f, 9.0f, 9.5f };

	lights[4]->position = { -35.0f, 35.0f, 5.0f };
	lights[4]->radius = 30.0f;
	lights[4]->color = { 100.0f, 100.0f, 0.0f };

	lights[5]->position = { -6.0f, -6.0f, 1.0f };
	lights[5]->radius = 5.0f;
	lights[5]->color = { 5.0f, 5.0f, 5.0f };

	lights[6]->radius = 15.0f;

	lights[7]->radius = 15.0f;

	SceneGraph::LightNode* lightNodes[2]{};

	lightNodes[0] = sceneGraph.addNode(SceneGraph::ROOT_NODE, lights[6]);
	lightNodes[1] = sceneGraph.addNode(SceneGraph::ROOT_NODE, lights[7]);

	renderer->showWindow();

	// Main loop
	MSG msg{};
	Timer timer;
	uint32_t frames = 0;
	float cumulativeElapsed = 0.0f;
	float time = 0.0f;
	while(true) {
		input->reset();

		// Process events
		while(PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if(!running) break;

		// Update everything in the scene for the current frame
		renderer->update();

		// Upload objects and materials and free buffers

		for(uint32_t i = 0; i < _countof(environmentFiles); ++i)
			if(environmentFiles[i].ready() && environmentTextures[i]->resident())
				environmentFiles[i].close();

		for(uint32_t i = 0; i < _countof(textureFiles); ++i) {
			for(uint32_t j = 0; j < _countof(textureFiles[i]); ++j) {
				if(textureFiles[i][j].ready()) {
					if(!textures[i][j])
						textures[i][j] = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, textureFormats[j], 2048, 2048, -1, textureFiles[i][j].data() + 148);
					else if(textures[i][j]->resident())
						textureFiles[i][j].close();
				}
			}

			if(!materials[i]) {
				bool ready = true;

				for(uint32_t j = 0; j < 5; ++j)
					ready = ready && textures[i][j];

				if(materialTypes[i] != RIN::MATERIAL_TYPE::PBR_STANDARD)
					ready = ready && textures[i][5];

				if(ready)
					materials[i] = renderer->addMaterial(materialTypes[i], textures[i][0], textures[i][1], textures[i][2], textures[i][3], textures[i][4], textures[i][5]);
			}
		}

		for(uint32_t i = 0; i < _countof(staticFiles); ++i) {
			if(staticFiles[i].ready()) {
				if(!staticMeshes[i]) {
					uint8_t lodCount = *(uint8_t*)(staticFiles[i].data() + 1);
					float* bsphere = (float*)(staticFiles[i].data() + 2);
					uint32_t* vertexCounts = (uint32_t*)(bsphere + 4);
					uint32_t* indexCounts = vertexCounts + lodCount;

					uint64_t totalVertexCount = 0;
					for(uint8_t i = 0; i < lodCount; ++i)
						totalVertexCount += vertexCounts[i];

					RIN::BoundingSphere boundingSphere(bsphere[0], bsphere[1], bsphere[2], bsphere[3]);
					RIN::StaticVertex* vertices = (RIN::StaticVertex*)(indexCounts + lodCount);
					RIN::index_type* indices = (RIN::index_type*)(vertices + totalVertexCount);

					staticMeshes[i] = renderer->addStaticMesh(boundingSphere, vertices, vertexCounts, indices, indexCounts, lodCount);
				} else if(staticMeshes[i]->resident())
					staticFiles[i].close();
			}
		}

		for(uint32_t i = 0; i < _countof(staticObjects); ++i)
			if(!staticObjects[i])
				staticObjects[i] = renderer->addStaticObject(staticMeshes[i], materials[staticObjectMaterials[i]]);

		for(uint32_t i = 0; i < _countof(dynamicFiles); ++i) {
			if(dynamicFiles[i].ready()) {
				if(!dynamicMeshes[i]) {
					uint8_t lodCount = *(uint8_t*)(dynamicFiles[i].data() + 1);
					float* bsphere = (float*)(dynamicFiles[i].data() + 2);
					uint32_t* vertexCounts = (uint32_t*)(bsphere + 4);
					uint32_t* indexCounts = vertexCounts + lodCount;

					uint64_t totalVertexCount = 0;
					for(uint8_t i = 0; i < lodCount; ++i)
						totalVertexCount += vertexCounts[i];

					RIN::BoundingSphere boundingSphere(bsphere[0], bsphere[1], bsphere[2], bsphere[3]);
					RIN::DynamicVertex* vertices = (RIN::DynamicVertex*)(indexCounts + lodCount);
					RIN::index_type* indices = (RIN::index_type*)(vertices + totalVertexCount);

					dynamicMeshes[i] = renderer->addDynamicMesh(boundingSphere, vertices, vertexCounts, indices, indexCounts, lodCount);
				} else if(dynamicMeshes[i]->resident())
					dynamicFiles[i].close();
			}
		}

		for(uint32_t i = 0; i < _countof(dynamicObjects); ++i) {
			if(!dynamicObjects[i]) {
				dynamicObjects[i] = renderer->addDynamicObject(dynamicMeshes[i], materials[dynamicObjectMaterials[i]]);
				if(dynamicObjects[i]) dynamicObjectNodes[i] = sceneGraph.addNode(SceneGraph::ROOT_NODE, dynamicObjects[i]);
			}
		}

		for(uint32_t i = 0; i < _countof(skinnedFiles); ++i) {
			if(skinnedFiles[i].ready()) {
				if(!skinnedMeshes[i]) {
					uint8_t lodCount = *(uint8_t*)(skinnedFiles[i].data() + 1);
					float* bsphere = (float*)(skinnedFiles[i].data() + 2);
					uint32_t* vertexCounts = (uint32_t*)(bsphere + 4);
					uint32_t* indexCounts = vertexCounts + lodCount;

					uint64_t totalVertexCount = 0;
					for(uint8_t i = 0; i < lodCount; ++i)
						totalVertexCount += vertexCounts[i];

					RIN::BoundingSphere boundingSphere(bsphere[0], bsphere[1], bsphere[2], bsphere[3]);
					RIN::SkinnedVertex* vertices = (RIN::SkinnedVertex*)(indexCounts + lodCount);
					RIN::index_type* indices = (RIN::index_type*)(vertices + totalVertexCount);

					skinnedMeshes[i] = renderer->addSkinnedMesh(boundingSphere, vertices, vertexCounts, indices, indexCounts, lodCount);
				} else if(skinnedMeshes[i]->resident())
					skinnedFiles[i].close();
			}
		}

		for(uint32_t i = 0; i < _countof(armatureFiles); ++i) {
			if(armatureFiles[i].ready()) {
				if(!armatures[i]) {
					uint8_t boneCount = *(uint8_t*)(armatureFiles[i].data());

					armatures[i] = renderer->addArmature(boneCount);
					
					if(armatures[i]) {
						boneNodes[i] = new SceneGraph::BoneNode*[boneCount] {};

						DirectX::XMMATRIX restMatrix = DirectX::XMLoadFloat4x4((DirectX::XMFLOAT4X4*)(armatureFiles[i].data() + 1));
						boneNodes[i][0] = sceneGraph.addNode(SceneGraph::ROOT_NODE, armatures[i]->bones, restMatrix);

						char* dataStart = (char*)(armatureFiles[i].data() + 1 + sizeof(DirectX::XMFLOAT4X4));
						for(uint8_t j = 0; j < boneCount - 1; ++j) {
							uint8_t boneIndex = *(uint8_t*)dataStart;
							uint8_t parentIndex = *(uint8_t*)(dataStart + 1);
							restMatrix = DirectX::XMLoadFloat4x4((DirectX::XMFLOAT4X4*)(dataStart + 2));

							boneNodes[i][boneIndex] = sceneGraph.addNode(boneNodes[i][parentIndex], armatures[i]->bones + boneIndex, restMatrix);

							dataStart += sizeof(uint8_t) + sizeof(uint8_t) + sizeof(DirectX::XMFLOAT4X4);
						}
					}
				} else if(armatures[i]->resident())
					armatureFiles[i].close();
			}
		}

		for(uint32_t i = 0; i < _countof(skinnedObjects); ++i)
			if(!skinnedObjects[i])
				skinnedObjects[i] = renderer->addSkinnedObject(skinnedMeshes[i], armatures[i], materials[skinnedObjectMaterials[i]]);

		// Update the scene for the next frame
		float elapsedSeconds = timer.elapsedSeconds();
		timer.start();

		time += elapsedSeconds;
		if(time >= 2.0f) time -= 2.0f;
		float scale = time / 2.0f;
		float sinSNorm = sinf(scale * DirectX::XM_2PI);
		float sinSNorm2 = sinf(scale * 2.0f * DirectX::XM_2PI);
		float sinUNormOffset = sinf((scale - 0.125f) * 2.0f * DirectX::XM_2PI) * 0.5f + 0.5f;
		float cosUNorm = cosf(scale * DirectX::XM_2PI) * 0.5f + 0.5f;
		float cosUNorm2 = cosf(scale * 2.0f * DirectX::XM_2PI) * 0.5f + 0.5f;

		if(dynamicObjectNodes[0]) dynamicObjectNodes[0]->setTansform(DirectX::XMMatrixRotationZ(DirectX::XM_PI) * DirectX::XMMatrixTranslation(-0.5f, -10.0f, 0.0f));
		if(dynamicObjectNodes[1]) dynamicObjectNodes[1]->setTansform(DirectX::XMMatrixRotationX(DirectX::XM_PIDIV4) * DirectX::XMMatrixRotationZ(scale * DirectX::XM_2PI) * DirectX::XMMatrixTranslation(9.0f, -8.0f, 2.5f + sinSNorm * 0.5f));

		if(boneNodes[0]) {
			// Body
			if(boneNodes[0][0]) boneNodes[0][0]->setTansform(DirectX::XMMatrixRotationZ(DirectX::XM_PI) * DirectX::XMMatrixTranslation(3.5f, -10.0f, 0.0f));
			// Left arm
			if(boneNodes[0][31]) boneNodes[0][31]->setBoneSpaceTansform(DirectX::XMMatrixRotationX(cosUNorm * DirectX::XM_PIDIV4 - DirectX::XM_PIDIV4 * 0.5f));
			if(boneNodes[0][32]) boneNodes[0][32]->setBoneSpaceTansform(DirectX::XMMatrixRotationX(cosUNorm * DirectX::XM_PIDIV2 * 0.75f));
			// Right arm
			if(boneNodes[0][10]) boneNodes[0][10]->setBoneSpaceTansform(DirectX::XMMatrixRotationX((1.0f - cosUNorm) * DirectX::XM_PIDIV4 - DirectX::XM_PIDIV4 * 0.5f));
			if(boneNodes[0][11]) boneNodes[0][11]->setBoneSpaceTansform(DirectX::XMMatrixRotationX((1.0f - cosUNorm) * DirectX::XM_PIDIV2 * 0.75f));
			// Head
			if(boneNodes[0][2]) boneNodes[0][2]->setBoneSpaceTansform(DirectX::XMMatrixRotationX(sinSNorm * DirectX::XM_PIDIV4 * 0.025f));
			if(boneNodes[0][3]) boneNodes[0][3]->setBoneSpaceTansform(DirectX::XMMatrixRotationX(sinSNorm2 * DirectX::XM_PIDIV4 * 0.025f));
			if(boneNodes[0][4]) boneNodes[0][4]->setBoneSpaceTansform(DirectX::XMMatrixRotationX(sinSNorm2 * DirectX::XM_PIDIV4 * 0.025f));
			if(boneNodes[0][7]) boneNodes[0][7]->setBoneSpaceTansform(DirectX::XMMatrixRotationX(cosUNorm2 * DirectX::XM_PIDIV4 * 0.5f));
			// Tail
			if(boneNodes[0][50]) boneNodes[0][50]->setBoneSpaceTansform(DirectX::XMMatrixRotationZ(sinSNorm * DirectX::XM_PIDIV4 * 0.05f));
			if(boneNodes[0][51]) boneNodes[0][51]->setBoneSpaceTansform(DirectX::XMMatrixRotationZ(sinSNorm * DirectX::XM_PIDIV4 * 0.125f));
			if(boneNodes[0][52]) boneNodes[0][52]->setBoneSpaceTansform(DirectX::XMMatrixRotationZ(sinSNorm * DirectX::XM_PIDIV4 * 0.25f));
			if(boneNodes[0][53]) boneNodes[0][53]->setBoneSpaceTansform(DirectX::XMMatrixRotationZ(sinSNorm * DirectX::XM_PIDIV4 * 0.25f));
			if(boneNodes[0][54]) boneNodes[0][54]->setBoneSpaceTansform(DirectX::XMMatrixRotationZ(sinSNorm * DirectX::XM_PIDIV4 * 0.125f));
			// Left leg
			if(boneNodes[0][62]) boneNodes[0][62]->setBoneSpaceTansform(DirectX::XMMatrixRotationX(cosUNorm * DirectX::XM_PIDIV4 * 0.5f));
			if(boneNodes[0][63]) boneNodes[0][63]->setBoneSpaceTansform(DirectX::XMMatrixRotationX(cosUNorm * -DirectX::XM_PIDIV4 * 1.5f));
			if(boneNodes[0][64]) boneNodes[0][64]->setBoneSpaceTansform(DirectX::XMMatrixRotationX(cosUNorm * -DirectX::XM_PIDIV4 * 0.25f));
			if(boneNodes[0][65]) boneNodes[0][65]->setBoneSpaceTansform(DirectX::XMMatrixRotationZ(sinUNormOffset * -DirectX::XM_PIDIV4 * 0.55f));
			// Right leg
			if(boneNodes[0][57]) boneNodes[0][57]->setBoneSpaceTansform(DirectX::XMMatrixRotationX((1.0f - cosUNorm) * DirectX::XM_PIDIV4 * 0.5f));
			if(boneNodes[0][58]) boneNodes[0][58]->setBoneSpaceTansform(DirectX::XMMatrixRotationX((1.0f - cosUNorm) * -DirectX::XM_PIDIV4 * 1.5f));
			if(boneNodes[0][59]) boneNodes[0][59]->setBoneSpaceTansform(DirectX::XMMatrixRotationX((1.0f - cosUNorm) * -DirectX::XM_PIDIV4 * 0.25f));
			if(boneNodes[0][60]) boneNodes[0][60]->setBoneSpaceTansform(DirectX::XMMatrixRotationZ(sinUNormOffset * DirectX::XM_PIDIV4 * 0.55f));
		}

		lightNodes[0]->setTansform(DirectX::XMMatrixTranslation(25.0f, 0.0f, 5.0f) * DirectX::XMMatrixRotationZ(scale * DirectX::XM_PI));
		lights[6]->color = { cosUNorm * 50.0f, cosUNorm * 45.0f, cosUNorm * 47.5f };

		lightNodes[1]->setTansform(DirectX::XMMatrixTranslation(-25.0f, 0.0f, 5.0f) * DirectX::XMMatrixRotationZ(scale * DirectX::XM_PI));
		lights[7]->color = lights[6]->color;

		sceneGraph.update();

		camera->update(elapsedSeconds);

		// Render the current frame
		if(visible) renderer->render();

		// Misc
		++frames;
		cumulativeElapsed += elapsedSeconds;
		if(cumulativeElapsed > 2.5f) {
			std::cout << "FPS: " << frames / cumulativeElapsed << std::endl;
			cumulativeElapsed = 0.0f;
			frames = 0;
		}
	}

	for(uint32_t i = 0; i < _countof(environmentTextures); ++i)
		renderer->removeTexture(environmentTextures[i]);

	for(uint32_t i = 0; i < _countof(textures); ++i)
		for(uint32_t j = 0; j < _countof(textures[i]); ++j)
			renderer->removeTexture(textures[i][j]);

	for(uint32_t i = 0; i < _countof(boneNodes); ++i)
		delete[] boneNodes[i];

	// Cleanup
	delete input;
	RIN::Renderer::destroy(renderer);

	return (int)msg.wParam;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch(message) {
	case WM_DESTROY:
	case WM_CLOSE:
	{
		running = false;
		return 0;
	}
	case WM_SIZE:
	{
		visible = wParam != SIZE_MINIMIZED;
		if(visible) {
			renderer->resizeSwapChain();

			uint32_t width = LOWORD(lParam);
			uint32_t height = HIWORD(lParam);
			camera->setPerspective(CAMERA_FOVY, (float)width / (float)height, CAMERA_NEARZ, CAMERA_FARZ);
		}
		return 0;
	}
	case WM_SYSKEYDOWN:
	{
		// Handle fullscreen transition
		if(visible && IS_ALT_ENTER(wParam, lParam)) {
			renderer->toggleFullScreen();
			return 0;
		}
	}
	case WM_KEYDOWN:
	{
		if(wParam == VK_ESCAPE) {
			running = false;
			return 0;
		}
	}
	case WM_MOUSEMOVE:
	{
		input->updateMousePos(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	}
	case WM_INPUT:
	{
		if(GET_RAWINPUT_CODE_WPARAM(wParam) == RIM_INPUT) {
			// For HIDs that are not the mouse or keyboard it is not
			// sufficient to assume inputSize <= sizeof(RAWINPUT)
			uint32_t inputSize = sizeof(RAWINPUT);
			RAWINPUT rawInput{};
			if(GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &rawInput, &inputSize, sizeof(RAWINPUTHEADER)) > sizeof(RAWINPUT))
				throw std::runtime_error("Failed to get input device data");
			
			if(rawInput.header.dwType == RIM_TYPEMOUSE)
				input->update(&rawInput.data.mouse);
			else if(rawInput.header.dwType == RIM_TYPEKEYBOARD)
				input->update(&rawInput.data.keyboard);
			
			DefWindowProc(hwnd, message, wParam, lParam);
			return 0;
		}
	}
	}

	return DefWindowProc(hwnd, message, wParam, lParam);
}