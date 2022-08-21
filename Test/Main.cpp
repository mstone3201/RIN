/*
To integrate RIN you should follow these guidelines:

The main loop should follow this format:
 1. wndProc event processing
 2. make scene changes for the current frame with Scene::upload
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
constexpr float CAMERA_FARZ = 50.0f;

LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);

bool running = true;
bool visible = true;
Input* input{};
RIN::Renderer* renderer{};
ThirdPersonCamera* camera{};

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
	config.uploadStreamSize = 128000000; // 128 mb
	config.staticVertexCount = 1280000;
	config.staticIndexCount = 1280000;
	config.staticMeshCount = 1280;
	config.staticObjectCount = 1280;
	config.dynamicVertexCount = 1280000;
	config.dynamicIndexCount = 1280000;
	config.dynamicMeshCount = 1280;
	config.dynamicObjectCount = 1280;
	config.texturesSize = 2231369728; // ~ 100 fully mipped 2048 x 2048 textures (2.2 gb)
	config.textureCount = 1000;
	config.materialCount = 200;

	RIN::Settings settings{};
	settings.backBufferWidth = 1920;
	settings.backBufferHeight = 1080;
	settings.backBufferCount = 2;
	settings.fullscreen = false;

	renderer = RIN::Renderer::create(hwnd, config, settings);

	camera = new ThirdPersonCamera(renderer->getCamera(), input);
	camera->setFocus(0.0f, 0.0f, 0.0f);
	camera->setLookAngle(0.0f, DirectX::XMConvertToRadians(35.0f));
	camera->setArmLength(5.0f);
	camera->setPerspective(CAMERA_FOVY, 1.0f, CAMERA_NEARZ, CAMERA_FARZ);

	SceneGraph sceneGraph(config.dynamicObjectCount);
	
	FilePool filePool;

	// TODO: Remove
	// Working directory is RIN/Test/
	FilePool::File textureFiles[12];
	filePool.readFile("../res/materials/dirt/basecolor.dds", textureFiles[0]);
	filePool.readFile("../res/materials/dirt/normal.dds", textureFiles[1]);
	filePool.readFile("../res/materials/dirt/roughnessao.dds", textureFiles[2]);
	filePool.readFile("../res/materials/dirt/height.dds", textureFiles[3]);
	filePool.readFile("../res/materials/metal/basecolor.dds", textureFiles[4]);
	filePool.readFile("../res/materials/metal/normal.dds", textureFiles[5]);
	filePool.readFile("../res/materials/metal/roughnessao.dds", textureFiles[6]);
	filePool.readFile("../res/materials/metal/height.dds", textureFiles[7]);
	filePool.readFile("../res/environments/panorama map/skybox.dds", textureFiles[8]);
	filePool.readFile("../res/environments/panorama map/diffuseIBL.dds", textureFiles[9]);
	filePool.readFile("../res/environments/panorama map/specularIBL.dds", textureFiles[10]);
	filePool.readFile("../res/environments/brdf.dds", textureFiles[11]);

	FilePool::File staticFiles[6];
	filePool.readFile("../res/meshes/Mat Ball.smesh", staticFiles[0]);
	filePool.readFile("../res/meshes/Plane.smesh", staticFiles[1]);
	filePool.readFile("../res/meshes/Rounded Cube.smesh", staticFiles[2]);
	filePool.readFile("../res/meshes/Rounded Cylinder.smesh", staticFiles[3]);
	filePool.readFile("../res/meshes/Suzanne.smesh", staticFiles[4]);
	filePool.readFile("../res/meshes/Torus.smesh", staticFiles[5]);

	FilePool::File dynamicFiles[6];
	filePool.readFile("../res/meshes/Mat Ball.dmesh", dynamicFiles[0]);
	filePool.readFile("../res/meshes/Plane.dmesh", dynamicFiles[1]);
	filePool.readFile("../res/meshes/Rounded Cube.dmesh", dynamicFiles[2]);
	filePool.readFile("../res/meshes/Rounded Cylinder.dmesh", dynamicFiles[3]);
	filePool.readFile("../res/meshes/Suzanne.dmesh", dynamicFiles[4]);
	filePool.readFile("../res/meshes/Torus.dmesh", dynamicFiles[5]);

	filePool.wait();

	for(uint32_t i = 0; i < _countof(textureFiles); ++i) {
		if(!textureFiles[i].ready()) {
			std::cout << "Texture file " << i << " could not be opened" << std::endl;
			std::terminate();
		}
	}
	for(uint32_t i = 0; i < _countof(staticFiles); ++i) {
		if(!staticFiles[i].ready()) {
			std::cout << "Static mesh file " << i << " could not be opened" << std::endl;
			std::terminate();
		}
	}
	for(uint32_t i = 0; i < _countof(dynamicFiles); ++i) {
		if(!dynamicFiles[i].ready()) {
			std::cout << "Dynamic mesh file " << i << " could not be opened" << std::endl;
			std::terminate();
		}
	}

	RIN::Texture* dirtBaseColor = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::BC7_UNORM_SRGB, 2048, 2048, -1, textureFiles[0].data() + 148);
	RIN::Texture* dirtNormal = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::BC5_UNORM, 2048, 2048, -1, textureFiles[1].data() + 148);
	RIN::Texture* dirtRoughnessAO = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::BC5_UNORM, 2048, 2048, -1, textureFiles[2].data() + 148);
	uint32_t black = 0;
	RIN::Texture* dirtMetallic = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::R8_UNORM, 1, 1, 1, (char*)&black);
	RIN::Texture* dirtHeight = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::BC4_UNORM, 2048, 2048, -1, textureFiles[3].data() + 148);
	RIN::Material* dirtMaterial = renderer->addMaterial(RIN::MATERIAL_TYPE::PBR_STANDARD, dirtBaseColor, dirtNormal, dirtRoughnessAO, dirtMetallic, dirtHeight);

	RIN::Texture* metalBaseColor = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::BC7_UNORM_SRGB, 2048, 2048, -1, textureFiles[4].data() + 148);
	RIN::Texture* metalNormal = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::BC5_UNORM, 2048, 2048, -1, textureFiles[5].data() + 148);
	RIN::Texture* metalRoughnessAO = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::BC5_UNORM, 2048, 2048, -1, textureFiles[6].data() + 148);
	uint32_t white = 0xFFFFFFFF;
	RIN::Texture* metalMetallic = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::R8_UNORM, 1, 1, 1, (char*)&white);
	RIN::Texture* metalHeight = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::BC4_UNORM, 2048, 2048, -1, textureFiles[7].data() + 148);
	RIN::Material* metalMaterial = renderer->addMaterial(RIN::MATERIAL_TYPE::PBR_STANDARD, metalBaseColor, metalNormal, metalRoughnessAO, metalMetallic, metalHeight);

	RIN::Texture* skybox = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_CUBE, RIN::TEXTURE_FORMAT::R16B16G16A16_FLOAT, 512, 512, 1, textureFiles[8].data() + 148);
	RIN::Texture* diffuseIBL = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_CUBE, RIN::TEXTURE_FORMAT::R16B16G16A16_FLOAT, 512, 512, 1, textureFiles[9].data() + 148);
	RIN::Texture* specularIBL = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_CUBE, RIN::TEXTURE_FORMAT::R16B16G16A16_FLOAT, 512, 512, -1, textureFiles[10].data() + 148);
	renderer->setSkybox(skybox, diffuseIBL, specularIBL);

	RIN::Texture* brdfLUT = renderer->addTexture(RIN::TEXTURE_TYPE::TEXTURE_2D, RIN::TEXTURE_FORMAT::R16G16_FLOAT, 512, 512, 1, textureFiles[11].data() + 148);
	renderer->setBRDFLUT(brdfLUT);

	for(uint32_t i = 0; i < _countof(staticFiles); ++i) {
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

		RIN::StaticMesh* mesh = renderer->addStaticMesh(boundingSphere, vertices, vertexCounts, indices, indexCounts, lodCount);
		RIN::StaticObject* object = renderer->addStaticObject(mesh, dirtMaterial);
	}

	SceneGraph::Node* nodes[_countof(dynamicFiles)]{};
	for(uint32_t i = 0; i < _countof(dynamicFiles); ++i) {
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

		RIN::DynamicMesh* mesh = renderer->addDynamicMesh(boundingSphere, vertices, vertexCounts, indices, indexCounts, lodCount);
		RIN::DynamicObject* object = renderer->addDynamicObject(mesh, metalMaterial);
		if(object) nodes[i] = sceneGraph.addNode(SceneGraph::ROOT_NODE, object);
	}
	// End remove

	renderer->showWindow();

	// Main loop
	MSG msg{};
	Timer timer;
	uint32_t frames = 0;
	float cumulativeElapsed = 0.0f;
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

		// Update the scene for the next frame
		float elapsedSeconds = timer.elapsedSeconds();
		timer.start();

		if(nodes[0]) nodes[0]->setTansform(DirectX::XMMatrixScaling(0.5f, 0.5f, 0.5f) * DirectX::XMMatrixTranslation(1.0f, 1.0f, 1.0f));
		if(nodes[1]) nodes[1]->setTansform(DirectX::XMMatrixRotationX(-DirectX::XM_PIDIV4) * DirectX::XMMatrixScaling(0.1f, 0.1f, 0.1f) * DirectX::XMMatrixTranslation(-1.0f, -1.0f, 1.0f));
		if(nodes[2]) nodes[2]->setTansform(DirectX::XMMatrixScaling(0.1f, 0.5f, 0.25f) * DirectX::XMMatrixTranslation(0.0f, 0.0f, 1.0f));
		if(nodes[3]) nodes[3]->setTansform(DirectX::XMMatrixScaling(0.125f, 0.25f, 0.25f) * DirectX::XMMatrixTranslation(1.0f, -1.0f, 1.5f));
		if(nodes[4]) nodes[4]->setTansform(DirectX::XMMatrixRotationZ(cumulativeElapsed / 2.5f * DirectX::XM_2PI) * DirectX::XMMatrixScaling(0.9f, 0.9f, 1.0f) * DirectX::XMMatrixRotationZ(-DirectX::XM_PI + DirectX::XM_PIDIV4) * DirectX::XMMatrixTranslation(5.0f, 5.0f, 2.0f));
		if(nodes[5]) nodes[5]->setTansform(DirectX::XMMatrixRotationY(cumulativeElapsed / 2.5f * DirectX::XM_2PI) * DirectX::XMMatrixTranslation(-3.0f, 0.0f, 1.0f));

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

	// TODO: Remove
	renderer->removeTexture(dirtBaseColor);
	renderer->removeTexture(dirtNormal);
	renderer->removeTexture(dirtRoughnessAO);
	renderer->removeTexture(dirtMetallic);
	renderer->removeTexture(dirtHeight);
	renderer->removeTexture(metalBaseColor);
	renderer->removeTexture(metalNormal);
	renderer->removeTexture(metalRoughnessAO);
	renderer->removeTexture(metalMetallic);
	renderer->removeTexture(metalHeight);
	renderer->removeTexture(skybox);
	renderer->removeTexture(diffuseIBL);
	renderer->removeTexture(specularIBL);
	renderer->removeTexture(brdfLUT);

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